/* Phase 6 Plan 3: `spu94` CLI entry point (full pipeline, Task 2).
 *
 * Flow:
 *   argv parse (getopt_long)
 *   -> load input WAV (dr_wav, planar int16 L/R)
 *   -> allocate caller-owned spu94_state + work buffer
 *   -> apply preset OR JSON config
 *   -> spu94_tick once so TICK_LATCHED registers commit
 *   -> process input in fixed blocks
 *   -> optional flush for --tail-seconds
 *   -> write output WAV
 *   -> free everything, return 0
 *
 * Error-message discipline (D-05):
 *   - Every error path exits non-zero (2 for user errors).
 *   - Every error writes exactly ONE line of stderr, prefixed `spu94: error:`.
 *   - No multi-line tracebacks, no stack frames, no C-compiler jargon.
 *   - Anthony is a recording/broadcast engineer, not a developer; messages
 *     read like gear-manual diagnostics.
 */
#include <getopt.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <spu94/spu94.h>
#include <spu94/spu94_register_facade.h>

#include "json_config.h"
#include "preset_names.h"
#include "wav_io.h"

/* One-line error printing discipline (D-05). No variadic spew. No tracebacks. */
#define SPU94_ERROR(...) do {                            \
        fputs("spu94: error: ", stderr);                 \
        fprintf(stderr, __VA_ARGS__);                    \
        fputc('\n', stderr);                             \
    } while (0)

static void print_help(void) {
    /* Tone: recording-engineer-oriented, polished. Anthony is not a coder. */
    fputs(
        "Usage: spu94 [OPTIONS] INPUT.wav OUTPUT.wav\n"
        "\n"
        "Render a WAV file through the PlayStation 1 SPU reverb.\n"
        "\n"
        "Signal choice (one of):\n"
        "  --preset <name>        Apply a factory preset. See --list-presets.\n"
        "  --config <path.json>   Apply a register-map or override from JSON.\n"
        "\n"
        "Optional:\n"
        "  --tail-seconds <N>     Append N seconds of reverb tail after input ends.\n"
        "\n"
        "Utility:\n"
        "  --list-presets         Print the 10 factory preset names and exit.\n"
        "  -h, --help             Show this message and exit.\n"
        "\n"
        "Examples:\n"
        "  spu94 --preset hall input.wav output.wav\n"
        "  spu94 --preset hall --tail-seconds 2 input.wav output.wav\n"
        "  spu94 --config my_override.json input.wav output.wav\n"
        "\n"
        "Input must be 16-bit PCM stereo at 44.1 kHz. Output is the same format.\n",
        stdout);
}

int main(int argc, char **argv) {
    static const struct option long_opts[] = {
        {"preset",       required_argument, NULL, 'p'},
        {"config",       required_argument, NULL, 'c'},
        {"tail-seconds", required_argument, NULL, 't'},
        {"list-presets", no_argument,       NULL, 'l'},
        {"help",         no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    const char *preset_name = NULL;
    const char *config_path = NULL;
    double tail_seconds = 0.0;
    int opt;

    /* Silence getopt's own stderr writes; we own the error shape (D-05). */
    opterr = 0;

    while ((opt = getopt_long(argc, argv, "p:c:t:lh", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'p':
                preset_name = optarg;
                break;
            case 'c':
                config_path = optarg;
                break;
            case 't': {
                char *endp = NULL;
                tail_seconds = strtod(optarg, &endp);
                if (!endp || *endp != '\0' || tail_seconds < 0.0) {
                    SPU94_ERROR("invalid value for --tail-seconds: '%s'", optarg);
                    return 2;
                }
                break;
            }
            case 'l':
                spu94_cli_list_presets(stdout);
                return 0;
            case 'h':
                print_help();
                return 0;
            case '?':
            default:
                SPU94_ERROR("unrecognized option (try --help)");
                return 2;
        }
    }

    /* Validate flag combination (D-05 contract). */
    if (preset_name && config_path) {
        SPU94_ERROR("--preset and --config are mutually exclusive");
        return 2;
    }
    if (!preset_name && !config_path) {
        SPU94_ERROR("one of --preset or --config is required (try --help)");
        return 2;
    }
    if (argc - optind != 2) {
        int positional = argc - optind;
        SPU94_ERROR("expected INPUT.wav OUTPUT.wav (got %d positional argument%s)",
                    positional, positional == 1 ? "" : "s");
        return 2;
    }

    const char *in_path  = argv[optind];
    const char *out_path = argv[optind + 1];

    /* --- Full pipeline --- */
    char err_buf[512];

    spu94_cli_wav_t input;
    memset(&input, 0, sizeof input);
    if (spu94_cli_wav_load(in_path, &input, err_buf, sizeof err_buf) != 0) {
        SPU94_ERROR("%s", err_buf);
        return 2;
    }

    /* Caller-allocated state storage (no library heap allocations per
     * API-01). SPU94_STATE_SIZE_MAX = 16384 bytes; actual struct fits
     * inside with ~97x headroom.
     *
     * work_buf: 512 KB is enough for all PS1 SPU preset buffer-offsets
     * (the largest delay address + 4 bytes is well under 64 KB). */
    enum { WORK_BUF_SIZE = 512u * 1024u };
    alignas(SPU94_STATE_ALIGN_MAX) static unsigned char state_buf[SPU94_STATE_SIZE_MAX];
    unsigned char *work_buf = (unsigned char *)malloc((size_t)WORK_BUF_SIZE);
    if (!work_buf) {
        free(input.L); free(input.R);
        SPU94_ERROR("out of memory allocating %u-byte work buffer",
                    (unsigned)WORK_BUF_SIZE);
        return 2;
    }

    spu94_state *state = spu94_init(state_buf, SPU94_STATE_SIZE_MAX,
                                    work_buf, (size_t)WORK_BUF_SIZE);
    if (!state) {
        free(input.L); free(input.R); free(work_buf);
        SPU94_ERROR("spu94_init failed (internal error)");
        return 2;
    }

    if (preset_name) {
        int pid = spu94_cli_preset_id_by_name(preset_name);
        if (pid < 0) {
            char names[256];
            spu94_cli_preset_name_list(names, sizeof names);
            spu94_destroy(state);
            free(input.L); free(input.R); free(work_buf);
            SPU94_ERROR("unknown preset '%s' — valid: %s", preset_name, names);
            return 2;
        }
        spu94_load_preset(state, (spu94_preset_id_t)pid);
        /* ADR-Phase-6-G: the 10 factory preset tables intentionally leave
         * vLOUT/vROUT at 0x0000 — those are master-mix levels configured
         * outside the preset surface (see spu94_presets.c comment at
         * lines 32-34). Under the wet-only 44.1 kHz output wiring the
         * CLI's 44.1 kHz output IS the wet-scaled signal, so vLOUT/vROUT
         * = 0 gates the rendered audio to silence. Default to full-scale
         * (0x7FFF) so `spu94 --preset hall input.wav out.wav` produces
         * an audible wet rendering without requiring a --config overlay.
         * The Off preset is a deliberate exception: its vLOUT/vROUT stay
         * at 0 (both the preset value AND this default would land at 0
         * if the user explicitly re-loaded Off, but in practice the user
         * doesn't run `--preset off` expecting output). */
        if ((spu94_preset_id_t)pid != SPU94_PRESET_OFF) {
            spu94_set_vLOUT(state, (int16_t)0x7FFF);
            spu94_set_vROUT(state, (int16_t)0x7FFF);
        }
    } else {
        if (spu94_cli_json_apply(config_path, state, err_buf, sizeof err_buf) != 0) {
            spu94_destroy(state);
            free(input.L); free(input.R); free(work_buf);
            SPU94_ERROR("%s", err_buf);
            return 2;
        }
    }

    /* One tick so all TICK_LATCHED (address / delay) registers commit
     * before we start processing audio. Without this the first block
     * of samples would use stale pending values. */
    spu94_tick(state);

    /* Output sample counts. Tail is optional; total = input + tail. */
    uint64_t tail_frames = (tail_seconds > 0.0)
        ? (uint64_t)(tail_seconds * (double)input.sample_rate + 0.5)
        : 0u;
    uint64_t total_out = input.num_frames + tail_frames;

    int16_t *L_out = (int16_t *)malloc((size_t)total_out * sizeof(int16_t));
    int16_t *R_out = (int16_t *)malloc((size_t)total_out * sizeof(int16_t));
    if (!L_out || !R_out) {
        free(L_out); free(R_out);
        spu94_destroy(state);
        free(input.L); free(input.R); free(work_buf);
        SPU94_ERROR("out of memory allocating output buffers");
        return 2;
    }

    enum { BLOCK = 1024u };
    uint64_t processed = 0;
    while (processed < input.num_frames) {
        uint64_t remaining = input.num_frames - processed;
        uint32_t block = (remaining < (uint64_t)BLOCK)
            ? (uint32_t)remaining
            : (uint32_t)BLOCK;
        spu94_process(state,
                      &input.L[processed], &input.R[processed],
                      &L_out[processed],   &R_out[processed],
                      block);
        processed += block;
    }

    if (tail_frames > 0) {
        uint64_t flushed = 0;
        while (flushed < tail_frames) {
            uint64_t remaining = tail_frames - flushed;
            uint32_t block = (remaining < (uint64_t)BLOCK)
                ? (uint32_t)remaining
                : (uint32_t)BLOCK;
            spu94_flush(state,
                        &L_out[input.num_frames + flushed],
                        &R_out[input.num_frames + flushed],
                        block);
            flushed += block;
        }
    }

    int wrc = spu94_cli_wav_write(out_path, L_out, R_out, total_out,
                                 input.sample_rate, err_buf, sizeof err_buf);
    free(L_out); free(R_out);
    spu94_destroy(state);
    free(input.L); free(input.R); free(work_buf);
    if (wrc != 0) {
        SPU94_ERROR("%s", err_buf);
        return 2;
    }

    return 0;
}

/* Phase 3 Plan 01: Reverb subcommand (extracted from original main.c).
 *
 * `spu94 reverb [OPTIONS] INPUT.wav OUTPUT.wav`
 * Also serves as the legacy fallback when no subcommand is given.
 *
 * Flow:
 *   argv parse (getopt_long)
 *   -> load input WAV (dr_wav, planar int16 L/R)
 *   -> allocate caller-owned spu94_state + work buffer
 *   -> apply preset OR JSON config
 *   -> optionally enable ADPCM coloration (--adpcm flag)
 *   -> spu94_tick once so TICK_LATCHED registers commit
 *   -> process input in fixed blocks
 *   -> optional flush for --tail-seconds
 *   -> write output WAV
 *   -> free everything, return 0
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

/* One-line error printing discipline (D-05). */
#define SPU94_ERROR(...) do {                            \
        fputs("spu94: error: ", stderr);                 \
        fprintf(stderr, __VA_ARGS__);                    \
        fputc('\n', stderr);                             \
    } while (0)

/* --tail-seconds hard cap. */
#define SPU94_CLI_TAIL_MS_MAX 600000u

/* Parse a non-negative decimal string into milliseconds. */
static int spu94_cli_parse_tail_ms(const char *s, uint64_t *out_ms) {
    if (s == NULL || *s == '\0' || out_ms == NULL) return -1;
    uint64_t int_part = 0;
    const char *p = s;
    int saw_digit = 0;
    while (*p >= '0' && *p <= '9') {
        if (int_part > (UINT64_MAX / 10u) - 9u) return -1;
        int_part = int_part * 10u + (uint64_t)(*p - '0');
        saw_digit = 1;
        p++;
    }
    uint64_t frac_ms = 0;
    if (*p == '.') {
        p++;
        uint32_t frac_digits = 0;
        uint64_t frac_val = 0;
        while (*p >= '0' && *p <= '9' && frac_digits < 3u) {
            frac_val = frac_val * 10u + (uint64_t)(*p - '0');
            frac_digits++;
            saw_digit = 1;
            p++;
        }
        while (frac_digits < 3u) {
            frac_val *= 10u;
            frac_digits++;
        }
        while (*p >= '0' && *p <= '9') p++;
        frac_ms = frac_val;
    }
    if (*p != '\0' || !saw_digit) return -1;
    if (int_part > UINT64_MAX / 1000u) return -1;
    uint64_t total_ms = int_part * 1000u + frac_ms;
    if (total_ms > (uint64_t)SPU94_CLI_TAIL_MS_MAX) return -1;
    *out_ms = total_ms;
    return 0;
}

static void print_reverb_help(void) {
    fputs(
        "Usage: spu94 reverb [OPTIONS] INPUT.wav OUTPUT.wav\n"
        "       spu94 [OPTIONS] INPUT.wav OUTPUT.wav  (legacy mode)\n"
        "\n"
        "Render a WAV file through the PlayStation 1 SPU reverb.\n"
        "\n"
        "Signal choice (one of):\n"
        "  --preset <name>        Apply a factory preset. See --list-presets.\n"
        "  --config <path.json>   Apply a register-map or override from JSON.\n"
        "\n"
        "Optional:\n"
        "  --adpcm                Enable ADPCM coloration (PS1 codec artifacts).\n"
        "  --tail-seconds <N>     Append N seconds of reverb tail after input ends.\n"
        "\n"
        "Utility:\n"
        "  --list-presets         Print the 10 factory preset names and exit.\n"
        "  -h, --help             Show this message and exit.\n"
        "\n"
        "Examples:\n"
        "  spu94 reverb --preset hall input.wav output.wav\n"
        "  spu94 --preset hall --tail-seconds 2 input.wav output.wav\n"
        "  spu94 reverb --adpcm --preset hall input.wav output.wav\n"
        "  spu94 --config my_override.json input.wav output.wav\n"
        "\n"
        "Input must be 16-bit PCM stereo at 44.1 kHz. Output is the same format.\n",
        stdout);
}

int cmd_reverb(int argc, char **argv) {
    static const struct option long_opts[] = {
        {"preset",       required_argument, NULL, 'p'},
        {"config",       required_argument, NULL, 'c'},
        {"tail-seconds", required_argument, NULL, 't'},
        {"adpcm",        no_argument,       NULL, 'a'},
        {"list-presets", no_argument,       NULL, 'l'},
        {"help",         no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    const char *preset_name = NULL;
    const char *config_path = NULL;
    uint64_t tail_ms = 0u;
    bool adpcm_enabled = false;
    int opt;

    /* Reset getopt state for subcommand dispatch (POSIX requires this). */
    optind = 1;
    opterr = 0;

    while ((opt = getopt_long(argc, argv, "p:c:t:alh", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'p':
                preset_name = optarg;
                break;
            case 'c':
                config_path = optarg;
                break;
            case 't':
                if (spu94_cli_parse_tail_ms(optarg, &tail_ms) != 0) {
                    SPU94_ERROR("invalid value for --tail-seconds: '%s' "
                                "(accepts 0 to %u seconds, up to ms resolution)",
                                optarg, (unsigned)(SPU94_CLI_TAIL_MS_MAX / 1000u));
                    return 2;
                }
                break;
            case 'a':
                adpcm_enabled = true;
                break;
            case 'l':
                spu94_cli_list_presets(stdout);
                return 0;
            case 'h':
                print_reverb_help();
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
    spu94_reset(state);

    /* Enable ADPCM coloration if requested (D-02/ADPCM-IO-02). */
    if (adpcm_enabled) {
        spu94_set_adpcm_enabled(state, 1);
        /* Phase 7: with the mixer architecture, ADPCM output goes to the
         * patina bus. Set patina fader and send so the ADPCM coloration
         * is audible. Without this, patina_fader=0 silences ADPCM. */
        spu94_set_patina_fader(state, 0x7FFF);
        spu94_set_patina_send(state, 0x7FFF);
    }

    int loaded_pid = -1;  /* -1 = JSON config, >= 0 = preset ID */
    if (preset_name) {
        int pid = spu94_cli_preset_id_by_name(preset_name);
        if (pid < 0) {
            char names[256];
            spu94_cli_preset_name_list(names, sizeof names);
            spu94_destroy(state);
            free(input.L); free(input.R); free(work_buf);
            SPU94_ERROR("unknown preset '%s' \xE2\x80\x94 valid: %s", preset_name, names);
            return 2;
        }
        spu94_result_t lrc = spu94_load_preset(state, (spu94_preset_id_t)pid);
        if (lrc != SPU94_OK) {
            const char *reason =
                (lrc == SPU94_WORK_BUF_TOO_SMALL)
                    ? "work buffer too small for this preset"
                : (lrc == SPU94_INVALID_ARG)
                    ? "preset id out of range (internal error)"
                : (lrc == SPU94_INVALID_STATE)
                    ? "state not initialized (internal error)"
                    : "internal error";
            spu94_destroy(state);
            free(input.L); free(input.R); free(work_buf);
            SPU94_ERROR("failed to load preset '%s': %s", preset_name, reason);
            return 2;
        }
        loaded_pid = pid;
    } else {
        if (spu94_cli_json_apply(config_path, state, err_buf, sizeof err_buf) != 0) {
            spu94_destroy(state);
            free(input.L); free(input.R); free(work_buf);
            SPU94_ERROR("%s", err_buf);
            return 2;
        }
    }

    spu94_tick(state);

    /* Phase 7: mixer faders default to 0 (silence). Set unity gains so the
     * CLI produces audible output without requiring JSON config entries.
     * Off preset (pid=0) stays silent -- no faders, matching the historic
     * "Off = silence" contract. JSON configs manage their own faders. */
    if (loaded_pid != 0) {
        spu94_set_input_gain(state, 0x7FFF);
        spu94_set_dry_fader(state, 0x7FFF);
        spu94_set_reverb_fader(state, 0x7FFF);
        spu94_set_dry_send(state, 0x7FFF);
    }

    uint64_t tail_frames = 0u;
    if (tail_ms > 0u) {
        uint64_t numer = tail_ms * (uint64_t)input.sample_rate + 500u;
        tail_frames = numer / 1000u;
    }
    if (tail_frames > UINT64_MAX - input.num_frames) {
        spu94_destroy(state);
        free(input.L); free(input.R); free(work_buf);
        SPU94_ERROR("output length overflow (input frames + tail frames too large)");
        return 2;
    }
    uint64_t total_out = input.num_frames + tail_frames;
    if (total_out > (uint64_t)(SIZE_MAX / sizeof(int16_t))) {
        spu94_destroy(state);
        free(input.L); free(input.R); free(work_buf);
        SPU94_ERROR("output length overflow (total_out exceeds host size_t range)");
        return 2;
    }

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

/* Phase 6 Plan 3: `spu94` CLI entry point.
 *
 * Task 1 (Wave 0) lands the argument-parsing skeleton, --help / --list-presets
 * handling, and the flag-combination error paths. The WAV / preset / config
 * processing pipeline is a deliberate stub here; Task 2 replaces it with the
 * full dr_wav + spu94_process + spu94_flush flow.
 *
 * Error-message discipline (D-05):
 *   - Every error path exits non-zero.
 *   - Every error writes exactly ONE line of stderr, prefixed `spu94: error:`.
 *   - No multi-line tracebacks, no stack frames, no C-compiler jargon.
 *   - Anthony is a recording/broadcast engineer, not a developer; messages
 *     read like gear manual diagnostics.
 */
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <spu94/spu94.h>

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
    (void)in_path;
    (void)out_path;
    (void)tail_seconds;

    /* Wave 0 stub: return error. Task 2 replaces this block with the full
     * WAV read + preset/config apply + spu94_process loop + tail flush +
     * WAV write. */
    SPU94_ERROR("processing pipeline not yet implemented (Wave 0)");
    return 2;
}

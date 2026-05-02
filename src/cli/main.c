/* Phase 3 Plan 01: `spu94` CLI entry point — subcommand dispatcher.
 *
 * Git-style subcommand dispatch (D-01):
 *   spu94 reverb [OPTIONS] INPUT.wav OUTPUT.wav
 *   spu94 adpcm-encode INPUT.wav OUTPUT.vag
 *   spu94 adpcm-decode INPUT.vag OUTPUT.wav
 *   spu94 adpcm-roundtrip INPUT.wav OUTPUT.wav
 *
 * No subcommand or leading '-': falls through to `cmd_reverb` (legacy mode).
 *
 * Error-message discipline (D-05):
 *   - Every error path exits non-zero (2 for user errors).
 *   - Every error writes exactly ONE line of stderr, prefixed `spu94: error:`.
 */
#include <stdio.h>
#include <string.h>

/* One-line error printing discipline (D-05). */
#define SPU94_ERROR(...) do {                            \
        fputs("spu94: error: ", stderr);                 \
        fprintf(stderr, __VA_ARGS__);                    \
        fputc('\n', stderr);                             \
    } while (0)

/* External subcommand handlers */
extern int cmd_reverb(int argc, char **argv);
extern int cmd_preset_dump(int argc, char **argv);
extern int cmd_adpcm_encode(int argc, char **argv);
extern int cmd_adpcm_decode(int argc, char **argv);
extern int cmd_adpcm_roundtrip(int argc, char **argv);

static void print_global_help(void) {
    fputs(
        "Usage: spu94 <command> [OPTIONS] ...\n"
        "\n"
        "Commands:\n"
        "  reverb             Render a WAV file through the PS1 SPU reverb.\n"
        "  preset-dump        Export a factory preset as .spu94 text.\n"
        "  adpcm-encode       Encode a WAV file to Sony ADPCM (.vag) format.\n"
        "  adpcm-decode       Decode a .vag file back to WAV.\n"
        "  adpcm-roundtrip    Encode WAV to ADPCM in memory, decode back to WAV.\n"
        "\n"
        "Run 'spu94 <command> --help' for command-specific options.\n"
        "\n"
        "Legacy mode:\n"
        "  spu94 [OPTIONS] INPUT.wav OUTPUT.wav\n"
        "  (equivalent to 'spu94 reverb [OPTIONS] INPUT.wav OUTPUT.wav')\n",
        stdout);
}

int main(int argc, char **argv) {
    /* Check for bare --help / -h before subcommand dispatch (D-03). */
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 ||
                      strcmp(argv[1], "-h") == 0)) {
        print_global_help();
        return 0;
    }

    if (argc >= 2 && argv[1][0] != '-') {
        if (strcmp(argv[1], "reverb") == 0)
            return cmd_reverb(argc - 1, argv + 1);
        if (strcmp(argv[1], "preset-dump") == 0)
            return cmd_preset_dump(argc - 1, argv + 1);
        if (strcmp(argv[1], "adpcm-encode") == 0)
            return cmd_adpcm_encode(argc - 1, argv + 1);
        if (strcmp(argv[1], "adpcm-decode") == 0)
            return cmd_adpcm_decode(argc - 1, argv + 1);
        if (strcmp(argv[1], "adpcm-roundtrip") == 0)
            return cmd_adpcm_roundtrip(argc - 1, argv + 1);
        SPU94_ERROR("unknown command '%s' (try --help)", argv[1]);
        return 2;
    }
    /* No subcommand or starts with '-': legacy reverb mode (D-01). */
    return cmd_reverb(argc, argv);
}

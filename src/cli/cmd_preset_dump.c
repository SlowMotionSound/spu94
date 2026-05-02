/* Phase 14 Plan 01: preset-dump subcommand.
 *
 * `spu94 preset-dump --preset <name> [--name <text>] [-o <file>]`
 *
 * Exports a factory preset as a .spu94 INI-style text file using the
 * C core spu94_preset_save API (Phase 13). Output goes to stdout by
 * default, or to a file with -o.
 */
#include <getopt.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <spu94/spu94.h>
#include "preset_names.h"

/* One-line error printing discipline (D-05). Each CLI TU carries its
 * own copy -- no shared macro header for these standalone commands. */
#define SPU94_ERROR(...) do {                            \
        fputs("spu94: error: ", stderr);                 \
        fprintf(stderr, __VA_ARGS__);                    \
        fputc('\n', stderr);                             \
    } while (0)

static void print_preset_dump_help(void) {
    fputs(
        "Usage: spu94 preset-dump --preset <name> [OPTIONS]\n"
        "\n"
        "Export a factory preset as a .spu94 text file.\n"
        "\n"
        "Options:\n"
        "  --preset <name>    Factory preset to export (required). See --list-presets.\n"
        "  --name <text>      Set the name metadata field (default: empty).\n"
        "  -o <file>          Write to file instead of stdout.\n"
        "  --list-presets     Print factory preset names and exit.\n"
        "  -h, --help         Show this help.\n"
        "\n"
        "Examples:\n"
        "  spu94 preset-dump --preset hall\n"
        "  spu94 preset-dump --preset hall -o hall.spu94\n"
        "  spu94 preset-dump --preset hall --name \"Dark Hall\" -o dark_hall.spu94\n",
        stdout);
}

int cmd_preset_dump(int argc, char **argv) {
    static const struct option long_opts[] = {
        {"preset",       required_argument, NULL, 'p'},
        {"name",         required_argument, NULL, 'n'},
        {"output",       required_argument, NULL, 'o'},
        {"list-presets", no_argument,       NULL, 'l'},
        {"help",         no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    const char *preset_name = NULL;
    const char *user_name   = NULL;
    const char *output_path = NULL;
    int opt;

    /* Reset getopt state for subcommand dispatch (POSIX requires this). */
    optind = 1;
    opterr = 0;

    while ((opt = getopt_long(argc, argv, "p:n:o:lh", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'p':
                preset_name = optarg;
                break;
            case 'n':
                user_name = optarg;
                break;
            case 'o':
                output_path = optarg;
                break;
            case 'l':
                spu94_cli_list_presets(stdout);
                return 0;
            case 'h':
                print_preset_dump_help();
                return 0;
            case '?':
            default:
                SPU94_ERROR("unrecognized option (try --help)");
                return 2;
        }
    }

    /* --preset is required. */
    if (!preset_name) {
        SPU94_ERROR("--preset is required (try --help)");
        return 2;
    }

    /* Resolve preset name to id. */
    int pid = spu94_cli_preset_id_by_name(preset_name);
    if (pid < 0) {
        char names[256];
        spu94_cli_preset_name_list(names, sizeof names);
        SPU94_ERROR("unknown preset '%s' \xE2\x80\x94 valid: %s", preset_name, names);
        return 2;
    }

    /* Allocate engine state + work buffer (one-shot CLI command). */
    enum { WORK_BUF_SIZE = 512u * 1024u };
    alignas(SPU94_STATE_ALIGN_MAX) static unsigned char state_buf[SPU94_STATE_SIZE_MAX];
    unsigned char *work_buf = (unsigned char *)malloc((size_t)WORK_BUF_SIZE);
    if (!work_buf) {
        SPU94_ERROR("out of memory allocating work buffer");
        return 1;
    }

    spu94_state *state = spu94_init(state_buf, SPU94_STATE_SIZE_MAX,
                                    work_buf, (size_t)WORK_BUF_SIZE);
    if (!state) {
        free(work_buf);
        SPU94_ERROR("spu94_init failed (internal error)");
        return 1;
    }
    spu94_reset(state);

    spu94_result_t lrc = spu94_load_preset(state, (spu94_preset_id_t)pid);
    if (lrc != SPU94_OK) {
        spu94_destroy(state);
        free(work_buf);
        SPU94_ERROR("failed to load preset '%s'", preset_name);
        return 1;
    }

    /* Serialize to INI-style text. */
    char buf[SPU94_PRESET_BUF_SIZE];
    int written = spu94_preset_save(state, user_name, NULL, buf, sizeof buf);
    spu94_destroy(state);
    free(work_buf);

    if (written < 0) {
        SPU94_ERROR("failed to serialize preset");
        return 1;
    }

    /* Output to stdout or file. */
    if (!output_path) {
        fwrite(buf, 1, (size_t)written, stdout);
    } else {
        FILE *fp = fopen(output_path, "w");
        if (!fp) {
            SPU94_ERROR("cannot open '%s' for writing", output_path);
            return 1;
        }
        fwrite(buf, 1, (size_t)written, fp);
        fclose(fp);
    }

    return 0;
}

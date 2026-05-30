/* Phase 6 Plan 3 Task 2: preset-name resolver (full implementation).
 *
 * The 10 factory presets expose human-readable display names in
 * `spu94_presets[i].name` ("Hall", "Half Echo", "Studio A", ...). This
 * translation layer normalizes those into the lowercase-underscore
 * canonical names the CLI uses on the command line ("hall",
 * "half_echo", "studio_a") and exposes:
 *   - --preset <name> resolution (case-insensitive, space↔underscore)
 *   - --list-presets output (stdout, one name per line)
 *   - error-message list-of-valid-names (for D-05 error shape)
 */
#include "preset_names.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Normalize a user-provided name to canonical form: lowercase + any
 * space converted to underscore. Writes at most (out_size - 1) chars
 * plus NUL terminator. Returns the number of chars written (excluding
 * the terminator). */
static size_t normalize_name(const char *in, char *out, size_t out_size) {
    if (!out_size) return 0;
    size_t i = 0;
    if (in) {
        for (; i + 1 < out_size && in[i] != '\0'; ++i) {
            unsigned char c = (unsigned char)in[i];
            if (c == ' ') {
                out[i] = '_';
            } else {
                out[i] = (char)tolower(c);
            }
        }
    }
    out[i] = '\0';
    return i;
}

int spu94_cli_preset_id_by_name(const char *name) {
    if (!name) return -1;
    char want[64];
    normalize_name(name, want, sizeof want);
    if (want[0] == '\0') return -1;
    for (int i = 0; i < (int)SPU94_PRESET__COUNT; ++i) {
        const char *display = spu94_presets[i].name;
        if (!display) continue;
        char canonical[64];
        normalize_name(display, canonical, sizeof canonical);
        if (strcmp(want, canonical) == 0) return i;
    }
    return -1;
}

static const char *spu94_cli_preset_canonical_name(int preset_id) {
    /* Cache canonical names on first call — amortized O(1) lookup on
     * subsequent calls, which we do a lot (every --list-presets +
     * every error-message assembly). */
    static char cache[SPU94_PRESET__COUNT][64];
    static int  initialized = 0;
    if (!initialized) {
        for (int i = 0; i < (int)SPU94_PRESET__COUNT; ++i) {
            const char *display = spu94_presets[i].name;
            normalize_name(display, cache[i], sizeof cache[i]);
        }
        initialized = 1;
    }
    if (preset_id < 0 || preset_id >= (int)SPU94_PRESET__COUNT) {
        return NULL;
    }
    return cache[preset_id];
}

void spu94_cli_list_presets(FILE *out) {
    if (!out) return;
    for (int i = 0; i < (int)SPU94_PRESET__COUNT; ++i) {
        const char *n = spu94_cli_preset_canonical_name(i);
        if (n && n[0] != '\0') {
            fprintf(out, "%s\n", n);
        }
    }
}

void spu94_cli_preset_name_list(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    buf[0] = '\0';
    size_t pos = 0;
    for (int i = 0; i < (int)SPU94_PRESET__COUNT; ++i) {
        const char *n = spu94_cli_preset_canonical_name(i);
        if (!n || n[0] == '\0') continue;
        const char *sep = (pos == 0) ? "" : ", ";
        int w = snprintf(buf + pos, buf_size - pos, "%s%s", sep, n);
        if (w <= 0) return;
        if ((size_t)w >= buf_size - pos) return;  /* truncation — stop early */
        pos += (size_t)w;
    }
}

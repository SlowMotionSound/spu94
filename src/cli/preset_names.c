/* Phase 6 Plan 3 Task 1: Wave-0 stub.
 *
 * Compile-ready empty bodies so the spu94_cli target builds. Task 2 fills
 * these in with the real name-normalization logic.
 */
#include "preset_names.h"

#include <string.h>

int spu94_cli_preset_id_by_name(const char *name) {
    (void)name;
    return -1;
}

const char *spu94_cli_preset_canonical_name(int preset_id) {
    (void)preset_id;
    return NULL;
}

void spu94_cli_list_presets(FILE *out) {
    (void)out;
}

void spu94_cli_preset_name_list(char *buf, size_t buf_size) {
    if (buf && buf_size) {
        buf[0] = '\0';
    }
}

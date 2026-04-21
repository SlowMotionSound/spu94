/* Phase 6 Plan 3 Task 1: Wave-0 stub for --config JSON parsing.
 *
 * Task 2 replaces this with the real jsmn-based parser that auto-detects
 * override / flat shape and validates register names + value ranges.
 *
 * Wave 0 does NOT actually parse JSON — the stub returns a one-line error
 * so the CLI exits cleanly. We intentionally skip the jsmn.h include here
 * because jsmn's JSMN_STATIC mode makes jsmn_init / jsmn_parse file-static,
 * which trips -Werror=unused-function under the CLI target's warning set.
 * Task 2 will both #include <jsmn.h> and call jsmn_init + jsmn_parse, at
 * which point the functions are "used" and the warning disappears. */

#include "json_config.h"

#include <stdio.h>

int spu94_cli_json_apply(const char *path, spu94_state *state,
                        char *err_buf, size_t err_buf_size) {
    (void)path;
    (void)state;
    if (err_buf && err_buf_size) {
        snprintf(err_buf, err_buf_size, "JSON parser not implemented (Wave 0 stub)");
    }
    return 1;
}

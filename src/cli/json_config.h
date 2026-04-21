#ifndef SPU94_CLI_JSON_CONFIG_H
#define SPU94_CLI_JSON_CONFIG_H

#include <stddef.h>
#include <spu94/spu94.h>

/* Phase 6 Plan 3: JSON --config parser for the spu94 CLI.
 *
 * Two accepted shapes (auto-detected by the presence of a top-level "base"
 * key):
 *   - Override: `{ "base": "hall", "overrides": { "vIIR": -8000 } }`
 *   - Flat:     `{ "vLOUT": 0, "vROUT": 0, ..., "vRIN": 0 }` (all 35 regs)
 *
 * Values may be JSON integers ("vIIR": -8000) OR hex strings
 * ("dCOMB1": "0x1000"). Signed / unsigned range checks are per-register.
 *
 * jsmn is header-only with JSMN_STATIC; the single-TU expansion
 * happens inside json_config.c so downstream TUs see only the
 * function surface. */

#ifdef __cplusplus
extern "C" {
#endif

/* Apply a JSON config file to `state`.
 * Returns 0 on success; non-zero on error.
 * Error messages land in err_buf (single line, no `spu94: error:` prefix —
 * main.c prepends that). */
int spu94_cli_json_apply(const char *path, spu94_state *state,
                        char *err_buf, size_t err_buf_size);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_CLI_JSON_CONFIG_H */

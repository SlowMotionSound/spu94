#ifndef SPU94_CLI_PRESET_NAMES_H
#define SPU94_CLI_PRESET_NAMES_H

#include <spu94/spu94.h>
#include <stdio.h>
#include <stddef.h>

/* Phase 6 Plan 3: Preset-name resolver for the spu94 CLI.
 *
 * The 10 factory presets in `spu94_presets[]` carry human-readable display
 * names ("Hall", "Half Echo", "Studio A", ...). The CLI accepts
 * lowercase-underscore canonical names on the command line ("hall",
 * "half_echo", "studio_a"). The translation lives here so `main.c` does
 * not carry the normalization logic. */

#ifdef __cplusplus
extern "C" {
#endif

/* Return the preset_id (0..9) for the given user-provided name, or -1 if
 * the name does not match any of the 10 factory presets.
 *
 * Matching is case-insensitive and treats spaces as equivalent to
 * underscores: both "studio_a" and "Studio A" and "STUDIO_A" resolve to 2. */
int spu94_cli_preset_id_by_name(const char *name);

/* Return the canonical lowercase-underscore display name for a preset id,
 * or NULL if id is out of range. Example: 5 -> "hall"; 2 -> "studio_a". */
const char *spu94_cli_preset_canonical_name(int preset_id);

/* Print the 10 preset names, one per line, to `out`. Used by --list-presets. */
void spu94_cli_list_presets(FILE *out);

/* Build a comma-separated string of all preset canonical names into `buf`.
 * Used in error messages. buf_size must be >= 128 (10 names ~ 12 chars each). */
void spu94_cli_preset_name_list(char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_CLI_PRESET_NAMES_H */

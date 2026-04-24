/* Phase 6 Plan 3 Task 2: --config JSON parser (jsmn-backed).
 *
 * Grammar (auto-detected by the presence of a top-level "base" key):
 *
 *   Override:  { "base": "hall", "overrides": { "vIIR": -8000, ... } }
 *   Flat:      { "vLOUT": 0, "vROUT": 0, ..., "vRIN": 0 }   (all 35 regs)
 *
 * Values may be JSON integers (decimal, including negative) OR hex
 * strings ("0x1000" or "-0x40"). Per-register signedness determines the
 * valid range:
 *   - I16 family (12 v-prefix regs):   [-32768, 32767]
 *   - U16 family (mBASE + 22 addr/dly): [0, 65535]
 *
 * Every error produces a single-line message in err_buf (no `spu94: error:`
 * prefix; main.c adds that). Unknown register names, out-of-range values,
 * malformed JSON, and missing fields all map to the same one-line contract.
 */
#define JSMN_STATIC
#include <jsmn.h>

#include "json_config.h"
#include "preset_names.h"

#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 1024 tokens caps worst-case: 35 keys * 2 tokens + container markers +
 * an "overrides" sub-object with 35 more pairs = ~150 tokens. 1024 is
 * >6x safety margin. Overflow maps to JSMN_ERROR_NOMEM (parse error). */
#define MAX_TOKENS 1024

/* Compare a jsmn token's text span against a C string. */
static int tok_streq(const char *json, const jsmntok_t *t, const char *s) {
    size_t slen = strlen(s);
    size_t tlen = (size_t)(t->end - t->start);
    return tlen == slen && memcmp(json + t->start, s, slen) == 0;
}

/* Copy a jsmn token's text span into a caller buffer (NUL-terminated).
 * Truncates silently if the span is longer than out_size - 1. */
static void tok_copy(const char *json, const jsmntok_t *t,
                     char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    size_t tlen = (size_t)(t->end - t->start);
    size_t copy_len = (tlen < out_size - 1) ? tlen : (out_size - 1);
    memcpy(out, json + t->start, copy_len);
    out[copy_len] = '\0';
}

/* Parse a value token as JSON integer OR hex-string ("0x..." or "-0x...").
 * Writes the signed 32-bit result to *out. Returns 0 on success, -1 on
 * any form of malformation (non-numeric, overflow, wrong token type). */
static int parse_int_or_hex(const char *json, const jsmntok_t *t, int32_t *out) {
    size_t len = (size_t)(t->end - t->start);
    if (len == 0 || len >= 32) return -1;
    char buf[32];
    memcpy(buf, json + t->start, len);
    buf[len] = '\0';

    char *endp = NULL;
    long v = 0;
    errno = 0;

    if (t->type == JSMN_STRING) {
        /* Hex-string form: "0xABCD" or "-0xABCD". Underlying integer must
         * fit in signed 32 bits. */
        const char *s = buf;
        int negative = 0;
        if (*s == '-') { negative = 1; ++s; }
        else if (*s == '+') { ++s; }
        if (s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) return -1;
        if (s[2] == '\0') return -1;  /* bare "0x" */
        v = strtol(s + 2, &endp, 16);
        if (negative) v = -v;
    } else if (t->type == JSMN_PRIMITIVE) {
        /* Decimal form, possibly negative. jsmn's PRIMITIVE includes
         * true/false/null, which will fail strtol's trailing-character
         * check below. */
        v = strtol(buf, &endp, 10);
    } else {
        return -1;
    }

    if (errno != 0) return -1;
    if (!endp || *endp != '\0') return -1;
    if (v < INT_MIN || v > INT_MAX) return -1;
    *out = (int32_t)v;
    return 0;
}

/* Find the spu94 register id for a jsmn key token; returns -1 if the
 * token's text doesn't match any of the 35 reflected names. */
static int find_reg_by_name(const char *json, const jsmntok_t *tkey) {
    size_t tlen = (size_t)(tkey->end - tkey->start);
    if (tlen == 0 || tlen >= 64) return -1;
    char name[64];
    memcpy(name, json + tkey->start, tlen);
    name[tlen] = '\0';
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        const char *n = spu94_reg_name((spu94_reg_t)i);
        if (n && strcmp(name, n) == 0) return i;
    }
    return -1;
}

/* Number of tokens consumed by the value starting at tokens[idx].
 * Handles JSMN_OBJECT and JSMN_ARRAY containers by walking their
 * children (recursively). Returns the total token count (1 for a
 * scalar, 1 + sum-of-child-sizes for a container). */
static int token_span(const jsmntok_t *tokens, int idx, int n) {
    if (idx >= n) return 0;
    const jsmntok_t *t = &tokens[idx];
    int consumed = 1;
    if (t->type == JSMN_OBJECT) {
        /* Each of t->size children is a (key, value) pair. */
        int remaining_pairs = t->size;
        int cursor = idx + 1;
        while (remaining_pairs > 0 && cursor < n) {
            /* Key */
            consumed += 1; cursor += 1;
            if (cursor >= n) break;
            /* Value (recurse for containers) */
            int sub = token_span(tokens, cursor, n);
            consumed += sub;
            cursor   += sub;
            remaining_pairs -= 1;
        }
    } else if (t->type == JSMN_ARRAY) {
        int remaining = t->size;
        int cursor = idx + 1;
        while (remaining > 0 && cursor < n) {
            int sub = token_span(tokens, cursor, n);
            consumed += sub;
            cursor   += sub;
            remaining -= 1;
        }
    }
    return consumed;
}

/* Apply a single (reg_name_key, value) pair to the spu94_state. Returns
 * 0 on success; non-zero on error with an explanatory err_buf message. */
static int apply_one(const char *json,
                    const jsmntok_t *tkey, const jsmntok_t *tval,
                    spu94_state *state,
                    char *err_buf, size_t err_buf_size) {
    /* H-04: keys >= 64 chars are silently rejected by find_reg_by_name
     * (its name buffer is 64). Surface that case distinctly so the user
     * sees "key is too long" instead of a misleading "unknown register
     * 'FIRST_63_CHARS_OF_GIBBERISH'" message. The longest canonical
     * register name is 7 chars (e.g. mLCOMB1), so 64 is plenty for
     * legitimate keys; anything larger is malformed config. */
    size_t key_len = (size_t)(tkey->end - tkey->start);
    if (key_len >= 64) {
        char preview[16];
        size_t ph = (key_len < 12) ? key_len : 12;
        memcpy(preview, json + tkey->start, ph);
        preview[ph] = '\0';
        snprintf(err_buf, err_buf_size,
                 "JSON key starting '%s...' is %zu chars long; expected "
                 "one of the 35 register names (none exceed 7 chars)",
                 preview, key_len);
        return 1;
    }
    int reg = find_reg_by_name(json, tkey);
    if (reg < 0) {
        char name[64];
        tok_copy(json, tkey, name, sizeof name);
        snprintf(err_buf, err_buf_size,
                 "unknown register '%s' (use one of the 35 canonical names like vIIR, mBASE, mLCOMB1)",
                 name);
        return 1;
    }

    int32_t value = 0;
    if (parse_int_or_hex(json, tval, &value) != 0) {
        char vb[64];
        tok_copy(json, tval, vb, sizeof vb);
        snprintf(err_buf, err_buf_size,
                 "invalid value '%s' for register '%s' (expected integer or hex string)",
                 vb, spu94_reg_name((spu94_reg_t)reg));
        return 1;
    }

    spu94_reg_type_t type = spu94_reg_type((spu94_reg_t)reg);
    if (type == SPU94_REG_TYPE_I16) {
        if (value < INT16_MIN || value > INT16_MAX) {
            snprintf(err_buf, err_buf_size,
                     "value %ld for register '%s' out of range [%d..%d]",
                     (long)value, spu94_reg_name((spu94_reg_t)reg),
                     (int)INT16_MIN, (int)INT16_MAX);
            return 1;
        }
        spu94_set_reg_i16(state, (spu94_reg_t)reg, (int16_t)value);
    } else {
        if (value < 0 || value > 0xFFFF) {
            snprintf(err_buf, err_buf_size,
                     "value %ld for register '%s' out of range [0..65535]",
                     (long)value, spu94_reg_name((spu94_reg_t)reg));
            return 1;
        }
        spu94_set_reg_u16(state, (spu94_reg_t)reg, (uint16_t)value);
    }
    return 0;
}

int spu94_cli_json_apply(const char *path, spu94_state *state,
                        char *err_buf, size_t err_buf_size) {
    if (!path || !state) {
        if (err_buf && err_buf_size) {
            snprintf(err_buf, err_buf_size, "internal error: NULL argument to json_apply");
        }
        return 1;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        snprintf(err_buf, err_buf_size, "config file '%s' not found", path);
        return 1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        snprintf(err_buf, err_buf_size, "could not measure size of '%s'", path);
        return 1;
    }
    long fs = ftell(fp);
    if (fs < 0 || fs > 1024L * 1024L) {
        fclose(fp);
        snprintf(err_buf, err_buf_size,
                 "config file '%s' is empty or larger than 1 MB", path);
        return 1;
    }
    rewind(fp);

    char *json = (char *)malloc((size_t)fs + 1);
    if (!json) {
        fclose(fp);
        snprintf(err_buf, err_buf_size, "out of memory reading '%s'", path);
        return 1;
    }
    size_t got = fread(json, 1, (size_t)fs, fp);
    fclose(fp);
    json[got] = '\0';

    jsmn_parser p;
    jsmntok_t tokens[MAX_TOKENS];
    jsmn_init(&p);
    int n = jsmn_parse(&p, json, got, tokens, MAX_TOKENS);
    if (n < 0 || n == 0 || tokens[0].type != JSMN_OBJECT) {
        free(json);
        snprintf(err_buf, err_buf_size,
                 "invalid JSON in '%s' (expected top-level object)", path);
        return 1;
    }

    /* Strict-shape check: jsmn is intentionally non-strict and will
     * happily tokenize `{ not valid json }` as an object containing
     * three bare primitives. Our schema requires every key at any
     * depth to be a JSMN_STRING. Walk the token array and reject
     * anything else with the same "invalid JSON" message shape as
     * a negative parse result. */
    {
        int pairs = tokens[0].size;
        int cursor = 1;
        for (int k = 0; k < pairs && cursor < n; ++k) {
            if (tokens[cursor].type != JSMN_STRING) {
                free(json);
                snprintf(err_buf, err_buf_size,
                         "invalid JSON in '%s' (non-string key at top level)", path);
                return 1;
            }
            cursor += 1;
            if (cursor >= n) {
                free(json);
                snprintf(err_buf, err_buf_size,
                         "invalid JSON in '%s' (truncated key/value pair)", path);
                return 1;
            }
            cursor += token_span(tokens, cursor, n);
        }
    }

    /* Auto-detect shape by scanning top-level keys for "base". We use
     * token_span to skip container children when walking. */
    int has_base = 0;
    {
        int pairs = tokens[0].size;
        int cursor = 1;
        for (int k = 0; k < pairs && cursor + 1 < n; ++k) {
            const jsmntok_t *key = &tokens[cursor];
            if (key->type == JSMN_STRING && tok_streq(json, key, "base")) {
                has_base = 1;
                break;
            }
            cursor += 1;  /* past key */
            cursor += token_span(tokens, cursor, n);  /* past value */
        }
    }

    if (has_base) {
        /* Override shape. */
        const jsmntok_t *base_val = NULL;
        const jsmntok_t *overrides = NULL;
        int overrides_idx = -1;
        int pairs = tokens[0].size;
        int cursor = 1;
        for (int k = 0; k < pairs && cursor + 1 < n; ++k) {
            const jsmntok_t *tkey = &tokens[cursor];
            const jsmntok_t *tval = &tokens[cursor + 1];
            if (tkey->type == JSMN_STRING) {
                if (tok_streq(json, tkey, "base")) {
                    base_val = tval;
                } else if (tok_streq(json, tkey, "overrides")) {
                    overrides = tval;
                    overrides_idx = cursor + 1;
                }
            }
            cursor += 1;
            cursor += token_span(tokens, cursor, n);
        }

        if (!base_val || base_val->type != JSMN_STRING) {
            free(json);
            snprintf(err_buf, err_buf_size,
                     "'%s' has \"base\" but its value is not a preset name string", path);
            return 1;
        }
        char bname[64];
        tok_copy(json, base_val, bname, sizeof bname);
        int pid = spu94_cli_preset_id_by_name(bname);
        if (pid < 0) {
            char names[256];
            spu94_cli_preset_name_list(names, sizeof names);
            free(json);
            snprintf(err_buf, err_buf_size,
                     "unknown preset '%s' in '%s' — valid: %s", bname, path, names);
            return 1;
        }
        spu94_result_t rc = spu94_load_preset(state, (spu94_preset_id_t)pid);
        if (rc != SPU94_OK) {
            free(json);
            snprintf(err_buf, err_buf_size,
                     "load_preset failed for base '%s' (code %d)", bname, (int)rc);
            return 1;
        }

        if (overrides) {
            if (overrides->type != JSMN_OBJECT) {
                free(json);
                snprintf(err_buf, err_buf_size,
                         "'%s' \"overrides\" must be a JSON object", path);
                return 1;
            }
            int ov_pairs = overrides->size;
            int cur = overrides_idx + 1;
            for (int k = 0; k < ov_pairs && cur + 1 < n; ++k) {
                const jsmntok_t *tkey = &tokens[cur];
                const jsmntok_t *tval = &tokens[cur + 1];
                if (apply_one(json, tkey, tval, state, err_buf, err_buf_size)) {
                    free(json);
                    return 1;
                }
                cur += 1;
                cur += token_span(tokens, cur, n);
            }
        }
    } else {
        /* Flat shape — all 35 registers required. */
        int pairs = tokens[0].size;
        if (pairs != (int)SPU94_REG__COUNT) {
            free(json);
            snprintf(err_buf, err_buf_size,
                     "flat config '%s' must specify all %d registers (found %d)",
                     path, (int)SPU94_REG__COUNT, pairs);
            return 1;
        }
        /* HI-05: jsmn counts duplicate keys as separate pairs, so a flat
         * config of `{"vIIR": ..., "vIIR": ..., <33 others>}` passes the
         * count check above. Track which register IDs have been applied
         * and reject duplicates explicitly — otherwise the second write
         * silently overrides the first and the missing register sits at
         * its post-reset zero value. */
        bool seen[SPU94_REG__COUNT] = {0};
        int cur = 1;
        for (int k = 0; k < pairs && cur + 1 < n; ++k) {
            const jsmntok_t *tkey = &tokens[cur];
            const jsmntok_t *tval = &tokens[cur + 1];
            int reg = find_reg_by_name(json, tkey);
            if (reg >= 0 && reg < (int)SPU94_REG__COUNT) {
                if (seen[reg]) {
                    free(json);
                    snprintf(err_buf, err_buf_size,
                             "duplicate register '%s' in flat config '%s'",
                             spu94_reg_name((spu94_reg_t)reg), path);
                    return 1;
                }
                seen[reg] = true;
            }
            /* Unknown-register / malformed-value errors still surface
             * through apply_one's existing message shapes. */
            if (apply_one(json, tkey, tval, state, err_buf, err_buf_size)) {
                free(json);
                return 1;
            }
            cur += 1;
            cur += token_span(tokens, cur, n);
        }
    }

    free(json);
    return 0;
}

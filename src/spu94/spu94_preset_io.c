/* spu94_preset_io.c -- User-preset serialization (Phase 13)
 *
 * Implements spu94_preset_save (Plan 01) and spu94_preset_load (Plan 02).
 *
 * Design constraints:
 *   - Zero heap: caller provides buffer, library fills it.
 *   - No FP types, no heap functions, no unqualified L-O-N-G keyword.
 *   - Uses public accessors only (no internal header).
 *   - All 16-bit values formatted as 4-digit hex (0xNNNN).
 *   - Booleans formatted as 0/1.
 */

#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Hand-rolled hex parser: avoids strtol (whose return type triggers the
 * grep-guard keyword ban in core sources).  Handles optional 0x prefix,
 * exactly 1-4 hex digits.  Returns 0 on invalid input. */
static uint16_t parse_hex_u16(const char *s)
{
    if (!s) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    uint16_t val = 0;
    int digits = 0;
    for (; *s && digits < 4; s++, digits++) {
        uint16_t nibble;
        if (*s >= '0' && *s <= '9')      nibble = (uint16_t)(*s - '0');
        else if (*s >= 'a' && *s <= 'f') nibble = (uint16_t)(*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') nibble = (uint16_t)(*s - 'A' + 10);
        else return 0;
        val = (uint16_t)((val << 4) | nibble);
    }
    return (digits > 0) ? val : 0;
}

/* -----------------------------------------------------------------------
 * spu94_preset_save — serialize full engine state to INI-style text
 * ----------------------------------------------------------------------- */

int spu94_preset_save(const spu94_state *state,
                      const char *name,
                      const char *description,
                      char *buf, size_t buf_size)
{
    if (!state || !buf || buf_size == 0) return -1;

    int pos = 0;
    int n;

    /* Macro-like overflow check after each snprintf.  If the write would
     * overflow buf_size, return -2 (buffer too small). */
#define EMIT(...)                                              \
    do {                                                       \
        n = snprintf(buf + pos, buf_size - (size_t)pos, __VA_ARGS__); \
        if (n < 0 || (size_t)(pos + n) >= buf_size) return -2; \
        pos += n;                                              \
    } while (0)

    /* Version header (first line, per D-01 / D-03) */
    EMIT("version=1\n");

    /* Metadata (D-04): name and description before any section */
    EMIT("name=%.64s\n", name ? name : "");
    EMIT("description=%.256s\n", description ? description : "");

    /* ---- [registers] section ---- */
    EMIT("\n[registers]\n");
    EMIT("# SPU reverb registers (35 values, hex)\n");

    int16_t regs[SPU94_REG__COUNT];
    spu94_snapshot_registers(state, regs);

    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        /* Cast chain (unsigned)(uint16_t) prevents sign-extension of negative
         * int16 values through variadic promotion (RESEARCH.md Pitfall 1). */
        EMIT("%s=0x%04X\n",
             spu94_reg_name((spu94_reg_t)r),
             (unsigned)(uint16_t)regs[r]);
    }

    /* ---- [mixer] section ---- */
    EMIT("\n[mixer]\n");
    EMIT("# Mixer faders (Q15 hex) and latency compensation\n");

    EMIT("input_gain=0x%04X\n",   (unsigned)(uint16_t)spu94_get_input_gain(state));
    EMIT("dry_fader=0x%04X\n",    (unsigned)(uint16_t)spu94_get_dry_fader(state));
    EMIT("patina_fader=0x%04X\n", (unsigned)(uint16_t)spu94_get_patina_fader(state));
    EMIT("dry_send=0x%04X\n",     (unsigned)(uint16_t)spu94_get_dry_send(state));
    EMIT("patina_send=0x%04X\n",  (unsigned)(uint16_t)spu94_get_patina_send(state));
    EMIT("reverb_fader=0x%04X\n", (unsigned)(uint16_t)spu94_get_reverb_fader(state));
    EMIT("latency_comp=%d\n",     spu94_get_latency_comp(state));

    /* ---- [dac] section ---- */
    EMIT("\n[dac]\n");
    EMIT("# DAC coloration toggles\n");

    EMIT("dac_enabled=%d\n",          spu94_get_dac_enabled(state));
    EMIT("dac_fir_enabled=%d\n",      spu94_get_dac_fir_enabled(state));
    EMIT("dac_noise_enabled=%d\n",    spu94_get_dac_noise_enabled(state));
    EMIT("dac_true_oversample=%d\n",  spu94_get_dac_true_oversample(state));

#undef EMIT

    buf[pos] = '\0';
    return pos;
}

/* -----------------------------------------------------------------------
 * spu94_preset_load — section-aware INI parser (Phase 13 Plan 02)
 * ----------------------------------------------------------------------- */

/* Section state machine for the line parser. */
typedef enum {
    SECTION_NONE = 0,
    SECTION_REGISTERS,
    SECTION_MIXER,
    SECTION_DAC
} preset_section_t;

/* Parse a boolean "0" or "1" value. Returns 0 or 1 on success, -1 on
 * invalid input (anything other than a single 0/1 digit). */
static int parse_bool(const char *s)
{
    if (!s) return -1;
    if (s[0] == '1' && (s[1] == '\0' || s[1] == '\n' || s[1] == '\r'))
        return 1;
    if (s[0] == '0' && (s[1] == '\0' || s[1] == '\n' || s[1] == '\r'))
        return 0;
    return -1;
}

spu94_result_t spu94_preset_load(spu94_state *state,
                                 const char *buf, size_t buf_len)
{
    if (!state) return SPU94_INVALID_STATE;
    if (!buf || buf_len == 0) return SPU94_INVALID_ARG;

    const char *p = buf;
    const char *end = buf + buf_len;
    preset_section_t section = SECTION_NONE;
    char line[512];

    while (p < end) {
        /* Find end of current line */
        const char *nl = p;
        while (nl < end && *nl != '\n') nl++;

        /* Copy line into local buffer (truncate if > 511 chars) */
        size_t line_len = (size_t)(nl - p);
        if (line_len >= sizeof line) line_len = sizeof line - 1;
        memcpy(line, p, line_len);
        line[line_len] = '\0';

        /* Advance past the newline */
        p = (nl < end) ? nl + 1 : end;

        /* Strip trailing \r */
        if (line_len > 0 && line[line_len - 1] == '\r') {
            line_len--;
            line[line_len] = '\0';
        }

        /* Skip empty lines */
        if (line[0] == '\0') continue;

        /* Skip comment lines */
        if (line[0] == '#') continue;

        /* Check for section headers */
        if (strcmp(line, "[registers]") == 0) {
            section = SECTION_REGISTERS;
            continue;
        }
        if (strcmp(line, "[mixer]") == 0) {
            section = SECTION_MIXER;
            continue;
        }
        if (strcmp(line, "[dac]") == 0) {
            section = SECTION_DAC;
            continue;
        }

        /* Find the '=' separator */
        char *eq = strchr(line, '=');
        if (!eq) continue;  /* malformed line, skip (D-09 tolerance) */

        /* Split into key and value */
        *eq = '\0';
        const char *key = line;
        const char *value = eq + 1;

        /* Dispatch by section */
        switch (section) {
        case SECTION_NONE:
            /* Metadata keys: version, name, description -- not stored in
             * engine state. Unknown keys silently ignored (D-09). */
            break;

        case SECTION_REGISTERS:
            /* Match key against the register name table */
            for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
                if (strcmp(key, spu94_reg_name((spu94_reg_t)r)) == 0) {
                    uint16_t val = parse_hex_u16(value);
                    if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
                        (void)spu94_set_reg_i16(state, (spu94_reg_t)r,
                                                (int16_t)val);
                    } else {
                        (void)spu94_set_reg_u16(state, (spu94_reg_t)r, val);
                    }
                    break;
                }
            }
            /* If no register matches, silently ignored (D-09) */
            break;

        case SECTION_MIXER:
            if (strcmp(key, "input_gain") == 0)
                spu94_set_input_gain(state, (int16_t)parse_hex_u16(value));
            else if (strcmp(key, "dry_fader") == 0)
                spu94_set_dry_fader(state, (int16_t)parse_hex_u16(value));
            else if (strcmp(key, "patina_fader") == 0)
                spu94_set_patina_fader(state, (int16_t)parse_hex_u16(value));
            else if (strcmp(key, "dry_send") == 0)
                spu94_set_dry_send(state, (int16_t)parse_hex_u16(value));
            else if (strcmp(key, "patina_send") == 0)
                spu94_set_patina_send(state, (int16_t)parse_hex_u16(value));
            else if (strcmp(key, "reverb_fader") == 0)
                spu94_set_reverb_fader(state, (int16_t)parse_hex_u16(value));
            else if (strcmp(key, "latency_comp") == 0) {
                int b = parse_bool(value);
                if (b >= 0) spu94_set_latency_comp(state, b);
            }
            /* else: unknown key, silently ignored (D-09) */
            break;

        case SECTION_DAC:
            if (strcmp(key, "dac_enabled") == 0) {
                int b = parse_bool(value);
                if (b >= 0) spu94_set_dac_enabled(state, b);
            } else if (strcmp(key, "dac_fir_enabled") == 0) {
                int b = parse_bool(value);
                if (b >= 0) spu94_set_dac_fir_enabled(state, b);
            } else if (strcmp(key, "dac_noise_enabled") == 0) {
                int b = parse_bool(value);
                if (b >= 0) spu94_set_dac_noise_enabled(state, b);
            } else if (strcmp(key, "dac_true_oversample") == 0) {
                int b = parse_bool(value);
                if (b >= 0) spu94_set_dac_true_oversample(state, b);
            }
            /* else: unknown key, silently ignored (D-09) */
            break;
        }
    }

    return SPU94_OK;
}

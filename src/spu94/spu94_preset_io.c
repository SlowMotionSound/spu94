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
    EMIT("reverb_fader=0x%04X\n",  (unsigned)(uint16_t)spu94_get_reverb_fader(state));
    EMIT("sampler_fader=0x%04X\n", (unsigned)(uint16_t)spu94_get_sampler_fader(state));
    EMIT("sampler_send=0x%04X\n",  (unsigned)(uint16_t)spu94_get_sampler_send(state));
    EMIT("latency_comp=%d\n",     spu94_get_latency_comp(state));

    /* ---- [dac] section ---- */
    EMIT("\n[dac]\n");
    EMIT("# DAC coloration toggles\n");

    EMIT("dac_enabled=%d\n",          spu94_get_dac_enabled(state));
    EMIT("dac_fir_enabled=%d\n",      spu94_get_dac_fir_enabled(state));
    EMIT("dac_noise_enabled=%d\n",    spu94_get_dac_noise_enabled(state));
    EMIT("dac_true_oversample=%d\n",  spu94_get_dac_true_oversample(state));

    /* ---- [morph] section ---- */
    EMIT("\n[morph]\n");
    EMIT("# Morph Grit: int (default, hardware-faithful) or fract (smoothed)\n");
    EMIT("grit=%s\n",
         spu94_get_morph_grit(state) == SPU94_GRIT_FRACT ? "fract" : "int");

    /* ---- [user_slot N] sections (one per filled slot) ----
     * Empty slots are omitted entirely so existing presets without user
     * customization stay byte-identical to their pre-feature serializations
     * (loader treats a missing section as "all empty"). */
    for (int s = 0; s < SPU94_INTERP_USER_SLOT_COUNT; s++) {
        if (!spu94_interp_user_slot_is_filled(state, s)) continue;
        int16_t slot_regs[SPU94_REG__COUNT];
        spu94_interp_get_user_slot(state, s, slot_regs);
        EMIT("\n[user_slot %d]\n", s);
        EMIT("# User waypoint slot %d (morph position %d/16)\n", s, 2 * s + 1);
        for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
            EMIT("%s=0x%04X\n",
                 spu94_reg_name((spu94_reg_t)r),
                 (unsigned)(uint16_t)slot_regs[r]);
        }
    }

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
    SECTION_DAC,
    SECTION_MORPH,
    SECTION_USER_SLOT
} preset_section_t;

/* Parse a "[user_slot N]" header. Returns slot index 0..7 on success,
 * -1 if the line is not a user_slot header or N is out of range. */
static int parse_user_slot_header(const char *line)
{
    int slot = -1;
    if (sscanf(line, "[user_slot %d]", &slot) != 1) return -1;
    if (slot < 0 || slot >= SPU94_INTERP_USER_SLOT_COUNT) return -1;
    return slot;
}

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

    /* User-slot section state. While we are inside a [user_slot N] block,
     * register lines accumulate into user_slot_scratch and only commit to the
     * engine when the section ends (next section header or end of buffer).
     * This batches the slot write so it goes through the public API exactly
     * once, marking the slot filled atomically. */
    int     pending_user_slot = -1;
    int16_t user_slot_scratch[SPU94_REG__COUNT];

#define FLUSH_USER_SLOT() do {                                              \
    if (pending_user_slot >= 0) {                                           \
        spu94_interp_set_user_slot(state, pending_user_slot,                \
                                   user_slot_scratch);                      \
        pending_user_slot = -1;                                             \
    }                                                                       \
} while (0)

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

        /* Check for section headers. Any header transition flushes a
         * pending user-slot section to the engine. */
        if (strcmp(line, "[registers]") == 0) {
            FLUSH_USER_SLOT();
            section = SECTION_REGISTERS;
            continue;
        }
        if (strcmp(line, "[mixer]") == 0) {
            FLUSH_USER_SLOT();
            section = SECTION_MIXER;
            continue;
        }
        if (strcmp(line, "[dac]") == 0) {
            FLUSH_USER_SLOT();
            section = SECTION_DAC;
            continue;
        }
        if (strcmp(line, "[morph]") == 0) {
            FLUSH_USER_SLOT();
            section = SECTION_MORPH;
            continue;
        }
        {
            int slot = parse_user_slot_header(line);
            if (slot >= 0) {
                FLUSH_USER_SLOT();
                section = SECTION_USER_SLOT;
                pending_user_slot = slot;
                /* Zero-init scratch so registers we don't see in the file
                 * default to 0 (matches the engine's clear-slot semantics). */
                for (int i = 0; i < (int)SPU94_REG__COUNT; i++)
                    user_slot_scratch[i] = 0;
                continue;
            }
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
            else if (strcmp(key, "sampler_fader") == 0)
                spu94_set_sampler_fader(state, (int16_t)parse_hex_u16(value));
            else if (strcmp(key, "sampler_send") == 0)
                spu94_set_sampler_send(state, (int16_t)parse_hex_u16(value));
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

        case SECTION_MORPH:
if (strcmp(key, "grit") == 0) {
                /* "int" or "fract" (case-sensitive); anything else
                 * silently falls back to INT via spu94_set_morph_grit. */
                spu94_set_morph_grit(state,
                    strcmp(value, "fract") == 0
                        ? SPU94_GRIT_FRACT : SPU94_GRIT_INT);
            }
            /* else: unknown key, silently ignored (D-09) */
            break;

        case SECTION_USER_SLOT:
            /* Register lines accumulate into scratch; commit happens on
             * section transition (FLUSH_USER_SLOT) or end of buffer. */
            if (pending_user_slot < 0) break;
            for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
                if (strcmp(key, spu94_reg_name((spu94_reg_t)r)) == 0) {
                    user_slot_scratch[r] = (int16_t)parse_hex_u16(value);
                    break;
                }
            }
            /* Unknown register names silently ignored (D-09). */
            break;
        }
    }

    /* End of buffer: commit any in-flight user slot. */
    FLUSH_USER_SLOT();
#undef FLUSH_USER_SLOT

    return SPU94_OK;
}

/* -----------------------------------------------------------------------
 * spu94_export_user_slot — emit a single user waypoint slot's regs
 * ----------------------------------------------------------------------- */

int spu94_export_user_slot(const spu94_state *state, int slot,
                           const char *name, const char *description,
                           char *buf, size_t buf_size)
{
    if (!state || !buf || buf_size == 0) return -1;
    if (slot < 0 || slot >= SPU94_INTERP_USER_SLOT_COUNT) return -1;
    if (!spu94_interp_user_slot_is_filled(state, slot)) return -3;

    int16_t slot_regs[SPU94_REG__COUNT];
    spu94_interp_get_user_slot(state, slot, slot_regs);

    int pos = 0;
    int n;

#define EMIT(...)                                                          \
    do {                                                                   \
        n = snprintf(buf + pos, buf_size - (size_t)pos, __VA_ARGS__);      \
        if (n < 0 || (size_t)(pos + n) >= buf_size) return -2;             \
        pos += n;                                                          \
    } while (0)

    EMIT("version=1\n");
    EMIT("name=%.64s\n", name ? name : "");
    EMIT("description=%.256s\n", description ? description : "");
    EMIT("type=user_slot\n");
    EMIT("\n[user_slot %d]\n", slot);
    EMIT("# User waypoint slot %d (morph position %d/16)\n", slot, 2 * slot + 1);
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        EMIT("%s=0x%04X\n",
             spu94_reg_name((spu94_reg_t)r),
             (unsigned)(uint16_t)slot_regs[r]);
    }

#undef EMIT

    buf[pos] = '\0';
    return pos;
}

/* -----------------------------------------------------------------------
 * spu94_load_user_slot — install one slot from buf into target_slot
 *
 * Parses the buffer for the FIRST [user_slot N] section found and writes
 * its register values into target_slot, regardless of the section's own N.
 * No other engine state (registers, mixer, DAC, morph, other user slots)
 * is touched. ----------------------------------------------------------- */

spu94_result_t spu94_load_user_slot(spu94_state *state, int target_slot,
                                    const char *buf, size_t buf_len)
{
    if (!state) return SPU94_INVALID_STATE;
    if (!buf || buf_len == 0) return SPU94_INVALID_ARG;
    if (target_slot < 0 || target_slot >= SPU94_INTERP_USER_SLOT_COUNT)
        return SPU94_INVALID_ARG;

    const char *p = buf;
    const char *end = buf + buf_len;
    char line[512];

    int     in_slot_section = 0;
    int     found_any       = 0;
    int16_t scratch[SPU94_REG__COUNT];
    for (int i = 0; i < (int)SPU94_REG__COUNT; i++) scratch[i] = 0;

    while (p < end) {
        const char *nl = p;
        while (nl < end && *nl != '\n') nl++;
        size_t line_len = (size_t)(nl - p);
        if (line_len >= sizeof line) line_len = sizeof line - 1;
        memcpy(line, p, line_len);
        line[line_len] = '\0';
        p = (nl < end) ? nl + 1 : end;
        if (line_len > 0 && line[line_len - 1] == '\r') {
            line_len--;
            line[line_len] = '\0';
        }
        if (line[0] == '\0' || line[0] == '#') continue;

        /* Section header? */
        if (line[0] == '[') {
            if (in_slot_section) {
                /* We just finished the first [user_slot] section -- commit. */
                spu94_interp_set_user_slot(state, target_slot, scratch);
                return SPU94_OK;
            }
            int parsed_slot = parse_user_slot_header(line);
            if (parsed_slot >= 0) {
                in_slot_section = 1;
                found_any = 1;
            }
            continue;
        }

        if (!in_slot_section) continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *value = eq + 1;
        for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
            if (strcmp(key, spu94_reg_name((spu94_reg_t)r)) == 0) {
                scratch[r] = (int16_t)parse_hex_u16(value);
                break;
            }
        }
    }

    /* End of buffer: commit if we were inside a slot section. */
    if (in_slot_section) {
        spu94_interp_set_user_slot(state, target_slot, scratch);
        return SPU94_OK;
    }
    return found_any ? SPU94_OK : SPU94_INVALID_ARG;
}

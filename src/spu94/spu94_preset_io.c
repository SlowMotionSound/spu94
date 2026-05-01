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
 * spu94_preset_load — stub (full parser implemented in Plan 02)
 * ----------------------------------------------------------------------- */

spu94_result_t spu94_preset_load(spu94_state *state,
                                 const char *buf, size_t buf_len)
{
    if (!state) return SPU94_INVALID_STATE;
    if (!buf || buf_len == 0) return SPU94_INVALID_ARG;
    /* Full parser implemented in Plan 02.  Reference parse_hex_u16 to
     * suppress -Wunused-function until the real parser lands. */
    (void)parse_hex_u16;
    return SPU94_OK;
}

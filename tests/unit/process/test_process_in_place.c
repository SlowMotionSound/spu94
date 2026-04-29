/* tests/unit/process/test_process_in_place.c -- Phase 5 Plan 05.
 *
 * D-04 in-place bit-identity proof: spu94_process(state, L, R, L, R, N)
 * produces output bit-identical to spu94_process with separate out
 * buffers, given matched initial state. The sample-at-a-time loop
 * (l = L_in[i]; ...; L_out[i] = lo) is alias-safe by construction --
 * each input sample is consumed BEFORE its output slot is written, so
 * L_out == L_in aliases do not corrupt the not-yet-read input.
 *
 * Closes API-03's D-04 in-place-allowed contract. Companion to
 * tests/unit/process/test_process_block_size.c which pins the other
 * half of the D-03/D-04 API-03 contract.
 */
#include "unity.h"
#include <spu94/spu94.h>
#include <stdalign.h>
#include <stdint.h>

#define N 1024
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf_a[SPU94_STATE_SIZE_MAX];
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf_b[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf_a[256 * 1024];
static unsigned char work_buf_b[256 * 1024];

static int16_t L_input[N];
static int16_t R_input[N];
static int16_t L_oop[N];       /* out-of-place output */
static int16_t R_oop[N];
static int16_t L_ip[N];        /* in-place buffer (starts as input, ends as output) */
static int16_t R_ip[N];

static void generate_input(void) {
    uint32_t s = 0x00BADA55;
    for (int i = 0; i < N; i++) {
        s = s * 1103515245u + 12345u;
        L_input[i] = (int16_t)((s >> 16) & 0xFFFF);
        s = s * 1103515245u + 12345u;
        R_input[i] = (int16_t)((s >> 16) & 0xFFFF);
    }
}

static spu94_state *fresh_state(unsigned char *sbuf, unsigned char *wbuf,
                                size_t sbuf_sz, size_t wbuf_sz) {
    spu94_state *s = spu94_init(sbuf, sbuf_sz, wbuf, wbuf_sz);
    TEST_ASSERT_NOT_NULL(s);
    spu94_reset(s);
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
        (int)spu94_load_preset(s, SPU94_PRESET_HALL));
    /* ADR-Phase-6-G: Hall's factory table leaves vLOUT/vROUT = 0, which
     * gates the wet output to silence. Set them to full-scale so the
     * in-place bit-identity comparison actually exercises non-zero output.
     * Without this, both OOP and in-place paths produce 0 and the test
     * proves 0 == 0 (see CR-02). */
    spu94_set_vLOUT(s, (int16_t)0x7FFF);
    spu94_set_vROUT(s, (int16_t)0x7FFF);
    /* Phase 7: mixer faders default to 0 (silence). Set unity gains. */
    spu94_set_input_gain(s, 0x7FFF);
    spu94_set_dry_fader(s, 0x7FFF);
    spu94_set_reverb_fader(s, 0x7FFF);
    spu94_set_dry_send(s, 0x7FFF);
    spu94_tick(s);
    return s;
}

void setUp(void)    { generate_input(); }
void tearDown(void) {}

static void test_in_place_bit_identical(void) {
    /* State A: out-of-place baseline. */
    spu94_state *sa = fresh_state(state_buf_a, work_buf_a,
                                  sizeof state_buf_a, sizeof work_buf_a);
    spu94_process(sa, L_input, R_input, L_oop, R_oop, N);
    spu94_destroy(sa);

    /* State B: in-place. Copy input, then process with L_out == L_in. */
    for (int i = 0; i < N; i++) { L_ip[i] = L_input[i]; R_ip[i] = R_input[i]; }
    spu94_state *sb = fresh_state(state_buf_b, work_buf_b,
                                  sizeof state_buf_b, sizeof work_buf_b);
    spu94_process(sb, L_ip, R_ip, L_ip, R_ip, N);
    spu94_destroy(sb);

    for (int i = 0; i < N; i++) {
        TEST_ASSERT_EQUAL_INT16(L_oop[i], L_ip[i]);
        TEST_ASSERT_EQUAL_INT16(R_oop[i], R_ip[i]);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_in_place_bit_identical);
    return UNITY_END();
}

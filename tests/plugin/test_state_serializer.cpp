/* test_state_serializer.cpp -- Unit tests for StateSerializer (Phase 24 Plan 01)
 *
 * Validates the binary state container format (PLUG-22..27):
 *   - Save produces a valid container (magic, version, bodyLen)
 *   - Round-trip save -> load is lossless for float appendix values
 *   - Future version byte is rejected (D-06)
 *   - Short data is rejected
 *   - Bad magic is rejected
 *   - Truncated body is rejected
 *   - Text body is valid .spu94 format
 *
 * Links against spu94_static (C core) and JUCE modules (for MemoryBlock).
 */

#include <JuceHeader.h>
#include "StateSerializer.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>

extern "C" {
#include <spu94/spu94.h>
}

/* -----------------------------------------------------------------------
 * Helper: allocate and initialize an spu94_state on the heap.
 * Returns nullptr on failure. Caller owns the memory.
 * ----------------------------------------------------------------------- */
struct EngineFixture {
    unsigned char* stateBuf  = nullptr;
    unsigned char* workBuf   = nullptr;
    spu94_state*   engine    = nullptr;

    bool init()
    {
        stateBuf = static_cast<unsigned char*>(
            std::calloc(1, SPU94_STATE_SIZE_MAX));
        workBuf = static_cast<unsigned char*>(
            std::calloc(1, SPU94_WORK_BUF_MAX_BYTES));
        if (!stateBuf || !workBuf) return false;

        engine = spu94_init(stateBuf, SPU94_STATE_SIZE_MAX,
                            workBuf,  SPU94_WORK_BUF_MAX_BYTES);
        if (!engine) return false;

        spu94_load_preset(engine, SPU94_PRESET_HALL);
        return true;
    }

    ~EngineFixture()
    {
        if (engine) spu94_destroy(engine);
        std::free(workBuf);
        std::free(stateBuf);
    }
};

/* -----------------------------------------------------------------------
 * Test helpers
 * ----------------------------------------------------------------------- */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", msg,             \
                         __FILE__, __LINE__);                            \
            g_fail++;                                                    \
        } else {                                                         \
            g_pass++;                                                    \
        }                                                                \
    } while (0)

#define CHECK_EQ_FLOAT(a, b, msg)                                        \
    do {                                                                 \
        if ((a) != (b)) {                                                \
            std::fprintf(stderr, "FAIL: %s (%.9g != %.9g) (%s:%d)\n",   \
                         msg, (double)(a), (double)(b),                  \
                         __FILE__, __LINE__);                            \
            g_fail++;                                                    \
        } else {                                                         \
            g_pass++;                                                    \
        }                                                                \
    } while (0)

/* -----------------------------------------------------------------------
 * Test 1: save produces a valid container
 * ----------------------------------------------------------------------- */
static void test_save_produces_valid_container()
{
    EngineFixture f;
    CHECK(f.init(), "engine init");

    juce::MemoryBlock block;
    bool ok = StateSerializer::save(
        f.engine, 2.0f, 0.3f, 0.7f, 1.0f, 0.0f, 0.0f, block);
    CHECK(ok, "save returns true");
    CHECK(block.getSize() >= StateSerializer::kHeaderSize,
          "block >= header size");

    auto* bytes = static_cast<const uint8_t*>(block.getData());

    // Magic bytes
    CHECK(bytes[0] == 'S', "magic[0] == 'S'");
    CHECK(bytes[1] == 'P', "magic[1] == 'P'");
    CHECK(bytes[2] == 'U', "magic[2] == 'U'");
    CHECK(bytes[3] == '9', "magic[3] == '9'");

    // Version
    CHECK(bytes[4] == 0x01, "version == 1");

    // Body length
    uint32_t bodyLen = 0;
    std::memcpy(&bodyLen, bytes + 5, 4);
    CHECK(block.getSize() == StateSerializer::kHeaderSize + bodyLen,
          "total size == 9 + bodyLen");
}

/* -----------------------------------------------------------------------
 * Test 2: load round-trip produces identical float values
 * ----------------------------------------------------------------------- */
static void test_load_roundtrip_identical()
{
    EngineFixture f;
    CHECK(f.init(), "engine init");

    const float inGain  = 2.0f;
    const float inMorph = 0.3f;
    const float inSpd   = 0.7f;

    juce::MemoryBlock block;
    bool ok = StateSerializer::save(
        f.engine, inGain, inMorph, inSpd, 0.0f, 0.0f, 0.0f, block);
    CHECK(ok, "save ok");

    auto result = StateSerializer::load(
        block.getData(), static_cast<int>(block.getSize()));
    CHECK(result.ok, "load ok");

    // Exact float equality (IEEE 754 binary round-trip is lossless)
    CHECK_EQ_FLOAT(result.inputGain, inGain, "inputGain round-trip");
    CHECK_EQ_FLOAT(result.morphPosition, inMorph, "morphPosition round-trip");
    CHECK_EQ_FLOAT(result.morphSpeed, inSpd, "morphSpeed round-trip");
}

/* -----------------------------------------------------------------------
 * Test 3: load rejects future version
 * ----------------------------------------------------------------------- */
static void test_load_rejects_future_version()
{
    EngineFixture f;
    CHECK(f.init(), "engine init");

    juce::MemoryBlock block;
    bool ok = StateSerializer::save(
        f.engine, 0.5f, 0.625f, 0.5f, 0.0f, 0.0f, 0.0f, block);
    CHECK(ok, "save ok");

    // Patch version byte to 2
    auto* bytes = static_cast<uint8_t*>(block.getData());
    bytes[4] = 0x02;

    auto result = StateSerializer::load(
        block.getData(), static_cast<int>(block.getSize()));
    CHECK(!result.ok, "future version rejected");
}

/* -----------------------------------------------------------------------
 * Test 4: load rejects short data (less than header)
 * ----------------------------------------------------------------------- */
static void test_load_rejects_short_data()
{
    uint8_t tiny[5] = {'S', 'P', 'U', '9', 1};
    auto result = StateSerializer::load(tiny, 5);
    CHECK(!result.ok, "short data rejected");
}

/* -----------------------------------------------------------------------
 * Test 5: load rejects bad magic
 * ----------------------------------------------------------------------- */
static void test_load_rejects_bad_magic()
{
    EngineFixture f;
    CHECK(f.init(), "engine init");

    juce::MemoryBlock block;
    bool ok = StateSerializer::save(
        f.engine, 0.5f, 0.625f, 0.5f, 0.0f, 0.0f, 0.0f, block);
    CHECK(ok, "save ok");

    // Patch magic to 'XXXX'
    auto* bytes = static_cast<uint8_t*>(block.getData());
    bytes[0] = 'X'; bytes[1] = 'X'; bytes[2] = 'X'; bytes[3] = 'X';

    auto result = StateSerializer::load(
        block.getData(), static_cast<int>(block.getSize()));
    CHECK(!result.ok, "bad magic rejected");
}

/* -----------------------------------------------------------------------
 * Test 6: load rejects truncated body
 * ----------------------------------------------------------------------- */
static void test_load_rejects_truncated_body()
{
    EngineFixture f;
    CHECK(f.init(), "engine init");

    juce::MemoryBlock block;
    bool ok = StateSerializer::save(
        f.engine, 0.5f, 0.625f, 0.5f, 0.0f, 0.0f, 0.0f, block);
    CHECK(ok, "save ok");

    // Pass one byte less than the full container
    auto result = StateSerializer::load(
        block.getData(), static_cast<int>(block.getSize()) - 1);
    CHECK(!result.ok, "truncated body rejected");
}

/* -----------------------------------------------------------------------
 * Test 7: text body is .spu94 format (starts with "version=1")
 * ----------------------------------------------------------------------- */
static void test_text_body_is_spu94_format()
{
    EngineFixture f;
    CHECK(f.init(), "engine init");

    juce::MemoryBlock block;
    bool ok = StateSerializer::save(
        f.engine, 0.5f, 0.625f, 0.5f, 0.0f, 0.0f, 0.0f, block);
    CHECK(ok, "save ok");

    auto result = StateSerializer::load(
        block.getData(), static_cast<int>(block.getSize()));
    CHECK(result.ok, "load ok");
    CHECK(result.textLen > 0, "textLen > 0");

    // The .spu94 format starts with "version=1\n" (per spu94_preset_io.c)
    const char* expected = "version=1\n";
    size_t expLen = std::strlen(expected);
    CHECK(result.textLen >= expLen, "textLen >= expected prefix length");
    CHECK(std::memcmp(result.textBody, expected, expLen) == 0,
          "text body starts with 'version=1\\n'");
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */
int main()
{
    std::printf("=== StateSerializer Unit Tests ===\n\n");

    test_save_produces_valid_container();
    test_load_roundtrip_identical();
    test_load_rejects_future_version();
    test_load_rejects_short_data();
    test_load_rejects_bad_magic();
    test_load_rejects_truncated_body();
    test_text_body_is_spu94_format();

    std::printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

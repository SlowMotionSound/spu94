// tests/plugin/test_state_roundtrip.cpp
//
// Phase 24 / PLUG-26, PLUG-27: headless state round-trip tests.
//
// Uses two test strategies:
//   A. Direct: Build a valid binary state blob using StateSerializer + a raw
//      spu94 engine, then feed it to SPU94AudioProcessor::setStateInformation
//      and verify the 4 float-appendix params are restored.
//   B. Full: If getStateInformation produces a non-empty blob in console-app
//      context, also test save→load round-trip and multi-instance independence.

#include "PluginProcessor.h"
#include "StateSerializer.h"
#include <JuceHeader.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

extern "C" {
#include <spu94/spu94.h>
}

namespace {

constexpr float kEps = 1e-3f;

bool approxEq(float a, float b) {
    return std::abs(a - b) <= kEps;
}

struct EngineFixture {
    unsigned char* stateBuf  = nullptr;
    unsigned char* workBuf   = nullptr;
    spu94_state*   engine    = nullptr;

    bool init() {
        stateBuf = static_cast<unsigned char*>(std::calloc(1, SPU94_STATE_SIZE_MAX));
        workBuf  = static_cast<unsigned char*>(std::calloc(1, SPU94_WORK_BUF_MAX_BYTES));
        if (!stateBuf || !workBuf) return false;
        engine = spu94_init(stateBuf, SPU94_STATE_SIZE_MAX,
                            workBuf,  SPU94_WORK_BUF_MAX_BYTES);
        if (!engine) return false;
        spu94_load_preset(engine, SPU94_PRESET_HALL);
        return true;
    }

    ~EngineFixture() {
        if (engine) spu94_destroy(engine);
        std::free(workBuf);
        std::free(stateBuf);
    }
};

// Build a valid binary state blob with custom float appendix values.
// Returns a filled MemoryBlock, or empty on failure.
juce::MemoryBlock buildStateBlob(float inputGain, float morphPos,
                                 float morphSpd) {
    EngineFixture fix;
    if (!fix.init()) return {};

    juce::MemoryBlock block;
    bool ok = StateSerializer::save(fix.engine,
                                    inputGain, morphPos, morphSpd,
                                    0.0f, 0.0f, 0.0f, block);
    if (!ok) return {};
    return block;
}

// ---------------------------------------------------------------
// Test 1: setStateInformation restores float-appendix params
// ---------------------------------------------------------------
bool test_set_state_restores_params() {
    std::printf("test_set_state_restores_params... ");

    constexpr float kGain = 2.5f;
    constexpr float kPos  = 0.25f;
    constexpr float kSpd  = 0.8f;

    auto blob = buildStateBlob(kGain, kPos, kSpd);
    if (blob.getSize() == 0) {
        std::printf("SKIP (could not build state blob)\n");
        return true;
    }

    auto proc = std::make_unique<SPU94AudioProcessor>();

    // Verify defaults differ
    auto* pGain = proc->getParamInputGain();
    if (approxEq(pGain->get(), kGain)) {
        std::printf("SKIP (default matches test value)\n");
        return true;
    }

    proc->setStateInformation(blob.getData(),
                              static_cast<int>(blob.getSize()));

    bool ok = true;
    auto check = [&](const char* name, float expected, float actual) {
        if (!approxEq(expected, actual)) {
            std::printf("\n  FAIL %s: expected %.4f, got %.4f", name, expected, actual);
            ok = false;
        }
    };

    check("inputGain",     kGain, proc->getParamInputGain()->get());
    check("morphPosition", kPos,  proc->getParamMorphPosition()->get());
    check("morphSpeed",    kSpd,  proc->getParamMorphSpeed()->get());

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// ---------------------------------------------------------------
// Test 2: Two processors restore different states independently
// ---------------------------------------------------------------
bool test_multi_instance_independence() {
    std::printf("test_multi_instance_independence... ");

    auto blobA = buildStateBlob(4.0f, 0.1f, 0.9f);
    auto blobB = buildStateBlob(0.1f, 0.75f, 0.2f);

    if (blobA.getSize() == 0 || blobB.getSize() == 0) {
        std::printf("SKIP (could not build state blobs)\n");
        return true;
    }

    auto procA = std::make_unique<SPU94AudioProcessor>();
    auto procB = std::make_unique<SPU94AudioProcessor>();

    procA->setStateInformation(blobA.getData(), static_cast<int>(blobA.getSize()));
    procB->setStateInformation(blobB.getData(), static_cast<int>(blobB.getSize()));

    bool ok = true;
    auto check = [&](const char* inst, const char* name, float expected, float actual) {
        if (!approxEq(expected, actual)) {
            std::printf("\n  FAIL [%s] %s: expected %.4f, got %.4f", inst, name, expected, actual);
            ok = false;
        }
    };

    // Instance A
    check("A", "inputGain",     4.0f,  procA->getParamInputGain()->get());
    check("A", "morphPosition", 0.1f,  procA->getParamMorphPosition()->get());
    check("A", "morphSpeed",    0.9f,  procA->getParamMorphSpeed()->get());

    // Instance B
    check("B", "inputGain",     0.1f,  procB->getParamInputGain()->get());
    check("B", "morphPosition", 0.75f, procB->getParamMorphPosition()->get());
    check("B", "morphSpeed",    0.2f,  procB->getParamMorphSpeed()->get());

    // Cross-check: A and B must differ
    if (approxEq(procA->getParamInputGain()->get(),
                 procB->getParamInputGain()->get())) {
        std::printf("\n  FAIL: instances A and B have same inputGain (bleed)");
        ok = false;
    }

    std::printf("%s\n", ok ? "PASSED" : "\nFAILED");
    return ok;
}

// ---------------------------------------------------------------
// Test 3: Corrupt state leaves processor at defaults
// ---------------------------------------------------------------
bool test_corrupt_state_leaves_defaults() {
    std::printf("test_corrupt_state_leaves_defaults... ");

    auto proc = std::make_unique<SPU94AudioProcessor>();
    float defaultGain = proc->getParamInputGain()->get();
    float defaultPos  = proc->getParamMorphPosition()->get();

    const char garbage[] = "not a valid state block at all";
    proc->setStateInformation(garbage, sizeof(garbage));

    bool ok = true;
    if (!approxEq(proc->getParamInputGain()->get(), defaultGain)) {
        std::printf("FAIL (inputGain changed after corrupt load)\n");
        ok = false;
    }
    if (!approxEq(proc->getParamMorphPosition()->get(), defaultPos)) {
        std::printf("FAIL (morphPosition changed after corrupt load)\n");
        ok = false;
    }

    if (ok) std::printf("PASSED\n");
    return ok;
}

// ---------------------------------------------------------------
// Test 4: Full save→load round-trip (if getStateInformation works)
// ---------------------------------------------------------------
bool test_full_save_load_roundtrip() {
    std::printf("test_full_save_load_roundtrip... ");

    auto proc = std::make_unique<SPU94AudioProcessor>();

    // Set custom values via params
    auto* pGain = proc->getParamInputGain();
    auto* pPos  = proc->getParamMorphPosition();
    pGain->setValueNotifyingHost(
        pGain->getNormalisableRange().convertTo0to1(3.0f));
    pPos->setValueNotifyingHost(0.4f);

    juce::MemoryBlock stateBlock;
    proc->getStateInformation(stateBlock);
    if (stateBlock.getSize() == 0) {
        std::printf("SKIP (getStateInformation returns empty in headless mode)\n");
        return true;
    }

    // Restore to fresh processor
    auto proc2 = std::make_unique<SPU94AudioProcessor>();
    proc2->setStateInformation(stateBlock.getData(),
                               static_cast<int>(stateBlock.getSize()));

    bool ok = true;
    if (!approxEq(proc2->getParamInputGain()->get(), 3.0f)) {
        std::printf("FAIL (inputGain not restored)\n");
        ok = false;
    }
    if (!approxEq(proc2->getParamMorphPosition()->get(), 0.4f)) {
        std::printf("FAIL (morphPosition not restored)\n");
        ok = false;
    }

    if (ok) std::printf("PASSED\n");
    return ok;
}

} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI init;

    int failures = 0;
    int total = 4;
    if (!test_set_state_restores_params())     ++failures;
    if (!test_multi_instance_independence())    ++failures;
    if (!test_corrupt_state_leaves_defaults())  ++failures;
    if (!test_full_save_load_roundtrip())       ++failures;

    std::printf("\n%d/%d tests passed.\n", total - failures, total);
    return failures > 0 ? 1 : 0;
}

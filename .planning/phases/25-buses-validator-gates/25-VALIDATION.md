---
phase: 25
slug: buses-validator-gates
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-05-12
---

# Phase 25 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | ctest (C unit tests) + pluginval + auval + lv2lint + sord_validate + VST3 validator |
| **Config file** | `CMakeLists.txt` (build config), `.github/workflows/plugins.yml` (CI config) |
| **Quick run command** | `cmake --build build --config Release && pluginval --validate-in-process --strictness-level 7 build/src/plugin/spu94_plugin_artefacts/Release/VST3/SPU-94.vst3` |
| **Full suite command** | `cmake --build build --config Release && ctest --test-dir build -C Release` |
| **Estimated runtime** | ~120 seconds (build + pluginval strictness-7) |

---

## Sampling Rate

- **After every task commit:** Run quick pluginval strictness-7 on local VST3
- **After every plan wave:** Run full ctest suite + local pluginval
- **Before `/gsd-verify-work`:** Full suite must be green
- **Max feedback latency:** 120 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 25-01-01 | 01 | 1 | PLUG-32,33,34,35 | — | N/A | integration | `pluginval --strictness-level 7 ...VST3/SPU-94.vst3` | ✅ | ⬜ pending |
| 25-01-02 | 01 | 1 | PLUG-36 | — | N/A | integration | `auval -v aufx Sp94 Spu9` (macOS only) | ❌ W0 | ⬜ pending |
| 25-02-01 | 02 | 2 | PLUG-37 | — | N/A | ci | `pluginval --strictness-level 7` per format per OS | ✅ | ⬜ pending |
| 25-02-02 | 02 | 2 | PLUG-38 | — | N/A | ci | `auval -v aufx Sp94 Spu9` macOS CI | ❌ W0 | ⬜ pending |
| 25-02-03 | 02 | 2 | PLUG-39 | — | N/A | ci | `lv2lint + sord_validate` Linux CI | ❌ W0 | ⬜ pending |
| 25-02-04 | 02 | 2 | PLUG-40 | — | N/A | ci | VST3 SDK validator CI | ❌ W0 | ⬜ pending |
| 25-02-05 | 02 | 2 | PLUG-41,42 | — | N/A | ci | `pluginval --strictness-level 7` exit code check | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

*Existing infrastructure covers all phase requirements.* pluginval is already installed in CI. auval, lv2lint, sord_validate, and VST3 validator installation steps are part of the phase tasks themselves.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Plugin loads on mono track in Logic | PLUG-36 | Requires macOS + Logic Pro | Open Logic, create mono audio track, insert SPU-94, verify it loads |

---

## Validation Sign-Off

- [ ] All tasks have automated verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 120s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending

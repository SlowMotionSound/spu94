# Research Synthesis — v1.7 DAW Plugin Port

**Synthesised:** 2026-05-10
**Inputs:** STACK-v1.7.md, FEATURES-v1.7.md, ARCHITECTURE-v1.7.md, PITFALLS-v1.7.md
**Audience:** the requirements/roadmap step. This document orients toward "what's already locked" and "what's still open."

---

## 1. Headline

**v1.7 is materially smaller than the milestone framing implies.** The architecture lane's most important finding (ARCHITECTURE-v1.7.md §0) is that the v1.6 standalone *already is* a `juce::AudioProcessor`: the threading model is canonical (audio thread / message thread / SPSC `RegisterBridge` / atomic scalars / double-buffered preset slot), `prepareToPlay` already allocates correctly, the C core is already RT-safe and ctest-gated, and `juce_add_plugin(... FORMATS Standalone)` is already the build target.

What v1.7 actually does is four things on top of that working processor:

1. Expand `FORMATS` from `Standalone` to `Standalone VST3 LV2 CLAP` (+ `AU` on Apple).
2. Add float↔int16 conversion at the `processBlock` boundary.
3. Add bidirectional host-SR ↔ 44.1 kHz sample-rate conversion in a real-time-safe library.
4. Fill in the currently-empty `getStateInformation`/`setStateInformation` stubs (PluginProcessor.cpp:587-595) by routing them through the v1.4 `.spu94` serialiser that already exists.

Plus the cross-cutting work: CMake/CI matrix, `clap-juce-extensions` glue (because JUCE 8 doesn't natively support CLAP — JUCE 9 will, but no public release date), pluginval/auval/lv2lint validation, channel-bus configuration that won't fail `auval`, latency reporting that includes the new SRC group delay, per-OS installers, and per-host UAT.

The single biggest *engineering* decision is the SRC library pick (§3.1). Everything else is well-trodden JUCE-template territory.

---

## 2. Locked decisions (from HANDOFF.json — restated for the requirements step)

These are not up for re-litigation. Requirements should treat them as inputs.

1. **All four plugin formats: VST3 + AU + LV2 + CLAP.** AU is macOS-only, so 10 unique binaries across 3 OSes (or 13 if you count the per-OS Standalone; STACK-v1.7.md §2 flags the count ambiguity).
2. **All three OSes: Linux + macOS + Windows.**
3. **Bit-faithful C core stays untouched.** SR and bit-depth compatibility live entirely in the wrapper. Same architectural principle as the standalone WAV loader, made real-time.
4. **Real-time SR conversion** in the plugin wrapper (host any-rate ↔ 44.1 kHz core).
5. **Real-time bit-depth conversion** (host float32 ↔ int16 core).
6. **Standalone (v1.6) continues to ship alongside plugin formats**, not in place of them.
7. **Custom plugin UI redesign is out of scope.** Current standalone GUI is reused in the plugin window via `wrapperType` detection (one if-statement gates the WAV-loader UI, per ARCHITECTURE-v1.7.md §7.3).
8. **Beta-tester preset return-loop is out of scope.** Whatever channel Anthony uses (Discord/email/GitHub issues) handles preset return.
9. **Code signing / notarisation may defer for beta.** Testers click through Gatekeeper/SmartScreen; revisit if install friction is reported.

---

## 3. Decisions still open (with research's leaning + cost)

These are the questions the requirements step must resolve. Research surfaces options and leanings; Anthony decides.

### 3.1 SRC library — the single biggest engineering choice

ARCHITECTURE-v1.7.md §2 walks through eight options. Net picture:

| Option | License | Latency (Sinc-Medium / equiv) | Verdict |
|---|---|---|---|
| **libsamplerate (Sinc-Medium)** | BSD-2-Clause | ~121 samples one-way at higher rate | **Architecture's lean.** Compatible with deferred MIT/Apache pick; used by Audacity/Ardour; quality preset axis available. |
| **r8brain-free-src** | MIT | Hundreds-to-thousands of samples (linear-phase only in free version) | Easiest possible drop-in (header-only). Fallback if libsamplerate friction. |
| **zita-resampler** | GPL-3.0 | Low (~100 samples) | **Disqualified** by licensing posture. |
| **libsoxr** | LGPL-2.1 | Configurable | LGPL linking is awkward for closed-source paths; not a blocker for MIT/Apache final but adds friction. |
| **JUCE built-ins** (Lagrange / Windowed-Sinc / Catmull-Rom / Linear) | ISC (JUCE) | Low | Architecture rates these as marginal-to-poor for fixed-fractional reverb-quality SRC. |

**Confidence disagreement to flag:** STACK-v1.7.md §1 and ARCHITECTURE-v1.7.md §2.2 both touch SRC but PITFALLS-v1.7.md B6 frames the latency-reporting failure mode as the dominant risk. Architecture is MEDIUM-HIGH confidence; Pitfalls treats the SRC group-delay numbers as needing measurement at integration time, not folklore. Trust the measurement, not the table.

**Cost of getting this wrong:** wrong SRC = audible aliasing on HF content, or PDC drift, or RT-safety violation if SRC allocates in `process()`. This is the engineering risk hotspot of v1.7.

### 3.2 Dither policy at float → int16 input

ARCHITECTURE-v1.7.md §3.1: three options — truncate (no dither), TPDF dither, noise-shaped dither.

- **Architecture's lean: no dither.** Argument: the SPU's authentic experience is already a 16-bit input stage; period-faithful sources arrived as int16 PCM. Dither is a modern courtesy the original hardware never had. North Star applies — quirks are the product.
- **FEATURES-v1.7.md §1.2** stays out of this directly but notes the parallel question (smoothing on the engine vs the wrapper). Same principle: smoothing/dither belongs on wrapper-level musical controls, not on the bit-faithful path.

This is a **DSP-character decision, not a correctness decision**. Anthony picks. PITFALLS-v1.7.md (Phase-Specific Warnings) flags that the wrapper should saturate the same way the core does (`sat_s16`, ADR-0001) — not let the cast wrap. That's table-stakes regardless of dither pick.

### 3.3 Host-automatable parameter surface — curated minimum vs full register exposure

ARCHITECTURE-v1.7.md §5.1 is unambiguous: the existing raw-atomics + SPSC bridge is the right pattern; APVTS is not the right fit for 35 SPU registers (DAWs choke on 35 cryptic register names in their automation list). What v1.7 adds is a *small curated set* of `juce::AudioProcessorParameter`s for host automation, sitting on top of the existing bridge.

Architecture's proposed initial set: `morph_position`, `input_gain`, 5 mixer faders, `dac_enabled`, `morph_speed` — 9 parameters. FEATURES-v1.7.md §1.1 + §8.1 confirms the user-facing minimum (morph + dry/wet + bypass + mix) is what beta testers will expect.

Requirements should lock the exact list. The cost differential is small — each added param is a few lines — but it sets the "automation contract" with the user.

### 3.4 Drop LV2 on Windows?

Locked decision says 10 binaries. STACK-v1.7.md §2 + ARCHITECTURE-v1.7.md §8.1 both note: LV2 on Windows is technically buildable but barely used (no major Windows DAW scans LV2). Dropping it brings the count to 10 exact (VST3+LV2+CLAP+Standalone × Linux + VST3+AU+CLAP+Standalone × macOS + VST3+CLAP+Standalone × Windows = 4+4+3 = 11; minus the Linux Standalone counted-or-not = 10).

Requirements should resolve the binary count by explicit enumeration, not by repeating "10."

### 3.5 Standalone vs plugin source split

ARCHITECTURE-v1.7.md §8.3 proposes restructuring `src/standalone/` → `src/plugin/` + `src/standalone_extra/` (the latter being just `WavLoader.{cpp,h}`). The cleaner alternative is to leave the folder name and gate the WAV-loader UI on `wrapperType == wrapperType_Standalone` — one if-statement.

Both work. Requirements step picks based on aesthetic vs migration cost.

### 3.6 Channel-bus configuration

FEATURES-v1.7.md §4.1 and PITFALLS-v1.7.md B5 agree: declare a small fixed set explicitly in `isBusesLayoutSupported`, otherwise `auval` fails Logic's mono-track instantiation.

**Research's lean: mono→mono + mono→stereo + stereo→stereo. Everything else rejected.** Pitfalls notes mono→stereo's semantic decision needs an ADR (most likely: drive both L+R reverb paths with the same input). Sidechain and surround explicitly out of scope.

### 3.7 Code-signing defer scope (which OS for which beta wave?)

Locked decision says signing may defer. STACK-v1.7.md §4 + PITFALLS-v1.7.md C1/M2 flag specifics:

- **macOS:** right-click → Open bypass works but Sequoia (2024-2025) tightened the UX. Worth manual UAT.
- **Windows:** SmartScreen reputation now built through download volume; EV-cert bypass removed in 2024. Testers click through.
- **Linux:** no signing concern.

Requirements should record *what testers will be told*, not just "defer." That's a copy/onboarding task, not infrastructure.

---

## 4. Roadmap implications

ARCHITECTURE-v1.7.md §11 (implicit in the document structure) suggests roughly 5-6 phases. Cross-referencing FEATURES and PITFALLS, the proposed decomposition is below. **Phase numbering continues from 20** (last v1.6 phase).

### Phase 21 — Build skeleton: FORMATS expansion + CLAP shim + CI matrix

- Change `FORMATS Standalone` → `FORMATS Standalone VST3 LV2 CLAP` (+`AU` on Apple).
- Wire `clap-juce-extensions` via FetchContent.
- Set up 3-OS GitHub Actions matrix (ubuntu-22.04 / macos-13 or 14 / windows-2022).
- Cache submodule + ccache.
- Builds-and-loads gate, no DSP-correctness gate yet.
- Reference template: Pamplejuce.
- **Pitfalls focus:** B7 (bundle structure per OS), M3 (VST3 SDK auto-download in CI), M4 (MSVC over MinGW).
- **Skip-research risk:** LOW — well-trodden.

### Phase 22 — SRC integration + latency reporting

- Pick SRC library (decision 3.1).
- Allocate SRC + scratch buffers in `prepareToPlay` (RT-safety hard rule).
- Implement `processBlock` SRC chain: host_float → int16 → core → int16 → host_float.
- Bypass-fast-path when `hostSampleRate == 44100.0`.
- `setLatencySamples(SRC_in + core + SRC_out)` from `prepareToPlay`.
- Null-test in Reaper for PDC alignment.
- **Pitfalls focus:** B1 (RT-safety regression), B6 (PDC), C3 (denormals — `ScopedNoDenormals`).
- **Skip-research risk:** HIGH — needs deeper research at phase planning time. SRC group-delay numbers need measurement, not table lookup.

### Phase 23 — Float↔int16 conversion + dither policy + headroom

- Resolve decision 3.2.
- `clamp(sample * 32768.0f, -32768.0f, 32767.0f)` at input boundary.
- Default input_gain to 0.5 (-6 dB) matching standalone.
- Keep the existing side-channel limiter (PluginProcessor.cpp:357-377).
- **Pitfalls focus:** match `sat_s16` saturation behaviour (ADR-0001).
- **Skip-research risk:** LOW — design space already enumerated by Architecture.

### Phase 24 — State persistence + automation parameter surface

- Wire `getStateInformation`/`setStateInformation` over the v1.4 `.spu94` serialiser, deferred-applied via existing `pendingPresetBuf` mechanism.
- Wrap with magic + version header (PITFALLS C4): `[4-byte 'SPU9'][1-byte version][body length][body = .spu94 bytes]`.
- Add curated `juce::AudioParameterFloat`/`Bool` set (decision 3.3) routed to existing atomics.
- Multi-instance smoke test.
- **Pitfalls focus:** C4 (locale-dependent float parsing — binary-wrap the existing text format to dodge), parameter ID stability across versions.
- **Skip-research risk:** LOW — `.spu94` format already exists and round-trips bit-identically.

### Phase 25 — Channel bus configuration + validation

- Declare `mono/mono`, `mono/stereo`, `stereo/stereo` in `isBusesLayoutSupported`; reject everything else.
- ADR on mono→stereo semantics.
- Wire `pluginval --strictness-level 7` (or 10) into CI on every build.
- Wire `auval -v aufx Spu1 Spu9` (or chosen codes) on macOS runner.
- Wire `lv2lint` + `sord_validate` on LV2 outputs.
- VST3 SDK `validator` baseline run.
- **Pitfalls focus:** B2 (auval), B5 (bus layout), B8 (LV2 ttl).
- **Skip-research risk:** MEDIUM — auval failure modes are folklore-heavy; per-host quirks need empirical triage.

### Phase 26 — Packaging + per-host UAT

- Per-OS installers: macOS `.pkg` or drag-install `.dmg`, Windows Inno Setup, Linux tarball + `install.sh`.
- Manual UAT in Reaper / Live / Logic / FL Studio (the locked beta DAWs). Add Bitwig (CLAP canonical) + Ardour (LV2 canonical) if reasonable.
- State save/reload round-trip in each.
- Multi-instance test.
- Beta-tester README: cache-reset procedure per host (PITFALLS B3), unsigned-binary workaround per OS.
- **Pitfalls focus:** B3 (stale caches), B4 (AU user-path vs system-path), C5 (keyboard focus), C6 (HiDPI), C7 (resizable contract — declare fixed-size).
- **Skip-research risk:** MEDIUM — per-host UAT is empirical; the host-quirk tables in FEATURES §6.x and PITFALLS B3 are the starting checklist.

### Phase 27 (optional) — Code signing

- Only if install friction reported in beta.
- macOS: Developer ID Application + Developer ID Installer + notarisation via `notarytool`.
- Windows: Azure Trusted Signing if eligible, else OV cert in cloud KMS.
- **Skip-research risk:** HIGH — signing-chain pricing/availability moved in 2024-2025 and is still moving (STACK-v1.7.md §4 / PITFALLS M2). Re-verify at execution time.

**On phase shapes:** Architecture proposed 5 phases; this synthesis splits validation out from packaging (B2/B5/B8 are heavy enough to warrant their own phase) and treats signing as a conditional Phase 27. Net: 6 phases firm, 1 conditional.

---

## 5. Top risks

Pulled from PITFALLS-v1.7.md (severity-ranked) + the single biggest architectural choice from ARCHITECTURE-v1.7.md:

1. **SRC library pick + RT-safety + group-delay measurement** (Arch §2, Pitfalls B1, B6). The wrapper currently has zero SRC; adding it is the biggest new RT-safety surface, and the latency reporting depends entirely on the library's measured group delay.
2. **AU validation failure on Logic** (Pitfalls B2). Logic silently refuses to load AUs that fail `auval`. JUCE forum has multiple open `BusesProperties`/`auval` mismatch threads.
3. **Stale host caches during beta** (Pitfalls B3). A first broken build can poison every tester's host cache; subsequent fixed builds appear "still broken" until cache reset. README must document the reset per host.
4. **State save/restore locale + version drift** (Pitfalls C4). German/French locales parse "1,5" as "1" via `juce::String::getFloatValue`. Fix: binary-wrap the `.spu94` bytes with magic + version header rather than letting JUCE round-trip text through host XML.
5. **Channel layout mismatch** (Pitfalls B5). JUCE templates default to stereo-only `BusesProperties`; Logic auto-instances on mono tracks and the plugin greys out.
6. **(Architecture's headline)** SRC choice cascades into RT-safety, PDC, CPU, and licensing. The library pick is *not* a "we can revisit later" decision — it shapes Phase 22's design.

---

## 6. Known unknowns to verify

Consolidated from each researcher's flagged-unknowns sections. The requirements step should either resolve these or explicitly defer them to phase-planning time.

| # | Unknown | Source | When to verify |
|---|---|---|---|
| 1 | JUCE 9 release date / CLAP-native availability | STACK §1 | Before locking Phase 21 — affects whether `clap-juce-extensions` is needed. |
| 2 | macOS Sequoia Gatekeeper UX for unsigned plugins | STACK §6, Pitfalls C7 | Before Phase 26 packaging. |
| 3 | `clap-juce-extensions` × JUCE 8.0.x specific build compatibility | STACK §6 | Phase 21 smoke build. |
| 4 | Azure Trusted Signing 2026 pricing/regional availability | STACK §4 | Phase 27 (conditional). |
| 5 | SRC library group-delay measurement at actual integration | Arch §2.5, Pitfalls B6 | Phase 22 integration. |
| 6 | Anthony's beta-tester DAW mix (assumed Reaper/Logic/Live/FL — verify) | Features Flag 5 | Requirements step — direct ask. |
| 7 | Logic `auval` timeout numeric threshold | Pitfalls B2 | Empirically during Phase 25 — Apple's docs are vague. |
| 8 | Bitwig CLAP scan priority order vs VST3 | Pitfalls B3 | Phase 26 UAT — KVR threads contradict the user guide. |
| 9 | Current standalone GUI HiDPI behaviour on 4K monitors | Features §6.2, Pitfalls C6 | Phase 26 — quick visual check. |
| 10 | VST3 binary count (10 vs 11 vs 13) — depends on Windows LV2 + Standalone counting | STACK §2, Arch §8.1 | Requirements step — explicit enumeration. |
| 11 | Resizable window contract per format if standalone GUI ends up resizable | Features §6.1, Pitfalls C7 | Phase 26 — current GUI is fixed-size so probably moot. |
| 12 | SmartScreen reputation aging "several weeks / hundreds of installs" | Pitfalls Not-verified section | Phase 27 (conditional). |

---

## 7. What is NOT in scope for v1.7 (restated so the requirements step doesn't drift)

From PROJECT.md, HANDOFF.json, and explicit notes in each research file:

- Beta-tester preset return-loop mechanism (handled out-of-band).
- Custom plugin UI redesign (standalone GUI reused).
- Code signing / notarisation as a firm deliverable (deferrable; Phase 27 only if beta install friction reported).
- A/B parameter compare (FEATURES §1.3 — JUCE doesn't help, defer to polish milestone).
- Resizable plugin window (FEATURES §6.1, PITFALLS C7 — declare fixed-size).
- HiDPI/Retina audit (FEATURES §6.2 — verify, don't refactor).
- Tempo-synced morph LFO (FEATURES §3.2, §8 — luxury, future).
- Transport-aware freeze (FEATURES §3.3 — luxury, future).
- CLAP per-sample-accurate automation (Arch §7.2 — host-block automation is fine for a reverb).
- Sidechain / surround / Atmos channel configs (FEATURES §4.1).
- Plugin-side MIDI-learn UI (FEATURES §5.1 — host handles it for free).
- Per-host installer polish beyond what the format requires.
- Linux distro packages (.deb/.rpm/Arch) — tarball + install.sh suffices.

---

## 8. Confidence assessment

| Area | Confidence | Notes |
|---|---|---|
| Stack (JUCE 8 / CMake / CI / formats) | HIGH | Vendor docs + community templates cross-checked. |
| Stack (signing chains, Azure Trusted Signing pricing) | MEDIUM | Policies moved in 2024-2025; re-verify before paying. |
| Features (table-stakes vs nice-to-have matrix) | HIGH | JUCE source + format specs verified. |
| Features (real-world host quirks) | MEDIUM | Folklore-heavy; symptom-triage at UAT time. |
| Architecture (lifecycle, threading, build layout) | HIGH | Current `PluginProcessor.cpp` already implements the canonical pattern. |
| Architecture (SRC library tradeoffs) | MEDIUM-HIGH | Latency claims from project READMEs, not benchmarked locally. |
| Architecture (CLAP path via clap-juce-extensions) | MEDIUM | Mature shim, used by Surge XT/ChowDSP; needs JUCE 8.0.x smoke verify. |
| Pitfalls (platform/host facts) | MEDIUM-HIGH | Cross-checked vendor support + JUCE forum. |
| Pitfalls (specific JUCE-version regressions) | LOW | Surface-level; symptom-driven. |

**Overall research confidence: MEDIUM-HIGH.** The architecture finding (v1.7 is smaller than it looked) is the load-bearing claim and it's HIGH confidence — grounded in actually reading the current `PluginProcessor.cpp`. The two genuine unknowns are SRC group-delay-at-integration (measurable in Phase 22) and signing-chain pricing (only relevant in conditional Phase 27).

---

## Sources

All four research files in `.planning/research/` carry their own source bibliographies. Top-level citations to re-verify before execution:

- Pamplejuce template — sudara/pamplejuce (de facto JUCE 8 CI reference).
- libsamplerate (Secret Rabbit Code) — BSD-2-Clause SRC; talaviram/juce_libsamplerate JUCE binding.
- clap-juce-extensions — free-audio/clap-juce-extensions (MIT shim for CLAP-on-JUCE-8).
- Tracktion/pluginval — strictness-level 7+ for RT-safety interception.
- JUCE 8 release tags + Q3 2024 / Q1 2025 roadmap blog posts.
- Melatonin blog — CMake/JUCE + macOS notarisation + Windows code-signing how-tos.

---

*Synthesis for v1.7 DAW Plugin Port. Inputs researched 2026-05-10 by four parallel gsd-project-researcher agents; synthesised 2026-05-10. No git commit at synthesis time — orchestrator handles.*

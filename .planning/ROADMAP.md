# Roadmap: SPU-94

**Updated:** 2026-04-27
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.

## Milestones

- ✅ **v1.1 ADPCM** — 4 phases (shipped 2026-04-27, tag `v1.1`)
- ✅ **v1.0 Product** — 8 phases (shipped 2026-04-26, standalone GUI)
- ✅ **M1 Reverb Core** — 7 phases (shipped 2026-04-25, tag `m1-reverb-core`)

## Completed Milestones

<details>
<summary>✅ v1.1 ADPCM (Phases 1-4) — SHIPPED 2026-04-27</summary>

Sony 4-bit ADPCM encode/decode: bit-faithful codec as toggleable reverb coloration stage. 23 requirements, 10 plans.

- [x] Phase 1: Core Codec (2/2 plans) — completed 2026-04-26
- [x] Phase 2: Pipeline Integration (2/2 plans) — completed 2026-04-27
- [x] Phase 3: I/O Layer (3/3 plans) — completed 2026-04-27
- [x] Phase 4: Verification + Documentation (3/3 plans) — completed 2026-04-27

</details>

<details>
<summary>✅ v1.0 Product (8 phases / 37 plans) — SHIPPED 2026-04-26</summary>

M1 reverb core + standalone JUCE GUI. Archived to `.planning/milestones/v1.0-product-ROADMAP.md`.

</details>

<details>
<summary>✅ M1 Reverb Core (7 phases / 33 plans) — SHIPPED 2026-04-25</summary>

49 requirements validated. Archived to `.planning/milestones/v1.0-ROADMAP.md`.

</details>

---

## Previous Milestone Archives

- `.planning/milestones/v1.1-ROADMAP.md` — Full M2 ADPCM roadmap
- `.planning/milestones/v1.1-REQUIREMENTS.md` — 23 requirements (all complete)
- `.planning/milestones/v1.0-product-ROADMAP.md` — v1.0 product roadmap
- `.planning/milestones/v1.0-ROADMAP.md` — M1 reverb core roadmap
- `.planning/milestones/v1.0-REQUIREMENTS.md` — M1 requirements

---
*Next milestone: TBD. Run `/gsd-new-milestone` to begin.*

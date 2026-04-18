# Unity v2.6.1 — vendored

Source: https://github.com/ThrowTheSwitch/Unity/releases/tag/v2.6.1
Vendored: 2026-04-18
Reason: zero runtime deps, embedded-C standard (per CONTEXT.md D-09).
NOT a git submodule (see RESEARCH.md Anti-Patterns — submodules break reproducible builds).

## Pinned file integrity (SHA-256)

| File | SHA-256 |
|------|---------|
| unity.c | b90e735a54cf3b3765ab6caa955d11a1488ee73d9c6152cdc98576c2d17cb871 |
| unity.h | 9db174d3c2c6424fd35c0980c5941d124c5ebb0f48e8172f997a2aa9554b64ea |
| unity_internals.h | fcd8b3f6b412ac0ab599547eb8a30b6d7f3f0af77aab31f7a1822a2a8fc9a2b2 |

Any version bump requires updating all three files and the hashes above in a single commit.
Per T-01-01 (Phase 1 threat model), this is the supply-chain integrity guard.

# Upstream integration readiness

Baseline: `custom-v0.1.0-rc5` at
`967730a0db710f9d90dbd70907223d3f75e25a81`

Audit date: 2026-08-29

## PR #210

- Upstream PR: <https://github.com/Gericom/GBARunner3/pull/210>
- State: open draft
- Base/head: `Gericom:develop` <-
  `GimoXagros:custom/rtc-romhack-compat-rc5`
- GitHub mergeability: mergeable
- Upstream checks: no check runs or commit statuses have been published. The
  upstream PR must therefore not be described as CI-passing.
- Fork build for the current documentation commit `b812ed4`: passed at
  <https://github.com/GimoXagros/GBARunner3/actions/runs/33185908101>.
- Reviews and maintainer comments: none at the audit date.
- Current branch delta after the hardware-verified commit: documentation only
  (`README.md`, `CUSTOM_BUILD.md`, `TODO.md`, and `LICENSE.md`). Runtime identity
  remains bound to the `967730a` release artifact, not the branch tip.

PR #210 currently combines RTC/GPIO, high-ROM hicode/JIT, save-path and scanner
changes, tests, configurations, and documentation. If the maintainer requests a
split, the dependency-preserving order is:

1. RTC/GPIO and RTC-specific tests;
2. high-ROM hicode/JIT and address/cache-coherence tests;
3. save path, creation, scanner, EEPROM/FLASH/SRAM changes and tests;
4. documentation and test infrastructure.

No split or history rewrite should be performed without a maintainer request,
because PR #210 already has a public head and the RC5 artifact is tied to its
existing commit history.

## PR #205 overlap

Upstream PR #205 is <https://github.com/Gericom/GBARunner3/pull/205>. It targets
`feature/cache-hicode` and adds IPS/UPS-to-GPO patching.

The direct overlap with PR #210 is limited to:

- `code/core/arm9/source/MemoryEmulator/HiCodeCacheMapping.s`;
- `code/core/arm9/source/MemoryEmulator/HiCodeCacheMappingC.c`;
- `code/core/arm9/source/main.cpp`.

RTC persistence must not modify either hicode file. Its only unavoidable PR
#205 overlap is the initialization sequence in `main.cpp`; keep that change
limited to constructing/loading the RTC sidecar before execution begins. ROM
patch metadata order remains a separate Priority 6 decision.

## Licensing boundary

No repository-wide license is selected or granted by this work. The upstream
maintainer and individual copyright holders retain that authority. Existing
file- and component-specific notices remain authoritative for their own scope.

## Merge-risk summary

- Highest risk: the combined scope of PR #210, not a current textual conflict.
- Known future conflict: PR #205 changes `main.cpp` and hicode mapping.
- RTC persistence constraint: do not touch the RC5 hicode/JIT implementation.
- Required evidence: upstream CI still requires an actual check result or
  maintainer approval; the successful fork run is supporting evidence only.

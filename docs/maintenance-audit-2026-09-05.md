# Repository maintenance audit — 2026-09-05

## Baseline

- Repository and default branch: `GimoXagros/GBARunner3:develop`
- Pre-maintenance `develop`: `dd3f44be5e9412ba29f3d831fc236dcc6016b71e`
- Latest release: `custom-v0.1.2`
- Release tag target: `dd3f44be5e9412ba29f3d831fc236dcc6016b71e`
- Upstream base: `Gericom/GBARunner3:develop` at
  `ecaa817815d9761745606592f04affe5ee9c3731`
- Fork delta from that upstream base: 51 commits; upstream-only commits: 0
- Submodule: `code/libs/libtwl` from <https://github.com/Gericom/libtwl> at
  `e069645bed14a93e149e873e9273f04851e3a04e`

The audit used a clean worktree created from the fork's `develop` branch. Older
worktrees were inspected without changing user modifications.

## Source audit

The review covered PR #2's exception dispatch, JIT metadata, SD-cache backing,
cache replacement, and MPU/instruction-cache invalidation paths.

- Low guest ROM addresses are relocated to the 2 MiB ARM9 static window.
- High-ROM executable PCs remain guest virtual addresses, while metadata access
  and patch writes resolve to the loaded SD-cache backing block.
- GamePak mirrors are normalized before indexing the ROM-block table.
- Dynamic JIT metadata is reset when an SD-cache block is filled or reused.
- The Thumb condition, undefined branch, and BL patch paths write through the
  resolved backing pointer.
- Whole instruction-cache invalidation in active JIT and DMA paths is preceded
  by hicode unmapping. The startup invalidation occurs before hicode
  initialization.
- The mapped undefined handler reloads the saved SPSR from DTCM and selects the
  correct Thumb halfword without a data read through the guest virtual address.

No out-of-bounds access, unbalanced exception stack, incorrect SPSR/CPSR return,
section overflow, or proven production source defect was found in the audited
change. The linked handler test provides execution evidence for the assembly
dispatch boundary; it does not emulate physical ARM946E-S cache hardware.

The production source and packaged NDS contain no B8CJ game-code branch,
Kingdom Hearts title check, known-PC bypass, Save Slot skip, `.g3diag` writer, or
runtime diagnostic marker. The synthetic `EA7B4FFE -> 0x09ED4000` regression
remains a generic test vector.

## TODO and warning audit

| Category | Reachability | Finding | Decision |
| --- | --- | --- | --- |
| JIT `armJitNotImplemented` / `thumbJitNotImplemented` | reachable for unsupported encodings | existing compatibility limit; no new reproduction | retain and track by instruction/game evidence |
| JSON `parseHexString()` | reachable for malformed external config | shipped configs are valid, but invalid characters and overlong values are not rejected | record in `TODO.md`; do not change settings behavior without target tests |
| MMC/SD TODO comments | driver-dependent | existing controller and media limitations | retain |
| commented/test `bkpt` | non-production or explicit unsupported path | expected diagnostic stop | retain |
| `assert(false)` / explicit `unreachable` | none found in audited production scope | no finding | no change |
| compiler and linker warnings | build-time | existing macro redefinitions, ignored result, qualifier, `noreturn`, and synchronization-stub warnings | record under build modernization |
| GitHub Actions Node warning | workflow runtime | Node.js 20 actions are forced onto Node.js 24 | track as a separate workflow update |

## Repository and release inventory

- `git fsck --full` reported three dangling objects and no corruption.
- Tracked binary assets are the bootstrap graphics, banner icon, and custom logo.
- No ROM, BIOS, save, diagnostic dump, compiled NDS/ELF, or release ZIP is
  tracked.
- The largest tracked files are FatFs Unicode data, test libraries, ArduinoJson,
  and the custom logo; no unexplained release artifact is present.
- All Markdown relative links resolve.
- All 304 packaged config files match the runtime field vocabulary, have valid
  `GAMECODEVV.json` names, contain no duplicate keys, and include 2,513 valid
  hexadecimal patch addresses. Duplicate contents across regions are allowed.

## Build and test evidence

| Check | Result | Evidence |
| --- | --- | --- |
| `git diff --check` | pass | maintenance worktree |
| `git fsck --full` | pass | dangling objects only; no corruption |
| submodule | pass | `libtwl` at `e069645` |
| ARM7, ARM9, bootstrap | pass | `develop` CI run 33939033107 |
| application NDS | pass | `develop` and release CI |
| GoogleTest NDS link | pass | `develop` and release CI |
| linked hicode semantic test | pass | CI plus local execution against tag-built `arm9.elf` |
| Python compilation | pass | `python -m compileall tools` with external cache |
| repository invariant tests | pass | four tests, 304 configs |
| maintenance branch build and semantic jobs | pass | CI run 33940112417 |
| release packaging | pass | release CI run 33939047544 |

The local host has no Docker executable, so a second local container build was
not possible. Reproduction instead used the two independently uploaded Actions
artifacts built from the v0.1.2 tag with the pinned
`devkitpro/devkitarm:20241104` image. Their application NDS files are identical
to each other and to the public release. Their 304 configs have the same
canonical JSON manifest hash as the tagged source:
`13DE875BB6219C7C537663CC4D1A740F21CF6E05215C84BE3AB069A2DD92D538`.

## Release identity

- Public ZIP SHA-256:
  `13AE1E2252ECF2245AD2236FF13EBEA3BA558C7B4E6EA7FB4F021CB25834CE77`
- Packaged NDS SHA-256:
  `CC09916848C6FB92092DB15D5D8EBDA21F4543A63589804F44268D2D810601CE`
- Hardware-tested implementation commit:
  `9b991ac9c89e1952b8573f4bf8bc9708bcade92b`
- Release-documentation commit already in the tag ancestry:
  `38e3dcc85cdf97a250c1390566f3f9f08a61b7d8`
- Release tag/merge commit:
  `dd3f44be5e9412ba29f3d831fc236dcc6016b71e`
- Release workflow: <https://github.com/GimoXagros/GBARunner3/actions/runs/33939047544>

The ZIP contains `GBARunner3.nds` and `_gba/configs` with 304 JSON files. It
contains no ROM, BIOS, save, `.g3diag`, or diagnostic writer. The release NDS is
byte-identical to the hardware-tested NDS.

## Branch audit

Ahead/behind counts are relative to pre-maintenance fork `develop` and are shown
as `ahead/behind`. `same upstream` means the fork and
`Gericom/GBARunner3` branch tips are the same commit. None of these branch tips
has an open fork PR.

| Fork branch | Tip | Merge base | Ahead/behind | Unique commits | Open PR | Tag/archive | Upstream counterpart | Decision | Reason |
| --- | --- | --- | --- | ---: | --- | --- | --- | --- | --- |
| `develop` | `dd3f44b` | `dd3f44b` | 0/0 | 0 | none | v0.1.2 | default | KEEP | default and release source |
| `chore/pre-next-task-maintenance` | PR head | `dd3f44b` | maintenance/0 | maintenance | maintenance PR | PR merge | none | DELETE | remove after merge |
| `fix/hicode-thumb-undefined-dispatch` | `b04ce8b` | `b04ce8b` | 0/3 | 0 | none | PR #2 and v0.1.2 | none | DELETE | fully merged ancestor |
| `fix/kh-save-select-black-screen` | `036bc32` | `853179f` | 22/4 | 22 | none | canonical archive tag | none | ARCHIVE_THEN_DELETE | diagnostic history; ancestor of canonical tip |
| `fix/kh-hardware-divergence-autocapture` | `9fa4b92` | `853179f` | 29/4 | 29 | none | canonical archive tag | none | ARCHIVE_THEN_DELETE | rejected M evidence; ancestor of canonical tip |
| `fix/kh-paused-capture-safe` | `5b92f70` | `853179f` | 30/4 | 30 | none | canonical archive tag | none | ARCHIVE_THEN_DELETE | contains both earlier diagnostic branches |
| `feature/hicode` | `e6f30aa` | `7b3e303` | 3/228 | 3 | none | upstream branch | same upstream | DELETE | no fork-only history |
| `feature/jit-detach` | `79dc7f0` | `f22f528` | 3/249 | 3 | none | upstream branch | same upstream | DELETE | no fork-only history |
| `feature/rfu` | `7a49cad` | `9b7caeb` | 2/152 | 2 | none | upstream branch | same upstream | DELETE | no fork-only history |
| `feature/rom-gpio` | `ac48f9d` | `3b5f49a` | 3/121 | 3 | none | upstream branch | same upstream | DELETE | no fork-only history |
| `feature/user-ldm-stm-without-nop` | `445e3e4` | `f9b071f` | 1/186 | 1 | none | upstream branch | same upstream | DELETE | no fork-only history |
| `dragon-quest-monsters-experiment` | `6ea798c` | `9e7707e` | 1/230 | 1 | none | upstream branch | same upstream | DELETE | no fork-only history |

The canonical annotated archive tag is
`archive/kh-save-select-investigation-2026-09-05` at `5b92f70`; that tip contains
the other two diagnostic branch tips. The tag is not a release tag.

## Local worktrees

| Local branch | Tip | Worktree | State | Preservation | Action |
| --- | --- | --- | --- | --- | --- |
| `chore/pre-next-task-maintenance` | PR head | maintenance | documentation changes | maintenance PR | remove after merge |
| `custom/rtc-romhack-compat` | `e8b5567` | custom | clean | `archive/rtc-persistence-prototype` | remove after tag check |
| `custom/rtc-romhack-compat-rc4` | `6691cb3` | rc4 | clean | `custom-v0.1.0-rc4` | remove after tag check |
| `custom/rtc-romhack-compat-rc5` | `07647c2` | rc5 | clean | `custom-v0.1.1` and v0.1.2 ancestry | remove after tag check |
| `develop` | `ecaa817` | none | upstream-tracking | `origin/develop` | KEEP |
| `fix/hicode-cache-invalidation-coherence` | `38e3dcc` | hicode-fix | untracked Python cache | v0.1.2 | KEEP unchanged |
| `fix/hicode-thumb-undefined-dispatch` | `b04ce8b` | none | clean | PR #2 and v0.1.2 | delete after merge |
| `fix/kh-save-select-black-screen` | `036bc32` | kh-video | modified and untracked investigation documents | canonical archive tag after publication | KEEP unchanged locally |
| `fix/kh-hardware-divergence-autocapture` | `9fa4b92` | kh-m | clean | canonical archive tag after publication | remove after tag check |
| `fix/kh-paused-capture-safe` | `5b92f70` | kh-n | clean | canonical archive tag after publication | remove after tag check |

The local `develop` branch tracks upstream `Gericom:develop`; it is not the fork
default checkout and is retained to avoid changing established remote
semantics. Dirty worktrees remain untouched even when their corresponding
remote diagnostic branch is later archived and removed.

## Post-merge cleanup gate

After the maintenance PR is merged: verify the archive tag, delete only the fork
branches marked DELETE or ARCHIVE_THEN_DELETE one at a time, prune, and recheck
the release tags and default branch. Any branch whose evidence differs at that
point must be retained for review.

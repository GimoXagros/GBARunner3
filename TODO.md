# GBARunner3 TODO and upstream issue comparison

Snapshot date: 2026-09-05

Compared baselines:

- upstream `Gericom/GBARunner3:develop`: `ecaa817815d9761745606592f04affe5ee9c3731`
- current custom release `custom-v0.1.2`:
  `dd3f44be5e9412ba29f3d831fc236dcc6016b71e`
- hardware-verified custom rc5: `967730a0db710f9d90dbd70907223d3f75e25a81`
- 95 open upstream issues at the snapshot date

An open issue is not marked fixed merely because this branch contains related
code. `Partial` means an implementation exists but a stated condition is still
missing. `Retest` means the branch could affect the symptom, but the exact issue
input and route have not been verified on rc5.

## Completed in custom-v0.1.2

- [x] **Fix the reported B8CJ New Game transition.** The generic fix preserves
  mapped high-ROM Thumb state, selects the correct cached halfword, resolves
  dynamic Thumb JIT metadata and patch writes to the SD-cache backing block, and
  unmaps hicode before whole instruction-cache invalidation. Nintendo 3DS +
  DSpico testing passed `Main Menu -> New Game -> Save Slot` with NDS SHA-256
  `CC09916848C6FB92092DB15D5D8EBDA21F4543A63589804F44268D2D810601CE`.
  Later progression remains a separate open verification item below.

## P0: release and integration blockers

- [x] **Persist RTC state per game** — Implemented for
  [#30](https://github.com/Gericom/GBARunner3/issues/30). The branch now stores
  RTC offset, weekday, status, and interrupt state in a versioned, checksummed
  per-ROM `.g3rtc` sidecar. Temporary/backup recovery and host-clock rollback
  behavior have regression coverage. Hardware cold-start and recovery testing
  remains in **Expand RTC verification** below.
- [ ] **Retest exact Emerald and Emerald-derived failures** — Retest
  [#164](https://github.com/Gericom/GBARunner3/issues/164),
  [#193](https://github.com/Gericom/GBARunner3/issues/193),
  [#194](https://github.com/Gericom/GBARunner3/issues/194),
  [#195](https://github.com/Gericom/GBARunner3/issues/195),
  [#203](https://github.com/Gericom/GBARunner3/issues/203),
  [#204](https://github.com/Gericom/GBARunner3/issues/204), and
  [#207](https://github.com/Gericom/GBARunner3/issues/207). rc5 enables hicode
  and corrects high-ROM branch targets, and one Korean `BPEE` revision passed,
  but that result does not prove other regions or hacks. Record exact ROM hashes,
  game codes/revisions, configuration, console mode, and first failing state.
- [ ] **Resolve save-path and save-protocol regressions** — Partial/retest for
  [#164](https://github.com/Gericom/GBARunner3/issues/164),
  [#173](https://github.com/Gericom/GBARunner3/issues/173),
  [#198](https://github.com/Gericom/GBARunner3/issues/198), and
  [#206](https://github.com/Gericom/GBARunner3/issues/206). rc5 safely creates
  and initializes adjacent save files, but EEPROM V124 still has a source TODO,
  slow in-game save operations and reload behavior are separate consumer paths.
- [ ] **Obtain a repository-wide licensing decision** — Unresolved
  [#200](https://github.com/Gericom/GBARunner3/issues/200) and
  [#208](https://github.com/Gericom/GBARunner3/issues/208). `LICENSE.md` currently
  records the absence of a general grant; only the relevant copyright holders
  can select or grant a repository-wide license.
- [ ] **Make the supported build path reproducible on current toolchains** —
  [#57](https://github.com/Gericom/GBARunner3/issues/57),
  [#148](https://github.com/Gericom/GBARunner3/issues/148),
  [#177](https://github.com/Gericom/GBARunner3/issues/177), and
  [#199](https://github.com/Gericom/GBARunner3/issues/199). Keep the known-good
  `devkitpro/devkitarm:20241104` CI path. Clean serial, -j2 and -j4 builds
  now produce matching NDS hashes after explicit dependency fixes. Official
  Actions use Node 24; existing first-party/third-party warnings remain visible
  in the [build audit and inventory](docs/build-reproducibility.md). The latest
  GCC 16.1.0 image fails in libtwl/calico at `setVectorBase`; its separate manual
  experiment is non-blocking. The conflicting BlocksDS port in PR
  [#178](https://github.com/Gericom/GBARunner3/pull/178) remains separate.

## P1: compatibility work directly adjacent to the current custom release

- [ ] **Extend hardware regression coverage for the high-ROM Thumb/JIT fix.**
  Nintendo 3DS + DSpico testing of build
  `9b991ac9c89e1952b8573f4bf8bc9708bcade92b` (`GBARunner3.nds`
  SHA-256 `CC09916848C6FB92092DB15D5D8EBDA21F4543A63589804F44268D2D810601CE`)
  passed the `[B8CJ][K]` route `Main Menu -> New Game -> Save Slot` and did not
  reproduce the diagnostic build's black scanlines, screen flicker, or irregular
  audio clicks in other tested games. Continue through slot selection, intro,
  gameplay, save, restart, and load, then broaden the title matrix. Keep these
  extended checks separate from the now-fixed reported transition.
- [ ] **Reject malformed JSON patch addresses before applying them.** Shipped
  configs contain 2,513 valid hexadecimal addresses, but the runtime
  `parseHexString()` helper does not reject non-hex characters or more than eight
  digits. Define the failure behavior and add target-side parser tests before
  changing production settings semantics.
- [ ] **Finish high-ROM save-signature scanning across 4 KiB boundaries.** The
  current search explicitly cannot match a 16-byte signature split between two
  SD-cache blocks. Add a boundary-safe comparison and unit vectors without
  assuming cache blocks are contiguous.
- [ ] **Add ROM-hack source-profile handling** —
  [#185](https://github.com/Gericom/GBARunner3/issues/185) for non-standard
  headers and [#202](https://github.com/Gericom/GBARunner3/issues/202) /
  [#16](https://github.com/Gericom/GBARunner3/issues/16) for streamed patching.
  Identify patched content before selecting save/JIT configuration, and ensure
  patched bytes remain coherent across the linear window and SD-cache reloads.
  Coordinate with upstream patching PR
  [#205](https://github.com/Gericom/GBARunner3/pull/205).
- [ ] **Reproduce Dr. Mario & Puzzle League separately** —
  [#198](https://github.com/Gericom/GBARunner3/issues/198). Verify EEPROM V124
  detection/patching, JIT-on crash, and JIT-off performance as three distinct
  checks.
- [ ] **Expand RTC verification.** Test write/read commands, 12/24-hour mode,
  date rollover, weekday behavior, cold restart, and independent per-game
  offsets. Do not infer these from normal Pokémon Emerald startup.
- [ ] **Convert repeated hardware failures into regression tests.** Preserve
  calculable ARM PC+8/high-ROM mapping cases in the test NDS and keep
  title-specific runtime evidence bound to exact NDS hashes.

## P1: application, documentation, and user experience

- [ ] **Merge and maintain a canonical README/build entry point** —
  [#153](https://github.com/Gericom/GBARunner3/issues/153). This fork now has a
  README; coordinate rather than duplicate open README PR
  [#188](https://github.com/Gericom/GBARunner3/pull/188).
- [ ] **Finish settings and input controls** —
  [#3](https://github.com/Gericom/GBARunner3/issues/3) and
  [#127](https://github.com/Gericom/GBARunner3/issues/127). Display/cache/save
  JSON settings exist; input remapping, emulator actions, sound/channel controls,
  and force-RTC UI remain.
- [ ] **Application feature backlog** — cheats
  [#2](https://github.com/Gericom/GBARunner3/issues/2), return to loader
  [#10](https://github.com/Gericom/GBARunner3/issues/10), sleep
  [#18](https://github.com/Gericom/GBARunner3/issues/18), screenshots
  [#22](https://github.com/Gericom/GBARunner3/issues/22), save states
  [#25](https://github.com/Gericom/GBARunner3/issues/25), media-removal detection
  [#44](https://github.com/Gericom/GBARunner3/issues/44), firmware screen choice
  [#47](https://github.com/Gericom/GBARunner3/issues/47), 3DS RTCom scaling
  [#51](https://github.com/Gericom/GBARunner3/issues/51), bad-header recovery
  [#64](https://github.com/Gericom/GBARunner3/issues/64), visible error codes
  [#116](https://github.com/Gericom/GBARunner3/issues/116), loadable color LUTs
  [#145](https://github.com/Gericom/GBARunner3/issues/145), splash-screen target
  [#149](https://github.com/Gericom/GBARunner3/issues/149), and configurable
  root folder [#197](https://github.com/Gericom/GBARunner3/issues/197).
- [ ] **Clarify project maintenance status** —
  [#201](https://github.com/Gericom/GBARunner3/issues/201). Document the active
  branches, supported build, release policy, and how experimental forks relate
  to upstream.

## P2: cartridge peripherals, saves, and connectivity

- [ ] Multi-save slots [#17](https://github.com/Gericom/GBARunner3/issues/17).
- [ ] Boktai solar level/sensor input
  [#28](https://github.com/Gericom/GBARunner3/issues/28).
- [ ] Load ROMs from Slot-2 [#34](https://github.com/Gericom/GBARunner3/issues/34).
- [ ] EEPROM interchange with open_agb_firm
  [#48](https://github.com/Gericom/GBARunner3/issues/48).
- [ ] GBA Wireless Adapter/RFU
  [#139](https://github.com/Gericom/GBARunner3/issues/139).
- [ ] SD access from GBA homebrew
  [#161](https://github.com/Gericom/GBARunner3/issues/161).
- [ ] Rumble Pak support [#175](https://github.com/Gericom/GBARunner3/issues/175).

## P2: core emulation, timing, JIT, DMA, graphics, and audio

These issues remain open and have no issue-specific rc5 closure evidence. Group
work by causal subsystem before adding per-game exceptions.

- [ ] **JIT/control flow:** Mega Man Zero 4 performance
  [#27](https://github.com/Gericom/GBARunner3/issues/27), JIT detachment
  [#45](https://github.com/Gericom/GBARunner3/issues/45), Rayman
  [#54](https://github.com/Gericom/GBARunner3/issues/54), Sonic Battle
  [#62](https://github.com/Gericom/GBARunner3/issues/62), Hudson collections
  [#75](https://github.com/Gericom/GBARunner3/issues/75), ARM `ldr pc` post-index
  [#76](https://github.com/Gericom/GBARunner3/issues/76), Magical Vacation
  [#110](https://github.com/Gericom/GBARunner3/issues/110), and V-Rally 3 LDM
  writeback [#131](https://github.com/Gericom/GBARunner3/issues/131).
- [ ] **Timing/interrupts/DMA:** Fortress speed
  [#14](https://github.com/Gericom/GBARunner3/issues/14), Final Fantasy VI
  prologue [#19](https://github.com/Gericom/GBARunner3/issues/19), Final Fantasy V
  kick [#40](https://github.com/Gericom/GBARunner3/issues/40), kernel IRQ idea
  [#46](https://github.com/Gericom/GBARunner3/issues/46), cascade timers
  [#91](https://github.com/Gericom/GBARunner3/issues/91), fast-DMA mirroring
  [#98](https://github.com/Gericom/GBARunner3/issues/98), and JOY registers
  [#126](https://github.com/Gericom/GBARunner3/issues/126).
- [ ] **Graphics/cache/streaming:** Super Mario Advance 4 unaligned scroll
  [#11](https://github.com/Gericom/GBARunner3/issues/11), OAM priority
  [#32](https://github.com/Gericom/GBARunner3/issues/32), fragmented-SD Sims
  graphics [#37](https://github.com/Gericom/GBARunner3/issues/37), Dragon Quest
  map [#74](https://github.com/Gericom/GBARunner3/issues/74), SD streaming
  [#136](https://github.com/Gericom/GBARunner3/issues/136), Herbie
  [#155](https://github.com/Gericom/GBARunner3/issues/155), Donkey Kong Country 3
  [#159](https://github.com/Gericom/GBARunner3/issues/159), Sonic Advance 3
  [#181](https://github.com/Gericom/GBARunner3/issues/181), and Shin Megami
  Tensei draw distance [#182](https://github.com/Gericom/GBARunner3/issues/182).
- [ ] **Audio:** Activision Anthology DSi sound
  [#81](https://github.com/Gericom/GBARunner3/issues/81), synchronized stereo
  [#146](https://github.com/Gericom/GBARunner3/issues/146), Sonic Advance
  [#183](https://github.com/Gericom/GBARunner3/issues/183), Golden Sun
  [#190](https://github.com/Gericom/GBARunner3/issues/190), and Rhythm Tengoku
  [#192](https://github.com/Gericom/GBARunner3/issues/192). Normal startup of the
  tested Korean Rhythm Tengoku revision does not close its sound issue.
- [ ] **BIOS/homebrew:** BIOS-free routines
  [#56](https://github.com/Gericom/GBARunner3/issues/56) and 240p-test-mini
  [#65](https://github.com/Gericom/GBARunner3/issues/65).
- [ ] **Remaining game-specific crashes/regressions:** Mario & Luigi
  [#43](https://github.com/Gericom/GBARunner3/issues/43), It's Mr. Pants
  [#58](https://github.com/Gericom/GBARunner3/issues/58), Jet Set Radio
  [#60](https://github.com/Gericom/GBARunner3/issues/60), Angelic Layer SD fetch
  [#61](https://github.com/Gericom/GBARunner3/issues/61), Hamtaro
  [#63](https://github.com/Gericom/GBARunner3/issues/63), Madden
  [#68](https://github.com/Gericom/GBARunner3/issues/68), Super Ghouls 'n Ghosts
  [#69](https://github.com/Gericom/GBARunner3/issues/69), Mario Party Advance
  [#77](https://github.com/Gericom/GBARunner3/issues/77), Earthworm Jim 2
  [#78](https://github.com/Gericom/GBARunner3/issues/78), Hello Kitty
  [#99](https://github.com/Gericom/GBARunner3/issues/99), Kunio-kun collections
  [#100](https://github.com/Gericom/GBARunner3/issues/100), Konjiki no Gashbell
  [#115](https://github.com/Gericom/GBARunner3/issues/115), Muppet Pinball
  [#117](https://github.com/Gericom/GBARunner3/issues/117), Tennis no Ouji-sama
  [#121](https://github.com/Gericom/GBARunner3/issues/121), Virtual Kasparov
  [#122](https://github.com/Gericom/GBARunner3/issues/122), Worms World Party
  [#129](https://github.com/Gericom/GBARunner3/issues/129), Caesars Palace
  [#132](https://github.com/Gericom/GBARunner3/issues/132), Street Racing Syndicate
  [#134](https://github.com/Gericom/GBARunner3/issues/134), Soccer Kid
  [#135](https://github.com/Gericom/GBARunner3/issues/135), and Summon Night 2
  [#168](https://github.com/Gericom/GBARunner3/issues/168).

Jet Set Radio also has an open fix PR
[#209](https://github.com/Gericom/GBARunner3/pull/209); verify its patch against
the current hicode branch instead of duplicating title configuration.

## Verification gates before closing an issue

- Reproduce the original symptom using an identified ROM revision, exact build,
  console/mode, launcher, storage path, configuration, and starting state.
- Separate direct observation from a cause hypothesis. A game merely starting is
  not proof that timing, sound, saves, RTC, or progression works.
- For a fix, connect the faulty input or instruction to the first incorrect state
  and the observed symptom.
- Rerun the exact reproduction after the change and check adjacent affected paths
  plus representative previously working titles.
- Never attach or commit commercial ROMs, patched ROM images, BIOS files, or
  copyrighted saves. Record hashes and lawful patch/reproduction instructions
  instead.

## Strict external patch addresses (2026-09-05)

- Implemented per-array validation and atomic replacement; see [format and verification](docs/config-patch-addresses.md).
- Shipped 304 configs / 2,513 addresses retain their values. Patch mapping and hardware compatibility remain separate checks.

## ROM-hack source-profile design (2026-09-05)

- [Lifecycle audit and upstream #205 conflict map](docs/romhack-source-profile-design.md) documented.
- Source/effective metadata, patch-view transaction, profile selection and save/RTC migration are design proposals, not implemented support.

## EEPROM V124 source audit (2026-09-05)

- [Source research and reproduction checklist](docs/eeprom-v124-research.md) complete; version-selection and wrapper tests added.
- Compatibility, new signatures/masks and issue #198 remain blocked on reproduction; no protocol fix claimed.

# GBARunner3 Custom v0.1.7

Pokémon Lazarus v2.0에서 저장 칩을 인식하지 못해 타이틀이 열리지 않던 문제를 수정했습니다. 수정 NDS로 타이틀에서 A를 눌러 새 게임의 첫 대화를 열고, 다시 A를 눌러 다음 대화까지 진행했습니다. 시험 환경은 로컬 melonDS의 DS 모드입니다. DSi 실기와 정식 melonDS 1.1에서의 동작은 추가 확인이 필요합니다.

## Compatibility

A recompiled FLASH1M V103 driver uses different function prologues and installs its own operation table. When the chip query returned 0xFFFF, the game discarded its initial screen callback. GBARunner3 now recognizes the complete set of eight operations in linear ROM, supplies the emulated Macronix chip ID and routes flash I/O to the existing save file. Discovery checks every operation before changing the guest code.

## Verification

- Used the application NDS, an empty save directory, a GBA BIOS and default packaged title settings. The Pokémon Lazarus v2.0 input ROM had CRC32 558AE42F.
- In DS mode with DLDI, the title displayed “Press A to begin!” A opened “Hi! Sorry to keep you waiting!” and a second A advanced to “Welcome to the world of Pokémon!”
- The local melonDS interpreter includes a fix for sequential ARM9 instruction-fetch permission checks at a 4 KiB page boundary. The test used ordinary controller input and no guest register overrides. On the same host the v0.1.6 updater build remained on a blank game screen.
- Linked ARM9 tests exercise incomplete driver detection, both flash banks, read, program, verify, sector and chip erase, and invalid arguments. A full Lazarus playthrough, in-game save/reload and physical DSi testing remain unverified.

## Installation

Back up the current NDS, saves and matching RTC sidecars. Install GBARunner3.nds and merge the supplied _gba/configs into /_gba/configs on the SD card. Keep your own BIOS in /_gba/bios.bin. The package retains the redrawn launcher icon. Download [GBARunner3-Custom.zip](https://github.com/GimoXagros/GBARunner3/releases/download/custom-v0.1.7/GBARunner3-Custom.zip).

The RTC format is unchanged from v0.1.6. See RTC-LEGACY-UPGRADE.md for original v1 sidecars. To roll back, restore the v0.1.6 NDS and your matching backup.

Pinned toolchain: devkitpro/devkitarm:20241104
Application SHA-256: 6670db8c8fc1abbde35ab14a91055d43df7a3c26303868cbdeefc8b07ef50cfb

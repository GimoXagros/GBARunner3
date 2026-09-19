# custom-v0.1.3-rc3 배포 기록

[custom-v0.1.3-rc3](https://github.com/GimoXagros/GBARunner3/releases/tag/custom-v0.1.3-rc3)은 저장장치 오류 전달과 고주소 ROM의 저장 기능 검색을 실기에서 검증하기 위한 **프리릴리즈**입니다. **Hardware verification: NOT COMPLETED.** 정식 권장판은 계속 [custom-v0.1.3](https://github.com/GimoXagros/GBARunner3/releases/tag/custom-v0.1.3)입니다. 중요한 save와 개인 설정을 백업한 뒤 복사본으로 시험하세요. [실기 테스트 안내](HARDWARE-TEST-custom-v0.1.3-rc3.md)를 따르고, 문제가 생기면 정식판 NDS로 돌아가세요.

## 포함 범위와 동작

- PR #15: ARM7의 DLDI/DSi-SD read/write 결과를 sequence ownership과 함께 ARM9의 diskio/FatFs 및 SD cache에 전달합니다. 실패한 cache mapping을 공개하지 않으며 ROM storage error에서 fail-closed로 중단합니다. 자동 재시도나 복구 UI는 없습니다.
- PR #19: 물리적으로 연속하지 않거나 재사용되는 cache slot 사이의 logical 4 KiB 경계, 2 MiB 선형 ROM/고주소 cache 경계의 4-byte-aligned save signature를 bounded search로 찾습니다. 동일 prefix 뒤의 정상 후보와 마지막 window 누락도 수정했습니다.
- `develop`의 PR #17 linked ROM DMA boundary 검사와 관련 회귀 검사를 유지했습니다.

PR #16 RTC BCD 수정, PR #18 save recovery, 기존 초안 PR #5/#6, 화면 진단 코드와 게임별 우회 코드는 포함하지 않았습니다. RC3의 runtime 변경은 별도 `release/custom-v0.1.3-rc3` 브랜치와 태그에만 있으며, 이 문서 PR은 `develop`에 후보 코드를 병합하지 않습니다. 실기에서의 물리 SD 오류·제거·전원 차단, ARM7/ARM9 cache 동시성 및 자동 복구는 검증되지 않았습니다. 일반 사용자에게 SD 제거 또는 전원 차단 실험을 요청하지 않습니다.

## 소스와 공개 파일 식별

| 항목 | 값 |
| --- | --- |
| 출발 `develop` | `405177357aeaf0ff464e727ab2010433ed132b3c` |
| release branch / annotated tag target | `5cb2111be1d3893ad2a31a11a22860dc613645e9` |
| toolchain | `devkitpro/devkitarm:20241104` |
| libtwl submodule | `e069645bed14a93e149e873e9273f04851e3a04e` |
| application NDS SHA-256 | `b2e14d0732ca270c4f5f4ff60b70b810017a357f3d48f5e69c4e4d88c6165f90` |
| test NDS SHA-256 (검증용, ZIP 미포함) | `50cce7e4ee4f5ae5fd814d0dea14edf39a0ecb4c017af5395cb327d32b713de7` |
| 공개 ZIP SHA-256 / GitHub asset digest | `e9c8e49c34729856b2d0389c26def1176155934ac414393859675db29d780b53` |
| 설정 수 / Git 원본 바이트 manifest SHA-256 | 304 / `0ada1a9e67e36a6b9780d65ad6c39f3eb8922c1750691ac8db46f34ec9d078b8` |

[태그 전용 릴리스 빌드](https://github.com/GimoXagros/GBARunner3/actions/runs/35457456332)가 성공했고, 공개 [GBARunner3.zip](https://github.com/GimoXagros/GBARunner3/releases/download/custom-v0.1.3-rc3/GBARunner3.zip)을 2026-09-20 KST에 새 디렉터리로 다시 받아 검증했습니다. ZIP의 309개 항목은 허용 목록과 정확히 같고, `SHA256SUMS` 전체와 packaged NDS, source/tag commit, 설정 304개의 원본 바이트가 일치했습니다. ROM, BIOS, save, ELF, MAP, test NDS 및 진단 파일은 공개되지 않았습니다. 공개 자산은 ZIP 하나입니다.

## 자동 검증과 실기 대기열

[기존 저장장치 실패 대조군](https://github.com/GimoXagros/GBARunner3/actions/runs/35455480369)에서 여섯 실패가 재현됐고, [최종 nightly](https://github.com/GimoXagros/GBARunner3/actions/runs/35456962701)에서는 production storage 48/48, 실제 FatFs 22/22, linked ARM7/ARM9 transport 16/16, linked save search 893/893, ROM DMA 63/63, JIT/address 45/45, parser 19/19, EEPROM source 12/12, repository invariant 4/4가 통과했습니다. ASan/UBSan, JSON, IRQ, hicode 및 release upload mock도 통과했습니다. [고정 도구 재현성·패키지 예행연습](https://github.com/GimoXagros/GBARunner3/actions/runs/35456972992)에서는 j1/j2/j4 각각 두 번의 application/test NDS 해시가 모두 같았습니다. 독립 읽기 전용 및 최종 XHigh 검토에서 실행 가능한 지적은 없었습니다. 선택적 최신 도구의 기존 libtwl `setVectorBase` 선언 실패는 여전히 별개입니다.

실기 검증은 아직 완료되지 않았습니다. BIOS/title/menu, 10분 이상 gameplay, BRIK·AZWJ·B3TJ·BE8K·BPRE·BPEE·B8CJ, SRAM·EEPROM V124·FLASH·FLASH1M의 저장→정상 종료→재시작→로드, 확장 ROM의 4 KiB 경계 signature·JIT·ROM DMA를 [실기 테스트 안내](HARDWARE-TEST-custom-v0.1.3-rc3.md)에 따라 확인해야 합니다. B8CJ는 Main Menu → New Game → Save Slot → slot 선택 → intro → gameplay 경로를 기록합니다. RTC는 수정이 포함되지 않았으므로 정식판과 같은 동작인지 smoke 수준으로만 확인합니다.

**HARDWARE VERIFICATION REQUIRED. custom-v0.1.3 remains the recommended stable release.**

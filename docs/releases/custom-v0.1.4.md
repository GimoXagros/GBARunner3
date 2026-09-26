# GBARunner3 Custom v0.1.4 정식 배포

릴리스 태그: `custom-v0.1.4`.

RC3의 저장장치 결과 전달과 logical 4 KiB 경계 저장 기능 검색을 정식판으로 승격합니다. 사용자는 2026-09-27에 RC3가 실기에서 문제없이 작동한다고 보고하고 정식 배포를 요청했습니다. 기기·게임·저장 형식별 상세 목록이나 해당 실기 세션의 파일 해시는 제공되지 않았습니다. 물리 SD 오류·제거·전원 차단 검증을 통과했다는 뜻은 아닙니다.

## 포함된 변경

- PR #15: ARM7의 DLDI/DSi-SD read/write 결과를 sequence ownership과 함께 ARM9의 FsIpc, diskio/FatFs와 SD cache까지 전달합니다. 실패한 cache read를 유효한 mapping으로 공개하지 않습니다.
- PR #19 및 RC3 추가 수정: 비연속·재사용 cache slot 사이의 4 KiB 경계와 2 MiB 선형 ROM/cache 경계에서 aligned save signature를 검색합니다. 잘못된 prefix 뒤 후보와 마지막 window 누락을 수정합니다.
- PR #20의 검증된 RC3 통합과 PR #21의 공개 자산 기록, 기존 PR #17 ROM DMA 경계 회귀 검사를 포함합니다.
- PR #18: 저장 파일 초기화와 seek/read/write/sync 결과 및 실제 처리 길이를 확인합니다. 저장 I/O가 실패하면 게임 실행을 멈추고 저장 오류 화면을 표시하며 소리를 끕니다. 사용되지 않던 불완전한 재시도 API는 제거합니다.
- 정식 배포용 source/tag·application/test·config 해시, release metadata, package allowlist 검사와 반복 빌드를 적용합니다.

PR #18이 추가되므로 실행 파일은 RC3와 다릅니다. 사용자의 실기 통과 보고는 RC3의 정상 동작에 대한 것이며, 새 저장 오류 화면이나 PR #18의 실기 검증 결과가 아닙니다. RTC BCD/기존 sidecar 호환 정책(PR #16), 이전 초안 #5/#6, 게임별 우회와 진단 코드는 포함하지 않습니다.

## 남아 있는 동작과 한계

ROM storage error는 오래되거나 손상된 데이터를 실행하지 않도록 fault address를 기록하고 ARM9을 fail-closed로 중지합니다. 예외 처리 중 파일시스템 기록·자동 재시도·복구 UI를 실행하지 않습니다. IRQ를 배제하는 unaligned bounce I/O의 실기 지연은 측정하지 않았습니다.

저장 오류 화면은 자동 복구 기능이 아닙니다. 오류를 감지한 뒤 추가 저장·RTC 쓰기를 중단하며, 이미 일부만 기록된 파일을 되돌리거나 저장하지 못한 데이터를 복원하지 않습니다. SD 카드를 그대로 둔 채 전원 버튼을 길게 눌러 종료하고 필요하면 백업을 복원하세요. RTC의 기존 호환 정책과 오류 처리 범위는 별도 과제로 남습니다. 중요한 저장 파일과 설정을 먼저 백업하세요. SD 제거·전원 차단 실험을 일반 사용자에게 요구하지 않습니다. 원본 저장 파일로 파괴적 시험을 하지 마세요.

## 설치와 되돌리기

1. 기존 `.sav`, `.g3rtc`와 개인 설정을 백업합니다.
2. `GBARunner3.zip`의 `GBARunner3.nds`를 launcher가 사용하는 위치에 복사합니다.
3. `_gba/configs`를 기존 설정과 비교해 반영합니다. 개인 설정을 무조건 덮어쓰지 마세요.
4. 저장 복사본으로 저장 → 정상 종료 → 재시작 → 로드를 확인합니다.
5. 문제가 있으면 보존된 `custom-v0.1.3` 정식판 NDS와 백업 파일로 돌아갑니다.

ROM·BIOS·save는 패키지에 포함하지 않습니다. BIOS와 ROM은 사용자가 보유한 파일을 사용하며 업로드할 필요가 없습니다. 전체 실기 범위는 이전 RC3 실기 안내의 시나리오를 참고할 수 있지만 완료되지 않은 항목을 통과로 간주하지 마세요.

## 바이너리와 소스 식별

PR #18의 최종 runtime `ac4480c1890a85bdd12d5a64283c7fbafbe2273e`는 [전체 자동검증](https://github.com/GimoXagros/GBARunner3/actions/runs/36277320288)을 통과했습니다.

| 검증 | 결과 |
|---|---|
| 저장 production-source / ASan·UBSan | 76 PASS |
| 실제 Save → FatFs → ARM7 합성 매체 | 62 PASS |
| Linked ARM 저장 / ARM7 상태 | 62 PASS |
| Storage source / actual FatFs / ARM7+ARM9 transport | 48 / 22 / 16 PASS |
| Linked save search / DMA / JIT | 893 / 63 / 45 PASS |
| Linked parser / EEPROM source | 19 / 12 PASS |
| JSON, IRQ, ARM·Thumb dispatch, repository invariants | PASS |
| 설정 | 304개, 원본 주소 2,513개 유지 |
| 독립 읽기 전용 코드·ELF 검토 | actionable finding 0 |

이전 PR #18의 host 79개가 76개로 바뀐 이유는 제거한 재시도 API 검사를 종료형 오류 처리 검사로 교체했기 때문입니다. 기존 오류·범위 검사를 유지했고 실제 FatFs 통합 62개를 추가했습니다. 합성 ARM 명령 실행과 RAM 매체 검사는 실기 성능·동시성·내구성 시험이 아닙니다.

RC3 대비 NDS 크기는 220,672 → 222,208바이트, ARM9 구간은 158,104 → 159,448바이트, ARM7 구간은 36,088 → 36,288바이트입니다. ARM9 `.text`는 54,720바이트, `.rodata`는 3,928바이트, EWRAM 코드는 15,448바이트입니다. EWRAM 코드 끝 `0x02043C58`과 BSS 시작 `0x02044000` 사이에 936바이트가 남고, heap은 기존 64 KiB를 유지합니다. ITCM 여유는 8바이트이며 DTCM, 스택, `gHicodeUndefinedData=0x0681F800`, VRAM A BSS 끝 `0x06820000`은 유지됩니다. 메모리 영역을 확장하지 않았습니다.

- 고정 도구: `devkitpro/devkitarm:20241104`
- 검증 후보: `custom-v0.1.3-rc3`, source `5cb2111be1d3893ad2a31a11a22860dc613645e9`
- Application NDS SHA-256: `9ddceb528334e6cadb9342245dd6f52a4ae16eafe54a36938dfb71cb5252f758`
- Test NDS SHA-256 (공개 ZIP 제외): `50cce7e4ee4f5ae5fd814d0dea14edf39a0ecb4c017af5395cb327d32b713de7`
- libtwl: `e069645bed14a93e149e873e9273f04851e3a04e`
- 설정 304개, Git 원본 바이트 manifest SHA-256: `0ada1a9e67e36a6b9780d65ad6c39f3eb8922c1750691ac8db46f34ec9d078b8`

최종 source commit은 빌드 시점의 `RELEASE-MANIFEST.json`에 기록합니다. 고정 도구 nightly/sanitizer/linked ARM 검사, j1/j2/j4 각 두 번의 재현성 및 package dry-run이 통과한 정확한 커밋만 태깅합니다. 실제 최종 CI 링크, source/tag, 공개 ZIP hash와 GitHub digest는 [릴리스 페이지](https://github.com/GimoXagros/GBARunner3/releases/tag/custom-v0.1.4)에 기록합니다. 과거 태그·자산은 변경하지 않습니다.

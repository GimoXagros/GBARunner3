# GBARunner3 Custom v0.1.3-rc3 — Storage Validation Candidate

릴리스 태그: `custom-v0.1.3-rc3`.

이 버전은 저장장치 오류 전달과 4 KiB 경계의 저장 기능 검색을 실기에서 검증하기 위한 **프리릴리즈**입니다. 정식 권장판은 계속 [custom-v0.1.3](https://github.com/GimoXagros/GBARunner3/releases/tag/custom-v0.1.3)입니다. **Hardware verification: NOT COMPLETED.** 중요한 저장 파일을 먼저 백업하고 복사본으로 시험하세요.

## 포함 범위

- PR #15: ARM7 DLDI/DSi-SD 결과를 sequence가 소유하는 transaction result로 ARM9의 FsIpc, diskio, FatFs 및 SD cache까지 전달합니다. 실패한 cache fetch는 유효한 ROM mapping으로 공개하지 않습니다.
- PR #19 및 RC3 독립 검토 보완: 기존 4-byte alignment 계약을 유지하면서, 물리적으로 연속하지 않거나 재사용된 cache slot 사이의 logical 4 KiB 경계와 2 MiB 선형 ROM/고주소 cache 경계를 넘는 저장 기능 signature를 검색합니다. 기존 assembly 검색기의 잘못된 첫 단어 뒤 정상 후보 누락도 수정했습니다. 최종 bounded window와 실패한 fetch도 처리합니다.
- develop에 병합된 PR #17의 linked ROM DMA boundary 테스트와 관련 자동검사.

PR #18 save recovery, PR #16 RTC BCD 변경, 기존 초안 PR #5/#6, 화면 깜박임 진단 코드, 자동 복구 UI, 게임별 우회 코드 및 성능 개선 주장은 포함하지 않습니다.

## 알려진 동작과 제한

ROM 저장장치 오류가 발생하면 오래되거나 손상된 데이터를 실행하지 않도록 fail-closed로 멈춥니다. fault 주소를 기록하지만 화면에 복구 UI를 표시하거나 자동으로 재시도하지 않습니다. 이 동작은 물리 SD 카드 오류, 카드 제거, 전원 차단을 통해 아직 검증하지 않았습니다. 실기 하드웨어의 속도나 지연도 측정하지 않았습니다.

저장 파일 쓰기 호출부의 기존 실패 처리 및 dirty-state 복구 정책은 PR #18에 남겨 두었습니다. 이 시험판은 저장 파일 쓰기 실패 후 데이터 보존이나 자동 재시도를 보장하지 않으므로 중요한 저장 파일에는 사용하지 마세요.

## 설치와 되돌리기

기존 save와 개인 설정을 백업합니다. ZIP의 `GBARunner3.nds`는 정식판과 구분되는 파일명으로 보관한 뒤 launcher에서 실행하고, `_gba/configs`를 필요한 위치에 복사합니다. 기존에 수정한 설정을 무조건 덮어쓰지 마세요. 문제가 생기면 정식판 custom-v0.1.3의 NDS로 돌아갑니다. 일반 사용자에게 SD 제거 또는 전원 차단 실험을 요청하지 않습니다.

자세한 실기 확인 항목은 동봉된 `HARDWARE-TEST-v0.1.3-rc3.md`를 따르세요. ROM, BIOS, save 파일을 제출할 필요는 없습니다. 결과와 해시만 기록합니다.

## 자동 검증 및 바이너리 식별

고정 도구 버전은 `devkitpro/devkitarm:20241104`입니다. 통합 직후 [nightly 검사](https://github.com/GimoXagros/GBARunner3/actions/runs/35455196672)에서는 production-source storage 48건, 실제 FatFs의 synthetic media 22건, 기존 save signature 19개와 offset 0..4097, linked ARM 검색 551건, linked ARM7/ARM9 transport 16건, ROM DMA 경계 63건, JIT/address 45건, parser 19건 및 repository invariant 4건이 통과했습니다. 독립 검토의 검색 반례를 반영한 linked ARM 검색은 893건(내부 false-prefix, 직접 assembly 호출, 선형/고주소 경계와 잘린 suffix 포함)이 통과했습니다. [고정 도구의 여섯 빌드](https://github.com/GimoXagros/GBARunner3/actions/runs/35455201813)는 j1/j2/j4 각각 두 번씩 application과 test NDS가 동일했습니다. 최종 exact-head CI 실행 링크와 결과는 공개 릴리스 설명에 기록합니다. 이 결과는 synthetic/linked 검사이며 실기 검증을 대신하지 않습니다.

- Application NDS SHA-256: `b2e14d0732ca270c4f5f4ff60b70b810017a357f3d48f5e69c4e4d88c6165f90`
- Test NDS SHA-256: `50cce7e4ee4f5ae5fd814d0dea14edf39a0ecb4c017af5395cb327d32b713de7`
- 설정 파일: 304개; Git에 저장된 원본 바이트를 이름순 `SHA-256  filename` 행으로 계산한 SHA-256: `0ada1a9e67e36a6b9780d65ad6c39f3eb8922c1750691ac8db46f34ec9d078b8`
- libtwl submodule: `e069645bed14a93e149e873e9273f04851e3a04e`

검증된 exact source commit은 이 소스에서 생성된 `RELEASE-MANIFEST.json`과 공개 릴리스 설명에 기록합니다. 소스 문서 자체에 자기 commit SHA를 넣으면 SHA가 바뀌므로 빌드 시점의 Git HEAD를 manifest에 기록합니다. 공개 ZIP SHA-256은 업로드된 자산을 다시 내려받아 검증한 뒤 릴리스 설명에 기록합니다.

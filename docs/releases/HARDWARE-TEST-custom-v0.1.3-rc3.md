# custom-v0.1.3-rc3 실기 검증 안내

**Hardware verification: NOT COMPLETED.** 이 프리릴리즈는 검증 후보이며 정식 권장판은 custom-v0.1.3입니다. 중요한 저장 파일과 설정은 먼저 백업하고, 시험에는 저장 파일 복사본을 사용하세요. 원본 저장 파일로 파괴적 시험을 하지 마세요. 일반 사용자에게 SD 카드 제거 또는 전원 차단을 요청하지 않습니다.

## 기록할 기본 정보

- 기기와 실행 모드, launcher, SD 카드 종류(원한다면), 정확한 RC3 NDS SHA-256
- BIOS·ROM은 파일을 공유하지 말고 해시만 기록
- 사용한 설정 파일과 기존 save 복사본의 해시
- 각 시나리오의 시작 상태, 반복 횟수, 관찰 시간, 결과 PASS/FAIL/NOT RUN

## 기본 smoke 및 기존 회귀

BIOS 로드, title/menu 진입, gameplay 10분 이상과 정상 종료를 확인합니다. 사용 가능한 합법적 개인 보유 게임에서 BRIK, AZWJ, B3TJ, BE8K, BPRE, BPEE, B8CJ를 각각 확인합니다. B8CJ에서는 Main Menu → New Game → Save Slot → slot 선택 → intro → gameplay 경로를 기록합니다. 화면·소리·진행 중단 여부를 함께 기록하고 정식판 custom-v0.1.3과 같은 조건으로 비교합니다.

## 저장 및 고주소 ROM

SRAM, EEPROM V124, FLASH, FLASH1M을 각각 가능한 범위에서 확인합니다. 저장 → 정상 종료 → 완전 재시작 → 다시 로드 순서로 복사본의 round trip을 확인합니다. 확장 ROM, logical 4 KiB 경계 근처의 signature가 있는 개인용 synthetic/private 테스트, 고주소 ROM의 JIT 진입과 ROM DMA 활동을 확인합니다. RC3는 RTC 수정판이 아니므로 RTC는 기존 정식판과 같은 동작인지 smoke 수준으로만 비교합니다.

저장장치 오류 확인이 필요하다면 별도의 전용 spare 카드와 버려도 되는 저장 데이터가 있는 개발자가 별도 계획으로 진행해야 합니다. 일반 사용자에게 물리 매체 오류를 재현하도록 요구하지 않습니다. 실패 시 RC3 사용을 중단하고 정식판 custom-v0.1.3으로 돌아가세요.

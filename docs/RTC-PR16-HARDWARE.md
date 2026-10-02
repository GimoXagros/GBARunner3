# PR #16 RTC 실기 검증 안내

> Current status (2026-10-02): PR #16 is merged and custom-v0.1.5 is stable.
> See the [release record](releases/custom-v0.1.5.md) for completed automated
> gates and the limited user-reported hardware result. The checkpoint statements
> below, including pending/NOT RUN and upstream issue status, are historical.


**실기 검증 상태: HARDWARE VERIFICATION REQUIRED.** 이 문서 작성 시점에
NDS/DSi 호환 기기 검증은 수행하지 않았다. 이 안내만으로 현재 PR 브랜치의
빌드나 실험용 파일을 배포 가능한 버전으로 간주하지 않는다.

## 시작 전

PR #16 전용 검증 artifact의 manifest에서 source SHA와 NDS SHA-256을 확인한다.
다른 SHA로 만든 파일이면 이 절차의 결과로 기록하지 않는다. 실제 사용 중인
세이브와 `.g3rtc`, `.g3rtc.tmp`, `.g3rtc.bak` 파일을 별도 보관하고, 검증에는
복사본과 다시 만들 수 있는 테스트 게임 데이터만 사용한다. 사용자 원본에
전환 도구를 실행하지 않는다.

RTC 파일은 ROM과 같은 폴더의 같은 basename을 사용한다. 기존 세 파일은
legacy v1이고, 새 파일 집합은 `.g3rtc2`, `.g3rtc2.tmp`, `.g3rtc2.bak`이다.
기존 파일은 새 실행 파일에서도 보존되어야 한다. 전환은 PC 시각이나 과거
offline 경과시간을 복구하지 않는다. 선택한 저장 RTC snapshot을 기준으로 새
상태를 만들고, 새 기준 host 시각은 DS의 RTC에서 읽는다.

## Legacy 파일 확인과 취소

검증 복사본에서 먼저 실행을 시도해 legacy 감지 안내가 VM 시작 전에 표시되는지
확인한다. 이 안내는 종료 상태이며 앱 안에 취소 항목은 없다. 전환하지 않으려면
pending 파일을 만들거나 설치하지 말고 기기의 보통 종료 절차를 이용한 뒤 stable
실행 파일로 돌아간다. 전후의 legacy 세 파일과 `.sav`를 비교해 byte-for-byte
동일하고 새 세이브가 생기거나 기존 세이브가 바뀌지 않았는지 확인한다. 안내
상태에서 자동으로 host 시각을 적용하거나 guest를 시작하면 안 된다.

전환 동의가 필요한 경우 호스트에서 `tools/rtc_migrate.py`를 inspect 모드로
실행해 각 파일의 상태, 선택할 파일의 SHA-256, ROM identity를 확인한다. 기본
명령은 읽기 전용이다. 실제 경로를 사용자의 검증 복사본 위치로 바꿔 다음과 같이
inspect한다.

```powershell
python tools/rtc_migrate.py `
  --rom 'D:\RTC-test\game.gba' `
  --legacy-primary 'D:\RTC-test\game.g3rtc' `
  --legacy-temp 'D:\RTC-test\game.g3rtc.tmp' `
  --legacy-backup 'D:\RTC-test\game.g3rtc.bak'
```

세 legacy 경로 중 파일이 없는 경우에도 해당 경로를 입력한다. 없는 파일은
`MISSING`으로 표시된다. 있는 파일은 모두 유효해야 한다. 손상·지원하지 않는
버전·다른 ROM identity·I/O 오류가 있으면 전환하지 않는다.

명시적 전환 예시는 아래와 같다. 모든 경로는 사용자의 검증 복사본을 가리켜야
하며 출력은 입력과 다른, 아직 존재하지 않는 경로여야 한다.

```powershell
python tools/rtc_migrate.py adopt-stored-snapshot `
  --rom 'D:\RTC-test\game.gba' `
  --legacy-primary 'D:\RTC-test\game.g3rtc' `
  --legacy-temp 'D:\RTC-test\game.g3rtc.tmp' `
  --legacy-backup 'D:\RTC-test\game.g3rtc.bak' `
  --source primary `
  --expected-sha256 '<inspect 출력의 선택 파일 SHA-256>' `
  --output 'D:\RTC-test\game.g3rtc2'
```

성공하면 지정한 새 `game.g3rtc2` 파일 자체가 pending record다. 출력 경로는
게임 basename과 일치하는 `.g3rtc2` 경로로 정하고, 같은 이름이 이미 있으면
덮어쓰지 말고 중단한다. 결과 파일을 검증용 ROM 복사본 옆에 둔다. 도구는 기존
파일을 바꾸지 않고 ROM 입력도 읽기만 한다. 있는 legacy sibling은 모두 유효해야
하며 선택한 파일의 SHA가 달라졌으면 중단한다. 사용자 데이터에는 적용하지 않는다.

## 실기 체크리스트

각 항목은 통과/실패와 사용한 게임·기기·파일 SHA를 기록한다. 예상 결과가
불명확하면 통과로 추정하지 말고 미검증으로 둔다.

1. Legacy 안내가 guest VM과 save 초기화보다 먼저 나타난다. 취소 뒤 세 legacy
   파일과 save 복사본은 그대로다.
2. 명시적 snapshot 채택 후 날짜, 시각, 요일을 게임 화면에서 확인한다. 전환이
   원래 실제 시간을 복구했다고 기록하지 않는다.
3. 테스트 가능한 RTC 게임 동작으로 초가 `39→40`, 분이 `39→40`, 시각이
   `19→20` 경계를 지나가는지 확인한다.
4. 게임에서 정상 저장·정상 종료하고, 기기를 다시 시작한 뒤 같은 테스트
   복사본을 재로드한다. 날짜·시각·요일이 연속되고 두 번째 전환을 요구하거나
   다시 초기화되지 않아야 한다.
5. 두 RTC 게임의 사이드카와 game-visible 시간이 서로 분리되는지 확인한다.
6. 검증 복사본에서 구형 실행 파일로 되돌린 뒤 legacy 파일이 남아 있는지
   확인한다. 구형 실행 중 새 진행 시간이 자동으로 반영된다고 기대하지 않는다.

전원 차단, SD 제거, 강제 종료 시험은 요구하지 않는다. 정상 종료와 재시작으로
확인한 결과를 FAT 전원 차단 내구성으로 확대 해석하지 않는다. 실제 검증 전에
NDS/DSi 호환 기기에서 실행하지 않았다면 결과는 `HARDWARE VERIFICATION REQUIRED`로 남긴다.

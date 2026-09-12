# v0.1.3-rc2: 설정 비교용 진단 프리릴리스

**깜박임 수정판이 아닙니다. 원인 판정은 `INSUFFICIENT_EVIDENCE`입니다.**
3DS DS mode + DSpico에서 여러 게임의 역동적인 장면 중 순간적인 깜박임이
보고됐으나, 사용자는 2026-09-12 기준 v0.1.2/RC1 동일 조건 비교를 아직 하지
않았다고 답했습니다. 실제 SD 설정과 실기 trace도 제공되지 않았습니다.
RC2는 사용자의 프리릴리스 요청에 따라 비교 도구와 감사 결과를 배포합니다.
첨부 조사 지시의 실기 검증 후 정식 수정판 출시 단계에 도달했다는 뜻이 아닙니다.

## 포함된 파일과 사용법

- `GBARunner3.nds`: RC1과 바이트가 같은 비교용 control. 새로운 화면 수정 없음.
- `GBARunner3-settings-diagnostic.nds`: RC1 strict parser를 유지하면서 부팅 때
  최종 설정을 `/_gba/diag000.txt`부터 `diag999.txt`까지 새 파일에 한 번 기록합니다.
  기존 번호가 있으면 다음 번호를 사용하며 기존 파일을 덮어쓰지 않습니다.
- `_gba/configs`: 기존 304개 설정. 원래 SD 설정을 백업하고, 비교 중에는 기존
  설정을 일괄 교체하지 마세요. 두 실행 파일을 같은 launcher 경로/ROM 인자로
  선택해서 실행합니다. 인자가 없을 때의 기존 기본 ROM 경로는 `/rom.gba`입니다.
- `hardware-runs.csv`: 아직 실행하지 않은 비교 실험 기록용 빈 표입니다.

진단 실행 파일로 게임을 시작한 뒤 새 `diagNNN.txt`를 확인하세요. 마지막 줄에
`END SETTINGS SNAPSHOT`이 있는 파일만 완전한 형식의 기록으로 취급하세요.
그 표시는 SD 하드웨어 쓰기 성공을 보증하지는 않습니다. RC1의 하위 SD 오류
전파 제약이 그대로 있습니다. 파일 생성/쓰기 실패나 1,000개 번호 소진 때는
게임 부팅을 계속합니다. 진단 파일은 사용자 저장 데이터가 아니며 내용 확인 후
별도로 보관할 수 있습니다. 게임 도중 X+Y 기능이나 frame trace는 **없습니다**.

설정 기록에는 game code의 little-endian 숫자와 revision, 시도한 global/title
경로와 파일 처리 성공 여부, JIT와 self-modifying 배열의 개수 및 FNV-1a LE hash,
네 가지 cache 설정, clock/BIOS 설정, screen/capture/border/gamma/brightness와
화면 영역 설정이 포함됩니다. 배열의 각 주소나 ROM/BIOS/save 내용은 출력하지
않습니다. `ConfigLoaded=1`은 설정 파일 처리가 성공했다는 뜻이며 모든 property가
승인됐다는 뜻이 아닙니다. 잘못된 배열은 RC1 규칙대로 이전/default 값을 유지합니다.
`gbaScreen`: 0=top, 1=bottom. `borderImage`: 0=none, 1=default, 2=game.
title 경로의 제어 문자와 역슬래시는 `\xNN`으로 escape됩니다.

## 확인된 배포 파일 사실

2026-09-12에 기존 release ZIP을 새로 내려받아 확인했습니다.

| 항목 | custom-v0.1.2 | custom-v0.1.3-rc1 / RC2 control |
|---|---|---|
| NDS SHA-256 | `cc09916848c6fb92092db15d5d8ebda21f4543a63589804f44268d2d810601ce` | `9968bb423430b2fcfc6aacec70a5c2e5603f711952c6eb2d6c57fbfac287a3b2` |
| ARM7 크기 | 35,840 | 35,840 |
| ARM9 bootstrap 크기 | 156,472 | 156,712 |
| packaged configs | 304개 | 전부 동일 |
| ARM7 SHA-256 | `bc3eda90e2c88484e452813806ceca950afa7330359a89c6f9d5771664592caa` | 동일 |
| banner | 동일 | 동일 |

ARM9 load/entry는 `0x02000000`/`0x02000800`, ARM7 load/entry는
`0x02380000`/`0x02380000`으로 같습니다. ARM9 bootstrap 차이만으로 core의
특정 함수가 바뀌었다고 판단하지 않았습니다. 대응 NDS hash가 맞는 linked ELF도
별도로 비교했습니다. 전체 해시/section/주소/변경 word는
[`v013rc2-evidence`](v013rc2-evidence/)에 기록했습니다.

19개 지정 함수 범위 중 12개는 byte-identical, 7개는 주소 relocation뿐입니다.
`emu_regDispCntStore`와 `displayModeChange`의 anonymous read-only switch table도
전체 ELF mapping block을 비교해 내용이 같음을 확인했습니다. 이동한 call veneer와
literal pointer를 명시적으로 구분했으며 의미가 달라진 display/IRQ/DMA source는
없었습니다. `sdc_getRomBlock`은 inline이므로 unchanged header와 linked
`sdc_loadRomBlockDirect`를 대신 감사했습니다. 이는 ELF 전체 의미 동등성 증명은
아닙니다. libtwl revision은 두 tag 모두
`e069645bed14a93e149e873e9273f04851e3a04e`입니다.

## IRQ 정렬 감사

두 버전 모두 linked `dtcmIrqStackEnd=0xFFFFE52C`이고 `SP & 7 = 4`입니다.
기존 5개 register push는 20바이트이므로 C 함수 호출 직전 SP는
`0xFFFFE518`, 즉 8바이트 정렬 상태입니다. 6개로 바꾸면 오히려 잘못 정렬됩니다.
이는 [ARM AAPCS32](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst)의
public interface 정렬 규칙과 일치합니다.

실제 linked ARM 명령을 실행해 HBlank DMA 4개, sound DMA 2개, VBlank DMA 4개,
save callback 1개, 총 11개 호출 지점의 SP와 caller register 보존 및 stack 경계
canary를 검사했습니다. 의도적으로 6개 push로 바꾼 negative control은 실패했습니다.
C/D/C capture 명령 교대와 8개 pending 조합의 HBlank→ARM7→VBlank dispatch 및
exception return도 검사했습니다. callback 본체는 ABI에 맞는 stub입니다.
이 테스트는 callback의 실제 stack 최대 사용량, SD 오류, DMA/IRQ latency,
capture 완료 시점, 실기 화면을 검증하지 않습니다.

## 남은 실기 비교와 판정

먼저 ROM/BIOS/save/global JSON/title JSON을 별도 백업하고 동일한 3DS, DS mode,
DSpico, SD, launcher 경로, 화면, 장면을 유지합니다. 최소 2개 게임의 정적/역동적
장면에서 v0.1.2와 RC1을 각각 3회 실행하고 CSV의 횟수와 증상 유형을 채웁니다.
빈 값은 미실행이며 0회 깜박임이라는 뜻이 아닙니다.

그다음 동일 RC1에서 global JSON의 기존 다른 값을 유지한 채
`displaySettings.enableCenterAndMask`만 true/false로 비교합니다.
false는 원인 분리 실험이며 최종 해결책이 아닙니다. 진단 실행 파일 자체에서
새 깜박임이 생기면 그 기록으로 원인을 단정하지 말고 exact release control로
돌아가 비교합니다. 현재 진단 실행 파일은 RC1 설정만 기록합니다. stable parser와의
effective settings 동등성은 아직 확인하지 않았습니다.

| 조사 항목 | 현재 상태 |
|---|---|
| stable에서 재현 여부 | 미비교 |
| RC1에서만 재현되는 회귀인지 | 미확정 |
| 실제 SD 설정 / pristine config / effective settings 비교 | 사용자 실기 결과 필요 |
| capture on/off 결과 | 미비교 |
| IRQ C-call 정렬 | 검사한 11개 지점 통과; 수정 불필요 |
| callback stack 최대 사용량 / 실제 canary 손상 | 미측정 |
| late VBlank / DMA·sound·SD와의 상관관계 | trace 없음 |
| 512-record ring / X+Y freeze / deferred dump | 미구현 |
| 일반 flicker fix / 수정 효과 | 입증되지 않음 |

ARM9의 기존 런타임 파일 저장 경로는 VBlank IRQ callback을 이용합니다.
새 IRQ 안에서 trace 파일을 쓰거나 임의 VM 복귀 경로를 넣지 않았습니다.
게임 도중 안전한 deferred dump와 저부하 frame trace는 별도 검증이 필요한
후속 단계입니다. parser rollback/layout bisection도 RC1-specific 결과가 나온 뒤
진행합니다. 진단 branch는 RC1 tag `6503f9bd1e5143904be3769f930db9fd3fd8f466`에서
시작했으며 `develop`에 병합하지 않습니다. 확인한 원격 develop은
`23bc6bcaf5dbae6ac1da532eb616caa07f852ff2`입니다.

## 재현 가능한 검증

`.github/workflows/flicker-investigation.yml`은 고정
`devkitpro/devkitarm:20241104` 도구로 j1/j2/j4 각각 control 1회와 diagnostic
clean build 2회를 만들고, control의 RC1 hash와 diagnostic의 6회 동일성을
검사합니다. 두 ELF에 IRQ 검사를 실행하고 host snapshot 검사는 sanitizer로
실행합니다. 일반 nightly의 strict JSON, JIT, dispatch, repository 검사도 유지합니다.

일반 build는 `GBAR3_DISPLAY_FLICKER_DIAGNOSTICS`를 정의하지 않아 probe 코드가
포함되지 않습니다. 진단 build는 clean 후 make 변수
`GBAR3_DISPLAY_FLICKER_DIAGNOSTICS=1`로 만듭니다. normal/diagnostic 사이에는 반드시
clean build를 합니다. RC2 release ZIP에만 opt-in 실행 파일과 안내가 추가됩니다.
기존 release/tag/asset과 최신 stable 지정은 유지됩니다. PR #5/#6의 미검증 저장
실험은 이 RC2에 포함하지 않았습니다.

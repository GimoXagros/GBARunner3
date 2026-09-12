# GBARunner3 Custom v0.1.3 정식 배포

RC1의 설정 검증 개선을 정식 버전으로 승격합니다. 정상 실행 파일은 RC1 및
RC2의 비교용 실행 파일과 바이트까지 동일합니다. 새로운 화면/IRQ 수정 코드를
추가한 버전은 아닙니다. 사용자는 2026-09-13 기본 표시 설정 복원 후 화면
일그러짐과 깜박임이 모두 사라졌다고 확인하고 정식 배포를 요청했습니다.

## 설치와 설정

1. 기존 save와 설정을 백업하고 정상 `GBARunner3.nds`를 launcher가 사용하는
   위치에 복사합니다. 이번 ZIP에는 진단 실행 파일이 없습니다.
2. 동봉된 `_gba/configs`는 기존과 동일한 304개 게임별 설정입니다. 개인적으로
   수정한 설정이 있다면 내용을 비교해 유지하세요.
3. BIOS 위치는 `/_gba/bios.bin`이며, launcher에서 게임 경로를 전달해 실행합니다.
   경로를 전달하지 않을 때의 기존 기본값은 `/rom.gba`입니다.
4. **global `/_gba/gbarunner3.json`은 필수가 아닙니다.** 없으면 기본값을 씁니다.
   이전 비교 실험용 파일에 `enableCenterAndMask=false`만 들어 있다면 그 임시
   파일을 별도 이름으로 옮겨 기본 설정으로 복원하세요. 다른 개인 설정을 담은
   파일을 무조건 삭제하거나 덮어쓰지 마세요. 사용자 SD에서 해당 임시 파일은
   이미 백업 이름으로 옮겨져 있습니다.
5. `GBARunner3-settings-diagnostic.nds`와 `diagNNN.txt`는 정식 실행에 필요하지
   않습니다. 보관해도 무방하며 이번 패키지에서는 생성·설치하지 않습니다.

기본 `enableCenterAndMask=true`를 유지합니다. 설정을 false로 바꾸는 방법은
깜박임 해결책으로 제공하지 않습니다. ROM, BIOS, save는 배포하지 않습니다.

## 이번 버전에 포함된 변경

- v0.1.2 이후 production 변경은 외부 JSON patch 주소의 엄격한 검증입니다.
  선택적 `0x`/`0X` 접두사와 1~8자리 16진수 문자열을 허용합니다.
- 잘못된 주소 배열은 전체를 거부하고 이전/default 값을 보존합니다.
- 기존 RTC, high-ROM, UTF-8 파일명 기능을 유지합니다.
- 고정 도구 버전의 재현 가능한 빌드와 JSON/JIT/ARM dispatch 자동 검증을 유지하고,
  실제 linked IRQ 호출 지점 및 ELF 감사의 negative control 검사를 추가합니다.
- 진단 branch의 production 코드는 합치지 않았습니다. 저장 검색 PR #5 및
  저장 I/O PR #6의 미해결 실험도 포함하지 않았습니다.

## 화면 조사와 사용자 확인

| 단계 | 관찰 및 판정 |
|---|---|
| RC1/RC2 조사 시작 | 3DS DS mode + DSpico에서 불규칙한 깜박임 보고. 원인 미확정 |
| 첫 두 부팅 로그 | B8CJ revision 0, global/title config 로드 미성공, 기록된 값은 기본값과 일치. 두 로그 동일 |
| 임시 설정 적용 | global 로드 성공, `enableCenterAndMask=0`; 다른 기록된 최종 설정은 동일. 비율 변화와 일그러진 깜박임 보고 |
| 비교 해석 정정 | 해당 옵션은 중앙 배치·마스킹·표시 엔진을 바꾸지만 VBlank 캡처 시작 및 VRAM C/D 교대 작업 자체는 중지하지 않음 |
| 기본값 복원 | 원래 없던 임시 global 설정을 백업 이름으로 이동. 이후 사용자가 일그러짐과 깜박임 모두 사라졌다고 명시적으로 확인 |
| 최종 판단 | 해당 사용자 환경의 증상 해소 확인. 특정 IRQ/capture 결함이나 parser 회귀를 입증한 것은 아님 |

기존 “capture off” 비교 안내는 부정확했습니다. 직접 표시에서 증상 형태가
달라진 결과만으로 capture 원인을 제외할 수 없습니다. RC2 release 설명에도
정정을 반영했으며 기존 tag와 asset은 보존했습니다.

사용자 확인은 보고된 환경·관찰 범위에 한정됩니다. 그 실기 실행의 정확한 NDS
hash, 실행 횟수 및 시간은 제공되지 않았습니다. 따라서 이번 정상 NDS가 새로
실기 hash 대조를 통과했다고 하거나, 모든 게임·장시간 플레이·저장 복구를
검증했다고 표시하지 않습니다. 정확한 원인 분류는 여전히
`INSUFFICIENT_EVIDENCE`이며, 증상 상태는 `USER-REPORTED RESOLVED`입니다.

## 바이너리와 IRQ 검증 근거

stable v0.1.2와 RC1 공개 ZIP을 다시 내려받아 비교한 결과 ARM7, banner와
304개 config는 동일했습니다. 지정 linked 함수 19개 중 12개는 byte-identical,
7개는 주소 relocation이었습니다. 인라인 `sdc_getRomBlock`은 unchanged header와
linked `sdc_loadRomBlockDirect`를 대신 감사했습니다. 함수 범위 감사이며 ELF
전체나 하드웨어 동등성 증명은 아닙니다.

두 버전의 IRQ stack 끝은 `0xFFFFE52C`입니다. 기존 5개 레지스터 push 후 C-call
직전 SP는 `0xFFFFE518`로 8바이트 정렬 상태입니다. 검사한 호출 지점 11개에서
정렬, caller 레지스터 보존 및 경계 canary가 통과했고, 6개 push로 바꾸는
negative control은 잘못된 정렬을 검출했습니다. C/D/C 교대 명령과 8개 pending
조합의 HBlank→ARM7→VBlank 순서 및 exception return도 검사했습니다.

callback 본체는 stub이며 실제 최대 stack 사용량이나 IRQ/DMA 지연은 측정하지
않았습니다. 512-record ring, X+Y freeze, deferred dump는 구현하지 않았습니다.
원인 입증 없이 IRQ 순서나 화면 레지스터를 임의 변경하지 않았습니다.

## 배포 무결성과 남은 범위

- Normal NDS SHA-256:
  `9968bb423430b2fcfc6aacec70a5c2e5603f711952c6eb2d6c57fbfac287a3b2`
- 기준 source: fork develop `23bc6bcaf5dbae6ac1da532eb616caa07f852ff2`의 production tree.
  정식 tag의 최종 source commit과 ZIP hash는
  [정식 release 페이지](https://github.com/GimoXagros/GBARunner3/releases/tag/custom-v0.1.3)에 기록합니다.
- Toolchain: `devkitpro/devkitarm:20241104`, devkitARM release 65 / GCC 14.2.0.
- libtwl: `e069645bed14a93e149e873e9273f04851e3a04e`, 변경 없음.
- ZIP의 `SHA256SUMS`는 정상 실행 파일, config와 이 안내 파일의 해시입니다.
- 기존 RC1/RC2 및 v0.1.2는 보존하고 v0.1.3을 최신 정식 배포로 지정합니다.
- 물리 SD 오류 전파, save/RTC 중단 복구, 장시간 플레이와 더 넓은 게임 호환성은
  이번 증상 해소 보고만으로 완료 처리하지 않습니다.

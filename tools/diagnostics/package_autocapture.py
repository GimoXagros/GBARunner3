#!/usr/bin/env python3
"""Create one diagnostic-only M hardware folder after verified build/lab gates.

Never copies ROM, BIOS, save payload, or an existing user's sidecars.
"""
import argparse
import json
import shutil
import subprocess
from pathlib import Path
from autocapture_format import SCHEMA, HEADER, COMPLETE_SIZE, select_pair
from hash_inputs import sha, tree

NDS_NAME = 'GBARunner3-M-KH-hardware-divergence-autocapture.nds'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--lab', type=Path, required=True, help='validated lab reference directory')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    root = Path(__file__).resolve().parents[2]
    commit = (a.build / 'SOURCE.txt').read_text().strip()
    nds = a.build / 'M.nds'
    lab = json.loads((a.lab / 'lab-result.json').read_text())
    semantic = json.loads((a.build / 'M-semantic-tests.json').read_text())
    assert semantic['runtime']['result'] == 'PASS'
    assert all(t['result'] == 'PASS' for t in semantic['trampolines'])
    assert lab['nds_sha256'] == sha(nds) and lab['save_slot_visible'] and lab['intro_visible']
    newest, payload, errors = select_pair([a.lab / 'rom.g3diag.a', a.lab / 'rom.g3diag.b'])
    assert not errors and payload and newest.metadata['build_id'] == commit
    assert newest.metadata['stack_flags'] == 0
    assert not a.output.exists(), 'Use a new output folder; existing artifacts are preserved.'
    a.output.mkdir(parents=True)
    shutil.copy2(nds, a.output / NDS_NAME)
    shutil.copytree(root / 'configs', a.output / '_gba' / 'configs')
    names = ('decode_g3diag.py', 'autocapture_format.py', 'autocapture_schema.json', 'compare_kh_runtime.py', 'hash_inputs.py')
    for name in names: shutil.copy2(Path(__file__).with_name(name), a.output / name)
    shutil.copytree(a.lab, a.output / 'lab-reference')
    (a.output / 'verification').mkdir()
    for name in ('M-semantic-tests.json', 'hicode-semantic-tests.json', 'toolchain.txt'):
        shutil.copy2(a.build / name, a.output / 'verification' / name)
    manifest = dict(release_status='DIAGNOSTIC ONLY — DO NOT RELEASE',
                    branch='fix/kh-hardware-divergence-autocapture', commit=commit,
                    build_flags=['debug', 'GBAR3_RUNTIME_DIAGNOSTICS=1', 'GBAR3_DIAG_AUTOCAPTURE=1', 'GBAR3_BUILD_ID='+commit],
                    container='devkitpro/devkitarm:20241104', toolchain=(a.build / 'toolchain.txt').read_text().splitlines()[0],
                    nds_filename=NDS_NAME, nds_sha256=sha(nds), configs=tree(root / 'configs'),
                    diagnostic_version=SCHEMA['version'], header_size=HEADER.size, complete_size=COMPLETE_SIZE,
                    persist_interval_vm_vblanks=SCHEMA['persist_interval'], lab=lab,
                    files={p.relative_to(a.output).as_posix(): sha(p) for p in a.output.rglob('*') if p.is_file()})
    (a.output / 'MANIFEST.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    (a.output / 'MANIFEST.txt').write_text(f"DIAGNOSTIC ONLY — DO NOT RELEASE\nBranch: {manifest['branch']}\nCommit: {commit}\nNDS: {NDS_NAME}\nSHA-256: {manifest['nds_sha256']}\nFormat: G3DGv{SCHEMA['version']}\nHeader: {HEADER.size} bytes\nComplete: {COMPLETE_SIZE} bytes\nConfigs SHA-256: {manifest['configs']['sha256']}\nContainer: {manifest['container']}\nSee MANIFEST.json for full evidence.\n", encoding='utf-8')
    instructions = f'''# M 실기 테스트 — 진단 전용

이 파일은 검은 화면 문제의 수정 완료판이 아닙니다.

1. 기존 ROM과 `.sav`, L 진단 로그를 PC에 백업합니다. 예전 `.g3diag.a/.b`는 별도 백업 폴더로 옮겨 이번 실행과 섞이지 않게 합니다.
2. `{NDS_NAME}` 한 개와 `_gba/configs`를 사용합니다. 같은 3DS + DSpico 실행 경로를 유지하고, 기존 GBA BIOS 및 게임별 설정을 임의로 바꾸지 않습니다. 기존 `_gba/gbarunner3.json`이 있으면 유지합니다. ROM과 같은 이름의 `.sav`를 그대로 사용합니다.
3. 게임을 실행하고 타이틀/메인 메뉴에서 약 5초 기다립니다. Select는 누를 필요가 없습니다. 첫 VM VBlank부터 기록되며 두 번째 callback에 첫 완전한 파일을 씁니다.
4. `New Game`에서 A를 한 번 누릅니다. 검은 화면 또는 Save Slot이 나타나면 더 누르지 말고 약 15초 기다립니다.
5. 전원을 끈 뒤 ROM 옆의 새 `.g3diag.a`와 `.g3diag.b`를 둘 다 복사해 전달합니다. 불규칙한 기계음 여부, 도달한 화면, 사용한 NDS 파일명을 함께 적어주세요. ROM/BIOS/save 파일을 업로드할 필요는 없습니다.

파일명은 `<ROM 기본 이름>.g3diag.a`와 `<ROM 기본 이름>.g3diag.b`입니다.
완전한 로그는 각각 **{COMPLETE_SIZE:,}바이트**, 헤더만 기록된 로그는 **{HEADER.size}바이트**입니다. 작은 파일도 버리지 마세요.
완전한 로그는 {SCHEMA['persist_interval']} VM VBlank마다 번갈아 저장합니다. 마지막 저장 도중 전원이 꺼졌으면 다른 쪽 유효 로그를 선택합니다.
카드에 전혀 쓰지 못한 경우 마지막 RAM 상태까지 알 수는 없고, 마지막으로 저장된 단계까지만 판단할 수 있습니다.

```text
python decode_g3diag.py "게임.g3diag.a" "게임.g3diag.b" -o M-runtime.csv
python compare_kh_runtime.py --hardware "게임.g3diag.a" "게임.g3diag.b" --lab lab-reference/rom.g3diag.a lab-reference/rom.g3diag.b --output comparison
python hash_inputs.py --rom "게임.gba" --bios "_gba/bios.bin" --save "게임.sav" --nds "{NDS_NAME}" --configs _gba/configs --output input-hashes.json
```

기본 해독/비교/해시에는 Python 표준 라이브러리만 필요합니다. 선택적 명령어 해독에는 `capstone==5.0.7`, ELF 매핑에는 `pyelftools==0.32`를 사용합니다.
`lab-reference`와 `verification`, Python 도구는 PC 분석용이며 SD 실행에 필요하지 않습니다.
`symbols`는 ELF/주소별 함수 이름 등 개발자 분석 자료를 뜻하며 실행 파일이 아닙니다. 이번 실기 묶음에는 포함하지 않았습니다.

SHA-256: `{manifest['nds_sha256']}`
빌드: `{commit}`

**DIAGNOSTIC ONLY — DO NOT RELEASE**
'''
    (a.output / 'HARDWARE-TEST.md').write_text(instructions, encoding='utf-8')
    print(a.output)

if __name__ == '__main__':
    main()

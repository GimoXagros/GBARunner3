#!/usr/bin/env python3
"""Trace driver failure through actual ARM7 handlers / FsIpc / diskio.

Known failures are deliberately NOT accepted by --require-fixed. Default mode
asserts their exact identity, so green observation CI never means fixed storage.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
KNOWN_FAILURES={f'{device}_{case}' for device in ('dldi','dsi') for case in
                ('failed_read_propagated','failed_write_propagated','failed_read_rejects_stale_signature')}
parser=argparse.ArgumentParser()
parser.add_argument('--require-fixed',action='store_true')
parser.add_argument('--output',type=Path)
args=parser.parse_args()
with tempfile.TemporaryDirectory(prefix='gbar3-storage-failure-') as temp:
    tmp=Path(temp)
    def stripped(path):
        return re.sub(r'^#include[^\n]*\n','',path.read_text(encoding='utf-8'),flags=re.M)
    arm7=stripped(ROOT/'code/core/arm7/source/IpcServices/FsIpcService.cpp')
    (tmp/'production_arm7_storage.h').write_text(arm7[arm7.index('void FsIpcService::DldiReadSectors'):],encoding='utf-8')
    (tmp/'production_fs_ipc.h').write_text(stripped(ROOT/'code/core/arm9/source/Fat/FsIpc.cpp'),encoding='utf-8')
    (tmp/'production_diskio.h').write_text(stripped(ROOT/'code/core/arm9/source/Fat/diskio.cpp'),encoding='utf-8')
    (tmp/'production_sd_cache.h').write_text(stripped(ROOT/'code/core/arm9/source/SdCache/SdCache.c'),encoding='utf-8')
    exe=tmp/('storage.exe' if os.name=='nt' else 'storage')
    command=[os.environ.get('CXX','g++'),'-std=c++17','-g','-Wall','-Wextra','-fpermissive',
             '-I',str(tmp),'-I',str(ROOT/'code/core/common'),'-I',str(ROOT/'code/core/arm9/source'),
             str(ROOT/'tools/tests/storage_failure_host.cpp'),'-o',str(exe)]
    if os.environ.get('SANITIZE')=='1': command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(command,check=True)
    report=subprocess.check_output([str(exe)],text=True,timeout=15)
    print(report,end='')
    results=dict(line.split(':') for line in report.splitlines())
    failures={name for name,value in results.items() if value=='FAIL'}
    if args.output: args.output.write_text(json.dumps({'results':results,'known_failures':sorted(KNOWN_FAILURES)},indent=2)+'\n',encoding='utf-8')
    assert failures==(set() if args.require_fixed else KNOWN_FAILURES), sorted(failures)
    print(f'{len(results)-len(failures)} PASS, {len(failures)} tracked storage propagation failures; physical error handling is NOT fixed')

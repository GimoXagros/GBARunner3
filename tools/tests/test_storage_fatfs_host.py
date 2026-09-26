#!/usr/bin/env python3
"""Compile real FatFs with the same production storage transport fixture."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
def stripped(path):
    return re.sub(r'^#include[^\n]*\n','',path.read_text(encoding='utf-8'),flags=re.M)
with tempfile.TemporaryDirectory(prefix='gbar3-storage-fatfs-') as temp:
    tmp=Path(temp)
    arm7=stripped(ROOT/'code/core/arm7/source/IpcServices/FsIpcService.cpp')
    (tmp/'production_arm7_storage.h').write_text(arm7[arm7.index('static void completeTransfer'):],encoding='utf-8')
    for name,path in [('fs_ipc','Fat/FsIpc.cpp'),('diskio','Fat/diskio.cpp'),('ff','Fat/ff.c')]:
        (tmp/f'production_{name}.h').write_text(stripped(ROOT/'code/core/arm9/source'/path),encoding='utf-8')
    fixture=(ROOT/'tools/tests/storage_failure_host.cpp').read_text(encoding='utf-8').split('#include "SdCache/SdCacheDefs.h"')[0]
    start=fixture.index('using BYTE=')
    end=fixture.index('constexpr unsigned IPCSYNC_')
    fixture=fixture[:start]+'#include "Fat/ff.h"\n#include "Fat/diskio.h"\n'+fixture[end:]
    fixture=fixture.replace('#include "Save/SaveSignatureSearch.h"','')
    (tmp/'storage_transport_fixture.h').write_text(fixture,encoding='utf-8')
    exe=tmp/('fatfs.exe' if os.name=='nt' else 'fatfs')
    command=[os.environ.get('CXX','g++'),'-std=c++17','-g','-Wall','-Wextra','-fpermissive','-DNOMINMAX',
        '-I',str(tmp),'-I',str(ROOT/'code/core/common'),'-I',str(ROOT/'code/core/arm9/source'),
        str(ROOT/'tools/tests/storage_fatfs_host.cpp'),str(ROOT/'code/core/arm9/source/Fat/ffunicode.c'),'-o',str(exe)]
    if os.environ.get('SANITIZE')=='1': command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(command,check=True)
    subprocess.run([str(exe)],check=True,timeout=30)

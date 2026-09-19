#!/usr/bin/env python3
"""Execute production BCD/calendar conversion (no GPIO or file-I/O claim)."""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path)
args = parser.parse_args()
source = (ROOT/'code/core/arm9/source/Peripherals/RomGpio/RomGpioRtc.cpp').read_text(encoding='utf-8')
start = source.index('RTC_EWRAM u32 RomGpioRtc::FromBcd')
calendar = source[start:]
cases = []
epoch = datetime.datetime(2000, 1, 1)
for year in range(2000, 2100):
    for month in range(1, 13):
        for day in (1, 28):
            cases.append((year-2000, month, day, int((datetime.datetime(year, month, day)-epoch).total_seconds())))
for year in range(2000, 2100, 4):
    cases.append((year-2000, 2, 29, int((datetime.datetime(year, 2, 29)-epoch).total_seconds())))
with tempfile.TemporaryDirectory(prefix='gbar3-rtc-calendar-') as temp:
    tmp = Path(temp)
    (tmp/'production_calendar.h').write_text(calendar, encoding='utf-8')
    (tmp/'date_vectors.h').write_text('const unsigned dates[][4] = {\n' + ''.join(
        '{%d,%d,%d,%du},\n' % x for x in cases) + '};\n', encoding='utf-8')
    exe = tmp/('rtc.exe' if os.name == 'nt' else 'rtc')
    command = [os.environ.get('CXX','g++'), '-std=c++17', '-O2', '-g', '-Wall', '-Wextra',
               '-I',str(tmp), '-I',str(ROOT/'code/core/arm9/source'),
               str(ROOT/'tools/tests/rtc_calendar_host.cpp'), '-o',str(exe)]
    if os.environ.get('SANITIZE') == '1':
        command += ['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    subprocess.run(command,check=True)
    run = subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,timeout=30)
    print(run.stdout, end='')
    if args.output:
        args.output.write_text(json.dumps(dict(exit_code=run.returncode,output=run.stdout,
            evidence='Production conversion only; host datetime oracle; hardware NOT RUN'),indent=2)+'\n',encoding='utf-8')
    raise SystemExit(run.returncode)

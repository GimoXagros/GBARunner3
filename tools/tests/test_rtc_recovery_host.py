#!/usr/bin/env python3
"""Execute production RTC GPIO/recovery; fake read-only files/host clock."""
import os
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--negative-control', choices=['no-dirty', 'partial-offset'])
args = parser.parse_args()
source = (ROOT/'code/core/arm9/source/Peripherals/RomGpio/RomGpioRtc.cpp').read_text(encoding='utf-8')
selection = source[source.index('RTC_EWRAM void RomGpioRtc::Initialize('):source.index('RTC_EWRAM bool RomGpioRtc::WriteStateFile(')]
dirty = source[source.index('RTC_EWRAM void RomGpioRtc::MarkStateDirty()'):source.index('RTC_EWRAM void RomGpioRtc::Update(RomGpio&')]
protocol = source[source.index('RTC_EWRAM void RomGpioRtc::Update(RomGpio&'):source.index('RTC_EWRAM void RomGpioRtc::UpdateDSDateTime()')]
clock_and_calendar = source[source.index('RTC_EWRAM void RomGpioRtc::UpdateDateTime()'):]
if args.negative_control == 'no-dirty':
    begin = clock_and_calendar.index('RTC_EWRAM void RomGpioRtc::UpdateRtcOffset()')
    end = clock_and_calendar.index('RTC_EWRAM void RomGpioRtc::SetYear(')
    body = clock_and_calendar[begin:end]
    assert body.count('MarkStateDirty();') == 1
    clock_and_calendar = clock_and_calendar[:begin]+body.replace('MarkStateDirty();', '')+clock_and_calendar[end:]
elif args.negative_control == 'partial-offset':
    assert protocol.count('if (_offsetUpdateRequired)') == 1
    protocol = protocol.replace('if (_offsetUpdateRequired)', 'if (_offsetUpdateRequired || _byteIndex == 2)')
defines = source[source.index('#define ROM_GPIO_PIN_SCK'):source.index('[[gnu::section(".ewram.bss")]]')]
gpio = (ROOT/'code/core/arm9/source/Peripherals/RomGpio/RomGpio.cpp').read_text(encoding='utf-8')
gpio_methods = gpio[gpio.index('void RomGpio::Initialize('):gpio.index('[[gnu::section(".ewram")]] void RomGpio::LoadRtcState(')]
gpio_methods += gpio[gpio.index('void RomGpio::Reset()'):gpio.index('static void updateRomGpioPeripherals()')]
with tempfile.TemporaryDirectory(prefix='gbar3-rtc-recovery-') as directory:
    tmp = Path(directory)
    (tmp/'production_recovery.h').write_text(defines+selection+dirty+protocol+clock_and_calendar+gpio_methods, encoding='utf-8')
    exe = tmp/('rtc-recovery.exe' if os.name == 'nt' else 'rtc-recovery')
    command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O2', '-g', '-Wall', '-Wextra',
               '-I',str(tmp), '-I',str(ROOT/'code/core/arm9/source'),
               str(ROOT/'tools/tests/rtc_recovery_host.cpp'), '-o',str(exe)]
    if os.environ.get('SANITIZE') == '1':
        command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    subprocess.run(command,check=True)
    run = subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE)
    print(run.stdout, end='')
    if args.negative_control:
        expected = 'GPIO write persists offset' if args.negative_control == 'no-dirty' else 'incomplete command does not commit offset'
        assert run.returncode != 0 and 'FAIL '+expected in run.stdout, 'negative control did not trigger its intended assertion'
        print('PASS: negative control rejected:', args.negative_control)
    else:
        run.check_returncode()

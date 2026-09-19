#!/usr/bin/env python3
"""Execute production RTC GPIO/recovery; fake read-only files/host clock."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT/'code/core/arm9/source/Peripherals/RomGpio/RomGpioRtc.cpp').read_text(encoding='utf-8')
selection = source[source.index('RTC_EWRAM void RomGpioRtc::Initialize('):source.index('RTC_EWRAM bool RomGpioRtc::WriteStateFile(')]
dirty = source[source.index('RTC_EWRAM void RomGpioRtc::MarkStateDirty()'):source.index('RTC_EWRAM void RomGpioRtc::Update(RomGpio&')]
protocol = source[source.index('RTC_EWRAM void RomGpioRtc::Update(RomGpio&'):source.index('RTC_EWRAM void RomGpioRtc::UpdateDSDateTime()')]
clock_and_calendar = source[source.index('RTC_EWRAM void RomGpioRtc::UpdateDateTime()'):]
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
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command,check=True)
    subprocess.run([str(exe)],check=True)

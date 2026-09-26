#!/usr/bin/env python3
"""Compatibility entry point for the expanded production RTC/GPIO host suite.

The v2 runner retains the old GPIO transactions and named negative controls,
and broadens recovery coverage to the new 200-byte journal format.
"""

from test_rtc_v2_host import main


if __name__ == "__main__":
    main()

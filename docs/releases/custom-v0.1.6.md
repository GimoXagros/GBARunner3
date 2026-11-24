# GBARunner3 Custom v0.1.6

GBA 카트리지 RTC의 초 경계 처리와 저장 후 재시작 동작을 개선했습니다.

- Writing the time starts a fresh one-second interval on the emulated RTC. ARM7 supplies the monotonic tick count. Invalid BCD seconds such as E5 read back as 65 until the next tick carries into the next minute.
- Weekday progression is preserved when the guest year wraps from 99 to 00. New version-3 records preserve pending seconds and fractional progress across restarts. While powered off, elapsed time comes from the DS wall clock at whole-second resolution.
- The GBA tests shared by asie/ws-rtc-test provide the comparison vectors. See [protocol details](https://github.com/GimoXagros/GBARunner3/blob/custom-v0.1.6/docs/rtc-s3511a-protocol.md). Automated GPIO and linked ARM tests cover specific cases. The Unbound hardware report covered boot, save/reload and RTC on 3DS + DSpico, without a recorded binary hash.
- Before updating, back up the .sav and all .g3rtc* siblings together. Install GBARunner3.nds and merge _gba/configs with your settings. Existing valid V2 records are readable. New writes use V3 in the same .g3rtc2 slots. To roll back to v0.1.5, restore the matching pre-upgrade backup. For original v1 sidecars see RTC-LEGACY-UPGRADE.md and the included tools/rtc_migrate.py.
- Reference build: devkitpro/devkitarm:20241104. Application SHA-256: 8cca060c40205e149c511b001d810e942249964a1b5921b15ea19723ee34c965.

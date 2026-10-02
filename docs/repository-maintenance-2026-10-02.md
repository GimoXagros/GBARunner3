# Repository maintenance — 2026-10-02

Stable release: custom-v0.1.5, source `76fec2fd6a2a8422fc51b961374c0208d3932ef6`.
Integration baseline: `703a0510f0aa4781c9b4236eb2149d831d76fa9f`.
This maintenance changes documentation/attribution and repository presentation,
not runtime code, configs, submodules, existing tag targets or release assets.

## Completed topic branches selected for removal

All 13 old topic branches were checked before removal. Ten heads are ancestors
of develop; the diagnostic branch is preserved by its existing RC2 tag. The two
superseded unmerged drafts remain accessible through closed PR #5/#6 head refs,
verified against the exact SHAs below. Their code is not merged into develop.
A verified complete local Git bundle and remote metadata snapshot were created
before cleanup. Existing local worktrees were not removed.

| Branch | Last head | Preservation |
| --- | --- | --- |
| `docs/record-custom-v0.1.3-rc3-assets` | `ddef35f539f405bfa1e11118dd5de5ac57b21db1` | Merged into develop |
| `fix/high-rom-save-search-boundary` | `245a0dba82cef259f897002555221376f9ab97f1` | Superseded closed PR #5 |
| `fix/high-rom-save-search-boundary-v2` | `a46b781dc8a290bf12684390a921c73916612c64` | Merged into develop |
| `fix/rtc-bcd-time-conversion` | `22fb48dfeca7575804476abf5f1a5c2a707ae606` | Merged into develop |
| `fix/save-io-recovery-v2` | `ac4480c1890a85bdd12d5a64283c7fbafbe2273e` | Merged into develop |
| `fix/storage-transaction-result-channel` | `b38eaf867634b3fb88b9d5dd3e23121579947fff` | Merged into develop |
| `investigate/v013rc1-display-flicker` | `fd7018c27e9aa655e56004592f096698a1f08bce` | Preserved by tag custom-v0.1.3-rc2 |
| `release/custom-v0.1.3-rc3` | `5cb2111be1d3893ad2a31a11a22860dc613645e9` | Merged into develop |
| `release/custom-v0.1.3` | `4384f20188ecb03f46e200db371a1dd63debfdec` | Merged into develop |
| `release/custom-v0.1.4` | `e603bc30c33542c17137ed23eb63247497c2ca4d` | Merged into develop |
| `release/custom-v0.1.5` | `76fec2fd6a2a8422fc51b961374c0208d3932ef6` | Merged into develop |
| `test/dma-rom-boundary-regressions` | `8f12a9ff17b2947699d4dbe80074b697fb6e0219` | Merged into develop |
| `test/save-io-fault-injection` | `7cdc3bf35032515573ad1888f9eeb80bfea19150` | Superseded closed PR #6 |

## Tags, releases and credits

All 14 pre-existing tags and all ten releases are retained. Historical release
notes point to v0.1.5 while keeping the original evidence below the notice.
Old versions retain their original stable/prerelease flags; only v0.1.5 is Latest.
The release index distinguishes source-only and archive tags from downloads.

README is the current user entry point. CUSTOM_BUILD is a short navigation page;
its historical records are consolidated under docs/history. TODO distinguishes
current remaining work from the 2026-09-05 upstream issue snapshot. RTC checkpoint
documents are labeled historical, and the v0.1.5 record contains public hashes.
Contributors are credited without rewriting commit authors; .mailmap normalizes
only names sharing the same existing email identity. No contributor is removed.

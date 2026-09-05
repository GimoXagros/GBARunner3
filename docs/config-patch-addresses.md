# External patch address validation

Both `runSettings.jitPatchAddresses` and
`runSettings.selfModifyingPatchAddresses` accept arrays of strings containing
1–8 hexadecimal digits, optionally prefixed by `0x` or `0X`. Digits are case
insensitive. `0` and `FFFFFFFF` are valid values; this validates representation,
not whether an address is mapped or suitable for a particular patch.

An invalid element or a non-array property rejects that entire property and
preserves its previous/default pointer, contents and count. It emits a debug
message. Other independent valid settings still apply, consistent with the
serializer's existing per-setting fallback policy. An omitted property is
unchanged; an empty array clears it. JSON syntax errors reject the document.
Signs, whitespace, overflow, partial parses, non-string elements and embedded
NUL characters are rejected. Validation finishes before allocation or mutation.

Validation: the host suite compiles the real serializer with platform/file shims
and exercises both arrays and all 304 shipped configurations (2,513 addresses).
CI runs AddressSanitizer/UndefinedBehaviorSanitizer, a pinned ARM parser probe,
the complete application/test build and existing linked dispatch tests. Target
GoogleTest cases also compile into the test NDS; compilation alone is not a
claim that the complete target test NDS has executed on hardware.

The regression fails on baseline `4bc4d73` because malformed arrays replace the
previous address buffer. It passes after strict validation and atomic commit.

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t;
struct FIL {}; struct SaveTypeInfo {};
u8 bytes[8192]; unsigned flushes = 0;
u8 sav_readSaveByteFromFileFromUserMode(u32 address) { assert(address < 8192); return bytes[address]; }
void sav_writeSaveByteToFileFromUserMode(u32 address, u8 value) { assert(address < 8192); bytes[address] = value; }
void sav_flushSaveFileFromUserMode() { ++flushes; }
std::vector<const u32*> signatures;
int failAt = -1;
bool sav_tryPatchFunction(const u32* signature, u32 swi, void*) {
    assert(swi == signatures.size()); signatures.push_back(signature);
    return static_cast<int>(swi) != failAt;
}
#include "production_eeprom.h"
int main() {
    using Patch = bool (*)(const SaveTypeInfo*, FIL*, u32, u8*);
    const Patch patches[] = {eeprom_patchV111, eeprom_patchV120, eeprom_patchV124, eeprom_patchV126};
    const u32* read[] = {sReadEepromDwordV111Sig, sReadEepromDwordV120Sig, sReadEepromDwordV120Sig, sReadEepromDwordV120Sig};
    const u32* write[] = {sProgramEepromDwordV111Sig, sProgramEepromDwordV120Sig, sProgramEepromDwordV124Sig, sProgramEepromDwordV126Sig};
    for (unsigned i = 0; i < 4; ++i) {
        for (int failure : {-1, 0, 1}) {
            signatures.clear(); failAt = failure;
            assert(patches[i](nullptr, nullptr, 0, nullptr) == (failure == -1));
            assert(signatures[0] == read[i]);
            assert(signatures.size() == (failure == 0 ? 1u : 2u));
            if (failure != 0) assert(signatures[1] == write[i]);
        }
    }
    const u8 synthetic[] = {1, 3, 5, 7, 9, 11, 13, 15};
    alignas(2) u8 src[8], dst[8];
    std::memcpy(src, synthetic, 8);
    for (unsigned address : {0u, 1u, 1023u}) {
        flushes = 0;
        assert(programEepromDword(address, reinterpret_cast<u16*>(src)) == 0);
        assert(flushes == 1);
        for (unsigned i = 0; i < 8; ++i) assert(bytes[address * 8 + i] == src[7-i]);
        assert(readEepromDword(address, reinterpret_cast<u16*>(dst)) == 0);
        assert(std::memcmp(src, dst, 8) == 0);
    }
    std::cout << "PASS: 12 source selector/failure cases, 3 synthetic byte-wrapper round trips\n";
}

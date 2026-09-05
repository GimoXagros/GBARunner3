#pragma once
#include <fstream>
using TCHAR = char;
constexpr int FR_OK = 0, FA_READ = 1, FA_OPEN_EXISTING = 2;
class File {
    std::ifstream stream;
public:
    int Open(const char* path, int) { stream.open(path, std::ios::binary); return stream ? 0 : 1; }
    u32 GetSize() { stream.seekg(0, std::ios::end); auto n = stream.tellg(); stream.seekg(0); return static_cast<u32>(n); }
    int Read(void* data, u32 size, u32& read) { stream.read(static_cast<char*>(data), size); read = stream.gcount(); return stream.bad() ? 1 : 0; }
    int Close() { stream.close(); return 0; }
};

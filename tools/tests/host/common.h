#pragma once
#include <cstdint>
#include <cstring>
#include <new>
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using bool16 = u16;
// Target heap's aligned allocation is compatible with ordinary delete[].
struct HostCacheAlignment {};
constexpr HostCacheAlignment cache_align;
inline void* operator new[](std::size_t size, HostCacheAlignment) { return ::operator new[](size); }
inline void operator delete[](void* ptr, HostCacheAlignment) { ::operator delete[](ptr); }
enum class LogLevel { Debug };
struct HostLogger { template<class... T> void Log(LogLevel, const char*, T...) {} };
inline HostLogger hostLogger;
inline HostLogger* gLogger = &hostLogger;

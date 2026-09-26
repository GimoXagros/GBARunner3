#pragma once

// Terminal persistence failure. Never retries, closes files or resumes guest code.
extern "C" [[gnu::noreturn]] void sav_persistenceFault(void);

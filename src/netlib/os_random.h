#ifndef ZND_OS_RANDOM_H
#define ZND_OS_RANDOM_H
#include <cstddef>
#include <cstring>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <sys/random.h>
#include <cerrno>
#endif
namespace ZNDNet
{
// Checked OS entropy only; no clock/counter fallback. The Windows function is
// resolved from the system library to avoid a new linker requirement for users.
inline bool
OsRandom(void* data, std::size_t size)
{
    auto p = static_cast<unsigned char*>(data);
    bool success = false;
#ifdef _WIN32
    HMODULE lib = LoadLibraryExW(L"bcrypt.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (lib)
    {
        using Gen = LONG(WINAPI*)(void*, unsigned char*, ULONG, ULONG);
        auto gen = reinterpret_cast<Gen>(GetProcAddress(lib, "BCryptGenRandom"));
        success = gen && size <= 0xffffffffu && gen(nullptr, p, static_cast<ULONG>(size), 2) == 0;
        FreeLibrary(lib);
    }
#elif defined(__linux__)
    std::size_t got = 0;
    while (got < size)
    {
        auto n = getrandom(p + got, size - got, GRND_NONBLOCK);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        got += n;
    }
    success = got == size;
#endif
    unsigned any = 0;
    if (success)
        for (std::size_t i = 0; i < size; ++i)
            any |= p[i];
    if (!success || !any)
    {
        std::memset(data, 0, size);
        return false;
    }
    return true;
}
}
#endif

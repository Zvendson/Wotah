/**
 * @file utils.cpp
 * The reusable helpers declared in utils.hpp.
 */
#include "utils.hpp"

#ifdef _WIN32
#    define NOMINMAX
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#endif

#include <string>
#include <system_error>

namespace
{
#ifdef _WIN32
    /** The first buffer size for GetModuleFileNameW: MAX_PATH covers most paths. */
    constexpr DWORD FirstPathBufferSize = MAX_PATH;

    /**
     * The retry's buffer size: the longest path Windows allows (32 767 UTF-16 units, "Maximum Path Length
     * Limitation" in the Win32 docs) plus the terminating zero.
     */
    constexpr DWORD MaxPathBufferSize = 32768;

    /** The executable's full path, from GetModuleFileNameW; empty on failure. */
    std::filesystem::path FindExecutablePath()
    {
        // GetModuleFileNameW cuts the path short (and returns the buffer size) when the buffer is too small, so a
        // too-long path is read again into the largest buffer a path can need.
        std::wstring buffer;
        for (const DWORD size : {FirstPathBufferSize, MaxPathBufferSize})
        {
            buffer.resize(size);
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), size);
            if (length == 0)
            {
                return {};
            }
            if (length < size)
            {
                buffer.resize(length);
                return buffer;
            }
        }
        return {};
    }
#else
    /** The executable's full path, from /proc/self/exe; empty on failure. */
    std::filesystem::path FindExecutablePath()
    {
        // Linux keeps a link to the running executable at /proc/self/exe (proc(5)).
        std::error_code       error;
        std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
        if (error)
        {
            return {};
        }
        return path;
    }
#endif
} // namespace

std::optional<std::filesystem::path> Wotah::FindExecutableDirectory()
{
    const std::filesystem::path path = FindExecutablePath();
    if (path.empty() || !path.has_parent_path())
    {
        return std::nullopt;
    }
    return path.parent_path();
}

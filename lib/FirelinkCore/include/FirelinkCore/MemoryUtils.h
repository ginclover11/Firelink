#pragma once

#include <FirelinkCore/Export.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#ifdef _WIN32
// Kept for Windows-only consumers (process hooks) that rely on this header pulling in the Windows API.
#include <windows.h>
#endif

namespace Firelink
{
    /// @brief Concept that constrains memory read/write methods to types that are trivially copyable.
    template <typename T>
    concept MemReadWriteType = std::is_trivially_copyable_v<T>;

    /// @brief Convert a UTF-16 string to a standard UTF-8 string without warning about data loss.
    FIRELINK_CORE_API std::string UTF16ToUTF8(const std::u16string& utf16);

    /// @brief Convert a 'wide' string to a standard UTF-8 string without warning.
    ///
    /// Only meaningful for UTF-16 `wchar_t` (Windows); on other platforms `wchar_t` is 32-bit and values are
    /// truncated to 16 bits, so prefer the `std::u16string` overload.
    FIRELINK_CORE_API std::string UTF16ToUTF8(const std::wstring& utf16);

    /// @brief Convert a UTF-8 string to a UTF-16 string.
    FIRELINK_CORE_API std::u16string UTF8ToUTF16(const std::string& utf8);

    /// @brief Get a pointer offset by a number of bytes.
    FIRELINK_CORE_API const void* GetOffsetPointer(const void* ptr, int offset);

    /// @brief Convert a string pattern, e.g. '45 2c ? 9a', into a pattern and wildcard mask.
    ///
    /// Returns true if pattern is valid. `pattern` and `wildcardMask` are output parameters.
    FIRELINK_CORE_API bool ParsePatternString(
        const std::string& patternString, std::vector<std::uint8_t>& pattern, std::vector<bool>& wildcardMask);
} // namespace Firelink

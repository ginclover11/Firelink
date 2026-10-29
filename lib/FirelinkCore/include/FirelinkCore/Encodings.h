#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Firelink
{
    /// @brief Support string encodings used across all FromSoft games and files.
    enum class FSEncoding : std::uint32_t
    {
        UTF_16 = 0,
        SHIFT_JIS = 932,
    };

    /// @brief String conversion for FromSoftEncoding.
    std::string ToString(FSEncoding encoding);

    /// @brief Decode raw FromSoft bytes to UTF-8 using given supported FromSoftEncoding.
    std::string DecodeString(const char* encoded, std::size_t byteLen, FSEncoding encoding);

    /// @brief Decode a FromSoft string to UTF-8 using given supported FromSoftEncoding.
    std::string DecodeString(const std::string& encoded, FSEncoding encoding);

    /// @brief Encode a FromSoft UTF-8 string using given supported FromSoftEncoding.
    std::string EncodeString(const std::string& decoded, FSEncoding encoding);
} // namespace Firelink

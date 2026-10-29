#include <FirelinkCore/Encodings.h>

#include <FirelinkCore/Logging.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

std::string Firelink::ToString(const FSEncoding encoding)
{
    switch (encoding)
    {
        case FSEncoding::UTF_16:
            return "UTF-16";
        case FSEncoding::SHIFT_JIS:
            return "Shift-JIS";
        default:
            return "<Unknown>";
    }
}

#ifndef _WIN32

// ============================================================================
// Portable (non-Windows) implementation.
//
// Windows uses `MultiByteToWideChar` / `WideCharToMultiByte`. Elsewhere we do the conversion ourselves, so there is no
// dependence on `iconv`/gconv modules being present on the user's system. Shift-JIS here means Windows code page 932
// (Microsoft's variant), via the frozen tables in `Cp932Table.inc` (see `tools/gen_cp932_table.py`).
// ============================================================================

namespace
{
#include "Cp932Table.inc"

    // Windows' default replacement character for unmappable CP932 bytes (KATAKANA MIDDLE DOT).
    constexpr char16_t CP932_DEFAULT_CHAR = 0x30FB;
    constexpr char16_t REPLACEMENT_CHAR = 0xFFFD;

    /// Decode CP932 bytes to UTF-16 code units.
    std::u16string Cp932ToUtf16(const char* data, const std::size_t size)
    {
        std::u16string out;
        out.reserve(size);

        std::size_t i = 0;
        while (i < size)
        {
            const auto b = static_cast<unsigned char>(data[i]);

            if (b < 0x80)
            {
                out.push_back(static_cast<char16_t>(b));
                ++i;
            }
            else if (b == 0x80 || b == 0xA0)
            {
                // Windows maps these straight through (U+0080, U+00A0).
                out.push_back(static_cast<char16_t>(b));
                ++i;
            }
            else if (b >= 0xA1 && b <= 0xDF)
            {
                // Half-width katakana.
                out.push_back(static_cast<char16_t>(0xFF61 + (b - 0xA1)));
                ++i;
            }
            else if (b >= 0xFD)
            {
                // Windows maps 0xFD-0xFF to the private-use code points U+F8F1-U+F8F3.
                out.push_back(static_cast<char16_t>(0xF8F1 + (b - 0xFD)));
                ++i;
            }
            else
            {
                // Double-byte lead byte (0x81-0x9F, 0xE0-0xFC).
                const int row = CP932_LEAD_ROW[b];
                if (row < 0 || i + 1 >= size)
                {
                    out.push_back(CP932_DEFAULT_CHAR);
                    ++i;
                    continue;
                }
                const auto trail = static_cast<unsigned char>(data[i + 1]);
                if (trail < CP932_TRAIL_MIN || trail >= CP932_TRAIL_MIN + CP932_TRAIL_COUNT)
                {
                    // Invalid trail byte: only the lead byte is consumed.
                    out.push_back(CP932_DEFAULT_CHAR);
                    ++i;
                    continue;
                }
                const char16_t mapped = CP932_DOUBLE_BYTE_DECODE[row * CP932_TRAIL_COUNT + (trail - CP932_TRAIL_MIN)];
                out.push_back(mapped != 0 ? mapped : CP932_DEFAULT_CHAR);
                i += 2;
            }
        }
        return out;
    }

    /// Encode UTF-16 code units to CP932 bytes. Throws if any character has no CP932 representation.
    std::string Utf16ToCp932(const std::u16string& wide)
    {
        std::string out;
        out.reserve(wide.size() * 2);

        for (const char16_t c : wide)
        {
            if (c < 0x80)
            {
                out.push_back(static_cast<char>(c));
                continue;
            }
            const auto* begin = std::begin(CP932_ENCODE);
            const auto* end = std::end(CP932_ENCODE);
            const auto* it = std::lower_bound(
                begin, end, c, [](const Cp932EncodeEntry& entry, const char16_t value) { return entry.unicode < value; });
            if (it == end || it->unicode != c)
                throw std::runtime_error("String contains characters not representable in given encoding");

            if (it->code <= 0xFF)
            {
                out.push_back(static_cast<char>(it->code));
            }
            else
            {
                out.push_back(static_cast<char>(it->code >> 8));
                out.push_back(static_cast<char>(it->code & 0xFF));
            }
        }
        return out;
    }

    /// UTF-16 code units -> UTF-8. Lone surrogates become U+FFFD.
    std::string Utf16ToUtf8(const std::u16string& wide)
    {
        std::string out;
        out.reserve(wide.size());

        for (std::size_t i = 0; i < wide.size(); ++i)
        {
            char32_t cp = wide[i];
            if (cp >= 0xD800 && cp <= 0xDBFF)
            {
                if (i + 1 < wide.size() && wide[i + 1] >= 0xDC00 && wide[i + 1] <= 0xDFFF)
                {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<char32_t>(wide[i + 1]) - 0xDC00);
                    ++i;
                }
                else
                {
                    cp = REPLACEMENT_CHAR;
                }
            }
            else if (cp >= 0xDC00 && cp <= 0xDFFF)
            {
                cp = REPLACEMENT_CHAR;
            }

            if (cp < 0x80)
            {
                out.push_back(static_cast<char>(cp));
            }
            else if (cp < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
        return out;
    }

    /// UTF-8 -> UTF-16 code units. Malformed sequences become U+FFFD (one per bad lead byte).
    std::u16string Utf8ToUtf16(const std::string& utf8)
    {
        std::u16string out;
        out.reserve(utf8.size());

        std::size_t i = 0;
        const std::size_t n = utf8.size();
        while (i < n)
        {
            const auto b = static_cast<unsigned char>(utf8[i]);
            char32_t cp;
            std::size_t extra;

            if (b < 0x80)       { cp = b;        extra = 0; }
            else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; extra = 1; }
            else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; extra = 2; }
            else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; extra = 3; }
            else
            {
                out.push_back(REPLACEMENT_CHAR);
                ++i;
                continue;
            }

            if (i + extra >= n)
            {
                // Truncated sequence at end of input (never true for ASCII, where `extra == 0`).
                out.push_back(REPLACEMENT_CHAR);
                ++i;
                continue;
            }

            bool valid = true;
            for (std::size_t k = 1; k <= extra; ++k)
            {
                const auto cont = static_cast<unsigned char>(utf8[i + k]);
                if ((cont & 0xC0) != 0x80)
                {
                    valid = false;
                    break;
                }
                cp = (cp << 6) | (cont & 0x3F);
            }
            if (!valid)
            {
                out.push_back(REPLACEMENT_CHAR);
                ++i;
                continue;
            }

            i += extra + 1;
            if (cp >= 0x10000)
            {
                cp -= 0x10000;
                out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
                out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
            }
            else
            {
                out.push_back(static_cast<char16_t>(cp));
            }
        }
        return out;
    }
} // namespace

std::string Firelink::DecodeString(const char* encoded, const size_t byteLen, const FSEncoding encoding)
{
    if (byteLen == 0)
        return {};

    if (encoding == FSEncoding::UTF_16)
    {
        // Raw bytes are UTF-16-LE code units (big-endian strings are byte-swapped by the reader beforehand).
        // We copy into a `char16_t` buffer rather than reinterpreting, to avoid alignment/aliasing issues.
        std::u16string wide(byteLen / sizeof(char16_t), u'\0');
        std::memcpy(wide.data(), encoded, wide.size() * sizeof(char16_t));
        return Utf16ToUtf8(wide);
    }

    if (encoding == FSEncoding::SHIFT_JIS)
        return Utf16ToUtf8(Cp932ToUtf16(encoded, byteLen));

    throw std::runtime_error(std::format("DecodeString: unsupported encoding {}", ToString(encoding)));
}

std::string Firelink::DecodeString(const std::string& encoded, const FSEncoding encoding)
{
    return DecodeString(encoded.data(), encoded.size(), encoding);
}

std::string Firelink::EncodeString(const std::string& decoded, const FSEncoding encoding)
{
    if (decoded.empty())
        return {};

    const std::u16string wide = Utf8ToUtf16(decoded);

    if (encoding == FSEncoding::UTF_16)
    {
        // Raw UTF-16-LE bytes (host is little-endian on all platforms we target).
        std::string bytes(wide.size() * sizeof(char16_t), '\0');
        std::memcpy(bytes.data(), wide.data(), bytes.size());
        return bytes;
    }

    if (encoding == FSEncoding::SHIFT_JIS)
        return Utf16ToCp932(wide);

    throw std::runtime_error(std::format("EncodeString: unsupported encoding {}", ToString(encoding)));
}

#else // _WIN32

std::string Firelink::DecodeString(const char* encoded, const size_t byteLen, const FSEncoding encoding)
{
    if (byteLen == 0) return {};

    std::wstring wide;

    if (encoding == FSEncoding::UTF_16)
    {
        // Raw bytes are already UTF-16-LE wide (byte pairs) — just reinterpret.
        // Big-endian encoded strings are byte-swapped by reader.
        wide.assign(
            reinterpret_cast<const wchar_t*>(encoded),
            byteLen / sizeof(wchar_t)
        );
    }
    else
    {
        // All other strings are assumed to be multibyte and are converted to generic wide here.
        const std::uint32_t codePage = static_cast<std::uint32_t>(encoding);
        const int wideLen = MultiByteToWideChar(codePage, 0, encoded, static_cast<int>(byteLen), nullptr, 0);
        if (wideLen == 0)
            throw std::runtime_error(std::format("MultiByteToWideChar ({}) size query failed", ToString(encoding)));
        wide.resize(wideLen);
        MultiByteToWideChar(codePage, 0, encoded, static_cast<int>(byteLen), wide.data(), wideLen);
    }

    // Wide (UTF-16-LE) -> UTF-8
    const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (utf8Len == 0)
        throw std::runtime_error("WideCharToMultiByte (UTF-8) size query failed");
    std::string utf8(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), utf8Len, nullptr, nullptr);

    return utf8;
}

std::string Firelink::DecodeString(const std::string& encoded, const FSEncoding encoding)
{
    return DecodeString(encoded.data(), encoded.size(), encoding);
}

std::string Firelink::EncodeString(const std::string& decoded, const FSEncoding encoding)
{
    if (decoded.empty()) return {};

    // UTF-8 → wide
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, decoded.data(), static_cast<int>(decoded.size()), nullptr, 0);
    if (wideLen == 0)
        throw std::runtime_error("MultiByteToWideChar (UTF-8) size query failed");
    std::wstring wide(wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, decoded.data(), static_cast<int>(decoded.size()), wide.data(), wideLen);

    if (encoding == FSEncoding::UTF_16)
    {
        // Reinterpret wide string as raw bytes (already UTF-16-LE on Windows).
        return std::string{reinterpret_cast<const char*>(wide.data()), wide.size() * sizeof(wchar_t)};
    }

    // Use multibyte code page.
    const std::uint32_t codePage = static_cast<std::uint32_t>(encoding);
    BOOL used_default = FALSE;
    const int multiByteLen = WideCharToMultiByte(codePage, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, &used_default);
    if (multiByteLen == 0)
        throw std::runtime_error(std::format("WideCharToMultiByte ({}) size query failed", ToString(encoding)));
    std::string encoded(multiByteLen, '\0');
    WideCharToMultiByte(codePage, 0, wide.data(), static_cast<int>(wide.size()), encoded.data(), multiByteLen, nullptr, &used_default);

    if (used_default)
        throw std::runtime_error("String contains characters not representable in given encoding");

    return encoded;
}

#endif // _WIN32

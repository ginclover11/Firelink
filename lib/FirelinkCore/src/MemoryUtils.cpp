#include <FirelinkCore/Logging.h>
#include <FirelinkCore/MemoryUtils.h>

#include <sstream>

std::string Firelink::UTF16ToUTF8(const std::u16string& utf16)
{
    std::string str;
    str.reserve(utf16.size());
    for (const char16_t c : utf16)
    {
        if (c < 0x80)
        {
            str.push_back(static_cast<char>(c));
        }
        else if (c < 0x800)
        {
            str.push_back(static_cast<char>(0xC0 | (c >> 6)));
            str.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
        else
        {
            str.push_back(static_cast<char>(0xE0 | (c >> 12)));
            str.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            str.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return str;
}

std::string Firelink::UTF16ToUTF8(const std::wstring& utf16)
{
    return UTF16ToUTF8(std::u16string(utf16.begin(), utf16.end()));
}

std::u16string Firelink::UTF8ToUTF16(const std::string& utf8)
{
    std::u16string result;
    result.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size())
    {
        char32_t cp;
        auto c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80)
        {
            cp = c;
            i += 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            cp = c & 0x1F;
            if (i + 1 < utf8.size())
                cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3F);
            i += 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            cp = c & 0x0F;
            if (i + 1 < utf8.size())
                cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3F);
            if (i + 2 < utf8.size())
                cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + 2]) & 0x3F);
            i += 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            cp = c & 0x07;
            if (i + 1 < utf8.size())
                cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3F);
            if (i + 2 < utf8.size())
                cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + 2]) & 0x3F);
            if (i + 3 < utf8.size())
                cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + 3]) & 0x3F);
            i += 4;
        }
        else
        {
            // Invalid byte; skip.
            i += 1;
            continue;
        }

        if (cp <= 0xFFFF)
        {
            result.push_back(static_cast<char16_t>(cp));
        }
        else if (cp <= 0x10FFFF)
        {
            // Surrogate pair.
            cp -= 0x10000;
            result.push_back(static_cast<char16_t>(0xD800 | (cp >> 10)));
            result.push_back(static_cast<char16_t>(0xDC00 | (cp & 0x3FF)));
        }
    }
    return result;
}

const void* Firelink::GetOffsetPointer(const void* ptr, const int offset)
{
    return static_cast<const char*>(ptr) + offset;
}

bool Firelink::ParsePatternString(
    const std::string& patternString, std::vector<std::uint8_t>& pattern, std::vector<bool>& wildcardMask)
{
    std::istringstream stream(patternString);
    std::string token;

    // Clear output std::vectors
    pattern.clear();
    wildcardMask.clear();

    while (stream >> token)
    {
        if (token == "?")
        {
            // Wildcard detected
            pattern.push_back(0x00); // Placeholder value (doesn't matter, as mask will ignore it)
            wildcardMask.push_back(true);
        }
        else
        {
            // Parse hex byte
            try
            {
                const auto byte = static_cast<std::uint8_t>(std::stoul(token, nullptr, 16));
                pattern.push_back(byte);
                wildcardMask.push_back(false);
            }
            catch (...)
            {
                Error("Invalid token in pattern std::string: " + token);
                return false; // Parsing error
            }
        }
    }

    // Ensure both std::vectors have the same size, and return true for successful parsing
    return pattern.size() == wildcardMask.size();
}
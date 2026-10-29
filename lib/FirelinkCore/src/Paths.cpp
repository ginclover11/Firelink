#include <FirelinkCore/Paths.h>

#include <algorithm>
#include <cctype>
#include <ranges>

std::string Firelink::ToLower(const std::string& s)
{
    std::string r = s;
    std::ranges::transform(r, r.begin(), [](const unsigned char c) { return std::tolower(c); });
    return r;
}

std::string Firelink::ToUpper(const std::string& s)
{
    std::string r = s;
    std::ranges::transform(r, r.begin(), [](const unsigned char c) { return std::toupper(c); });
    return r;
}

std::string Firelink::StemOf(const std::filesystem::path& p)
{
    std::string name = p.filename().string();
    const size_t dot = name.find('.');
    return ToLower(dot == std::string::npos ? name : name.substr(0, dot));
}

bool Firelink::MatchesGlob(const std::string& filename, const std::string& glob)
{
    const size_t star = glob.find('*');
    if (star == std::string::npos) return filename == glob;

    const std::string prefix = glob.substr(0, star);
    const std::string suffix = glob.substr(star + 1);

    const std::string lower_name = ToLower(filename);
    const std::string lower_prefix = ToLower(prefix);
    const std::string lower_suffix = ToLower(suffix);

    return lower_name.size() >= lower_prefix.size() + lower_suffix.size()
        && lower_name.compare(0, lower_prefix.size(), lower_prefix) == 0
        && lower_name.compare(lower_name.size() - lower_suffix.size(), lower_suffix.size(), lower_suffix) == 0;
}
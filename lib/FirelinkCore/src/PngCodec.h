// Minimal PNG decode/encode for platforms without the Windows Imaging Component (WIC).
//
// On Windows, `DDS::FromPNG` / `DDS::ToPNG` go through DirectXTex's WIC helpers. DirectXTex has no PNG support
// elsewhere (its WIC sources are Windows-only), so on Linux we use stb_image / stb_image_write instead. This is an
// internal header: it is not part of Firelink's public API and is only compiled on non-Windows platforms.

#pragma once

#ifndef _WIN32

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Firelink::PngCodec
{
    /// @brief A decoded 8-bit RGBA image (rows are tightly packed: pitch == width * 4).
    struct DecodedImage
    {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> rgba;
    };

    /// @brief Decode PNG bytes into 8-bit RGBA, expanding grayscale/palette/RGB sources. Throws std::runtime_error.
    DecodedImage DecodeToRGBA(const std::uint8_t* data, std::size_t size);

    /// @brief Encode 8-bit RGBA pixels as a PNG. `rowPitch` is the distance in bytes between rows. Throws on failure.
    std::vector<std::byte> EncodeRGBA(const std::uint8_t* rgba, int width, int height, std::size_t rowPitch);
} // namespace Firelink::PngCodec

#endif // !_WIN32

// PS4 GNF texture swizzle / deswizzle for FromSoftware TPF files.
//
// FromSoftware PS4 TPF textures store pixel data in the AMD GNF macro-tile
// format: the image is divided into 8x8-block tiles, and blocks within each
// tile are ordered by a Morton (Z-order) curve.  PC DDS consumers expect a
// plain row-major (linear) layout, so the data must be deswizzled on read
// and re-swizzled on write.
//
// Both operations work on the raw pixel/block data only — no DDS header is
// included in the input or output.  For block-compressed formats (BC1–BC7)
// each "block" is one 4x4-pixel BC block; for uncompressed formats each
// "block" is one pixel.

#pragma once

#include <FirelinkCore/Export.h>

#include <cstddef>
#include <vector>

#ifdef _WIN32
#include <dxgiformat.h>    // DXGI_FORMAT
#else
#include <directx/dxgiformat.h>    // DXGI_FORMAT (DirectX-Headers package)
#endif

namespace Firelink
{
    // -------------------------------------------------------------------------
    // Core swizzle / deswizzle
    //
    // Parameters
    //   data           Raw source bytes (no DDS header).
    //   size           Source buffer size in bytes.
    //   width          Texture width  in pixels.
    //   height         Texture height in pixels.
    //   mipCount       Number of mip levels (>= 1).
    //   textureCount   Array slices / faces (1 for a plain texture, 6 for a cubemap).
    //   bytesPerBlock  Bytes per block element:
    //                    8  for BC1, BC4
    //                    16 for BC2, BC3, BC5, BC6H, BC7
    //                    bytes-per-pixel for uncompressed formats
    //   pixelsPerBlock Width (== height) of one block in pixels:
    //                    4  for any BCn format  (each block covers 4x4 texels)
    //                    1  for uncompressed formats
    //
    // The swizzled buffer is larger than the linear one: each mip level is
    // padded to a full grid of 8x8-block tiles.
    // -------------------------------------------------------------------------

    /// @brief Convert PS4 tiled (swizzled) texture data to linear row-major layout.
    [[nodiscard]] FIRELINK_CORE_API
    std::vector<std::byte> DeswizzlePS4(
        const std::byte* data, std::size_t size,
        int width, int height,
        int mipCount, int textureCount,
        int bytesPerBlock, int pixelsPerBlock);

    /// @brief Convert linear row-major texture data to PS4 tiled (swizzled) layout.
    [[nodiscard]] FIRELINK_CORE_API
    std::vector<std::byte> SwizzlePS4(
        const std::byte* data, std::size_t size,
        int width, int height,
        int mipCount, int textureCount,
        int bytesPerBlock, int pixelsPerBlock);

    // -------------------------------------------------------------------------
    // DXGI_FORMAT convenience overloads
    // Derives bytesPerBlock and pixelsPerBlock from the format automatically.
    // Throws std::invalid_argument for unsupported formats.
    // -------------------------------------------------------------------------

    /// @brief Deswizzle PS4 texture data using format-derived block parameters.
    [[nodiscard]] FIRELINK_CORE_API
    std::vector<std::byte> DeswizzlePS4(
        const std::byte* data, std::size_t size,
        int width, int height,
        int mipCount, int textureCount,
        DXGI_FORMAT format);

    /// @brief Swizzle linear texture data to PS4 layout using format-derived block parameters.
    [[nodiscard]] FIRELINK_CORE_API
    std::vector<std::byte> SwizzlePS4(
        const std::byte* data, std::size_t size,
        int width, int height,
        int mipCount, int textureCount,
        DXGI_FORMAT format);

    /// @brief Bytes per compressed block or uncompressed pixel for a DXGI format.
    ///        Returns 0 for unrecognized formats.
    [[nodiscard]] FIRELINK_CORE_API int PS4BytesPerBlock(DXGI_FORMAT format) noexcept;

    /// @brief Pixels per block side (4 for BCn, 1 for uncompressed).
    ///        Returns 0 for unrecognized formats.
    [[nodiscard]] FIRELINK_CORE_API int PS4PixelsPerBlock(DXGI_FORMAT format) noexcept;

} // namespace Firelink


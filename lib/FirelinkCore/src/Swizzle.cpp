// PS4 GNF texture swizzle / deswizzle implementation.
//
// The PS4 GPU stores textures in a macro-tiled layout:
//
//   ┌──────────────────────────────────────────┐
//   │  Image (in block coordinates)            │
//   │  ┌───────┬───────┬───────┐               │
//   │  │ tile  │ tile  │ tile  │               │
//   │  │ 0     │ 1     │ 2     │               │
//   │  │       │       │       │               │
//   │  ├───────┼───────┼───────┤               │
//   │  │ tile  │ tile  │ ...   │               │
//   │  │ 3     │ 4     │       │               │
//   │  └───────┴───────┴───────┘               │
//   └──────────────────────────────────────────┘
//
//  • Each tile is 8×8 blocks (64 blocks total).
//  • Blocks within a tile are ordered by a Morton (Z-order) curve, so that
//    spatially close texels share cache lines on the GPU.
//  • The tile grid width is rounded up to the next multiple of 8 blocks.
//  • Each mip level is stored consecutively in the swizzled buffer; smaller
//    mips are still rounded up to a full 8×8-block tile.
//
// For block-compressed (BCn) textures the "block" unit is one 4×4-pixel BC
// block.  For uncompressed formats the "block" is one pixel.

#include <FirelinkCore/Swizzle.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace Firelink
{
    namespace
    {
        // -----------------------------------------------------------------------
        // Helpers
        // -----------------------------------------------------------------------

        /// Round @p value up to the next multiple of @p align (power-of-two).
        constexpr int AlignUp(const int value, const int align) noexcept
        {
            return (value + align - 1) & ~(align - 1);
        }

        /// Z-order (Morton) index for (x, y) within an 8×8 tile.
        /// x and y must each be in [0, 7].
        /// Bit layout (LSB → MSB): x0 y0 x1 y1 x2 y2
        constexpr int MortonIndex(const int x, const int y) noexcept
        {
            int result = 0;
            for (int bit = 0; bit < 3; ++bit)
            {
                result |= ((x >> bit) & 1) << (bit * 2);
                result |= ((y >> bit) & 1) << (bit * 2 + 1);
            }
            return result;
        }

        /// Number of blocks in a given pixel dimension.
        /// Minimum 1 (even for 1×1 pixel mips).
        constexpr int BlockCount(const int pixels, const int pixelsPerBlock) noexcept
        {
            return std::max(1, (pixels + pixelsPerBlock - 1) / pixelsPerBlock);
        }

        /// Swizzled (tiled + padded) byte count for one mip level.
        constexpr int SwizzledMipBytes(const int wBlocks, const int hBlocks, const int bytesPerBlock) noexcept
        {
            return AlignUp(wBlocks, 8) * AlignUp(hBlocks, 8) * bytesPerBlock;
        }

        /// Linear (row-major) byte count for one mip level.
        constexpr int LinearMipBytes(const int wBlocks, const int hBlocks, const int bytesPerBlock) noexcept
        {
            return wBlocks * hBlocks * bytesPerBlock;
        }

        // -----------------------------------------------------------------------
        // Per-mip converters
        // -----------------------------------------------------------------------

        /// Deswizzle one mip level: PS4 tiled → linear row-major.
        void DeswizzleMip(
            const std::byte* src,
            std::byte*       dst,
            const int wBlocks, const int hBlocks,
            const int bytesPerBlock) noexcept
        {
            const int tiledWidth  = AlignUp(wBlocks, 8);
            const int tilesPerRow = tiledWidth / 8;

            for (int by = 0; by < hBlocks; ++by)
            {
                for (int bx = 0; bx < wBlocks; ++bx)
                {
                    const int tileCol   = bx / 8;
                    const int tileRow   = by / 8;
                    const int inTileX   = bx % 8;
                    const int inTileY   = by % 8;
                    const int tileIdx   = tileRow * tilesPerRow + tileCol;
                    const int inTileIdx = MortonIndex(inTileX, inTileY);

                    const int srcIdx = tileIdx * 64 + inTileIdx;   // swizzled
                    const int dstIdx = by * wBlocks + bx;          // linear

                    std::memcpy(
                        dst + dstIdx * bytesPerBlock,
                        src + srcIdx * bytesPerBlock,
                        static_cast<std::size_t>(bytesPerBlock));
                }
            }
        }

        /// Swizzle one mip level: linear row-major → PS4 tiled.
        void SwizzleMip(
            const std::byte* src,
            std::byte*       dst,
            const int wBlocks, const int hBlocks,
            const int bytesPerBlock) noexcept
        {
            const int tiledWidth  = AlignUp(wBlocks, 8);
            const int tiledHeight = AlignUp(hBlocks, 8);
            const int tilesPerRow = tiledWidth / 8;

            // Zero the destination — the padded tile regions won't be written.
            std::memset(dst, 0, tiledWidth * tiledHeight * bytesPerBlock);

            for (int by = 0; by < hBlocks; ++by)
            {
                for (int bx = 0; bx < wBlocks; ++bx)
                {
                    const int tileCol   = bx / 8;
                    const int tileRow   = by / 8;
                    const int inTileX   = bx % 8;
                    const int inTileY   = by % 8;
                    const int tileIdx   = tileRow * tilesPerRow + tileCol;
                    const int inTileIdx = MortonIndex(inTileX, inTileY);

                    const int srcIdx = by * wBlocks + bx;          // linear
                    const int dstIdx = tileIdx * 64 + inTileIdx;   // swizzled

                    std::memcpy(
                        dst + dstIdx * bytesPerBlock,
                        src + srcIdx * bytesPerBlock,
                        static_cast<std::size_t>(bytesPerBlock));
                }
            }
        }

        // -----------------------------------------------------------------------
        // Total size helpers (all mips × all faces)
        // -----------------------------------------------------------------------

        std::size_t TotalLinearSize(
            const int width, const int height,
            const int mipCount, const int textureCount,
            const int bytesPerBlock, const int pixelsPerBlock) noexcept
        {
            std::size_t total = 0;
            for (int face = 0; face < textureCount; ++face)
            {
                int w = width, h = height;
                for (int mip = 0; mip < mipCount; ++mip)
                {
                    total += static_cast<std::size_t>(
                        LinearMipBytes(BlockCount(w, pixelsPerBlock),
                                       BlockCount(h, pixelsPerBlock),
                                       bytesPerBlock));
                    w = std::max(1, w / 2);
                    h = std::max(1, h / 2);
                }
            }
            return total;
        }

        std::size_t TotalSwizzledSize(
            const int width, const int height,
            const int mipCount, const int textureCount,
            const int bytesPerBlock, const int pixelsPerBlock) noexcept
        {
            std::size_t total = 0;
            for (int face = 0; face < textureCount; ++face)
            {
                int w = width, h = height;
                for (int mip = 0; mip < mipCount; ++mip)
                {
                    total += static_cast<std::size_t>(
                        SwizzledMipBytes(BlockCount(w, pixelsPerBlock),
                                         BlockCount(h, pixelsPerBlock),
                                         bytesPerBlock));
                    w = std::max(1, w / 2);
                    h = std::max(1, h / 2);
                }
            }
            return total;
        }

    } // anonymous namespace

    // =========================================================================
    // DeswizzlePS4
    // =========================================================================

    std::vector<std::byte> DeswizzlePS4(
        const std::byte* data, const std::size_t size,
        const int width, const int height,
        const int mipCount, const int textureCount,
        const int bytesPerBlock, const int pixelsPerBlock)
    {
        if (!data || size == 0)
            throw std::invalid_argument("DeswizzlePS4: null or empty input");
        if (width <= 0 || height <= 0 || mipCount <= 0 || textureCount <= 0)
            throw std::invalid_argument("DeswizzlePS4: invalid dimensions");
        if (bytesPerBlock <= 0 || pixelsPerBlock <= 0)
            throw std::invalid_argument("DeswizzlePS4: invalid block parameters");

        const std::size_t expectedSwizzled =
            TotalSwizzledSize(width, height, mipCount, textureCount,
                              bytesPerBlock, pixelsPerBlock);
        if (size < expectedSwizzled)
            throw std::runtime_error(
                "DeswizzlePS4: input too small — got " + std::to_string(size) +
                " bytes, expected " + std::to_string(expectedSwizzled));

        const std::size_t linearTotal =
            TotalLinearSize(width, height, mipCount, textureCount,
                            bytesPerBlock, pixelsPerBlock);

        std::vector<std::byte> result(linearTotal);

        std::size_t srcOff = 0;
        std::size_t dstOff = 0;

        for (int face = 0; face < textureCount; ++face)
        {
            int w = width, h = height;
            for (int mip = 0; mip < mipCount; ++mip)
            {
                const int wb = BlockCount(w, pixelsPerBlock);
                const int hb = BlockCount(h, pixelsPerBlock);

                const auto swBytes  = static_cast<std::size_t>(SwizzledMipBytes(wb, hb, bytesPerBlock));
                const auto linBytes = static_cast<std::size_t>(LinearMipBytes(wb, hb, bytesPerBlock));

                DeswizzleMip(data + srcOff, result.data() + dstOff, wb, hb, bytesPerBlock);

                srcOff += swBytes;
                dstOff += linBytes;
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
            }
        }

        return result;
    }

    // =========================================================================
    // SwizzlePS4
    // =========================================================================

    std::vector<std::byte> SwizzlePS4(
        const std::byte* data, const std::size_t size,
        const int width, const int height,
        const int mipCount, const int textureCount,
        const int bytesPerBlock, const int pixelsPerBlock)
    {
        if (!data || size == 0)
            throw std::invalid_argument("SwizzlePS4: null or empty input");
        if (width <= 0 || height <= 0 || mipCount <= 0 || textureCount <= 0)
            throw std::invalid_argument("SwizzlePS4: invalid dimensions");
        if (bytesPerBlock <= 0 || pixelsPerBlock <= 0)
            throw std::invalid_argument("SwizzlePS4: invalid block parameters");

        const std::size_t expectedLinear =
            TotalLinearSize(width, height, mipCount, textureCount,
                            bytesPerBlock, pixelsPerBlock);
        if (size < expectedLinear)
            throw std::runtime_error(
                "SwizzlePS4: input too small — got " + std::to_string(size) +
                " bytes, expected " + std::to_string(expectedLinear));

        const std::size_t swizzledTotal =
            TotalSwizzledSize(width, height, mipCount, textureCount,
                              bytesPerBlock, pixelsPerBlock);

        std::vector<std::byte> result(swizzledTotal);

        std::size_t srcOff = 0;
        std::size_t dstOff = 0;

        for (int face = 0; face < textureCount; ++face)
        {
            int w = width, h = height;
            for (int mip = 0; mip < mipCount; ++mip)
            {
                const int wb = BlockCount(w, pixelsPerBlock);
                const int hb = BlockCount(h, pixelsPerBlock);

                const auto swBytes  = static_cast<std::size_t>(SwizzledMipBytes(wb, hb, bytesPerBlock));
                const auto linBytes = static_cast<std::size_t>(LinearMipBytes(wb, hb, bytesPerBlock));

                SwizzleMip(data + srcOff, result.data() + dstOff, wb, hb, bytesPerBlock);

                srcOff += linBytes;
                dstOff += swBytes;
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
            }
        }

        return result;
    }


    // =========================================================================
    // DXGI format helpers
    // =========================================================================

    int PS4BytesPerBlock(const DXGI_FORMAT format) noexcept
    {
        switch (format)
        {
            // --- 8 bytes / block ---
            case DXGI_FORMAT_BC1_TYPELESS:
            case DXGI_FORMAT_BC1_UNORM:
            case DXGI_FORMAT_BC1_UNORM_SRGB:
            case DXGI_FORMAT_BC4_TYPELESS:
            case DXGI_FORMAT_BC4_UNORM:
            case DXGI_FORMAT_BC4_SNORM:
                return 8;

            // --- 16 bytes / block ---
            case DXGI_FORMAT_BC2_TYPELESS:
            case DXGI_FORMAT_BC2_UNORM:
            case DXGI_FORMAT_BC2_UNORM_SRGB:
            case DXGI_FORMAT_BC3_TYPELESS:
            case DXGI_FORMAT_BC3_UNORM:
            case DXGI_FORMAT_BC3_UNORM_SRGB:
            case DXGI_FORMAT_BC5_TYPELESS:
            case DXGI_FORMAT_BC5_UNORM:
            case DXGI_FORMAT_BC5_SNORM:
            case DXGI_FORMAT_BC6H_TYPELESS:
            case DXGI_FORMAT_BC6H_UF16:
            case DXGI_FORMAT_BC6H_SF16:
            case DXGI_FORMAT_BC7_TYPELESS:
            case DXGI_FORMAT_BC7_UNORM:
            case DXGI_FORMAT_BC7_UNORM_SRGB:
                return 16;

            // --- Uncompressed: bytes per pixel ---
            case DXGI_FORMAT_A8_UNORM:
                return 1;
            case DXGI_FORMAT_R8G8_UNORM:
            case DXGI_FORMAT_R8G8_SNORM:
                return 2;
            case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            case DXGI_FORMAT_R8G8B8A8_SNORM:
            case DXGI_FORMAT_B8G8R8A8_UNORM:
            case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            case DXGI_FORMAT_B8G8R8X8_UNORM:
            case DXGI_FORMAT_B8G8R8X8_TYPELESS:
            case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            case DXGI_FORMAT_R10G10B10A2_UNORM:
            case DXGI_FORMAT_R10G10B10A2_TYPELESS:
                return 4;
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
            case DXGI_FORMAT_R16G16B16A16_UNORM:
            case DXGI_FORMAT_R16G16B16A16_SNORM:
            case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                return 8;
            case DXGI_FORMAT_R32G32B32A32_FLOAT:
            case DXGI_FORMAT_R32G32B32A32_TYPELESS:
                return 16;

            default:
                return 0;
        }
    }

    int PS4PixelsPerBlock(const DXGI_FORMAT format) noexcept
    {
        switch (format)
        {
            case DXGI_FORMAT_BC1_TYPELESS:   case DXGI_FORMAT_BC1_UNORM:   case DXGI_FORMAT_BC1_UNORM_SRGB:
            case DXGI_FORMAT_BC2_TYPELESS:   case DXGI_FORMAT_BC2_UNORM:   case DXGI_FORMAT_BC2_UNORM_SRGB:
            case DXGI_FORMAT_BC3_TYPELESS:   case DXGI_FORMAT_BC3_UNORM:   case DXGI_FORMAT_BC3_UNORM_SRGB:
            case DXGI_FORMAT_BC4_TYPELESS:   case DXGI_FORMAT_BC4_UNORM:   case DXGI_FORMAT_BC4_SNORM:
            case DXGI_FORMAT_BC5_TYPELESS:   case DXGI_FORMAT_BC5_UNORM:   case DXGI_FORMAT_BC5_SNORM:
            case DXGI_FORMAT_BC6H_TYPELESS:  case DXGI_FORMAT_BC6H_UF16:   case DXGI_FORMAT_BC6H_SF16:
            case DXGI_FORMAT_BC7_TYPELESS:   case DXGI_FORMAT_BC7_UNORM:   case DXGI_FORMAT_BC7_UNORM_SRGB:
                return 4;   // each BCn block covers a 4×4 pixel region
            default:
                return 1;   // uncompressed: one element = one pixel
        }
    }

    // -------------------------------------------------------------------------
    // DXGI_FORMAT convenience overloads
    // -------------------------------------------------------------------------

    std::vector<std::byte> DeswizzlePS4(
        const std::byte* data, const std::size_t size,
        const int width, const int height,
        const int mipCount, const int textureCount,
        const DXGI_FORMAT format)
    {
        const int bpb = PS4BytesPerBlock(format);
        const int ppb = PS4PixelsPerBlock(format);
        if (bpb == 0)
            throw std::invalid_argument(
                "DeswizzlePS4: unsupported DXGI_FORMAT " + std::to_string(static_cast<int>(format)));
        return DeswizzlePS4(data, size, width, height, mipCount, textureCount, bpb, ppb);
    }

    std::vector<std::byte> SwizzlePS4(
        const std::byte* data, const std::size_t size,
        const int width, const int height,
        const int mipCount, const int textureCount,
        const DXGI_FORMAT format)
    {
        const int bpb = PS4BytesPerBlock(format);
        const int ppb = PS4PixelsPerBlock(format);
        if (bpb == 0)
            throw std::invalid_argument(
                "SwizzlePS4: unsupported DXGI_FORMAT " + std::to_string(static_cast<int>(format)));
        return SwizzlePS4(data, size, width, height, mipCount, textureCount, bpb, ppb);
    }


} // namespace Firelink


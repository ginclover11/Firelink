#pragma once

#include <FirelinkCore/Export.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _WIN32
#include <dxgiformat.h>    // DXGI_FORMAT enum
#else
#include <directx/dxgiformat.h>    // DXGI_FORMAT enum (DirectX-Headers package)
#endif

namespace Firelink
{
    /// @brief DDS_PIXELFORMAT.dwFlags bits (DDPF_*).
    enum DDSPixelFormatFlags : std::uint32_t
    {
        DDPF_ALPHAPIXELS = 0x1,
        DDPF_ALPHA       = 0x2,
        DDPF_FOURCC      = 0x4,
        DDPF_RGB         = 0x40,
        DDPF_YUV         = 0x200,
        DDPF_LUMINANCE   = 0x20000,
    };

    /// @brief Everything needed to synthesize a DDS header around raw pixel data.
    ///
    /// Console TPFs (PS3, Xbox 360, PS4, Xbox One) store their textures *headerless*:
    /// the TPF entry carries the dimensions and an internal format enum, and the stored
    /// bytes are the mip chain alone. DirectXTex cannot read such data, so a header has
    /// to be rebuilt from that metadata before conversion. See `DDS::FromHeaderlessData`.
    struct DDSHeaderParams
    {
        int width = 0;
        int height = 0;
        /// @brief Mip level count. Pass 0 to derive the full chain from the dimensions.
        /// @note Clamped down to the number of mips that actually fit in the pixel data.
        int mipCount = 0;
        bool isCubemap = false;
        bool isVolume = false;

        /// @brief FourCC code, e.g. "DXT1" or "DX10". All-null means "described by bit masks".
        std::array<char, 4> fourCC{};
        /// @brief DDPF_* bits. DDPF_FOURCC is added automatically when `fourCC` is set.
        std::uint32_t pixelFormatFlags = 0;
        int rgbBitCount = 0;
        std::uint32_t rBitMask = 0;
        std::uint32_t gBitMask = 0;
        std::uint32_t bBitMask = 0;
        std::uint32_t aBitMask = 0;

        /// @brief Bytes per BC block (compressed) or bytes per pixel (uncompressed).
        int bytesPerBlock = 0;
        /// @brief True for the BCn/DXTn family, whose mips are measured in 4x4 blocks.
        bool isCompressed = false;
        /// @brief Written into the DX10 header; required when `fourCC` is "DX10".
        DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN;
    };

    /// @brief DDS container class with conversion methods and de/swizzling.
    /// @note It is up to the user to track whether the DDS data is swizzled or not,
    ///       based on the source of the data (e.g. dumped PS4 files are swizzled).
    ///       Trying to export swizzled data or view it on PC will look garbled.
    class FIRELINK_CORE_API DDS
    {
    public:

        // --- Constructors ---

        /// @brief Default (empty) constructor.
        DDS() = default;

        /// @brief Construct DDS from stored bytes (must be moved in).
        explicit DDS(std::vector<std::byte>&& data)
            : m_storage{std::move(data)}
        {
        }

        /// @brief Construct DDS from raw data and size.
        explicit DDS(const std::byte* data, size_t size);

        /// @brief Convert TGA data to DDS with the given format.
        /// @note Block-compressed formats (BC1–BC7) are supported; the source pixels are
        /// automatically compressed. Uses TEX_COMPRESS_PARALLEL internally for speed.
        /// @param mipLevels Number of mip levels to generate: 1 (default) writes the base image only, 0 generates the
        ///        full chain, N > 1 generates N levels. Mirrors `texconv -m`.
        /// @param forceDX10 Always write the extended DX10 header (`texconv -dx10`).
        [[nodiscard]] static DDS FromTGA(
            const std::byte* data, size_t size, DXGI_FORMAT targetFormat,
            size_t mipLevels = 1, bool forceDX10 = false);
        /// @brief Convert PNG data to DDS with the given format.
        /// @note Block-compressed formats (BC1–BC7) are supported; the source pixels are
        /// automatically compressed. Uses TEX_COMPRESS_PARALLEL internally for speed.
        /// @param mipLevels See `FromTGA`.
        /// @param forceDX10 See `FromTGA`.
        [[nodiscard]] static DDS FromPNG(
            const std::byte* data, size_t size, DXGI_FORMAT targetFormat,
            size_t mipLevels = 1, bool forceDX10 = false);

        /// @brief Build a DDS by prepending a synthesized header to headerless pixel data.
        /// @note Used for console TPF textures, which store the mip chain with no DDS header.
        ///       The header's mip count is clamped to the number of mips that actually fit in
        ///       @p size, since console TPF entries routinely advertise a full chain they do
        ///       not contain (DirectXTex fails with ERROR_HANDLE_EOF otherwise).
        [[nodiscard]] static DDS FromHeaderlessData(
            const std::byte* data, size_t size, const DDSHeaderParams& params);

        /// @brief Get a view of the DDS bytes.
        [[nodiscard]] const std::vector<std::byte>& GetBytes() const noexcept { return m_storage; }
        /// @brief Set the DDS bytes (must be moved in).
        void SetBytes(std::vector<std::byte>&& data) { m_storage = std::move(data); }

        /// @brief Raw byte array access.
        [[nodiscard]] std::byte* GetData() noexcept { return m_storage.data(); }
        /// @brief Raw byte array size.
        [[nodiscard]] size_t GetSize() const noexcept { return m_storage.size(); }
        /// @brief Check if DDS bytes are empty.
        [[nodiscard]] bool IsEmpty() const noexcept { return m_storage.empty(); }

        // --- DDS -> Image -----------------------------------------------------------

        /// @brief Convert DDS to TGA data. Will be garbled if not deswizzled.
        [[nodiscard]] std::vector<std::byte> ToTGA() const;
        /// @brief Convert DDS to PNG data. Will be garbled if not deswizzled.
        [[nodiscard]] std::vector<std::byte> ToPNG() const;

        // --- PS4 GNF swizzle --------------------------------------------------------
        // FromSoftware PS4 DDS files store pixel data in the AMD GNF macro-tile layout
        // rather than the linear row-major layout that DirectXTex and GPU upload expect.
        //
        // Both functions keep the DDS header unchanged and only replace the pixel data
        // section.  The format and dimensions needed to drive the swizzle algorithm are
        // derived automatically from the DDS header.
        //
        // DeswizzlePS4: PS4 tiled DDS  → linear DDS  (use before converting to TGA/PNG)
        // SwizzlePS4:   linear DDS     → PS4 tiled DDS  (use when writing back to TPF)

        /// @brief Construct a deswizzled copy of this swizzled PS4 DDS.
        [[nodiscard]] DDS DeswizzlePS4() const;
        /// @brief Construct a swizzled PS4 copy of this non-swizzled DDS.
        [[nodiscard]] DDS SwizzlePS4() const;

    private:
        std::vector<std::byte> m_storage{};

    };

}
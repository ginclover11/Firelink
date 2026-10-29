#include <FirelinkCore/DDS.h>
#include <FirelinkCore/BinaryReadWrite.h>
#include <FirelinkCore/Logging.h>
#include <FirelinkCore/Swizzle.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <combaseapi.h>
#include <wincodec.h>      // GUID_ContainerFormatPng
#include <d3d11.h>
#include <wrl/client.h>    // Microsoft::WRL::ComPtr
#else
#include "PngCodec.h"      // stb-based PNG codec (no WIC outside Windows)
#endif

#include <DirectXTex.h>

namespace Firelink
{
    namespace
    {
#ifdef _WIN32
        /// Initialise COM once for the lifetime of the process.
        /// SaveToWICMemory (PNG output) and D3D11 device creation require COM.
        void EnsureCOMInitialised()
        {
            static std::once_flag flag;
            std::call_once(flag, []
            {
                if (const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                    FAILED(hr) && hr != RPC_E_CHANGED_MODE)
                {
                    throw std::runtime_error("ConvertDDS: CoInitializeEx failed");
                }
            });
        }

        /// Lazily create and cache a D3D11 device for GPU-accelerated
        /// BC6H/BC7 compression via DirectCompute.  Returns nullptr if
        /// no suitable GPU is available (caller falls back to CPU).
        ID3D11Device* GetD3D11Device()
        {
            static Microsoft::WRL::ComPtr<ID3D11Device> device;
            static std::once_flag flag;
            std::call_once(flag, []
            {
                static constexpr D3D_FEATURE_LEVEL featureLevels[] = {
                    D3D_FEATURE_LEVEL_11_0,
                    D3D_FEATURE_LEVEL_10_1,
                    D3D_FEATURE_LEVEL_10_0,
                };
                const HRESULT hr = D3D11CreateDevice(
                    nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                    featureLevels, static_cast<UINT>(std::size(featureLevels)),
                    D3D11_SDK_VERSION,
                    device.GetAddressOf(), nullptr, nullptr);
                if (FAILED(hr))
                    device.Reset();   // no GPU — will fall back to CPU
            });
            return device.Get();
        }
#endif

        /// Format an HRESULT as a hex string for error messages.
        std::string HResultToString(const HRESULT hr)
        {
            std::ostringstream ss;
            ss << "0x" << std::hex << static_cast<unsigned int>(hr);
            return ss.str();
        }

        /// Load DDS from memory, decompress/convert to B8G8R8A8_UNORM.
        /// Returns only the base mip level (mip 0, array index 0, slice 0).
        DirectX::ScratchImage LoadAndDecompressDDS(const std::byte* data, const size_t size)
        {
#ifdef _WIN32
            EnsureCOMInitialised();
#endif
            using namespace DirectX;

            // --- Load the DDS from the raw byte buffer ---
            TexMetadata metadata{};
            ScratchImage ddsImage;
            HRESULT hr = LoadFromDDSMemory(
                reinterpret_cast<const uint8_t*>(data), size,
                DDS_FLAGS_NONE, &metadata, ddsImage);
            if (FAILED(hr))
                throw std::runtime_error(
                    "ConvertDDS: LoadFromDDSMemory failed (" + HResultToString(hr) + ")");

            constexpr DXGI_FORMAT targetFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

            // Grab the base mip image (mip 0, item 0, slice 0).
            const Image* baseImage = ddsImage.GetImage(0, 0, 0);
            if (!baseImage)
                throw std::runtime_error("ConvertDDS: failed to get base mip image");

            // --- Decompress block-compressed formats (BC1–BC7) ---
            if (IsCompressed(metadata.format))
            {
                ScratchImage decompressed;
                hr = Decompress(*baseImage, DXGI_FORMAT_UNKNOWN, decompressed);
                if (FAILED(hr))
                    throw std::runtime_error(
                        "ConvertDDS: Decompress failed (" + HResultToString(hr) + ")");

                // If the decompressed format still isn't our target, convert.
                if (decompressed.GetMetadata().format != targetFormat)
                {
                    const Image* decompImg = decompressed.GetImage(0, 0, 0);
                    if (!decompImg)
                        throw std::runtime_error("ConvertDDS: failed to get decompressed image");

                    ScratchImage converted;
                    hr = Convert(*decompImg, targetFormat,
                                 TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, converted);
                    if (FAILED(hr))
                        throw std::runtime_error(
                            "ConvertDDS: Convert failed (" + HResultToString(hr) + ")");
                    return converted;
                }
                return decompressed;
            }

            // --- Non-compressed but wrong format — convert directly ---
            if (metadata.format != targetFormat)
            {
                ScratchImage converted;
                hr = Convert(*baseImage, targetFormat,
                             TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, converted);
                if (FAILED(hr))
                    throw std::runtime_error(
                        "ConvertDDS: Convert failed (" + HResultToString(hr) + ")");
                return converted;
            }

            // Already B8G8R8A8_UNORM — return as-is (move the whole ScratchImage).
            return ddsImage;
        }

        /// Copy a DirectXTex Blob into a std::vector<std::byte>.
        std::vector<std::byte> BlobToVector(const DirectX::Blob& blob)
        {
            const auto* p = reinterpret_cast<const std::byte*>(blob.GetBufferPointer());
            return {p, p + blob.GetBufferSize()};
        }

        // -----------------------------------------------------------------------
        // PS4 DDS header helpers
        // -----------------------------------------------------------------------

        // A standard DDS file is: 4-byte magic + 124-byte DDS_HEADER = 128 bytes,
        // optionally followed by a 20-byte DDS_HEADER_DXT10 when the pixel-format
        // fourCC is "DX10" (offset 84 from file start).

        constexpr std::size_t DDS_FOURCC_OFFSET    = 84;   // absolute offset of ddspf.dwFourCC
        constexpr uint32_t    DDS_FOURCC_DX10      = 0x30315844u; // "DX10"
        constexpr std::size_t DDS_BASE_HEADER_SIZE = 128;  // magic + DDS_HEADER
        constexpr std::size_t DDS_DXT10_SIZE       = 20;   // DDS_HEADER_DXT10

        /// Return the byte offset at which pixel data begins.
        std::size_t DDSPixelDataOffset(const std::byte* data, const std::size_t size)
        {
            if (size < DDS_FOURCC_OFFSET + 4)
                return DDS_BASE_HEADER_SIZE;
            uint32_t fourCC = 0;
            std::memcpy(&fourCC, data + DDS_FOURCC_OFFSET, 4);
            return (fourCC == DDS_FOURCC_DX10)
                ? DDS_BASE_HEADER_SIZE + DDS_DXT10_SIZE
                : DDS_BASE_HEADER_SIZE;
        }

        /// Parse DDS metadata (width, height, format, mip count, array/face count)
        /// without decoding pixel data.
        struct DDSMeta
        {
            int width;
            int height;
            int mipLevels;
            int textureCount;   // array slices × faces (6 per cubemap entry)
            DXGI_FORMAT format;
        };

        DDSMeta ParseDDSMeta(const std::byte* data, const std::size_t size, const char* callerName)
        {
            DirectX::TexMetadata tm{};
            const HRESULT hr = DirectX::GetMetadataFromDDSMemory(
                reinterpret_cast<const uint8_t*>(data), size,
                DirectX::DDS_FLAGS_NONE, tm);
            if (FAILED(hr))
                throw std::runtime_error(
                    std::string(callerName) + ": failed to parse DDS header ("
                    + HResultToString(hr) + ")");

            DDSMeta m{};
            m.width        = static_cast<int>(tm.width);
            m.height       = static_cast<int>(tm.height);
            m.mipLevels    = static_cast<int>(tm.mipLevels);
            // Cubemap: 6 faces per array element.
            m.textureCount = static_cast<int>(
                (tm.miscFlags & DirectX::TEX_MISC_TEXTURECUBE)
                    ? tm.arraySize * 6
                    : tm.arraySize);
            m.format = static_cast<DXGI_FORMAT>(tm.format);
            return m;
        }

        /// @brief Compute how many mip levels actually fit in @p pixelSize bytes.
        ///
        /// The DDS header's mipLevels field cannot always be trusted — PS4 game dumps
        /// often claim a full mip chain while the pixel data only contains the base
        /// level (or a subset of mips).  We determine the real count by summing mip
        /// sizes until we exhaust the buffer.
        ///
        /// @param isSwizzledInput  true  → measure swizzled (tile-padded) mip sizes.
        ///                         false → measure linear (row-major) mip sizes.
        int EffectiveMipCount(
            const DDSMeta& m, const int bpb, const int ppb,
            const std::size_t pixelSize, const bool isSwizzledInput) noexcept
        {
            // All faces share the same mip structure; total = textureCount × oneFaceTotal.
            // We accumulate one face's worth and multiply to check against pixelSize.
            std::size_t oneFaceConsumed = 0;
            int w = m.width, h = m.height;
            for (int mip = 0; mip < m.mipLevels; ++mip)
            {
                const int wb = std::max(1, (w + ppb - 1) / ppb);
                const int hb = std::max(1, (h + ppb - 1) / ppb);

                const std::size_t mipBytes = isSwizzledInput
                    ? static_cast<std::size_t>(((wb + 7) & ~7) * ((hb + 7) & ~7) * bpb)
                    : static_cast<std::size_t>(wb * hb * bpb);

                // Would adding this mip (for all faces) exceed the available data?
                if ((oneFaceConsumed + mipBytes)
                        * static_cast<std::size_t>(m.textureCount) > pixelSize)
                    return mip;   // mip = number of complete mips seen so far

                oneFaceConsumed += mipBytes;
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
            }
            return m.mipLevels;   // all mips fit
        }

        /// @brief Shared implementation for deswizzle / swizzle of a DDS pixel-data section.
        /// @p swizzleOp is either DeswizzlePS4 or SwizzlePS4.
        /// @p isSwizzledInput  true for DeswizzlePS4DDS (input is swizzled),
        ///                     false for SwizzlePS4DDS  (input is linear).
        template<typename Op>
        std::vector<std::byte> PS4SwizzleOpDDS(
            const std::byte* data, const std::size_t size,
            const char* callerName, Op swizzleOp,
            const bool isSwizzledInput)
        {
            if (!data || size < DDS_BASE_HEADER_SIZE)
                throw std::runtime_error(
                    std::string(callerName) + ": data too small to be a DDS");
            if (std::memcmp(data, "DDS ", 4) != 0)
                throw std::runtime_error(
                    std::string(callerName) + ": missing DDS magic");

            const DDSMeta m = ParseDDSMeta(data, size, callerName);

            const int bpb = PS4BytesPerBlock(m.format);
            const int ppb = PS4PixelsPerBlock(m.format);
            if (bpb == 0)
                throw std::invalid_argument(
                    std::string(callerName) + ": unsupported DXGI_FORMAT "
                    + std::to_string(static_cast<int>(m.format)));

            const std::size_t pixelOffset = DDSPixelDataOffset(data, size);
            if (size <= pixelOffset)
                throw std::runtime_error(
                    std::string(callerName) + ": no pixel data after DDS header");

            const std::byte* pixels     = data + pixelOffset;
            const std::size_t pixelSize = size - pixelOffset;

            // Use the pixel data size to determine how many mips are actually present.
            // The DDS header's mipLevels cannot be relied upon for PS4 game dumps.
            const int effectiveMips = EffectiveMipCount(m, bpb, ppb, pixelSize, isSwizzledInput);
            if (effectiveMips == 0)
                throw std::runtime_error(
                    std::string(callerName) + ": pixel data too small for one mip level");
            if (effectiveMips != m.mipLevels)
            {
                Warning(
                    std::string(callerName) + ": header claims " + std::to_string(m.mipLevels)
                    + " mip levels, but only " + std::to_string(effectiveMips)
                    + " fit in the pixel data — proceeding with " + std::to_string(effectiveMips));
            }

            auto converted = swizzleOp(pixels, pixelSize,
                                       m.width, m.height,
                                       effectiveMips, m.textureCount,
                                       bpb, ppb);

            // Output = original header bytes + converted pixel data.
            std::vector<std::byte> result;
            result.reserve(pixelOffset + converted.size());
            result.insert(result.end(), data, data + pixelOffset);
            result.insert(result.end(), converted.begin(), converted.end());

            // Patch DDS_HEADER.dwMipMapCount (little-endian uint32 at file offset 28)
            // if we processed fewer mips than the header originally advertised.
            // Without this, LoadFromDDSMemory will attempt to read mips that aren't
            // present and fail with ERROR_HANDLE_EOF (0x80070026).
            if (effectiveMips != m.mipLevels)
            {
                constexpr std::size_t MIP_COUNT_OFFSET = 28;
                const auto patched = static_cast<uint32_t>(effectiveMips);
                std::memcpy(result.data() + MIP_COUNT_OFFSET, &patched, sizeof(patched));
            }

            return result;
        }

        /// Compress or convert an uncompressed image to the target format and
        /// save as an in-memory DDS blob.  Uses GPU-accelerated DirectCompute
        /// for BC6H/BC7 when available, falling back to CPU + OpenMP.
        std::vector<std::byte> CompressAndSaveBaseImageDDS(
            const DirectX::ScratchImage& image, const DXGI_FORMAT targetFormat)
        {
            using namespace DirectX;

            const Image* img = image.GetImage(0, 0, 0);
            if (!img)
                throw std::runtime_error("CompressAndSaveDDS: failed to get source image");

            // Declared outside the branch so it outlives the conversion step;
            // `img` may be re-pointed at result.GetImage() below.
            ScratchImage result;
            if (const DXGI_FORMAT srcFormat = image.GetMetadata().format; srcFormat != targetFormat)
            {
                if (IsCompressed(targetFormat))
                {
                    HRESULT hr = E_FAIL;

#ifdef _WIN32
                    // BC6H / BC7 benefit enormously from GPU compression.
                    const bool isBC6or7 =
                        targetFormat == DXGI_FORMAT_BC6H_TYPELESS ||
                        targetFormat == DXGI_FORMAT_BC6H_UF16 ||
                        targetFormat == DXGI_FORMAT_BC6H_SF16 ||
                        targetFormat == DXGI_FORMAT_BC7_TYPELESS ||
                        targetFormat == DXGI_FORMAT_BC7_UNORM ||
                        targetFormat == DXGI_FORMAT_BC7_UNORM_SRGB;

                    if (isBC6or7)
                    {
                        if (ID3D11Device* dev = GetD3D11Device())
                        {
                            hr = Compress(dev, *img, targetFormat,
                                          TEX_COMPRESS_DEFAULT, 1.0f, result);
                        }
                    }
#endif
                    // CPU fallback (or non-BC6H/BC7 formats like BC1–BC5).
                    if (FAILED(hr))
                    {
                        hr = Compress(*img, targetFormat,
                                      TEX_COMPRESS_PARALLEL, TEX_THRESHOLD_DEFAULT, result);
                    }

                    if (FAILED(hr))
                        throw std::runtime_error(
                            "CompressAndSaveDDS: Compress failed (" + HResultToString(hr) + ")");
                }
                else
                {
                    const HRESULT hr = Convert(
                        *img, targetFormat,
                        TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, result);
                    if (FAILED(hr))
                        throw std::runtime_error(
                            "CompressAndSaveDDS: Convert failed (" + HResultToString(hr) + ")");
                }

                img = result.GetImage(0, 0, 0);
                if (!img)
                    throw std::runtime_error(
                        "CompressAndSaveDDS: failed to get converted image");
            }

            Blob blob;
            if (const HRESULT hr = SaveToDDSMemory(*img, DDS_FLAGS_NONE, blob); FAILED(hr))
                throw std::runtime_error(
                    "CompressAndSaveDDS: SaveToDDSMemory failed (" + HResultToString(hr) + ")");

            return BlobToVector(blob);
        }

        /// Like `CompressAndSaveBaseImageDDS`, but can also generate a mip chain and force the DX10 header. This mirrors
        /// the `texconv -m <levels>` and `texconv -dx10` options.
        ///
        /// With `mipLevels == 1` and `forceDX10 == false` this is exactly the base-image path (unchanged behaviour).
        /// `mipLevels == 0` generates the full chain. The BC6H/BC7 GPU path is not used when mips are requested; every
        /// level goes through DirectXTex's CPU encoder.
        std::vector<std::byte> CompressAndSaveDDS(
            const DirectX::ScratchImage& image, const DXGI_FORMAT targetFormat,
            const std::size_t mipLevels = 1, const bool forceDX10 = false)
        {
            if (mipLevels == 1 && !forceDX10)
                return CompressAndSaveBaseImageDDS(image, targetFormat);

            using namespace DirectX;

            const Image* base = image.GetImage(0, 0, 0);
            if (!base)
                throw std::runtime_error("CompressAndSaveDDS: failed to get source image");

            // 1. Build the mip chain from the (uncompressed) source image.
            ScratchImage chain;
            const ScratchImage* source = &image;
            if (mipLevels != 1)
            {
                if (const HRESULT hr = GenerateMipMaps(*base, TEX_FILTER_DEFAULT, mipLevels, chain); FAILED(hr))
                    throw std::runtime_error(
                        "CompressAndSaveDDS: GenerateMipMaps failed (" + HResultToString(hr) + ")");
                source = &chain;
            }

            // 2. Convert/compress every level to the target format.
            ScratchImage converted;
            const ScratchImage* output = source;
            if (source->GetMetadata().format != targetFormat)
            {
                const HRESULT hr = IsCompressed(targetFormat)
                    ? Compress(
                        source->GetImages(), source->GetImageCount(), source->GetMetadata(), targetFormat,
                        TEX_COMPRESS_PARALLEL, TEX_THRESHOLD_DEFAULT, converted)
                    : Convert(
                        source->GetImages(), source->GetImageCount(), source->GetMetadata(), targetFormat,
                        TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, converted);
                if (FAILED(hr))
                    throw std::runtime_error(
                        "CompressAndSaveDDS: " + std::string(IsCompressed(targetFormat) ? "Compress" : "Convert")
                        + " failed (" + HResultToString(hr) + ")");
                output = &converted;
            }

            // 3. Serialize, optionally forcing the extended DX10 header.
            Blob blob;
            if (const HRESULT hr = SaveToDDSMemory(
                    output->GetImages(), output->GetImageCount(), output->GetMetadata(),
                    forceDX10 ? DDS_FLAGS_FORCE_DX10_EXT : DDS_FLAGS_NONE, blob);
                FAILED(hr))
                throw std::runtime_error(
                    "CompressAndSaveDDS: SaveToDDSMemory failed (" + HResultToString(hr) + ")");

            return BlobToVector(blob);
        }

        // -----------------------------------------------------------------------
        // Headerless (console) DDS helpers
        // -----------------------------------------------------------------------

        /// @brief Size in bytes of one mip level of the given dimensions.
        std::size_t MipByteSize(
            const int width, const int height, const int bytesPerBlock, const bool isCompressed) noexcept
        {
            if (isCompressed)
            {
                const auto blocksW = static_cast<std::size_t>(std::max(1, (width + 3) / 4));
                const auto blocksH = static_cast<std::size_t>(std::max(1, (height + 3) / 4));
                return blocksW * blocksH * static_cast<std::size_t>(bytesPerBlock);
            }
            return static_cast<std::size_t>(std::max(1, width))
                 * static_cast<std::size_t>(std::max(1, height))
                 * static_cast<std::size_t>(bytesPerBlock);
        }

        /// @brief Number of mip levels in a complete chain down to 1x1.
        int FullMipChainLength(const int width, const int height) noexcept
        {
            int levels = 1;
            int w = width, h = height;
            while (w > 1 || h > 1)
            {
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
                ++levels;
            }
            return levels;
        }

        /// @brief How many of @p requestedMips actually fit in @p pixelSize bytes.
        /// @note All faces share the same mip structure, so one face's chain is
        ///       accumulated and multiplied by @p faceCount.
        int MipsThatFit(
            const DDSHeaderParams& p, const int requestedMips,
            const int faceCount, const std::size_t pixelSize) noexcept
        {
            std::size_t oneFaceConsumed = 0;
            int w = p.width, h = p.height;
            for (int mip = 0; mip < requestedMips; ++mip)
            {
                const std::size_t mipBytes = MipByteSize(w, h, p.bytesPerBlock, p.isCompressed);
                if ((oneFaceConsumed + mipBytes) * static_cast<std::size_t>(faceCount) > pixelSize)
                    return mip;
                oneFaceConsumed += mipBytes;
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
            }
            return requestedMips;
        }

        // DDS_HEADER.dwFlags (DDSD_*) bits.
        constexpr uint32_t DDSD_CAPS        = 0x1;
        constexpr uint32_t DDSD_HEIGHT      = 0x2;
        constexpr uint32_t DDSD_WIDTH       = 0x4;
        constexpr uint32_t DDSD_PITCH       = 0x8;
        constexpr uint32_t DDSD_PIXELFORMAT = 0x1000;
        constexpr uint32_t DDSD_MIPMAPCOUNT = 0x20000;
        constexpr uint32_t DDSD_LINEARSIZE  = 0x80000;

        // DDS_HEADER.dwCaps (DDSCAPS_*) bits.
        constexpr uint32_t DDSCAPS_COMPLEX = 0x8;
        constexpr uint32_t DDSCAPS_TEXTURE = 0x1000;
        constexpr uint32_t DDSCAPS_MIPMAP  = 0x400000;

        // DDS_HEADER.dwCaps2 (DDSCAPS2_*) bits.
        constexpr uint32_t DDSCAPS2_CUBEMAP           = 0x200;
        constexpr uint32_t DDSCAPS2_CUBEMAP_ALL_FACES = 0xFC00;  // +X -X +Y -Y +Z -Z
        constexpr uint32_t DDSCAPS2_VOLUME            = 0x200000;

        // DDS_HEADER_DXT10.resourceDimension / .miscFlag.
        constexpr uint32_t D3D10_RESOURCE_DIMENSION_TEXTURE2D = 3;
        constexpr uint32_t D3D10_RESOURCE_DIMENSION_TEXTURE3D = 4;
        constexpr uint32_t D3D10_RESOURCE_MISC_TEXTURECUBE    = 0x4;
    } // anonymous namespace

    DDS::DDS(const std::byte* data, const size_t size)
    {
        m_storage.resize(size);
        memcpy(m_storage.data(), data, size);
    }

    DDS DDS::FromHeaderlessData(
        const std::byte* data, const size_t size, const DDSHeaderParams& params)
    {
        using BinaryReadWrite::BufferWriter;
        using BinaryReadWrite::Endian;

        if (!data || size == 0)
            throw std::invalid_argument("DDS::FromHeaderlessData: no pixel data");
        if (params.width <= 0 || params.height <= 0)
            throw std::invalid_argument("DDS::FromHeaderlessData: width and height must be positive");
        if (params.bytesPerBlock <= 0)
            throw std::invalid_argument("DDS::FromHeaderlessData: bytesPerBlock must be positive");

        const bool isDX10 = std::memcmp(params.fourCC.data(), "DX10", 4) == 0;
        if (isDX10 && params.dxgiFormat == DXGI_FORMAT_UNKNOWN)
            throw std::invalid_argument(
                "DDS::FromHeaderlessData: a \"DX10\" fourCC requires a known dxgiFormat");

        const int faceCount = params.isCubemap ? 6 : 1;

        // A headerless console texture often advertises a full mip chain that it does not
        // actually store (and `mipCount == 0` means "derive the full chain"). Trust the
        // pixel data over the metadata: a header claiming absent mips makes DirectXTex
        // fail the entire load with ERROR_HANDLE_EOF.
        const int requestedMips = params.mipCount > 0
            ? params.mipCount
            : FullMipChainLength(params.width, params.height);
        const int mipCount = MipsThatFit(params, requestedMips, faceCount, size);
        if (mipCount < 1)
            throw std::runtime_error(
                "DDS::FromHeaderlessData: pixel data (" + std::to_string(size)
                + " bytes) is too small for even the base mip level of a "
                + std::to_string(params.width) + "x" + std::to_string(params.height)
                + " texture");
        if (mipCount != requestedMips)
        {
            Warning(
                "DDS::FromHeaderlessData: metadata implies " + std::to_string(requestedMips)
                + " mip levels, but only " + std::to_string(mipCount)
                + " fit in the pixel data — proceeding with " + std::to_string(mipCount));
        }

        uint32_t flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT;
        uint32_t pitchOrLinearSize;
        if (params.isCompressed)
        {
            flags |= DDSD_LINEARSIZE;
            pitchOrLinearSize = static_cast<uint32_t>(
                MipByteSize(params.width, params.height, params.bytesPerBlock, true));
        }
        else
        {
            flags |= DDSD_PITCH;
            pitchOrLinearSize =
                static_cast<uint32_t>(params.width) * static_cast<uint32_t>(params.bytesPerBlock);
        }

        uint32_t caps1 = DDSCAPS_TEXTURE;
        if (mipCount > 1)
            caps1 |= DDSCAPS_COMPLEX | DDSCAPS_MIPMAP;
        if (params.isCubemap)
            caps1 |= DDSCAPS_COMPLEX;

        uint32_t caps2 = 0;
        if (params.isCubemap)
            caps2 = DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_ALL_FACES;
        else if (params.isVolume)
            caps2 = DDSCAPS2_VOLUME;

        uint32_t pixelFormatFlags = params.pixelFormatFlags;
        if (params.fourCC != std::array<char, 4>{})
            pixelFormatFlags |= DDPF_FOURCC;

        // DDS files are always little-endian, whatever console the pixels came from.
        BufferWriter w(Endian::Little);
        w.ReserveCapacity(DDS_BASE_HEADER_SIZE + DDS_DXT10_SIZE + size);

        w.WriteRaw("DDS ", 4);
        w.Write<uint32_t>(124);                                   // DDS_HEADER.dwSize
        w.Write<uint32_t>(flags);
        w.Write<uint32_t>(static_cast<uint32_t>(params.height));
        w.Write<uint32_t>(static_cast<uint32_t>(params.width));
        w.Write<uint32_t>(pitchOrLinearSize);
        w.Write<uint32_t>(0);                                     // dwDepth (unused here)
        w.Write<uint32_t>(static_cast<uint32_t>(mipCount));
        w.WritePad(11 * 4);                                       // dwReserved1[11]

        // DDS_PIXELFORMAT (32 bytes).
        w.Write<uint32_t>(32);                                    // dwSize
        w.Write<uint32_t>(pixelFormatFlags);
        w.WriteRaw(params.fourCC.data(), 4);
        w.Write<uint32_t>(static_cast<uint32_t>(params.rgbBitCount));
        w.Write<uint32_t>(params.rBitMask);
        w.Write<uint32_t>(params.gBitMask);
        w.Write<uint32_t>(params.bBitMask);
        w.Write<uint32_t>(params.aBitMask);

        w.Write<uint32_t>(caps1);
        w.Write<uint32_t>(caps2);
        w.Write<uint32_t>(0);                                     // dwCaps3
        w.Write<uint32_t>(0);                                     // dwCaps4
        w.Write<uint32_t>(0);                                     // dwReserved2

        if (isDX10)
        {
            w.Write<uint32_t>(static_cast<uint32_t>(params.dxgiFormat));
            w.Write<uint32_t>(params.isVolume
                ? D3D10_RESOURCE_DIMENSION_TEXTURE3D
                : D3D10_RESOURCE_DIMENSION_TEXTURE2D);
            w.Write<uint32_t>(params.isCubemap ? D3D10_RESOURCE_MISC_TEXTURECUBE : 0u);
            w.Write<uint32_t>(1);                                 // arraySize (cube faces are implicit)
            w.Write<uint32_t>(0);                                 // miscFlags2 (ALPHA_MODE_UNKNOWN)
        }

        w.WriteRaw(data, size);
        return DDS(w.Finalize());
    }

    std::vector<std::byte> DDS::ToTGA() const
    {
        using namespace DirectX;

        const ScratchImage image = LoadAndDecompressDDS(m_storage.data(), m_storage.size());

        const Image* img = image.GetImage(0, 0, 0);
        if (!img)
            throw std::runtime_error("ConvertDDSToTGA: failed to get image for save");

        Blob blob;
        if (const HRESULT hr = SaveToTGAMemory(*img, TGA_FLAGS_NONE, blob); FAILED(hr))
            throw std::runtime_error(
                "ConvertDDSToTGA: SaveToTGAMemory failed (" + HResultToString(hr) + ")");

        return BlobToVector(blob);
    }

    std::vector<std::byte> DDS::ToPNG() const
    {
        using namespace DirectX;

        const ScratchImage image = LoadAndDecompressDDS(m_storage.data(), m_storage.size());

        const Image* img = image.GetImage(0, 0, 0);
        if (!img)
            throw std::runtime_error("ConvertDDSToPNG: failed to get image for save");

#ifdef _WIN32
        Blob blob;
        const HRESULT hr = SaveToWICMemory(*img, WIC_FLAGS_NONE,
                                           GUID_ContainerFormatPng, blob);
        if (FAILED(hr))
            throw std::runtime_error(
                "ConvertDDSToPNG: SaveToWICMemory failed (" + HResultToString(hr) + ")");

        return BlobToVector(blob);
#else
        // No WIC: swizzle B8G8R8A8 -> R8G8B8A8 with DirectXTex, then encode with stb_image_write.
        ScratchImage rgba;
        if (const HRESULT hr = Convert(
                *img, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, rgba);
            FAILED(hr))
            throw std::runtime_error("ConvertDDSToPNG: Convert failed (" + HResultToString(hr) + ")");

        const Image* out = rgba.GetImage(0, 0, 0);
        if (!out)
            throw std::runtime_error("ConvertDDSToPNG: failed to get converted image");

        return PngCodec::EncodeRGBA(out->pixels, static_cast<int>(out->width), static_cast<int>(out->height),
                                    out->rowPitch);
#endif
    }

    DDS DDS::FromTGA(
        const std::byte* data, const size_t size, const DXGI_FORMAT targetFormat,
        const size_t mipLevels, const bool forceDX10)
    {
        using namespace DirectX;

        TexMetadata metadata{};
        ScratchImage image;
        const HRESULT hr = LoadFromTGAMemory(
            reinterpret_cast<const uint8_t*>(data), size,
            TGA_FLAGS_NONE, &metadata, image);
        if (FAILED(hr))
            throw std::runtime_error(
                "ConvertTGAToDDS: LoadFromTGAMemory failed (" + HResultToString(hr) + ")");

        return DDS(CompressAndSaveDDS(image, targetFormat, mipLevels, forceDX10));
    }

    DDS DDS::FromPNG(
        const std::byte* data, const size_t size, const DXGI_FORMAT targetFormat,
        const size_t mipLevels, const bool forceDX10)
    {
        using namespace DirectX;
        ScratchImage image;

#ifdef _WIN32
        EnsureCOMInitialised();

        TexMetadata metadata{};
        const HRESULT hr = LoadFromWICMemory(
            reinterpret_cast<const uint8_t*>(data), size,
            WIC_FLAGS_NONE, &metadata, image);
        if (FAILED(hr))
            throw std::runtime_error(
                "ConvertPNGToDDS: LoadFromWICMemory failed (" + HResultToString(hr) + ")");
#else
        // No WIC: decode with stb_image into an R8G8B8A8_UNORM ScratchImage.
        const PngCodec::DecodedImage decoded = PngCodec::DecodeToRGBA(reinterpret_cast<const uint8_t*>(data), size);
        if (const HRESULT hr = image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, decoded.width, decoded.height, 1, 1);
            FAILED(hr))
            throw std::runtime_error(
                "ConvertPNGToDDS: ScratchImage::Initialize2D failed (" + HResultToString(hr) + ")");

        const Image* dest = image.GetImage(0, 0, 0);
        if (!dest)
            throw std::runtime_error("ConvertPNGToDDS: failed to get destination image");
        const std::size_t srcPitch = static_cast<std::size_t>(decoded.width) * 4;
        for (int y = 0; y < decoded.height; ++y)
            std::memcpy(dest->pixels + static_cast<std::size_t>(y) * dest->rowPitch,
                        decoded.rgba.data() + static_cast<std::size_t>(y) * srcPitch, srcPitch);
#endif

        return DDS(CompressAndSaveDDS(image, targetFormat, mipLevels, forceDX10));
    }

    // --- PS4 GNF swizzle --------------------------------------------------------

    DDS DDS::DeswizzlePS4() const
    {
        auto deswizzledData = PS4SwizzleOpDDS(m_storage.data(), m_storage.size(), "DeswizzlePS4",
            [](const std::byte* px, const std::size_t sz,
               const int w, const int h, const int mips, const int tc, const int bpb, const int ppb)
            {
                return Firelink::DeswizzlePS4(px, sz, w, h, mips, tc, bpb, ppb);
            },
            /*isSwizzledInput=*/true);

        return DDS(std::move(deswizzledData));
    }

    DDS DDS::SwizzlePS4() const
    {
        auto swizzledData = PS4SwizzleOpDDS(m_storage.data(), m_storage.size(), "SwizzlePS4DDS",
            [](const std::byte* px, const std::size_t sz,
               const int w, const int h, const int mips, const int tc, const int bpb, const int ppb)
            {
                return Firelink::SwizzlePS4(px, sz, w, h, mips, tc, bpb, ppb);
            },
            /*isSwizzledInput=*/false);

        return DDS(std::move(swizzledData));
    }
} // namespace Firelink

#include "PngCodec.h"

#ifndef _WIN32

#include <zlib.h>

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

// --- stb configuration -----------------------------------------------------------------------------------------------
// Decode PNG only, no stdio, no SIMD/threads surprises. Encode via a callback so we write straight into a std::vector.

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION

#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION

// Use zlib (already linked for DCX) for the PNG IDAT stream: stb's built-in deflate is fast but compresses poorly.
// stb_image_write expects a malloc'd buffer back and frees it with STBIW_FREE (plain `free`).
#define STBIW_ZLIB_COMPRESS Firelink_StbZlibCompress

static unsigned char* Firelink_StbZlibCompress(unsigned char* data, int dataLen, int* outLen, int quality)
{
    uLongf bound = compressBound(static_cast<uLong>(dataLen));
    auto* out = static_cast<unsigned char*>(std::malloc(bound));
    if (!out)
        return nullptr;
    const int level = quality < 1 ? 1 : (quality > 9 ? 9 : quality);
    if (compress2(out, &bound, data, static_cast<uLong>(dataLen), level) != Z_OK)
    {
        std::free(out);
        return nullptr;
    }
    *outLen = static_cast<int>(bound);
    return out;
}

// GCC warns about harmless things inside the single-header libraries; silence them for these includes only.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <stb_image.h>
#include <stb_image_write.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace Firelink::PngCodec
{
    DecodedImage DecodeToRGBA(const std::uint8_t* data, const std::size_t size)
    {
        int width = 0, height = 0, channelsInFile = 0;
        // Force 4 channels: alpha is filled with 255 when the PNG has none.
        unsigned char* pixels = stbi_load_from_memory(
            data, static_cast<int>(size), &width, &height, &channelsInFile, 4);
        if (!pixels)
        {
            const char* reason = stbi_failure_reason();
            throw std::runtime_error(std::string("PNG decode failed: ") + (reason ? reason : "unknown error"));
        }

        DecodedImage image;
        image.width = width;
        image.height = height;
        image.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
        stbi_image_free(pixels);
        return image;
    }

    std::vector<std::byte> EncodeRGBA(
        const std::uint8_t* rgba, const int width, const int height, const std::size_t rowPitch)
    {
        std::vector<std::byte> out;
        const auto writeCallback = [](void* context, void* chunk, const int chunkSize)
        {
            auto* dest = static_cast<std::vector<std::byte>*>(context);
            const auto* bytes = static_cast<const std::byte*>(chunk);
            dest->insert(dest->end(), bytes, bytes + chunkSize);
        };

        stbi_write_png_compression_level = 6;  // balance of size and speed (passed to our zlib hook as `quality`)
        const int ok = stbi_write_png_to_func(
            writeCallback, &out, width, height, 4, rgba, static_cast<int>(rowPitch));
        if (!ok || out.empty())
            throw std::runtime_error("PNG encode failed");
        return out;
    }
} // namespace Firelink::PngCodec

#endif // !_WIN32

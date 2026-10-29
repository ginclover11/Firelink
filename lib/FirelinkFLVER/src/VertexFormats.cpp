// Vertex format table + codec implementations.
//
// This is the authoritative table mapping (VertexUsage, VertexDataFormatEnum)
// pairs to their compressed/decompressed layouts and codecs.

#include <FirelinkFLVER/VertexFormats.h>

#include <FirelinkFLVER/Exceptions.h>

#include <algorithm>
#include <cmath>
#include <cstring>


namespace Firelink
{
    // ---------------------------------------------------------------------------
    // Format table
    // ---------------------------------------------------------------------------

    namespace
    {
        struct TableEntry
        {
            VertexUsage usage;
            VertexDataFormatEnum format;
            VertexFormatInfo info;
        };

        // Helper: make a single-field VertexFormatInfo.
        constexpr VertexFormatInfo single(
            const char* name,
            const VertexScalarType cs, const std::uint8_t cc,
            const VertexScalarType ds, const std::uint8_t dc,
            const VertexCodec codec)
        {
            VertexFormatInfo fi{};
            fi.field_count = 1;
            fi.fields[0] = {name, cs, cc, ds, dc, codec};
            return fi;
        }

        // Helper: make a two-field VertexFormatInfo.
        constexpr VertexFormatInfo dual(
            const char* n0, const VertexScalarType cs0, const std::uint8_t cc0, const VertexScalarType ds0, const std::uint8_t dc0,
            const VertexCodec codec0,
            const char* n1, const VertexScalarType cs1, const std::uint8_t cc1, const VertexScalarType ds1, const std::uint8_t dc1,
            const VertexCodec codec1)
        {
            VertexFormatInfo fi{};
            fi.field_count = 2;
            fi.fields[0] = {n0, cs0, cc0, ds0, dc0, codec0};
            fi.fields[1] = {n1, cs1, cc1, ds1, dc1, codec1};
            return fi;
        }

        using S = VertexScalarType;
        using C = VertexCodec;
        using Fmt = VertexDataFormatEnum;

        // The full table, verified against Python vertex_array_layout.py.
        // Entries sharing the same spec across multiple format enum values are listed
        // individually (the table is small enough that this is fine for clarity).

        constexpr TableEntry kTable[] = {

            // =========================================================================
            // Position (type_int=0)
            // =========================================================================
            {VertexUsage::Position, Fmt::ThreeFloats, single("position", S::F32, 3, S::F32, 3, C::Identity)},
            {VertexUsage::Position, Fmt::FourFloats, single("position", S::F32, 4, S::F32, 4, C::Identity)},

            // =========================================================================
            // BoneWeights (type_int=1)
            // =========================================================================
            // 0x10: 4 signed bytes / 127.0
            {VertexUsage::BoneWeights, Fmt::FourBytesA, single("bone_weights", S::S8, 4, S::F32, 4, C::IntTo127Float)},
            // 0x13: 4 unsigned bytes / 255.0
            {VertexUsage::BoneWeights, Fmt::FourBytesC, single("bone_weights", S::U8, 4, S::F32, 4, C::IntTo255Float)},
            // 0x16: 4 shorts / 32767.0
            {
                VertexUsage::BoneWeights, Fmt::FourShorts,
                single("bone_weights", S::S16, 4, S::F32, 4, C::IntTo32767Float)
            },
            // 0x1A: 4 shorts / 32767.0
            {
                VertexUsage::BoneWeights, Fmt::FourShortsToFloats,
                single("bone_weights", S::S16, 4, S::F32, 4, C::IntTo32767Float)
            },

            // =========================================================================
            // BoneIndices (type_int=2)
            // =========================================================================
            // 0x11: 4 unsigned bytes -> 4 int32 (identity widen)
            {VertexUsage::BoneIndices, Fmt::FourBytesB, single("bone_indices", S::U8, 4, S::S32, 4, C::IdentityWiden)},
            // 0x2F: same
            {VertexUsage::BoneIndices, Fmt::FourBytesE, single("bone_indices", S::U8, 4, S::S32, 4, C::IdentityWiden)},
            // 0x18: 4 shorts -> 4 int32 (identity widen)
            {
                VertexUsage::BoneIndices, Fmt::FourShortsBones,
                single("bone_indices", S::S16, 4, S::S32, 4, C::IdentityWiden)
            },

            // =========================================================================
            // Normal (type_int=3) — multi-field entries
            // =========================================================================
            // 0x03: 3 floats normal only, no normal_w
            {VertexUsage::Normal, Fmt::ThreeFloats, single("normal", S::F32, 3, S::F32, 3, C::Identity)},

            // 0x10, 0x11, 0x13, 0x2F: 3 unsigned bytes (normal) + 1 unsigned byte (normal_w)
            //   normal: (val - 127) / 127.0
            //   normal_w: identity (kept as U8)
            {
                VertexUsage::Normal, Fmt::FourBytesA, dual(
                    "normal", S::U8, 3, S::F32, 3, C::SignedIntTo127Float,
                    "normal_w", S::U8, 1, S::U8, 1, C::Identity)
            },
            {
                VertexUsage::Normal, Fmt::FourBytesB, dual(
                    "normal", S::U8, 3, S::F32, 3, C::SignedIntTo127Float,
                    "normal_w", S::U8, 1, S::U8, 1, C::Identity)
            },
            {
                VertexUsage::Normal, Fmt::FourBytesC, dual(
                    "normal", S::U8, 3, S::F32, 3, C::SignedIntTo127Float,
                    "normal_w", S::U8, 1, S::U8, 1, C::Identity)
            },
            {
                VertexUsage::Normal, Fmt::FourBytesE, dual(
                    "normal", S::U8, 3, S::F32, 3, C::SignedIntTo127Float,
                    "normal_w", S::U8, 1, S::U8, 1, C::Identity)
            },

            // 0x12 (NormalWFirst): 1 unsigned byte (normal_w) + 3 signed bytes (normal)
            //   normal_w is FIRST on disk, normal second. Codec on normal is (val / 127.0).
            // TODO: Double check this one is correct.
            {
                VertexUsage::Normal, Fmt::FourBytesD_NormalW, dual(
                    "normal_w", S::U8, 1, S::U8, 1, C::Identity,
                    "normal", S::S8, 3, S::F32, 3, C::SignedIntTo127Float)
            },

            // 0x1A: 3 signed shorts (normal) + 1 signed short (normal_w)
            //   normal: (val - 32767) / 32767.0   (SignedIntTo32767Float)
            //   normal_w: identity (kept as S16)
            {
                VertexUsage::Normal, Fmt::FourShortsToFloats, dual(
                    "normal", S::S16, 3, S::F32, 3, C::SignedIntTo32767Float,
                    "normal_w", S::S16, 1, S::S16, 1, C::Identity)
            },

            // 0x2E: 3 unsigned shorts (normal) + 1 signed short (normal_w)
            //   normal: (val - 32767) / 32767.0   (SignedIntTo32767Float)
            //   normal_w: identity (kept as S16)
            {
                VertexUsage::Normal, Fmt::FourShortsToFloatsB, dual(
                    "normal", S::U16, 3, S::F32, 3, C::SignedIntTo32767Float,
                    "normal_w", S::S16, 1, S::S16, 1, C::Identity)
            },

            // =========================================================================
            // UV (type_int=5) — codec depends on whether float or compressed
            // =========================================================================
            // 0x01: 2 floats (no uv_factor)
            {VertexUsage::UV, Fmt::TwoFloats, single("uv", S::F32, 2, S::F32, 2, C::Identity)},
            // 0x02: 3 floats (no uv_factor) — rare
            {VertexUsage::UV, Fmt::ThreeFloats, single("uv", S::F32, 3, S::F32, 3, C::Identity)},
            // 0x03: 4 floats = 2 UV pairs, each 2 floats. We treat as single 4-float block.
            {VertexUsage::UV, Fmt::FourFloats, single("uv", S::F32, 4, S::F32, 4, C::Identity)},

            // 0x10, 0x11, 0x12, 0x13, 0x15: 2 shorts -> 2 floats via uv_factor
            {VertexUsage::UV, Fmt::FourBytesA, single("uv", S::S16, 2, S::F32, 2, C::UvFactor)},
            {VertexUsage::UV, Fmt::FourBytesB, single("uv", S::S16, 2, S::F32, 2, C::UvFactor)},
            {VertexUsage::UV, Fmt::FourBytesD_NormalW, single("uv", S::S16, 2, S::F32, 2, C::UvFactor)},
            {VertexUsage::UV, Fmt::FourBytesC, single("uv", S::S16, 2, S::F32, 2, C::UvFactor)},
            {VertexUsage::UV, Fmt::TwoShorts, single("uv", S::S16, 2, S::F32, 2, C::UvFactor)},

            // 0x16: 4 shorts = UV pair -> 4 floats via uv_factor
            {VertexUsage::UV, Fmt::FourShorts, single("uv", S::S16, 4, S::F32, 4, C::UvFactor)},

            // 0x1A, 0x2E: 4 shorts -> 4 floats via uv_factor (last 2 are unknown/zero)
            {VertexUsage::UV, Fmt::FourShortsToFloats, single("uv", S::S16, 4, S::F32, 4, C::UvFactor)},
            {VertexUsage::UV, Fmt::FourShortsToFloatsB, single("uv", S::S16, 4, S::F32, 4, C::UvFactor)},

            // =========================================================================
            // Tangent (type_int=6)
            // =========================================================================
            // 0x03: 3 floats
            {VertexUsage::Tangent, Fmt::FourFloats, single("tangent", S::F32, 3, S::F32, 3, C::Identity)},
            // 0x10, 0x11, 0x13, 0x1A, 0x2F: 4 unsigned bytes, (val - 127) / 127.0 -> 4 floats
            {VertexUsage::Tangent, Fmt::FourBytesA, single("tangent", S::U8, 4, S::F32, 4, C::SignedIntTo127Float)},
            {VertexUsage::Tangent, Fmt::FourBytesB, single("tangent", S::U8, 4, S::F32, 4, C::SignedIntTo127Float)},
            {VertexUsage::Tangent, Fmt::FourBytesC, single("tangent", S::U8, 4, S::F32, 4, C::SignedIntTo127Float)},
            {
                VertexUsage::Tangent, Fmt::FourShortsToFloats,
                single("tangent", S::U8, 4, S::F32, 4, C::SignedIntTo127Float)
            },
            {VertexUsage::Tangent, Fmt::FourBytesE, single("tangent", S::U8, 4, S::F32, 4, C::SignedIntTo127Float)},

            // =========================================================================
            // Bitangent (type_int=7)
            // =========================================================================
            // 0x10, 0x11, 0x13, 0x2F: 4 unsigned bytes -> 4 floats via (val-127)/127
            {VertexUsage::Bitangent, Fmt::FourBytesA, single("bitangent", S::U8, 4, S::F32, 4, C::SignedIntTo127Float)},
            {VertexUsage::Bitangent, Fmt::FourBytesB, single("bitangent", S::U8, 4, S::F32, 4, C::SignedIntTo127Float)},
            {VertexUsage::Bitangent, Fmt::FourBytesC, single("bitangent", S::U8, 4, S::F32, 4, C::SignedIntTo127Float)},
            {VertexUsage::Bitangent, Fmt::FourBytesE, single("bitangent", S::U8, 4, S::F32, 4, C::SignedIntTo127Float)},

            // =========================================================================
            // Color (type_int=10)
            // =========================================================================
            // 0x03: 4 floats
            {VertexUsage::Color, Fmt::FourFloats, single("color", S::F32, 4, S::F32, 4, C::Identity)},
            // 0x10: 4 unsigned bytes / 255.0 -> 4 floats
            {VertexUsage::Color, Fmt::FourBytesA, single("color", S::U8, 4, S::F32, 4, C::IntTo255Float)},
            // 0x13: same
            {VertexUsage::Color, Fmt::FourBytesC, single("color", S::U8, 4, S::F32, 4, C::IntTo255Float)},
        };
    } // namespace

    std::optional<VertexFormatInfo>
    FindVertexFormatInfo(const VertexUsage usage, const VertexDataFormatEnum format) noexcept
    {
        for (const auto& tableEntry : kTable)
        {
            if (tableEntry.usage == usage && tableEntry.format == format)
            {
                return tableEntry.info;
            }
        }
        return std::nullopt;
    }

    std::uint32_t FormatEnumSize(const VertexDataFormatEnum format) noexcept
    {
        switch (format)
        {
        case Fmt::TwoFloats: return 8;
        case Fmt::ThreeFloats: return 12;
        case Fmt::FourFloats: return 16;
        case Fmt::FourBytesA: return 4;
        case Fmt::FourBytesB: return 4;
        case Fmt::FourBytesD_NormalW: return 8; // 0x12: 4 bytes + 4 bytes (8 total? wait...)
        case Fmt::FourBytesC: return 4;
        case Fmt::TwoShorts: return 4;
        case Fmt::FourShorts: return 8;
        case Fmt::FourShortsBones: return 8;
        case Fmt::FourShortsToFloats: return 8;
        case Fmt::FourShortsToFloatsB: return 8;
        case Fmt::FourBytesE: return 4;
        case Fmt::EdgeCompressed: return 1; // per Python; not supported
        case Fmt::Ignored: return 0;
        }
        return 0;
    }

    // ---------------------------------------------------------------------------
    // Decompress / compress primitives
    // ---------------------------------------------------------------------------

    namespace
    {
        // ---------------------------------------------------------------------------
        // Byte-swap helpers
        // ---------------------------------------------------------------------------

        template <typename T>
        T byte_swap(T val) noexcept
        {
            static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
                          "byte_swap: unsupported size");
            if constexpr (sizeof(T) == 1)
            {
                return val;
            }
            else if constexpr (sizeof(T) == 2)
            {
                std::uint16_t u;
                std::memcpy(&u, &val, 2);
                u = static_cast<std::uint16_t>((u >> 8) | (u << 8));
                T r;
                std::memcpy(&r, &u, 2);
                return r;
            }
            else if constexpr (sizeof(T) == 4)
            {
                std::uint32_t u;
                std::memcpy(&u, &val, 4);
                u = ((u & 0xFF000000u) >> 24) | ((u & 0x00FF0000u) >> 8)
                  | ((u & 0x0000FF00u) << 8)  | ((u & 0x000000FFu) << 24);
                T r;
                std::memcpy(&r, &u, 4);
                return r;
            }
            else // sizeof(T) == 8
            {
                std::uint64_t u;
                std::memcpy(&u, &val, 8);
                u = ((u & 0xFF00000000000000ull) >> 56) | ((u & 0x00FF000000000000ull) >> 40)
                  | ((u & 0x0000FF0000000000ull) >> 24) | ((u & 0x000000FF00000000ull) >>  8)
                  | ((u & 0x00000000FF000000ull) <<  8) | ((u & 0x0000000000FF0000ull) << 24)
                  | ((u & 0x000000000000FF00ull) << 40) | ((u & 0x00000000000000FFull) << 56);
                T r;
                std::memcpy(&r, &u, 8);
                return r;
            }
        }

        // Read one scalar from compressed data, swapping bytes when big-endian.
        template <typename T>
        T read_scalar(const std::byte* p, const BinaryReadWrite::Endian endian)
        {
            T val;
            std::memcpy(&val, p, sizeof(T));
            if (endian == BinaryReadWrite::Endian::Big)
                val = byte_swap(val);
            return val;
        }

        // Write one scalar to compressed data, swapping bytes when big-endian.
        template <typename T>
        void write_scalar(std::byte* p, T val, const BinaryReadWrite::Endian endian)
        {
            if (endian == BinaryReadWrite::Endian::Big)
                val = byte_swap(val);
            std::memcpy(p, &val, sizeof(T));
        }

        // Generic per-vertex, per-component decompress loop.
        template <typename SrcT, typename DstT, typename Func>
        void decompress_loop(
            const std::byte* src, std::byte* dst,
            const std::size_t vertex_count,
            const std::size_t compressed_stride, const std::size_t compressed_offset,
            const std::uint8_t component_count, Func fn,
            const BinaryReadWrite::Endian endian)
        {
            const std::size_t src_elem = sizeof(SrcT);
            const std::size_t dst_elem = sizeof(DstT);
            for (std::size_t v = 0; v < vertex_count; ++v)
            {
                const std::byte* sp = src + v * compressed_stride + compressed_offset;
                std::byte* dp = dst + v * (component_count * dst_elem);
                for (std::uint8_t c = 0; c < component_count; ++c)
                {
                    SrcT sv = read_scalar<SrcT>(sp + c * src_elem, endian);
                    DstT dv = fn(sv);
                    // Decompressed output is always native (little-endian).
                    write_scalar<DstT>(dp + c * dst_elem, dv, BinaryReadWrite::Endian::Little);
                }
            }
        }

        // Generic per-vertex, per-component compress loop (inverse direction).
        template <typename SrcT, typename DstT, typename Func>
        void compress_loop(
            const std::byte* src, std::byte* dst,
            const std::size_t vertex_count,
            const std::size_t compressed_stride, const std::size_t compressed_offset,
            const std::uint8_t component_count, Func fn,
            const BinaryReadWrite::Endian endian)
        {
            const std::size_t src_elem = sizeof(SrcT);
            const std::size_t dst_elem = sizeof(DstT);
            for (std::size_t v = 0; v < vertex_count; ++v)
            {
                // Source (decompressed) data is always native (little-endian).
                const std::byte* sp = src + v * (component_count * src_elem);
                std::byte* dp = dst + v * compressed_stride + compressed_offset;
                for (std::uint8_t c = 0; c < component_count; ++c)
                {
                    SrcT sv = read_scalar<SrcT>(sp + c * src_elem, BinaryReadWrite::Endian::Little);
                    DstT dv = fn(sv);
                    write_scalar<DstT>(dp + c * dst_elem, dv, endian);
                }
            }
        }

        // Identity copy (same-size src/dst), with per-element byte-swap when big-endian.
        void identity_decompress(
            const VertexFieldSpec& spec,
            const std::byte* src, std::byte* dst,
            const std::size_t vertex_count,
            const std::size_t compressed_stride, const std::size_t compressed_offset,
            const BinaryReadWrite::Endian endian)
        {
            const std::size_t total_bytes = spec.GetCompressedSize();
            const std::size_t elem_size = (spec.compressed_count > 0)
                                              ? total_bytes / spec.compressed_count
                                              : total_bytes;
            for (std::size_t v = 0; v < vertex_count; ++v)
            {
                const std::byte* s = src + v * compressed_stride + compressed_offset;
                std::byte* d = dst + v * total_bytes;
                if (endian == BinaryReadWrite::Endian::Big && elem_size > 1)
                {
                    for (std::size_t i = 0; i < total_bytes; i += elem_size)
                    {
                        std::memcpy(d + i, s + i, elem_size);
                        std::reverse(
                            reinterpret_cast<std::uint8_t*>(d + i),
                            reinterpret_cast<std::uint8_t*>(d + i) + elem_size);
                    }
                }
                else
                {
                    std::memcpy(d, s, total_bytes);
                }
            }
        }

        void identity_compress(
            const VertexFieldSpec& spec,
            const std::byte* src, std::byte* dst,
            const std::size_t vertex_count,
            const std::size_t compressed_stride, const std::size_t compressed_offset,
            const BinaryReadWrite::Endian endian)
        {
            const std::size_t total_bytes = spec.GetCompressedSize();
            const std::size_t elem_size = (spec.compressed_count > 0)
                                              ? total_bytes / spec.compressed_count
                                              : total_bytes;
            for (std::size_t v = 0; v < vertex_count; ++v)
            {
                const std::byte* s = src + v * total_bytes;
                std::byte* d = dst + v * compressed_stride + compressed_offset;
                if (endian == BinaryReadWrite::Endian::Big && elem_size > 1)
                {
                    for (std::size_t i = 0; i < total_bytes; i += elem_size)
                    {
                        std::memcpy(d + i, s + i, elem_size);
                        std::reverse(
                            reinterpret_cast<std::uint8_t*>(d + i),
                            reinterpret_cast<std::uint8_t*>(d + i) + elem_size);
                    }
                }
                else
                {
                    std::memcpy(d, s, total_bytes);
                }
            }
        }
    } // namespace

    void DecompressField(
        const VertexFieldSpec& spec,
        const std::byte* src,
        std::byte* dst,
        const std::size_t vertex_count,
        const std::size_t compressed_stride,
        const std::size_t compressed_offset,
        float uv_factor,
        const BinaryReadWrite::Endian endian)
    {
        const auto cc = spec.compressed_count;

        switch (spec.codec)
        {
        case VertexCodec::Identity:
            identity_decompress(spec, src, dst, vertex_count, compressed_stride, compressed_offset, endian);
            return;

        case VertexCodec::IdentityWiden:
            // Byte -> int32 or short -> int32.
            if (spec.compressed_scalar == S::U8 && spec.decompressed_scalar == S::S32)
            {
                decompress_loop<std::uint8_t, std::int32_t>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const std::uint8_t v) -> std::int32_t { return static_cast<std::int32_t>(v); }, endian);
            }
            else if (spec.compressed_scalar == S::S16 && spec.decompressed_scalar == S::S32)
            {
                decompress_loop<std::int16_t, std::int32_t>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const std::int16_t v) -> std::int32_t { return static_cast<std::int32_t>(v); }, endian);
            }
            else
            {
                throw FLVERError("IdentityWiden: unsupported type pair");
            }
            return;

        case VertexCodec::IntTo127Float:
            if (spec.compressed_scalar == S::S8)
            {
                decompress_loop<std::int8_t, float>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const std::int8_t v) -> float { return static_cast<float>(v) / 127.0f; }, endian);
            }
            else if (spec.compressed_scalar == S::U8)
            {
                decompress_loop<std::uint8_t, float>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const std::uint8_t v) -> float { return static_cast<float>(v) / 127.0f; }, endian);
            }
            else
            {
                throw FLVERError("IntTo127Float: unsupported compressed scalar type");
            }
            return;

        case VertexCodec::IntTo255Float:
            if (spec.compressed_scalar == S::U8)
            {
                decompress_loop<std::uint8_t, float>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const std::uint8_t v) -> float { return static_cast<float>(v) / 255.0f; }, endian);
            }
            else
            {
                throw FLVERError("IntTo255Float: unsupported compressed scalar type");
            }
            return;

        case VertexCodec::IntTo32767Float:
            if (spec.compressed_scalar == S::S16)
            {
                decompress_loop<std::int16_t, float>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const std::int16_t v) -> float { return static_cast<float>(v) / 32767.0f; }, endian);
            }
            else
            {
                throw FLVERError("IntTo32767Float: unsupported compressed scalar type");
            }
            return;

        case VertexCodec::SignedIntTo127Float:
            // (val - 127) / 127.0 — applied to either unsigned or signed bytes.
            if (spec.compressed_scalar == S::U8)
            {
                decompress_loop<std::uint8_t, float>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const std::uint8_t v) -> float { return (static_cast<float>(v) - 127.0f) / 127.0f; }, endian);
            }
            else if (spec.compressed_scalar == S::S8)
            {
                decompress_loop<std::int8_t, float>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const std::int8_t v) -> float { return (static_cast<float>(v) - 127.0f) / 127.0f; }, endian);
            }
            else
            {
                throw FLVERError("SignedIntTo127Float: unsupported compressed scalar type");
            }
            return;

        case VertexCodec::SignedIntTo255Float:
            if (spec.compressed_scalar == S::U8)
            {
                decompress_loop<std::uint8_t, float>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const std::uint8_t v) -> float { return (static_cast<float>(v) - 255.0f) / 255.0f; }, endian);
            }
            else
            {
                throw FLVERError("SignedIntTo255Float: unsupported compressed scalar type");
            }
            return;

        case VertexCodec::SignedIntTo32767Float:
            // Applied to both signed and unsigned shorts (format 0x1A uses S16, 0x2E uses U16).
            if (spec.compressed_scalar == S::S16)
            {
                decompress_loop<std::int16_t, float>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const std::int16_t v) -> float { return (static_cast<float>(v) - 32767.0f) / 32767.0f; }, endian);
            }
            else if (spec.compressed_scalar == S::U16)
            {
                decompress_loop<std::uint16_t, float>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const std::uint16_t v) -> float { return (static_cast<float>(v) - 32767.0f) / 32767.0f; }, endian);
            }
            else
            {
                throw FLVERError("SignedIntTo32767Float: unsupported compressed scalar type");
            }
            return;

        case VertexCodec::UvFactor:
            if (spec.compressed_scalar == S::S16)
            {
                decompress_loop<std::int16_t, float>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [uv_factor](const std::int16_t v) -> float { return static_cast<float>(v) / uv_factor; }, endian);
            }
            else
            {
                throw FLVERError("UvFactor: unsupported compressed scalar type");
            }
            return;
        }
        throw FLVERError("decompress_field: unknown codec");
    }

    void CompressField(
        const VertexFieldSpec& spec,
        const std::byte* src,
        std::byte* dst,
        const std::size_t vertex_count,
        const std::size_t compressed_stride,
        const std::size_t compressed_offset,
        float uv_factor,
        const BinaryReadWrite::Endian endian)
    {
        const auto cc = spec.compressed_count;

        switch (spec.codec)
        {
        case VertexCodec::Identity:
            identity_compress(spec, src, dst, vertex_count, compressed_stride, compressed_offset, endian);
            return;

        case VertexCodec::IdentityWiden:
            if (spec.compressed_scalar == S::U8 && spec.decompressed_scalar == S::S32)
            {
                compress_loop<std::int32_t, std::uint8_t>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const std::int32_t v) -> std::uint8_t { return static_cast<std::uint8_t>(v); }, endian);
            }
            else if (spec.compressed_scalar == S::S16 && spec.decompressed_scalar == S::S32)
            {
                compress_loop<std::int32_t, std::int16_t>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const std::int32_t v) -> std::int16_t { return static_cast<std::int16_t>(v); }, endian);
            }
            else
            {
                throw FLVERError("IdentityWiden compress: unsupported type pair");
            }
            return;

        case VertexCodec::IntTo127Float:
            if (spec.compressed_scalar == S::S8)
            {
                compress_loop<float, std::int8_t>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const float v) -> std::int8_t { return static_cast<std::int8_t>(std::round(v * 127.0f)); }, endian);
            }
            else if (spec.compressed_scalar == S::U8)
            {
                compress_loop<float, std::uint8_t>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const float v) -> std::uint8_t { return static_cast<std::uint8_t>(std::round(v * 127.0f)); }, endian);
            }
            else
            {
                throw FLVERError("IntTo127Float compress: unsupported type");
            }
            return;

        case VertexCodec::IntTo255Float:
            compress_loop<float, std::uint8_t>(
                src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                [](const float v) -> std::uint8_t { return static_cast<std::uint8_t>(std::round(v * 255.0f)); }, endian);
            return;

        case VertexCodec::IntTo32767Float:
            compress_loop<float, std::int16_t>(
                src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                [](const float v) -> std::int16_t { return static_cast<std::int16_t>(std::round(v * 32767.0f)); }, endian);
            return;

        case VertexCodec::SignedIntTo127Float:
            if (spec.compressed_scalar == S::U8)
            {
                compress_loop<float, std::uint8_t>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const float v) -> std::uint8_t { return static_cast<std::uint8_t>(std::round(v * 127.0f + 127.0f)); }, endian);
            }
            else if (spec.compressed_scalar == S::S8)
            {
                compress_loop<float, std::int8_t>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const float v) -> std::int8_t { return static_cast<std::int8_t>(std::round(v * 127.0f + 127.0f)); }, endian);
            }
            else
            {
                throw FLVERError("SignedIntTo127Float compress: unsupported type");
            }
            return;

        case VertexCodec::SignedIntTo255Float:
            compress_loop<float, std::uint8_t>(
                src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                [](const float v) -> std::uint8_t { return static_cast<std::uint8_t>(std::round(v * 255.0f + 255.0f)); }, endian);
            return;

        case VertexCodec::SignedIntTo32767Float:
            if (spec.compressed_scalar == S::S16)
            {
                compress_loop<float, std::int16_t>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const float v) -> std::int16_t
                    {
                        return static_cast<std::int16_t>(std::round(v * 32767.0f + 32767.0f));
                    }, endian);
            }
            else if (spec.compressed_scalar == S::U16)
            {
                compress_loop<float, std::uint16_t>(
                    src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                    [](const float v) -> std::uint16_t
                    {
                        return static_cast<std::uint16_t>(std::round(v * 32767.0f + 32767.0f));
                    }, endian);
            }
            else
            {
                throw FLVERError("SignedIntTo32767Float compress: unsupported type");
            }
            return;

        case VertexCodec::UvFactor:
            compress_loop<float, std::int16_t>(
                src, dst, vertex_count, compressed_stride, compressed_offset, cc,
                [uv_factor](const float v) -> std::int16_t { return static_cast<std::int16_t>(std::round(v * uv_factor)); }, endian);
            return;
        }
        throw FLVERError("compress_field: unknown codec");
    }
} // namespace Firelink

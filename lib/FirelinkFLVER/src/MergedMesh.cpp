// merged_mesh.cpp — C++ port of MergedMesh from soulstruct.python.mesh_tools.

#include "../include/FirelinkFLVER/MergedMesh.h"

#include <FirelinkFLVER/FLVER.h>
#include <FirelinkFLVER/Mesh.h>
#include <FirelinkFLVER/VertexFormats.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <numeric>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

namespace Firelink
{
    namespace
    {
        // ============================================================================
        // Field extraction helpers
        // ============================================================================
        // These read named fields from the decompressed interleaved vertex buffer.
        // We build a "field map" per layout describing where each named field lives
        // in the decompressed stride, then use it to bulk-extract columns.

        struct FieldInfo
        {
            std::string name;
            std::uint32_t offset = 0; // byte offset within decompressed vertex
            std::uint32_t count = 0; // number of scalar elements
            VertexScalarType scalar = VertexScalarType::F32; // element type
        };

        // Build a map from field name -> FieldInfo for the decompressed buffer.
        // This mirrors the C API's build_decomp_fields but returns byte offsets.
        std::vector<FieldInfo> build_field_map(const VertexArrayLayout& layout)
        {
            std::vector<FieldInfo> fields;
            int uv_index = 0, tangent_index = 0, color_index = 0;
            std::uint32_t decomp_offset = 0;

            for (const auto& dt : layout.types)
            {
                if (dt.usage == VertexUsage::Ignore) continue;
                const auto info = FindVertexFormatInfo(dt.usage, dt.format);
                if (!info) continue;

                for (std::uint8_t f = 0; f < info->field_count; ++f)
                {
                    const auto& fs = info->fields[f];

                    std::string name;
                    const std::uint32_t effective_count = fs.decompressed_count;

                    if (dt.usage == VertexUsage::UV)
                    {
                        const bool has_two = (dt.format == VertexDataFormatEnum::FourFloats ||
                            dt.format == VertexDataFormatEnum::FourShorts);
                        if (has_two && fs.decompressed_count == 4)
                        {
                            // Split into two 2-component UV fields.
                            fields.push_back(
                                {
                                    "uv_" + std::to_string(uv_index),
                                    decomp_offset, 2, fs.decompressed_scalar
                                });
                            fields.push_back(
                                {
                                    "uv_" + std::to_string(uv_index + 1),
                                    decomp_offset + 2 * static_cast<uint32_t>(GetScalarSize(fs.decompressed_scalar)),
                                    2, fs.decompressed_scalar
                                });
                            decomp_offset += static_cast<std::uint32_t>(fs.GetDecompressedSize());
                            uv_index += 2;
                            continue;
                        }
                        name = "uv_" + std::to_string(uv_index);
                        uv_index++;
                    }
                    else if (dt.usage == VertexUsage::Tangent)
                    {
                        name = "tangent_" + std::to_string(tangent_index);
                        if (f == info->field_count - 1) tangent_index++;
                    }
                    else if (dt.usage == VertexUsage::Color)
                    {
                        name = "color_" + std::to_string(color_index);
                        if (f == info->field_count - 1) color_index++;
                    }
                    else
                    {
                        name = fs.name;
                    }

                    fields.push_back({std::move(name), decomp_offset, effective_count, fs.decompressed_scalar});
                    decomp_offset += static_cast<std::uint32_t>(fs.GetDecompressedSize());
                }
            }
            return fields;
        }

        // Find a field by name. Returns nullptr if not found.
        const FieldInfo* find_field(const std::vector<FieldInfo>& fields, const std::string& name)
        {
            for (const auto& f : fields)
            {
                if (f.name == name) return &f;
            }
            return nullptr;
        }

        // Read `count` float32 values from the decompressed buffer at the given field
        // offset for vertex `v`. Stride = decompressed_vertex_size.
        void read_floats(
            const std::byte* data, const std::uint32_t stride, const std::uint32_t vertex,
            const FieldInfo& fi, float* out)
        {
            const auto* src = data + vertex * stride + fi.offset;
            if (fi.scalar == VertexScalarType::F32)
            {
                std::memcpy(out, src, fi.count * sizeof(float));
            }
            else
            {
                // Shouldn't happen for decompressed position/normal/etc but handle gracefully.
                for (std::uint32_t i = 0; i < fi.count; ++i) out[i] = 0.f;
            }
        }

        // Read `count` int32 values (for bone_indices).
        void read_int32s(
            const std::byte* data, const std::uint32_t stride, const std::uint32_t vertex,
            const FieldInfo& fi, std::int32_t* out)
        {
            const auto* src = data + vertex * stride + fi.offset;
            if (fi.scalar == VertexScalarType::S32)
            {
                std::memcpy(out, src, fi.count * sizeof(std::int32_t));
            }
            else
            {
                for (std::uint32_t i = 0; i < fi.count; ++i) out[i] = 0;
            }
        }

        // Read uint8 (for normal_w).
        void read_uint8s(
            const std::byte* data, const std::uint32_t stride, const std::uint32_t vertex,
            const FieldInfo& fi, std::uint8_t* out)
        {
            const auto* src = data + vertex * stride + fi.offset;
            if (fi.scalar == VertexScalarType::U8)
            {
                std::memcpy(out, src, fi.count);
            }
            else
            {
                for (std::uint32_t i = 0; i < fi.count; ++i) out[i] = 0;
            }
        }

        // ============================================================================
        // Triangulation
        // ============================================================================

        // Triangulate face set 0 of a mesh. Returns (n, 3) triangle indices.
        // Handles triangle strips with optional 0xFFFF primitive restarts.
        std::vector<std::array<std::uint32_t, 3>> triangulate(
            const FaceSet& fs,
            const bool uses_0xffff_separators)
        {
            std::vector<std::array<std::uint32_t, 3>> triangles;

            if (!fs.is_triangle_strip)
            {
                // Already triangle list — every 3 indices is a face.
                for (std::size_t i = 0; i + 2 < fs.vertex_indices.size(); i += 3)
                {
                    triangles.push_back(
                        {
                            fs.vertex_indices[i],
                            fs.vertex_indices[i + 1],
                            fs.vertex_indices[i + 2]
                        });
                }
                return triangles;
            }

            // Triangle strip.
            bool flip = false;
            for (std::size_t i = 0; i + 2 < fs.vertex_indices.size(); ++i)
            {
                const std::uint32_t a = fs.vertex_indices[i];
                const std::uint32_t b = fs.vertex_indices[i + 1];
                const std::uint32_t c = fs.vertex_indices[i + 2];

                if (uses_0xffff_separators && (a == 0xFFFF || b == 0xFFFF || c == 0xFFFF))
                {
                    flip = false;
                    continue;
                }

                // Skip degenerate faces.
                if (a != b && b != c && a != c)
                {
                    if (flip)
                        triangles.push_back({c, b, a});
                    else
                        triangles.push_back({a, b, c});
                }
                flip = !flip;
            }
            return triangles;
        }

        // ============================================================================
        // Vertex hash for merging
        // ============================================================================

        // Hash key: position (3 floats) + bone_indices (4 int32) + bone_weights (4 floats)
        // = 44 bytes. We hash these bytes directly.
        struct VertexKey
        {
            float position[3];
            std::int32_t bone_indices[4];
            float bone_weights[4];

            bool operator==(const VertexKey& o) const
            {
                for (std::uint32_t i = 0; i < 3; ++i)
                {
                    if (position[i] != o.position[i]) return false;
                }
                for (std::uint32_t i = 0; i < 4; ++i)
                {
                    if (bone_indices[i] != o.bone_indices[i]) return false;
                    if (bone_weights[i] != o.bone_weights[i]) return false;
                }
                return true;
            }
        };

        struct VertexKeyHash
        {
            std::size_t operator()(const VertexKey& k) const noexcept
            {
                // FNV-1a over the raw bytes.
                const auto* p = reinterpret_cast<const std::uint8_t*>(&k);
                std::size_t h = 14695981039346656037ULL;
                for (std::size_t i = 0; i < sizeof(VertexKey); ++i)
                {
                    h ^= p[i];
                    h *= 1099511628211ULL;
                }
                return h;
            }
        };

        float dot3(const float* a, const float* b)
        {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        }

        // ============================================================================
        // Material display mask extraction
        // ============================================================================

        // Parse "#NN#" prefix from material name to get display mask. Returns -1 if no mask.
        int get_display_mask(const std::string& mat_name)
        {
            // Material name bytes may be UTF-16-LE encoded. Try to detect "#NN#" pattern.
            // For simplicity, search for '#' in the first 8 bytes of the raw name.
            // In UTF-16-LE, '#' is 0x23 0x00. We'll check both encodings.

            // Try as ASCII/Shift-JIS first.
            if (mat_name.size() >= 4 && mat_name[0] == '#')
            {
                if (const auto end = mat_name.find('#', 1); end != std::string::npos && end >= 2 && end <= 4)
                {
                    int mask = 0;
                    bool ok = true;
                    for (std::size_t i = 1; i < end; ++i)
                    {
                        const char c = mat_name[i];
                        if (c >= '0' && c <= '9') mask = mask * 10 + (c - '0');
                        else
                        {
                            ok = false;
                            break;
                        }
                    }
                    if (ok) return mask;
                }
            }

            // Try as UTF-16-LE: '#' = {0x23, 0x00}, digit = {0x3N, 0x00}
            if (mat_name.size() >= 8)
            {
                if (const auto* b = reinterpret_cast<const std::uint8_t*>(mat_name.data());
                    b[0] == 0x23 && b[1] == 0x00)
                {
                    // Find closing '#'
                    for (std::size_t i = 2; i + 1 < mat_name.size(); i += 2)
                    {
                        if (b[i] == 0x23 && b[i + 1] == 0x00 && i >= 4)
                        {
                            int mask = 0;
                            bool ok = true;
                            for (std::size_t j = 2; j < i; j += 2)
                            {
                                if (b[j + 1] == 0x00 && b[j] >= '0' && b[j] <= '9')
                                    mask = mask * 10 + (b[j] - '0');
                                else
                                {
                                    ok = false;
                                    break;
                                }
                            }
                            if (ok) return mask;
                            break;
                        }
                    }
                }
            }

            return -1;
        }
    } // namespace

    MergedMesh::MergedMesh(
        const FLVER& flver,
        const std::vector<std::uint32_t>& mesh_material_indices_in,
        const std::vector<std::vector<std::string>>& materialUVLayerNames,
        bool mergeVertices)
    {
        vertices_merged = mergeVertices;

        if (flver.Meshes().empty()) 
            return;

        // Assign material indices.
        std::vector<std::uint32_t> mat_indices = mesh_material_indices_in;
        if (mat_indices.empty())
        {
            mat_indices.resize(flver.Meshes().size());
            std::iota(mat_indices.begin(), mat_indices.end(), 0u);
        }

        // Filter out meshes with NaN positions.
        struct ValidMesh
        {
            std::uint32_t original_index;
            std::uint32_t material_index;
        };
        std::vector<ValidMesh> valid_meshes;
        for (std::uint32_t i = 0; i < flver.Meshes().size(); ++i)
        {
            const auto& mesh = flver.Meshes()[i];
            if (mesh.vertex_arrays.empty()) continue;
            // Skip meshes with no decompressed vertex data (e.g. DSR layout mismatch
            // sets vertex_count to 0 while face sets still have the original indices).
            if (mesh.vertex_arrays[0].vertex_count == 0) continue;
            // Quick NaN check on positions.
            const auto& va = mesh.vertex_arrays[0];
            auto fmap = build_field_map(va.layout);
            auto* pos_fi = find_field(fmap, "position");
            bool has_nan = false;
            if (pos_fi)
            {
                const auto stride = va.layout.decompressed_vertex_size;
                for (std::uint32_t v = 0; v < va.vertex_count && !has_nan; ++v)
                {
                    float p[4];
                    read_floats(va.decompressed_data.data(), stride, v, *pos_fi, p);
                    for (std::uint32_t c = 0; c < pos_fi->count && c < 3; ++c)
                    {
                        if (std::isnan(p[c]))
                        {
                            has_nan = true;
                            break;
                        }
                    }
                }
            }
            if (!has_nan)
            {
                valid_meshes.push_back({i, i < mat_indices.size() ? mat_indices[i] : i});
            }
        }

        if (valid_meshes.empty()) 
            return;

        // ========================================================================
        // Phase 1: Compute total loop count and discover all field names.
        //          Handle multi-VA meshes (e.g. Elden Ring cloth with 3 VAs).
        // ========================================================================

        std::uint32_t total_loops = 0;
        for (const auto& [original_index, material_index] : valid_meshes)
        {
            total_loops += flver.Meshes()[original_index].vertex_arrays[0].vertex_count;
        }

        // A field source tracks which VA a field comes from plus the field info
        // (with offsets relative to that VA's decompressed buffer).
        struct FieldSource
        {
            std::uint32_t va_index{}; // which vertex array
            FieldInfo info; // field info within that VA
        };

        // Per-mesh: map from merged field name -> source.
        // For 3-VA ER cloth: VA 1's tangent_0 -> "cloth_tangent", bitangent -> "cloth_bitangent",
        //                    other VA 1 fields keep their original names.
        struct MeshFieldCache
        {
            std::unordered_map<std::string, FieldSource> fields; // merged name -> source
            std::uint32_t vertex_count{};
        };
        std::vector<MeshFieldCache> mesh_caches(valid_meshes.size());

        std::unordered_set<std::string> all_field_names;

        for (std::size_t mi = 0; mi < valid_meshes.size(); ++mi)
        {
            const auto& mesh = flver.GetMesh(valid_meshes[mi].original_index);
            auto& [fields, mesh_vc] = mesh_caches[mi];
            mesh_vc = mesh.vertex_arrays[0].vertex_count;

            // VA 0: all fields used directly.
            {
                for (auto fields0 = build_field_map(mesh.vertex_arrays[0].layout);
                     auto& fi : fields0)
                {
                    fields[fi.name] = {0, fi};
                }
            }

            // Multi-VA handling.
            if (mesh.vertex_arrays.size() > 1)
            {
                bool is_er_cloth = (mesh.vertex_arrays.size() == 3);
                // For ER cloth: check VA[0] == VA[2] (we skip the expensive check here
                // and just assume the 3-VA pattern is ER cloth).

                for (std::uint32_t vai = 1; vai < mesh.vertex_arrays.size(); ++vai)
                {
                    if (is_er_cloth && vai == 2) continue; // Skip VA[2] (duplicate of VA[0]).

                    auto extra_fields = build_field_map(mesh.vertex_arrays[vai].layout);
                    for (auto& fi : extra_fields)
                    {
                        std::string merged_name = fi.name;
                        if (is_er_cloth && vai == 1)
                        {
                            // Remap tangent_0 -> cloth_tangent, bitangent -> cloth_bitangent
                            if (fi.name == "tangent_0") merged_name = "cloth_tangent";
                            else if (fi.name == "bitangent") merged_name = "cloth_bitangent";
                        }
                        // Later VAs overwrite earlier for same merged name.
                        fields[merged_name] = {vai, fi};
                    }
                }
            }

            for (const auto& name : fields | std::views::keys)
            {
                all_field_names.insert(name);
            }
        }

        // ========================================================================
        // Allocate and populate stacked loop arrays.
        // ========================================================================

        // Vertex-data arrays (position + bones) for all loops, pre-merge.
        std::vector all_positions(total_loops * 3, 0.f);
        std::vector all_bone_weights(total_loops * 4, 0.f);
        std::vector all_bone_indices(total_loops * 4, 0);

        // Loop arrays.
        const bool has_normal = all_field_names.contains("normal");
        const bool has_normal_w = all_field_names.contains("normal_w");
        const bool has_bone_indices = all_field_names.contains("bone_indices");
        const bool has_bitangent = all_field_names.contains("bitangent");

        loop_normals.resize(has_normal ? total_loops * 3 : 0, 0.f);
        // Allocate normal_w whenever either normal_w or bone_indices is present: for non-dynamic mesh layouts
        // on modern games (BB+) that encode the single static bone index in normal_w rather than a dedicated
        // bone_indices field, the value is derived from bone_indices[0] at population time so the split step
        // never needs to special-case it.
        loop_normals_w.resize((has_normal_w || has_bone_indices) ? total_loops : 0, 127);
        loop_bitangents.resize(has_bitangent ? total_loops * 4 : 0, 0.f);

        // Count tangent and color slots.
        int max_tangent = -1, max_color = -1;
        for (const auto& name : all_field_names)
        {
            if (name.rfind("tangent_", 0) == 0)
            {
                int idx = std::stoi(name.substr(8));
                max_tangent = std::max(max_tangent, idx);
            }
            if (name.rfind("color_", 0) == 0)
            {
                int idx = std::stoi(name.substr(6));
                max_color = std::max(max_color, idx);
            }
        }
        loop_tangents.resize(max_tangent + 1);
        for (auto& t : loop_tangents) t.resize(total_loops * 4, 0.f);
        loop_vertex_colors.resize(max_color + 1);
        for (auto& c : loop_vertex_colors) c.resize(total_loops * 4, 0.f);

        // UV layers: we'll build them on-the-fly, keyed by layer name.
        // Map from UV layer name -> index in loop_uvs.
        std::unordered_map<std::string, std::uint32_t> uv_layer_index;

        // Default tangent/bitangent/color values for meshes missing the field.
        constexpr float default_tangent[4] = {1.f, 0.f, 0.f, 1.f};
        constexpr float default_bitangent[4] = {0.f, 0.f, 1.f, 1.f};
        constexpr float default_color[4] = {0.f, 0.f, 0.f, 1.f};
        constexpr float default_normal[3] = {0.f, 1.f, 0.f};

        // Helper: get VA buffer and stride for a field source.
        auto get_va_data = [&](
            const Mesh& mesh, const FieldSource& src)
            -> std::pair<const std::byte*, std::uint32_t>
        {
            const auto& va = mesh.vertex_arrays[src.va_index];
            return {va.decompressed_data.data(), va.layout.decompressed_vertex_size};
        };

        // Helper: find a field source in a mesh cache, or nullptr.
        auto find_src = [](
            const MeshFieldCache& cache, const std::string& name)
            -> const FieldSource*
        {
            const auto it = cache.fields.find(name);
            return it != cache.fields.end() ? &it->second : nullptr;
        };

        std::uint32_t loop_offset = 0;
        for (std::size_t mi = 0; mi < valid_meshes.size(); ++mi)
        {
            const auto& mesh = flver.GetMesh(valid_meshes[mi].original_index);
            const auto& cache = mesh_caches[mi];
            const auto vc = cache.vertex_count;
            const auto material_index = valid_meshes[mi].material_index;

            auto* pos_src = find_src(cache, "position");
            auto* bw_src = find_src(cache, "bone_weights");
            auto* bi_src = find_src(cache, "bone_indices");
            auto* norm_src = find_src(cache, "normal");
            auto* nw_src = find_src(cache, "normal_w");
            auto* bt_src = find_src(cache, "bitangent");

            for (std::uint32_t v = 0; v < vc; ++v)
            {
                std::uint32_t li = loop_offset + v;

                // Position
                if (pos_src)
                {
                    auto [vdata, stride] = get_va_data(mesh, *pos_src);
                    float p[4];
                    read_floats(vdata, stride, v, pos_src->info, p);
                    all_positions[li * 3 + 0] = p[0];
                    all_positions[li * 3 + 1] = p[1];
                    all_positions[li * 3 + 2] = p[2];
                }

                // Bone weights
                if (bw_src)
                {
                    auto [vdata, stride] = get_va_data(mesh, *bw_src);
                    read_floats(vdata, stride, v, bw_src->info, &all_bone_weights[li * 4]);
                }

                // Bone indices
                if (bi_src)
                {
                    auto [vdata, stride] = get_va_data(mesh, *bi_src);
                    std::int32_t bi[4];
                    read_int32s(vdata, stride, v, bi_src->info, bi);
                    for (int& k : bi)
                    {
                        if (k == 255) k = 0;
                    }
                    if (!mesh.bone_indices.empty())
                    {
                        for (int& k : bi)
                        {
                            if (k >= 0 && k < static_cast<std::int32_t>(mesh.bone_indices.size()))
                                k = mesh.bone_indices[k];
                        }
                    }
                    std::memcpy(&all_bone_indices[li * 4], bi, 4 * sizeof(std::int32_t));
                }

                // Normal
                if (has_normal)
                {
                    if (norm_src)
                    {
                        auto [vdata, stride] = get_va_data(mesh, *norm_src);
                        read_floats(vdata, stride, v, norm_src->info, &loop_normals[li * 3]);
                    }
                    else
                    {
                        std::memcpy(&loop_normals[li * 3], default_normal, 12);
                    }
                }

                // Normal W — also derived from bone_indices when normal_w is absent.
                if (has_normal_w || has_bone_indices)
                {
                    if (nw_src)
                    {
                        auto [vdata, stride] = get_va_data(mesh, *nw_src);
                        read_uint8s(vdata, stride, v, nw_src->info, &loop_normals_w[li]);
                    }
                    else if (bi_src)
                    {
                        // Derive normal_w from the first already-remapped bone index. Non-dynamic mesh
                        // layouts in modern games encode the single static-mesh bone index in normal_w
                        // rather than a dedicated bone_indices field; pre-deriving it here means the
                        // split step can use it without any special-casing.
                        loop_normals_w[li] = static_cast<std::uint8_t>(
                            std::clamp(all_bone_indices[li * 4], 0, 255));
                    }
                    else
                    {
                        loop_normals_w[li] = 127;
                    }
                }

                // Bitangent
                if (has_bitangent)
                {
                    if (bt_src)
                    {
                        auto [vdata, stride] = get_va_data(mesh, *bt_src);
                        read_floats(vdata, stride, v, bt_src->info, &loop_bitangents[li * 4]);
                    }
                    else
                    {
                        std::memcpy(&loop_bitangents[li * 4], default_bitangent, 16);
                    }
                }
            }

            // Tangents
            for (int ti = 0; ti <= max_tangent; ++ti)
            {
                std::string tangent_name = "tangent_" + std::to_string(ti);
                auto* t_src = find_src(cache, tangent_name);
                auto& tarr = loop_tangents[ti];
                for (std::uint32_t v = 0; v < vc; ++v)
                {
                    std::uint32_t li = loop_offset + v;
                    if (t_src)
                    {
                        auto [vdata, stride] = get_va_data(mesh, *t_src);
                        read_floats(vdata, stride, v, t_src->info, &tarr[li * 4]);
                    }
                    else
                    {
                        std::memcpy(&tarr[li * 4], default_tangent, 16);
                    }
                }
            }

            // Colors
            for (int ci = 0; ci <= max_color; ++ci)
            {
                std::string cname = "color_" + std::to_string(ci);
                auto* c_src = find_src(cache, cname);
                auto& carr = loop_vertex_colors[ci];
                for (std::uint32_t v = 0; v < vc; ++v)
                {
                    std::uint32_t li = loop_offset + v;
                    if (c_src)
                    {
                        auto [vdata, stride] = get_va_data(mesh, *c_src);
                        read_floats(vdata, stride, v, c_src->info, &carr[li * 4]);
                    }
                    else
                    {
                        std::memcpy(&carr[li * 4], default_color, 16);
                    }
                }
            }

            // UVs
            for (const auto& [field_name, field_src] : cache.fields)
            {
                if (field_name.rfind("uv_", 0) != 0) continue;
                int uv_i = std::stoi(field_name.substr(3));
                std::uint32_t uv_dim = field_src.info.count;

                // Determine UV layer name.
                std::string layer_name;
                if (!materialUVLayerNames.empty() &&
                    material_index < materialUVLayerNames.size() &&
                    uv_i < static_cast<int>(materialUVLayerNames[material_index].size()))
                {
                    layer_name = materialUVLayerNames[material_index][uv_i];
                }
                else
                {
                    layer_name = "UVMap" + std::to_string(uv_i);
                }

                // Get or create UV layer.
                std::uint32_t layer_idx;
                auto it = uv_layer_index.find(layer_name);
                if (it != uv_layer_index.end())
                {
                    layer_idx = it->second;
                    if (auto& layer = loop_uvs[layer_idx];
                        uv_dim > layer.dim)
                    {
                        std::vector new_data(total_loops * uv_dim, 0.f);
                        for (std::uint32_t r = 0; r < loop_offset; ++r)
                        {
                            for (std::uint32_t c = 0; c < layer.dim; ++c)
                            {
                                new_data[r * uv_dim + c] = layer.data[r * layer.dim + c];
                            }
                        }
                        layer.data = std::move(new_data);
                        layer.dim = uv_dim;
                    }
                }
                else
                {
                    layer_idx = static_cast<std::uint32_t>(loop_uvs.size());
                    uv_layer_index[layer_name] = layer_idx;
                    loop_uvs.push_back({layer_name, uv_dim, {}});
                    loop_uvs.back().data.resize(total_loops * uv_dim, 0.f);
                }

                auto [vdata, stride] = get_va_data(mesh, field_src);
                auto& layer = loop_uvs[layer_idx];
                for (std::uint32_t v = 0; v < vc; ++v)
                {
                    std::uint32_t li = loop_offset + v;
                    float vals[4] = {};
                    read_floats(vdata, stride, v, field_src.info, vals);
                    for (std::uint32_t c = 0; c < std::min(uv_dim, layer.dim); ++c)
                    {
                        layer.data[li * layer.dim + c] = vals[c];
                    }
                }
            }

            loop_offset += vc;
        }

        total_loop_count = total_loops;

        // ========================================================================
        // Phase 3: Triangulate and build faces.
        // ========================================================================

        std::vector<std::array<std::uint32_t, 3>> all_triangles;
        std::vector<std::uint32_t> triangle_materials;

        loop_offset = 0;
        for (auto& [original_index, material_index] : valid_meshes)
        {
            const auto& mesh = flver.Meshes()[original_index];
            if (mesh.face_sets.empty())
            {
                loop_offset += mesh.vertex_arrays[0].vertex_count;
                continue;
            }

            // A 16-bit index buffer uses 0xFFFF as the primitive-restart sentinel.
            // The maximum valid vertex_count for a 16-bit strip is 65535 (indices
            // 0..65534), so the sentinel check must be inclusive: vertex_count <= 0xFFFF.
            bool uses_sep = mesh.vertex_arrays.size() == 1 && mesh.vertex_arrays[0].vertex_count <= 0xFFFF;

            for (auto tris = triangulate(mesh.face_sets[0], uses_sep);
                 auto& tri : tris)
            {
                tri[0] += loop_offset;
                tri[1] += loop_offset;
                tri[2] += loop_offset;
                all_triangles.push_back(tri);
                triangle_materials.push_back(material_index);
            }

            loop_offset += mesh.vertex_arrays[0].vertex_count;
        }

        face_count = static_cast<std::uint32_t>(all_triangles.size());
        faces.resize(face_count * 4);
        for (std::uint32_t fi = 0; fi < face_count; ++fi)
        {
            faces[fi * 4 + 0] = all_triangles[fi][0];
            faces[fi * 4 + 1] = all_triangles[fi][1];
            faces[fi * 4 + 2] = all_triangles[fi][2];
            faces[fi * 4 + 3] = triangle_materials[fi];
        }

        // ========================================================================
        // Phase 4: Merge vertices (or simple stack).
        // ========================================================================

        if (!mergeVertices)
        {
            // No merging — vertices and loops are 1:1.
            positions = std::move(all_positions);
            bone_weights = std::move(all_bone_weights);
            bone_indices = std::move(all_bone_indices);
            vertex_count = total_loops;
            loop_vertex_indices.resize(total_loops);
            std::iota(loop_vertex_indices.begin(), loop_vertex_indices.end(), 0u);
            return;
        }

        // Merge: identify unique vertices by (position, bone_indices, bone_weights)
        // with normal-based disambiguation for near-inverted faces.

        // Build per-loop VertexKey.
        std::vector<VertexKey> loop_keys(total_loops);
        for (std::uint32_t li = 0; li < total_loops; ++li)
        {
            VertexKey& key = loop_keys[li];
            std::memcpy(key.position, &all_positions[li * 3], 12);
            std::memcpy(key.bone_indices, &all_bone_indices[li * 4], 16);
            std::memcpy(key.bone_weights, &all_bone_weights[li * 4], 16);
        }

        // Get display mask for each mesh to disambiguate cross-mask vertices.
        std::vector loop_display_masks(total_loops, -1);
        loop_offset = 0;
        for (auto& [original_index, material_index] : valid_meshes)
        {
            const auto& mesh = flver.Meshes()[original_index];
            int mask = get_display_mask(mesh.material.name);
            std::uint32_t vc = mesh.vertex_arrays[0].vertex_count;
            for (std::uint32_t v = 0; v < vc; ++v)
            {
                loop_display_masks[loop_offset + v] = mask;
            }
            loop_offset += vc;
        }

        // Merge map: (display_mask, vertex_key_hash) -> (vertex_index, normal).
        // Use a combined key of display mask + VertexKey bytes.
        struct MaskKey
        {
            int mask;
            VertexKey key;

            bool operator==(const MaskKey& o) const
            {
                return mask == o.mask && key == o.key;
            }
        };
        struct MaskKeyHash
        {
            VertexKeyHash kh;

            std::size_t operator()(const MaskKey& mk) const noexcept
            {
                std::size_t h = kh(mk.key);
                h ^= std::hash<int>{}(mk.mask) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };

        struct VertexEntry
        {
            std::uint32_t vertex_index;
            float normal[3];
        };

        std::unordered_map<MaskKey, VertexEntry, MaskKeyHash> vertex_map;
        std::unordered_map<MaskKey, std::uint32_t, MaskKeyHash> inv_vertex_map;

        std::vector<std::uint32_t> reduced_indices; // loop indices of unique vertices
        loop_vertex_indices.resize(total_loops, UINT32_MAX);
        std::uint32_t vert_count = 0;

        // Process faces to determine merge targets.
        std::unordered_set<std::uint32_t> resolved_loops;

        for (std::uint32_t fi = 0; fi < face_count; ++fi)
        {
            for (int corner = 0; corner < 3; ++corner)
            {
                std::uint32_t li = faces[fi * 4 + corner];

                if (resolved_loops.contains(li)) continue;
                resolved_loops.insert(li);


                MaskKey mk{loop_display_masks[li], loop_keys[li]};
                float loop_normal[3] = {0.f, 1.f, 0.f};
                if (!loop_normals.empty())
                {
                    std::memcpy(loop_normal, &loop_normals[li * 3], 12);
                }

                auto it = vertex_map.find(mk);
                if (it == vertex_map.end())
                {
                    // New unique vertex.
                    std::uint32_t vi = vert_count++;
                    vertex_map[mk] = {vi, {loop_normal[0], loop_normal[1], loop_normal[2]}};
                    loop_vertex_indices[li] = vi;
                    reduced_indices.push_back(li);
                    continue;
                }

                // Existing vertex — check normal.
                const auto& [vertex_index, normal] = it->second;
                if (const float dp = dot3(loop_normal, normal); dp < -0.9f)
                {
                    // Inverted normal — use separate inverted vertex.
                    if (auto inv_it = inv_vertex_map.find(mk);
                        inv_it == inv_vertex_map.end())
                    {
                        std::uint32_t vi = vert_count++;
                        inv_vertex_map[mk] = vi;
                        loop_vertex_indices[li] = vi;
                        reduced_indices.push_back(li);
                    }
                    else
                    {
                        loop_vertex_indices[li] = inv_it->second;
                    }
                    continue;
                }

                // Suitable existing vertex.
                loop_vertex_indices[li] = vertex_index;
            }
        }

        // Handle unreferenced loops (not used by any face).
        for (std::uint32_t li = 0; li < total_loops; ++li)
        {
            if (loop_vertex_indices[li] == UINT32_MAX)
            {
                loop_vertex_indices[li] = UINT32_MAX; // mark unused
            }
        }

        // Build reduced vertex arrays.
        vertex_count = vert_count;
        positions.resize(vert_count * 3);
        bone_weights.resize(vert_count * 4);
        bone_indices.resize(vert_count * 4);

        for (std::uint32_t vi = 0; vi < vert_count; ++vi)
        {
            std::uint32_t li = reduced_indices[vi];
            std::memcpy(&positions[vi * 3], &all_positions[li * 3], 12);
            std::memcpy(&bone_weights[vi * 4], &all_bone_weights[li * 4], 16);
            std::memcpy(&bone_indices[vi * 4], &all_bone_indices[li * 4], 16);
        }
    }
} // namespace Firelink

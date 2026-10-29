// pyrelink_core_dds.cpp — pybind11 bindings for FirelinkCore DDS module.
//
// Exposes the DDS class and the DXGIFormat enum to Python.
// Input byte buffers are borrowed from Python (zero-copy); all conversions
// release the GIL so they don't block the interpreter.

#include <pybind11/pybind11.h>

#include <FirelinkCore/DDS.h>
#include <pyrelink_helpers.h>

namespace py = pybind11;
using namespace Firelink;

void bind_firelink_core_dds(py::module& m)
{
    // -------------------------------------------------------------------------
    // DXGIFormat enum
    // -------------------------------------------------------------------------
    py::enum_<DXGI_FORMAT>(m, "DXGIFormat",
        "DXGI pixel format identifiers (subset relevant to FromSoftware textures).")
        .value("UNKNOWN",              DXGI_FORMAT_UNKNOWN)
        // Uncompressed
        .value("R8G8B8A8_UNORM",       DXGI_FORMAT_R8G8B8A8_UNORM)
        .value("R8G8B8A8_UNORM_SRGB",  DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        .value("B8G8R8A8_UNORM",       DXGI_FORMAT_B8G8R8A8_UNORM)
        .value("B8G8R8A8_UNORM_SRGB",  DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
        // BC compressed
        .value("BC1_UNORM",            DXGI_FORMAT_BC1_UNORM)
        .value("BC1_UNORM_SRGB",       DXGI_FORMAT_BC1_UNORM_SRGB)
        .value("BC2_UNORM",            DXGI_FORMAT_BC2_UNORM)
        .value("BC2_UNORM_SRGB",       DXGI_FORMAT_BC2_UNORM_SRGB)
        .value("BC3_UNORM",            DXGI_FORMAT_BC3_UNORM)
        .value("BC3_UNORM_SRGB",       DXGI_FORMAT_BC3_UNORM_SRGB)
        .value("BC4_UNORM",            DXGI_FORMAT_BC4_UNORM)
        .value("BC4_SNORM",            DXGI_FORMAT_BC4_SNORM)
        .value("BC5_UNORM",            DXGI_FORMAT_BC5_UNORM)
        .value("BC5_SNORM",            DXGI_FORMAT_BC5_SNORM)
        .value("BC6H_UF16",            DXGI_FORMAT_BC6H_UF16)
        .value("BC6H_SF16",            DXGI_FORMAT_BC6H_SF16)
        .value("BC7_UNORM",            DXGI_FORMAT_BC7_UNORM)
        .value("BC7_UNORM_SRGB",       DXGI_FORMAT_BC7_UNORM_SRGB)
        // Single/dual channel
        .value("R8_UNORM",             DXGI_FORMAT_R8_UNORM)
        .value("R8G8_UNORM",           DXGI_FORMAT_R8G8_UNORM)
        .value("R16_FLOAT",            DXGI_FORMAT_R16_FLOAT)
        .value("R16G16_FLOAT",         DXGI_FORMAT_R16G16_FLOAT)
        .value("R16G16B16A16_FLOAT",   DXGI_FORMAT_R16G16B16A16_FLOAT)
        .value("R32_FLOAT",            DXGI_FORMAT_R32_FLOAT)
        .value("R32G32B32A32_FLOAT",   DXGI_FORMAT_R32G32B32A32_FLOAT)
        // Additional formats reachable through legacy/DX10 DDS headers (needed to cover every `texconv -f` name).
        .value("A8_UNORM",             DXGI_FORMAT_A8_UNORM)
        .value("B8G8R8X8_UNORM",       DXGI_FORMAT_B8G8R8X8_UNORM)
        .value("B8G8R8X8_UNORM_SRGB",  DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
        .value("B5G6R5_UNORM",         DXGI_FORMAT_B5G6R5_UNORM)
        .value("B5G5R5A1_UNORM",       DXGI_FORMAT_B5G5R5A1_UNORM)
        .value("B4G4R4A4_UNORM",       DXGI_FORMAT_B4G4R4A4_UNORM)
        .value("R10G10B10A2_UNORM",    DXGI_FORMAT_R10G10B10A2_UNORM)
        .value("R11G11B10_FLOAT",      DXGI_FORMAT_R11G11B10_FLOAT)
        .value("R16_UNORM",            DXGI_FORMAT_R16_UNORM)
        .value("R16G16_UNORM",         DXGI_FORMAT_R16G16_UNORM)
        .value("R16G16B16A16_UNORM",   DXGI_FORMAT_R16G16B16A16_UNORM);

    // -------------------------------------------------------------------------
    // DDS class
    // -------------------------------------------------------------------------
    py::class_<DDS>(m, "DDS",
        "DDS texture container.\n\n"
        "Wraps raw DDS bytes with conversion and PS4 de/swizzling methods.\n"
        "It is the caller's responsibility to track whether the pixel data is\n"
        "PS4-swizzled; converting or displaying swizzled data will look garbled.")

        // --- Construction ---

        .def(py::init([](const py::buffer& buf)
        {
            auto [ptr, size] = borrow_buffer(buf);
            return DDS(ptr, size);
        }),
        py::arg("data"),
        "Construct from raw DDS bytes (bytes / bytearray / memoryview).")

        .def_static("from_tga", [](const py::buffer& buf, DXGI_FORMAT fmt, std::size_t mip_levels, bool force_dx10)
        {
            auto [ptr, size] = borrow_buffer(buf);
            DDS result;
            {
                py::gil_scoped_release release;
                result = DDS::FromTGA(ptr, size, fmt, mip_levels, force_dx10);
            }
            return result;
        },
        py::arg("data"), py::arg("format"), py::arg("mip_levels") = 1, py::arg("force_dx10") = false,
        "Convert TGA image bytes to DDS with the given DXGI format.\n\n"
        "`mip_levels`: 1 (default) writes only the base image, 0 generates the full mip chain, N > 1 generates N "
        "levels (like `texconv -m`). `force_dx10` always writes the extended DX10 header (like `texconv -dx10`).\n\n"
        "On Windows, BC6H/BC7 compression uses GPU acceleration when available (base image only).")

        .def_static("from_png", [](const py::buffer& buf, DXGI_FORMAT fmt, std::size_t mip_levels, bool force_dx10)
        {
            auto [ptr, size] = borrow_buffer(buf);
            DDS result;
            {
                py::gil_scoped_release release;
                result = DDS::FromPNG(ptr, size, fmt, mip_levels, force_dx10);
            }
            return result;
        },
        py::arg("data"), py::arg("format"), py::arg("mip_levels") = 1, py::arg("force_dx10") = false,
        "Convert PNG image bytes to DDS with the given DXGI format.\n\n"
        "`mip_levels` and `force_dx10` behave as in `from_tga`.\n\n"
        "On Windows, BC6H/BC7 compression uses GPU acceleration when available (base image only).")

        // --- Properties ---

        .def_property_readonly("data",
            [](const DDS& dds) { return vector_to_bytes(dds.GetBytes()); },
            "Raw DDS bytes.")

        .def_property_readonly("size", &DDS::GetSize,
            "Size of the DDS data in bytes.")

        .def_property_readonly("is_empty", &DDS::IsEmpty,
            "True if no DDS data is stored.")

        // --- Conversions ---

        .def("to_tga", [](const DDS& dds)
        {
            std::vector<std::byte> result;
            {
                py::gil_scoped_release release;
                result = dds.ToTGA();
            }
            return vector_to_bytes(result);
        },
        "Convert DDS to TGA bytes.\n\n"
        "PS4-swizzled data must be deswizzled first or the output will be garbled.")

        .def("to_png", [](const DDS& dds)
        {
            std::vector<std::byte> result;
            {
                py::gil_scoped_release release;
                result = dds.ToPNG();
            }
            return vector_to_bytes(result);
        },
        "Convert DDS to PNG bytes.\n\n"
        "PS4-swizzled data must be deswizzled first or the output will be garbled.")

        // --- PS4 swizzle ---

        .def("deswizzle_ps4", [](const DDS& dds)
        {
            DDS result;
            {
                py::gil_scoped_release release;
                result = dds.DeswizzlePS4();
            }
            return result;
        },
        "Return a new DDS with PS4 tiled pixel data converted to linear row-major layout.\n\n"
        "Call this before to_tga() / to_png() on textures sourced from a PS4 game dump.")

        .def("swizzle_ps4", [](const DDS& dds)
        {
            DDS result;
            {
                py::gil_scoped_release release;
                result = dds.SwizzlePS4();
            }
            return result;
        },
        "Return a new DDS with linear pixel data converted to PS4 tiled layout.\n\n"
        "Call this before storing a DDS back into a PS4 TPF.")

        // --- Dunder ---

        .def("__len__",  &DDS::GetSize)
        .def("__bool__", [](const DDS& dds) { return !dds.IsEmpty(); })
        .def("__repr__", [](const DDS& dds)
        {
            return "<DDS " + std::to_string(dds.GetSize()) + " bytes>";
        });
}
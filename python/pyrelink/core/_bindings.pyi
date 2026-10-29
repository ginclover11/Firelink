"""Type stubs for the pyrelink_core pybind11 extension module."""

from __future__ import annotations

__all__ = [
    # Core
    "GameType",
    "Vector2",
    "Vector3",
    "Vector4",
    "EulerRad",
    "Color4b",
    "AABB",
    "GroupBitSet128",
    "GroupBitSet256",
    "GroupBitSet1024",
    "GameFile",
    # Binder
    "BinderVersion",
    "BinderFlags",
    "BinderVersion4Info",
    "BinderEntry",
    "BinderError",
    "Binder",
    # DCX
    "DCXType",
    "DCXResult",
    "DCXError",
    "is_dcx",
    "detect_dcx",
    "decompress_dcx",
    "compress_dcx",
    # Oodle
    "set_oodle_dll_path",
    "load_oodle_dll",
    "is_oodle_available",
    "oodle_dll_path",
    # DDS
    "DXGIFormat",
    "DDS",
    # TPF
    "TPFPlatform",
    "TextureType",
    "TPFTexture",
    "TPFError",
    "TPF",
]

import re
from pathlib import Path
from typing import Callable, Union, Optional, Self, Sequence
from enum import IntEnum

# ---------------------------------------------------------------------------
# Primitive math types
# ---------------------------------------------------------------------------

class Vector2:
    """Basic XY vector."""
    x: float
    y: float
    def __init__(self, x: float = 0.0, y: float = 0.0) -> None: ...
    def __init__(self, seq: Sequence[float]) -> None: ...  # type: ignore[misc]
    def __len__(self) -> int: ...
    def __getitem__(self, i: int) -> float: ...
    def __setitem__(self, i: int, val: float) -> None: ...
    def __iter__(self): ...
    def __repr__(self) -> str: ...

class Vector3:
    """Basic XYZ vector."""
    x: float
    y: float
    z: float
    def __init__(self, x: float = 0.0, y: float = 0.0, z: float = 0.0) -> None: ...
    def __init__(self, seq: Sequence[float]) -> None: ...  # type: ignore[misc]
    def __len__(self) -> int: ...
    def __getitem__(self, i: int) -> float: ...
    def __setitem__(self, i: int, val: float) -> None: ...
    def __iter__(self): ...
    def __repr__(self) -> str: ...
    @staticmethod
    def zero() -> Vector3: ...
    @staticmethod
    def one() -> Vector3: ...

class Vector4:
    """Basic XYZW vector."""
    x: float
    y: float
    z: float
    w: float
    def __init__(self, x: float = 0.0, y: float = 0.0, z: float = 0.0, w: float = 0.0) -> None: ...
    def __init__(self, seq: Sequence[float]) -> None: ...  # type: ignore[misc]
    def __len__(self) -> int: ...
    def __getitem__(self, i: int) -> float: ...
    def __setitem__(self, i: int, val: float) -> None: ...
    def __iter__(self): ...
    def __repr__(self) -> str: ...

class EulerRad:
    """Euler XYZ angles in radians."""
    x: float
    y: float
    z: float
    def __init__(self, x: float = 0.0, y: float = 0.0, z: float = 0.0) -> None: ...
    def __init__(self, seq: Sequence[float]) -> None: ...  # type: ignore[misc]
    def __len__(self) -> int: ...
    def __getitem__(self, i: int) -> float: ...
    def __setitem__(self, i: int, val: float) -> None: ...
    def __iter__(self): ...
    def __repr__(self) -> str: ...
    @staticmethod
    def zero() -> EulerRad: ...

class Color4b:
    """Color RGBA as bytes [0-255]."""
    r: int
    g: int
    b: int
    a: int
    def __init__(self, r: int = 0, g: int = 0, b: int = 0, a: int = 255) -> None: ...
    def __init__(self, seq: Sequence[int]) -> None: ...  # type: ignore[misc]
    def __getitem__(self, i: int) -> int: ...
    def __setitem__(self, i: int, val: int) -> None: ...
    def __iter__(self): ...

class AABB:
    """Axis-aligned bounding box."""
    min: Vector3
    max: Vector3
    def __init__(self) -> None: ...

class GroupBitSet128:
    def __init__(self, bits: set[int] = ...) -> None: ...
    @staticmethod
    def all_on() -> GroupBitSet128: ...
    @staticmethod
    def all_off() -> GroupBitSet128: ...
    def enable(self, index: int) -> None: ...
    def disable(self, index: int) -> None: ...
    def __getitem__(self, i: int) -> bool: ...
    def to_list(self) -> list[int]: ...
    def __repr__(self) -> str: ...
    def __eq__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...

class GroupBitSet256:
    def __init__(self, bits: set[int] = ...) -> None: ...
    @staticmethod
    def all_on() -> GroupBitSet256: ...
    @staticmethod
    def all_off() -> GroupBitSet256: ...
    def enable(self, index: int) -> None: ...
    def disable(self, index: int) -> None: ...
    def __getitem__(self, i: int) -> bool: ...
    def to_list(self) -> list[int]: ...
    def __repr__(self) -> str: ...
    def __eq__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...

class GroupBitSet1024:
    def __init__(self, bits: set[int] = ...) -> None: ...
    @staticmethod
    def all_on() -> GroupBitSet1024: ...
    @staticmethod
    def all_off() -> GroupBitSet1024: ...
    def enable(self, index: int) -> None: ...
    def disable(self, index: int) -> None: ...
    def __getitem__(self, i: int) -> bool: ...
    def to_list(self) -> list[int]: ...
    def __repr__(self) -> str: ...
    def __eq__(self, other: object) -> bool: ...
    def __ne__(self, other: object) -> bool: ...

# ---------------------------------------------------------------------------
# GameFile (mixin — methods added to all GameFile subclasses by bind_game_file)
# ---------------------------------------------------------------------------

class GameFile:
    """Base mixin for all GameFile subclasses. Methods are defined on each concrete class."""

    def __init__(self) -> None:
        """All GameFiles are default constructible."""
        ...

    @classmethod
    def from_path(cls, path: Union[str, Path]) -> Self:
        """Read from a file path, decompressing DCX if needed."""
        ...

    @classmethod
    def from_paths_parallel(
        cls,
        paths: Sequence[Union[str, Path]],
        max_threads: int = 0,
    ) -> list[Self]:
        """Read multiple files in parallel, decompressing DCX if needed."""
        ...

    @classmethod
    def from_bytes(cls, data: Union[bytes, bytearray, memoryview]) -> Self:
        """Parse from raw (already DCX-decompressed) bytes."""
        ...

    @classmethod
    def from_bytes_parallel(
        cls,
        buffers: list[Union[bytes, bytearray, memoryview]],
        max_threads: int = 0,
    ) -> list[Self]:
        """Parse multiple byte buffers in parallel."""
        ...

    def to_bytes(self) -> bytes:
        """Serialize to bytes, compressing with DCX if dcx_type is set."""
        ...

    def __bytes__(self) -> bytes:
        """Serialize to bytes, compressing with DCX if dcx_type is set.

        Enables use of `data = bytes(game_file)`.
        """
        ...

    def write_to_path(self, path: Union[str, Path, None] = None) -> Path:
        """Write to path (or stored path if None).

        Returns actual path written, which may have '.dcx' suffix added.
        """
        ...

    @property
    def dcx_type(self) -> DCXType: ...
    @dcx_type.setter
    def dcx_type(self, value: DCXType) -> None: ...

    @property
    def path(self) -> str | None:
        """Stored file path as a string."""
        ...
    @path.setter
    def path(self, value: Union[str, Path, None]) -> None: ...

    @property
    def path_name(self) -> str | None:
        """Filename portion of the stored path (e.g. 'c1000.chrbnd.dcx'), or None."""
        ...

    @property
    def path_stem(self) -> str | None:
        """Minimal stem with ALL suffixes removed (e.g. 'c1000.chrbnd.dcx' -> 'c1000'), or None."""
        ...


class GameType(IntEnum):
    """Game identifier."""
    DemonsSouls = 0
    DarkSoulsPTDE = 1
    DarkSoulsDSR = 2
    DarkSouls2 = 3
    DarkSouls2SOTFS = 4
    Bloodborne = 5
    DarkSouls3 = 6
    Sekiro = 7
    EldenRing = 8

# ---------------------------------------------------------------------------
# Binder
# ---------------------------------------------------------------------------

class BinderVersion(IntEnum):
    """Binder archive version."""
    V3 = 3
    V4 = 4

class BinderFlags:
    """Bit flags for a Binder archive header."""
    value: int
    @property
    def is_big_endian(self) -> bool: ...
    @property
    def has_ids(self) -> bool: ...
    @property
    def has_names(self) -> bool: ...
    @property
    def has_long_offsets(self) -> bool: ...
    @property
    def has_compression(self) -> bool: ...
    @property
    def entry_header_size(self) -> int: ...

class BinderVersion4Info:
    """Extra info for BND4 binders."""
    unknown1: bool
    unknown2: bool
    unicode: bool
    hash_table_type: int

class BinderEntry:
    """A single entry in a Binder archive."""
    entry_id: int
    path: str
    flags: int
    data: bytes
    @property
    def name(self) -> str:
        """Basename of the entry path."""
        ...
    @property
    def stem(self) -> str:
        """Minimal stem (before first '.') of the entry basename."""
        ...

    def get_uncompressed_data(self) -> bytes:
        """Decompress entry data if compression flag is set, or just return data."""
        ...

    def __repr__(self) -> str: ...

class BinderError(RuntimeError):
    """Error raised by Binder operations."""
    ...

class Binder(GameFile):
    """A FromSoftware BND3/BND4 multi-file archive."""
    version: BinderVersion
    signature: str
    flags: BinderFlags
    big_endian: bool
    bit_big_endian: bool
    @property
    def entries(self) -> list[BinderEntry]:
        """Live reference to the internal entry list (mutable)."""
        ...

    @staticmethod
    def from_split_bytes(
        bhd_data: Union[bytes, bytearray, memoryview],
        bdt_data: Union[bytes, bytearray, memoryview],
    ) -> Binder:
        """Parse a split BHF/BDT binder from raw bytes."""
        ...

    @property
    def entry_count(self) -> int: ...

    def find_entry_by_id(self, entry_id: int) -> BinderEntry:
        """Find entry by ID. Raises a KeyError if not found and a LookupError if multiple found."""
        ...

    def find_entry_by_path(self, path_string: str) -> BinderEntry:
        """Find entry by full path. Raises a KeyError if not found and a LookupError if multiple found."""
        ...

    def find_entry_by_path_regex(self, pattern: str | re.Pattern, full_match: bool = False) -> BinderEntry:
        """Find entry by regex on path. Raises a KeyError if not found and a LookupError if multiple found."""
        ...

    def find_entries_by_path_regex(self, pattern: str | re.Pattern, full_match: bool = False) -> list[BinderEntry]:
        """Find all entries that match regex on path."""
        ...

    def find_entry_by_name(self, name: str) -> BinderEntry:
        """Find entry by basename. Raises a KeyError if not found and a LookupError if multiple found."""
        ...

    def find_entry_by_name_regex(self, pattern: str | re.Pattern, full_match: bool = False) -> BinderEntry:
        """Find entry by regex on basename. Raises a KeyError if not found and a LookupError if multiple found."""
        ...

    def find_entries_by_name_regex(self, pattern: str | re.Pattern, full_match: bool = False) -> list[BinderEntry]:
        """Find all entries that match regex on basename."""
        ...

    def find_entry_by_filter(self, filter: Callable[[BinderEntry], bool]) -> BinderEntry:
        """Find entry by generic `BinderEntry` filter function. Raises a KeyError if not found."""
        ...

    def find_entries_by_filter(self, filter: Callable[[BinderEntry], bool]) -> list[BinderEntry]:
        """Find all entries that match generic `BinderEntry` filter function."""
        ...

    def __len__(self) -> int: ...
    def __repr__(self) -> str: ...


# ---------------------------------------------------------------------------
# DCX
# ---------------------------------------------------------------------------

class DCXType(IntEnum):
    """DCX compression type identifier."""
    Unknown = -1
    Null = 0
    Zlib = 1
    DCP_EDGE = 2
    DCP_DFLT = 3
    DCX_EDGE = 4
    DCX_DFLT_10000_24_9 = 5
    DCX_DFLT_10000_44_9 = 6
    DCX_DFLT_11000_44_8 = 7
    DCX_DFLT_11000_44_9 = 8
    DCX_DFLT_11000_44_9_15 = 9
    DCX_KRAK = 10
    DCX_ZSTD = 11

class DCXResult:
    """Result of DCX decompression."""
    @property
    def data(self) -> bytes: ...
    @property
    def type(self) -> DCXType: ...

class DCXError(RuntimeError): ...

def is_dcx(data: Union[bytes, bytearray, memoryview]) -> bool: ...
def detect_dcx(data: Union[bytes, bytearray, memoryview]) -> DCXType: ...
def decompress_dcx(data: Union[bytes, bytearray, memoryview]) -> DCXResult: ...
def compress_dcx(data: Union[bytes, bytearray, memoryview], dcx_type: DCXType) -> bytes: ...

# ---------------------------------------------------------------------------
# Oodle
# ---------------------------------------------------------------------------

def set_oodle_dll_path(directory: str) -> None: ...
def load_oodle_dll(directory: str) -> bool: ...
def is_oodle_available() -> bool: ...
def oodle_dll_path() -> str: ...

# ---------------------------------------------------------------------------
# DDS
# ---------------------------------------------------------------------------

class DXGIFormat(IntEnum):
    """DXGI pixel format identifiers (subset relevant to FromSoftware textures)."""
    UNKNOWN = 0
    R8G8B8A8_UNORM = 28
    R8G8B8A8_UNORM_SRGB = 29
    B8G8R8A8_UNORM = 87
    B8G8R8A8_UNORM_SRGB = 91
    BC1_UNORM = 71
    BC1_UNORM_SRGB = 72
    BC2_UNORM = 74
    BC2_UNORM_SRGB = 75
    BC3_UNORM = 77
    BC3_UNORM_SRGB = 78
    BC4_UNORM = 80
    BC4_SNORM = 81
    BC5_UNORM = 83
    BC5_SNORM = 84
    BC6H_UF16 = 95
    BC6H_SF16 = 96
    BC7_UNORM = 98
    BC7_UNORM_SRGB = 99
    R8_UNORM = 61
    R8G8_UNORM = 49
    R16_FLOAT = 54
    R16G16_FLOAT = 34
    R16G16B16A16_FLOAT = 10
    R32_FLOAT = 41
    R32G32B32A32_FLOAT = 2
    R10G10B10A2_UNORM = 24
    R11G11B10_FLOAT = 26
    R16G16B16A16_UNORM = 11
    R16G16_UNORM = 35
    R16_UNORM = 56
    A8_UNORM = 65
    B5G6R5_UNORM = 85
    B5G5R5A1_UNORM = 86
    B8G8R8X8_UNORM = 88
    B8G8R8X8_UNORM_SRGB = 93
    B4G4R4A4_UNORM = 115

class DDS:
    """DDS texture container.

    Wraps raw DDS bytes with conversion and PS4 de/swizzling methods.
    It is the caller's responsibility to track whether the pixel data is
    PS4-swizzled; converting or displaying swizzled data will look garbled.
    """

    def __init__(self, data: Union[bytes, bytearray, memoryview]) -> None:
        """Construct from raw DDS bytes."""
        ...

    @staticmethod
    def from_tga(data: Union[bytes, bytearray, memoryview], format: DXGIFormat, mip_levels: int = 1, force_dx10: bool = False) -> DDS:
        """Convert TGA image bytes to DDS with the given DXGI format.

        `mip_levels`: 1 (default) writes only the base image, 0 generates the full mip chain, N > 1 generates N levels
        (like `texconv -m`). `force_dx10` always writes the extended DX10 header (like `texconv -dx10`).

        On Windows, BC6H/BC7 compression uses GPU acceleration when available (base image only).
        """
        ...

    @staticmethod
    def from_png(data: Union[bytes, bytearray, memoryview], format: DXGIFormat, mip_levels: int = 1, force_dx10: bool = False) -> DDS:
        """Convert PNG image bytes to DDS with the given DXGI format.

        `mip_levels` and `force_dx10` behave as in `from_tga`.

        On Windows, BC6H/BC7 compression uses GPU acceleration when available (base image only).
        """
        ...

    @property
    def data(self) -> bytes:
        """Raw DDS bytes."""
        ...

    @property
    def size(self) -> int:
        """Size of the DDS data in bytes."""
        ...

    @property
    def is_empty(self) -> bool:
        """True if no DDS data is stored."""
        ...

    def to_tga(self) -> bytes:
        """Convert DDS to TGA bytes.

        PS4-swizzled data must be deswizzled first or the output will be garbled.
        """
        ...

    def to_png(self) -> bytes:
        """Convert DDS to PNG bytes.

        PS4-swizzled data must be deswizzled first or the output will be garbled.
        """
        ...

    def deswizzle_ps4(self) -> DDS:
        """Return a new DDS with PS4 tiled pixel data converted to linear row-major layout.

        Call this before to_tga() / to_png() on textures sourced from a PS4 game dump.
        """
        ...

    def swizzle_ps4(self) -> DDS:
        """Return a new DDS with linear pixel data converted to PS4 tiled layout.

        Call this before storing a DDS back into a PS4 TPF.
        """
        ...

    def __len__(self) -> int: ...
    def __bool__(self) -> bool: ...
    def __repr__(self) -> str: ...

# ---------------------------------------------------------------------------
# TPF
# ---------------------------------------------------------------------------

class TPFPlatform(IntEnum):
    """TPF target platform."""
    PC = 0
    Xbox360 = 1
    PS3 = 2
    PS4 = 4
    XboxOne = 5

class TextureType(IntEnum):
    """TPF texture type."""
    Texture = 0
    Cubemap = 1
    Volume = 2

class ConsoleInfo:
    """Extra info for console-targeted TPFs."""
    width: int
    height: int
    texture_count: int
    unk1: int
    unk2: int
    dxgi_format: int
    def __repr__(self) -> str: ...

class TPFTexture:
    """A single texture in a TPF archive."""
    stem: str
    format: int
    texture_type: TextureType
    mipmap_count: int
    texture_flags: int
    platform: TPFPlatform
    console_info: Optional[ConsoleInfo]
    data: bytes

    @property
    def has_dds_header(self) -> bool:
        """True if the texture data starts with a DDS header."""
        ...

    def to_dds(self) -> DDS:
        """Create a DDS object (with header added if required) from the data."""
        ...

    def __repr__(self) -> str:
        ...

class TPFError(RuntimeError): ...

class TPF(GameFile):
    """A FromSoftware TPF texture pack file."""
    platform: TPFPlatform
    tpf_flags: int
    encoding_type: int

    @property
    def textures(self) -> list[TPFTexture]:
        """Live reference to the internal texture list (mutable)."""
        ...

    @property
    def texture_count(self) -> int: ...

    def find_texture(self, stem: str) -> Optional[TPFTexture]:
        """Find texture by stem (case-insensitive). Returns None if not found."""
        ...

    def __len__(self) -> int: ...
    def __repr__(self) -> str: ...

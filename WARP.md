# WARP.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

## Project Overview

PCSX2 is a PlayStation 2 emulator written in C++20. It emulates the PS2's EE (Emotion Engine), IOP (I/O Processor), VU (Vector Units), GS (Graphics Synthesizer), SPU2, and peripheral hardware.

## Build Commands

### Windows (MSBuild - Recommended)
```powershell
# Build using Visual Studio solution (preferred for Windows development)
msbuild PCSX2_qt.sln /m /v:m /p:Configuration=Release /p:Platform=x64

# Debug build
msbuild PCSX2_qt.sln /m /v:m /p:Configuration=Debug /p:Platform=x64

# Devel build (RelWithDebInfo equivalent)
msbuild PCSX2_qt.sln /m /v:m /p:Configuration=Devel /p:Platform=x64
```

**Default build command:** `msbuild PCSX2_qt.sln /m /v:m /p:Configuration=Devel /p:Platform=x64`

### Windows (CMake)
```powershell
# Configure with Ninja (requires Visual Studio environment)
cmake . -B build -DCMAKE_PREFIX_PATH=deps -DQT_BUILD=ON -DCMAKE_BUILD_TYPE=Release -G Ninja

# Build
cmake --build build --config Release
```

### Linux (CMake)
```bash
# Configure
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-17 \
  -DCMAKE_CXX_COMPILER=clang++-17 \
  -DCMAKE_EXE_LINKER_FLAGS_INIT="-fuse-ld=lld"

# Build
ninja -C build
```

### CMake Presets (Linux/macOS with Clang)
```bash
cmake --preset clang-devel    # Development build (RelWithDebInfo)
cmake --preset clang-release  # Release build with LTO
cmake --preset clang-debug    # Debug build
cmake --build build
```

## Running Tests

```bash
# Build and run unit tests (CMake builds)
cmake --build build --target unittests

# Run tests directly after building
ctest --test-dir build --output-on-failure
```

Tests use GoogleTest and are located in `tests/ctest/`. Test targets are defined via the `add_pcsx2_test` macro.

## Key Build Options

| Option | Description | Default |
|--------|-------------|---------|
| `ENABLE_TESTS` | Build unit tests | ON |
| `ENABLE_QT_UI` | Build Qt frontend | ON |
| `ENABLE_GSRUNNER` | Build GSRunner tool | OFF |
| `USE_OPENGL` | OpenGL renderer | ON (not on macOS) |
| `USE_VULKAN` | Vulkan renderer | ON |
| `DISABLE_ADVANCE_SIMD` | Multi-ISA build (SSE4.1 baseline) | OFF |
| `USE_ASAN` | Address sanitizer | OFF |
| `LTO_PCSX2_CORE` | LTO for core only | OFF |

## Architecture Overview

### Core Emulation (`pcsx2/`)
- **EE (Emotion Engine)**: Main MIPS R5900 CPU - `R5900.cpp`, `R5900OpcodeImpl.cpp`
- **IOP**: I/O Processor (MIPS R3000A) - `R3000A.cpp`, `IopBios.cpp`
- **VU0/VU1**: Vector Units - `VU0.cpp`, `VU1micro.cpp`, `VUops.cpp`
- **Memory**: PS2 memory management - `Memory.cpp`, `vtlb.cpp`
- **DMA**: Direct Memory Access - `Dmac.cpp`, `Vif*.cpp`, `Sif*.cpp`

### Recompilers (`pcsx2/x86/`)
- **microVU**: VU recompiler - `microVU*.cpp`, `microVU*.inl`
- **iR5900**: EE recompiler - `iR5900*.cpp`
- **iR3000A**: IOP recompiler - `iR3000A.cpp`
- x86 code emitter in `common/emitter/`

### Graphics Synthesizer (`pcsx2/GS/`)
- **GSState**: Core GS state machine
- **Renderers**: `GS/Renderers/` - DX11, DX12, OpenGL, Vulkan, Metal, Software
- **GSVector**: SIMD vector classes for both x86 and ARM64

### Hardware Subsystems
- **CDVD** (`pcsx2/CDVD/`): CD/DVD drive emulation, ISO formats (CHD, CSO, GZ)
- **SPU2** (`pcsx2/SPU2/`): Sound Processing Unit
- **DEV9** (`pcsx2/DEV9/`): Network adapter, HDD emulation
- **USB** (`pcsx2/USB/`): USB device emulation (controllers, cameras, etc.)
- **SIO** (`pcsx2/SIO/`): Serial I/O - controllers, memory cards

### Frontend (`pcsx2-qt/`)
Qt6-based GUI. Main entry point is `QtHost.cpp`, window management in `MainWindow.cpp`.

### Common Library (`common/`)
Shared utilities: file system, string utils, threading, platform abstraction, HTTP downloading.

### VMManager
Central emulation coordinator in `VMManager.cpp`. Manages VM lifecycle, settings, save states.

## Code Style

- Uses `.clang-format` (Allman brace style, 4-space tabs, no column limit)
- Headers use `#pragma once`
- Precompiled headers: `PrecompiledHeader.h`
- Supported compilers: MSVC and Clang (GCC is unsupported)

## Third-Party Dependencies

Located in `3rdparty/`. Key dependencies:
- **Qt6**: UI framework
- **fmt**: String formatting
- **imgui**: In-game overlays
- **rapidyaml**: YAML parsing (GameDB)
- **rcheevos/rainterface**: RetroAchievements
- **libchdr/libzip/lzma**: Compression formats
- **vulkan/glad**: Graphics APIs
- **xbyak**: x86 JIT assembler
- **vixl**: ARM64 assembler (for Apple Silicon)

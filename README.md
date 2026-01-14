# PCSX2 RTX Remix

A fork of [PCSX2](https://github.com/PCSX2/pcsx2) with NVIDIA RTX Remix integration for path-traced graphics on PlayStation 2 games.

## Overview

This project adds a DirectX 9 hardware renderer to PCSX2 that is compatible with [NVIDIA RTX Remix](https://github.com/NVIDIAGameWorks/rtx-remix). RTX Remix intercepts D3D9 draw calls and replaces the rasterized output with fully path-traced rendering, enabling real-time ray tracing, global illumination, and modern material support for classic PS2 titles.

## Features

- **DirectX 9 Renderer**: Custom `GSDevice9` backend designed for RTX Remix compatibility
- **RTX Remix Integration**: Automatic interception of D3D9 calls for path-traced rendering
- **Texture Hashing**: Remix-compatible texture identification for asset replacement
- **Configurable Depth Reconstruction**: Per-game camera/projection settings via `RemixConfig`
- **Material Support**: Configurable terrain, skybox, water, UI, and light-emitting texture categories

## Requirements

- **Windows 10/11** (RTX Remix is Windows-only)
- **NVIDIA RTX GPU** (20-series or newer recommended)
- **RTX Remix Runtime**: Download from [NVIDIA RTX Remix](https://github.com/NVIDIAGameWorks/rtx-remix)
- **PS2 BIOS**: A BIOS dump from a legitimately-owned PS2 console

## Setup

1. Build the project (see Building section below)
2. Install RTX Remix runtime (place `d3d9.dll` and related files in the PCSX2 executable directory)
3. Launch a game - RTX Remix will automatically intercept rendering

## Configuration

### RTX Remix Settings

The `rtx.conf` file in the `bin/` directory contains Remix-specific settings including:
- Texture categories (UI, skybox, terrain, water, lights)
- Path tracing quality presets
- Material defaults (roughness, metallic)
- Scene scale and camera settings

### Per-Game Config

Game-specific Remix configurations are stored via `RemixConfig` and include:
- Near/far plane distances for depth reconstruction
- Field of view settings
- Custom texture categorization

## Building

### Windows (MSBuild)

```powershell
msbuild PCSX2_qt.sln /m /v:m /p:Configuration=Release /p:Platform=x64
```

### Windows (CMake)

```powershell
cmake . -B build -DCMAKE_PREFIX_PATH=deps -DQT_BUILD=ON -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build --config Release
```

## Directory Structure

- `pcsx2/GS/Renderers/DX9/` - DirectX 9 renderer implementation
- `pcsx2/GS/Renderers/Common/RemixConfig.*` - Per-game Remix configuration
- `bin/rtx.conf` - RTX Remix runtime configuration
- `rtx-remix/` - Remix runtime data (captures, mods, logs)

## Upstream

This project is based on [PCSX2](https://github.com/PCSX2/pcsx2), a free and open-source PlayStation 2 emulator. See the upstream repository for general emulator documentation and the [PCSX2 compatibility list](https://pcsx2.net/compat/) for game compatibility information.

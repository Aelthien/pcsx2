// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"
#include <vector>
#include <mutex>

// Captured raw vertex from VU1 input memory (before transformation)
struct VU1RawVertex
{
	float x, y, z, w;   // Position (object/model space)
	float nx, ny, nz;   // Normal (if detected)
	float s, t;         // Texture coordinates
	u32 color;          // Vertex color (RGBA packed)
	bool hasNormal;
	bool hasUV;
	bool hasColor;
};

// Snapshot of VU1 input data before transformation
struct VU1InputSnapshot
{
	std::vector<VU1RawVertex> vertices;
	float mvpMatrix[16];    // MVP matrix captured from VU1 memory
	u32 vertexStartAddr;    // Where vertex data starts in VU1 mem
	u32 vertexCount;
	u64 frameNumber;
	bool hasMVP;
	bool isValid;
};

// Captures VU1 input data (raw vertices + matrices) before VU1 transforms them
class VU1InputCapture
{
public:
	static VU1InputCapture& GetInstance();

	// Enable/disable capture
	void SetEnabled(bool enabled);
	bool IsEnabled() const { return m_enabled; }

	// Configuration for game-specific vertex layouts
	void SetVertexDataOffset(u32 offset) { m_vertexDataOffset = offset; }
	void SetMVPOffset(u32 offset) { m_mvpOffset = offset; }
	void SetVertexStride(u32 stride) { m_vertexStride = stride; } // In quadwords (16 bytes)

	// Called when VU1 is about to execute - captures VU1 memory snapshot
	// vuMem: pointer to VU1.Mem (16KB)
	// itop: ITOP register value (often points to vertex data)
	void OnVU1Execute(const u8* vuMem, u32 itop, u32 top);

	// Get the latest captured input snapshot
	bool GetLatestSnapshot(VU1InputSnapshot& outSnapshot);

	// Get and clear all snapshots
	std::vector<VU1InputSnapshot> GetAndClearSnapshots();

	// Frame boundary
	void OnFrameEnd();

	// Statistics
	u32 GetCaptureCount() const { return m_captureCount; }

private:
	VU1InputCapture() = default;

	bool m_enabled = false;
	std::mutex m_mutex;

	// Game-specific offsets (from JSON config)
	u32 m_vertexDataOffset = 0;    // Offset in VU1 mem where vertices start
	u32 m_mvpOffset = 0;           // Offset where MVP matrix is stored
	u32 m_vertexStride = 3;        // Quadwords per vertex (default: 3 = pos+normal+uv)

	// Captured snapshots
	std::vector<VU1InputSnapshot> m_snapshots;

	// Statistics
	u32 m_captureCount = 0;
	u64 m_frameNumber = 0;

	// Parse vertices from VU1 memory
	void ParseVertices(VU1InputSnapshot& snapshot, const u8* vuMem, u32 startAddr, u32 maxVerts);
	void ParseMVP(VU1InputSnapshot& snapshot, const u8* vuMem, u32 mvpAddr);
};

// Hook function - call from VU1 execution path
extern void VU1InputCapture_OnExecute(const u8* vuMem, u32 itop, u32 top);

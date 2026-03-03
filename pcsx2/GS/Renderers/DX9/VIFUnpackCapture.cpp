// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "VIFUnpackCapture.h"
#include <cstring>
#include <cstdio>
#include <cmath>

VU1InputCapture& VU1InputCapture::GetInstance()
{
	static VU1InputCapture instance;
	return instance;
}

void VU1InputCapture::SetEnabled(bool enabled)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_enabled = enabled;
	
	// FFX uses stride of 1 (position only per quadword)
	m_vertexStride = 1;
	
	if (!enabled)
	{
		m_snapshots.clear();
		m_captureCount = 0;
	}
}

void VU1InputCapture::OnVU1Execute(const u8* vuMem, u32 itop, u32 top)
{
	if (!m_enabled || !vuMem)
		return;

	std::lock_guard<std::mutex> lock(m_mutex);
	
	// Debug: log TOP values once
	static bool topLogged = false;
	static u32 topSamples[20];
	static int topSampleCount = 0;
	if (!topLogged && topSampleCount < 20)
	{
		topSamples[topSampleCount++] = top;
		if (topSampleCount == 20)
		{
			printf("VU1 TOP samples: ");
			for (int i = 0; i < 20; i++)
				printf("%u ", topSamples[i]);
			printf("\n");
			topLogged = true;
		}
	}

	VU1InputSnapshot snapshot = {};
	snapshot.frameNumber = m_frameNumber;
	snapshot.isValid = true;

	// FFX vertex data layout discovered via memory scan:
	// TOP=96 -> vertices at 0x0E80 (buffer 1)
	// TOP=560 -> vertices at 0x2B80 (buffer 2)
	// The TOP value points to metadata, not actual vertex data
	u32 vertexAddr;
	if (top == 96)
		vertexAddr = 0x0E80;  // First vertex buffer
	else if (top == 560)
		vertexAddr = 0x2B80;  // Second vertex buffer  
	else
		vertexAddr = 0x0E80;  // Default
	
	snapshot.vertexStartAddr = vertexAddr;
	
	// Debug disabled - we found the vertex addresses
	// TOP=96 -> vertices at 0x0E80, TOP=560 -> vertices at 0x2B80


	// Parse MVP matrix from VU1 memory
	if (m_mvpOffset > 0)
	{
		ParseMVP(snapshot, vuMem, m_mvpOffset);
	}

	// Parse vertices from VU1 memory
	// Estimate max vertices based on remaining memory
	u32 maxVerts = (0x4000 - vertexAddr) / (m_vertexStride * 16);
	if (maxVerts > 1024) maxVerts = 1024; // Reasonable limit

	ParseVertices(snapshot, vuMem, vertexAddr, maxVerts);

	if (!snapshot.vertices.empty())
	{
		m_snapshots.push_back(std::move(snapshot));
		m_captureCount++;
	}
}

void VU1InputCapture::ParseMVP(VU1InputSnapshot& snapshot, const u8* vuMem, u32 mvpAddr)
{
	// VU1 memory is 16KB (0x4000 bytes)
	if (mvpAddr + 64 > 0x4000)
		return;

	// MVP is stored as 4 rows of 4 floats (column-major typically on PS2)
	const float* matPtr = reinterpret_cast<const float*>(vuMem + mvpAddr);
	for (int i = 0; i < 16; i++)
	{
		snapshot.mvpMatrix[i] = matPtr[i];
	}
	snapshot.hasMVP = true;
}

void VU1InputCapture::ParseVertices(VU1InputSnapshot& snapshot, const u8* vuMem, u32 startAddr, u32 maxVerts)
{
	// Each vertex is m_vertexStride quadwords (default 3 = pos + normal + ST)
	// Quadword = 16 bytes = 4 floats
	u32 strideBytes = m_vertexStride * 16;

	for (u32 i = 0; i < maxVerts; i++)
	{
		u32 addr = startAddr + (i * strideBytes);
		if (addr + strideBytes > 0x4000)
			break;

		const float* vertData = reinterpret_cast<const float*>(vuMem + addr);

		// Basic validity check - positions shouldn't be NaN or extremely large
		if (std::isnan(vertData[0]) || std::isnan(vertData[1]) || std::isnan(vertData[2]))
			break;
		if (std::abs(vertData[0]) > 100000.0f || std::abs(vertData[1]) > 100000.0f || std::abs(vertData[2]) > 100000.0f)
			break;

	// FFX vertex data has W=1.0 for valid vertices
		// Stop when we hit data that doesn't look like a valid vertex
		// (W != 1.0 or all zeros)
		if (vertData[3] != 1.0f)
		{
			// Allow a few non-1.0 W values at the start (might be header)
			if (i > 5)
				break;
		}

		VU1RawVertex vert = {};

		// First quadword: position (XYZW)
		vert.x = vertData[0];
		vert.y = vertData[1];
		vert.z = vertData[2];
		vert.w = vertData[3];

		// Second quadword: normal (if stride >= 2)
		if (m_vertexStride >= 2)
		{
			const float* normData = reinterpret_cast<const float*>(vuMem + addr + 16);
			vert.nx = normData[0];
			vert.ny = normData[1];
			vert.nz = normData[2];
			vert.hasNormal = true;
		}

		// Third quadword: ST/UV coordinates (if stride >= 3)
		if (m_vertexStride >= 3)
		{
			const float* stData = reinterpret_cast<const float*>(vuMem + addr + 32);
			vert.s = stData[0];
			vert.t = stData[1];
			vert.hasUV = true;

			// Fourth float might be color (packed as u32 or float)
			// This varies by game
		}

		snapshot.vertices.push_back(vert);
	}

	snapshot.vertexCount = static_cast<u32>(snapshot.vertices.size());
}

bool VU1InputCapture::GetLatestSnapshot(VU1InputSnapshot& outSnapshot)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_snapshots.empty())
		return false;

	outSnapshot = m_snapshots.back();
	return true;
}

std::vector<VU1InputSnapshot> VU1InputCapture::GetAndClearSnapshots()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::vector<VU1InputSnapshot> result = std::move(m_snapshots);
	m_snapshots.clear();
	return result;
}

void VU1InputCapture::OnFrameEnd()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_frameNumber++;
	// Clear old snapshots at frame boundary
	m_snapshots.clear();
}

// Global hook function - called from VU1 execution path
void VU1InputCapture_OnExecute(const u8* vuMem, u32 itop, u32 top)
{
	VU1InputCapture::GetInstance().OnVU1Execute(vuMem, itop, top);
}

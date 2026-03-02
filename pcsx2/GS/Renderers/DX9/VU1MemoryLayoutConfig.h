// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

#include <string>
#include <unordered_map>

// Configuration for extracting data from VU1 memory for a specific game
struct VU1MemoryLayout
{
	std::string gameName;
	
	// MVP matrix location
	u32 mvpOffset = 0x00;
	bool mvpColumnMajor = false; // true = column-major (OpenGL style), false = row-major (DirectX style)
	
	// Scale vector (optional, some games store scale separately)
	u32 scaleOffset = 0x40;
	bool hasScale = false;
	
	// Bone matrices
	u32 boneStartOffset = 0x100;
	u32 boneStride = 0x30;        // 48 bytes for 4x3 matrix, 64 bytes for 4x4
	u32 maxBones = 64;
	bool boneColumnMajor = false;
	bool bone4x4 = false;         // true = 4x4 matrix, false = 4x3 matrix
	
	// Vertex data (for future use)
	u32 vertexDataOffset = 0x400;
	u32 vertexStride = 0x10;
	
	std::string notes;
};

// Manages per-game VU1 memory layout configurations
class VU1MemoryLayoutConfig
{
public:
	VU1MemoryLayoutConfig();
	~VU1MemoryLayoutConfig();
	
	// Initialize for a specific game (loads config from JSON)
	void Initialize(const std::string& gameSerial);
	void Shutdown();
	void Reload();
	
	// Get the current layout for the loaded game
	const VU1MemoryLayout& GetLayout() const { return m_currentLayout; }
	
	// Check if we have a custom layout for the current game
	bool HasCustomLayout() const { return m_hasCustomLayout; }
	
	// Get game serial
	const std::string& GetGameSerial() const { return m_gameSerial; }

private:
	bool LoadConfigFromJSON(const std::string& filepath);
	bool ParseGameLayout(const std::string& jsonContent, const std::string& gameSerial, VU1MemoryLayout& layout);
	bool ParseDefaultLayout(const std::string& jsonContent, VU1MemoryLayout& layout);
	u32 ParseHexValue(const std::string& value);
	std::string ExtractValue(const std::string& json, const std::string& key);
	
	std::string m_gameSerial;
	std::string m_configPath;
	VU1MemoryLayout m_currentLayout;
	VU1MemoryLayout m_defaultLayout;
	bool m_hasCustomLayout = false;
};

// Global instance
extern VU1MemoryLayoutConfig g_vu1LayoutConfig;

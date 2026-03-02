// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "VU1MemoryLayoutConfig.h"

#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/StringUtil.h"

#include "Config.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

VU1MemoryLayoutConfig g_vu1LayoutConfig;

VU1MemoryLayoutConfig::VU1MemoryLayoutConfig() = default;
VU1MemoryLayoutConfig::~VU1MemoryLayoutConfig() = default;

void VU1MemoryLayoutConfig::Initialize(const std::string& gameSerial)
{
	m_gameSerial = gameSerial;
	m_hasCustomLayout = false;
	
	// Reset to defaults
	m_defaultLayout = VU1MemoryLayout();
	m_currentLayout = m_defaultLayout;
	
	if (m_gameSerial.empty())
	{
		Console.WriteLn("[VU1Layout] No game serial, using default layout.");
		return;
	}
	
	// Look for the config file in the textures directory for this game
	// First try game-specific directory
	std::string gameDir = Path::Combine(EmuFolders::Textures, m_gameSerial);
	std::string configPath = Path::Combine(gameDir, "VU1MemoryLayout.json");
	
	if (FileSystem::FileExists(configPath.c_str()))
	{
		if (LoadConfigFromJSON(configPath))
		{
			Console.WriteLn("[VU1Layout] Loaded game-specific config from: %s", configPath.c_str());
			return;
		}
	}
	
	// Try the global config in the DX9 renderer directory (embedded resource path)
	// For now, try relative to textures folder
	configPath = Path::Combine(EmuFolders::Textures, "VU1MemoryLayouts.json");
	if (FileSystem::FileExists(configPath.c_str()))
	{
		if (LoadConfigFromJSON(configPath))
		{
			Console.WriteLn("[VU1Layout] Loaded layout from global config for %s", m_gameSerial.c_str());
			return;
		}
	}
	
	// Try user resources folder
	configPath = Path::Combine(EmuFolders::UserResources, "VU1MemoryLayouts.json");
	if (FileSystem::FileExists(configPath.c_str()))
	{
		if (LoadConfigFromJSON(configPath))
		{
			Console.WriteLn("[VU1Layout] Loaded layout from user resources for %s", m_gameSerial.c_str());
			return;
		}
	}
	
	// Try app resources folder
	configPath = Path::Combine(EmuFolders::Resources, "VU1MemoryLayouts.json");
	if (FileSystem::FileExists(configPath.c_str()))
	{
		if (LoadConfigFromJSON(configPath))
		{
			Console.WriteLn("[VU1Layout] Loaded layout from resources for %s", m_gameSerial.c_str());
			return;
		}
	}
	
	Console.WriteLn("[VU1Layout] No config found for %s, using defaults.", m_gameSerial.c_str());
}

void VU1MemoryLayoutConfig::Shutdown()
{
	m_gameSerial.clear();
	m_configPath.clear();
	m_currentLayout = VU1MemoryLayout();
	m_hasCustomLayout = false;
}

void VU1MemoryLayoutConfig::Reload()
{
	const std::string serial = m_gameSerial;
	Shutdown();
	Initialize(serial);
}

bool VU1MemoryLayoutConfig::LoadConfigFromJSON(const std::string& filepath)
{
	std::optional<std::string> content = FileSystem::ReadFileToString(filepath.c_str());
	if (!content.has_value())
	{
		Console.Warning("[VU1Layout] Failed to read config file: %s", filepath.c_str());
		return false;
	}
	
	m_configPath = filepath;
	const std::string& jsonContent = content.value();
	
	// First parse defaults
	ParseDefaultLayout(jsonContent, m_defaultLayout);
	m_currentLayout = m_defaultLayout;
	
	// Then try to find game-specific settings
	if (ParseGameLayout(jsonContent, m_gameSerial, m_currentLayout))
	{
		m_hasCustomLayout = true;
		Console.WriteLn("[VU1Layout] Found custom layout for %s: %s",
			m_gameSerial.c_str(), m_currentLayout.gameName.c_str());
		Console.WriteLn("[VU1Layout]   MVP offset: 0x%X, Bone offset: 0x%X, stride: 0x%X, max: %u",
			m_currentLayout.mvpOffset, m_currentLayout.boneStartOffset,
			m_currentLayout.boneStride, m_currentLayout.maxBones);
		return true;
	}
	
	// No game-specific config, but we loaded defaults
	Console.WriteLn("[VU1Layout] Using default layout for %s", m_gameSerial.c_str());
	return true;
}

u32 VU1MemoryLayoutConfig::ParseHexValue(const std::string& value)
{
	if (value.empty())
		return 0;
	
	std::string trimmed = value;
	// Remove quotes if present
	if (trimmed.front() == '"')
		trimmed = trimmed.substr(1);
	if (!trimmed.empty() && trimmed.back() == '"')
		trimmed.pop_back();
	
	// Handle 0x prefix
	if (trimmed.size() > 2 && trimmed[0] == '0' && (trimmed[1] == 'x' || trimmed[1] == 'X'))
	{
		return static_cast<u32>(std::strtoul(trimmed.c_str() + 2, nullptr, 16));
	}
	
	// Try as decimal
	return static_cast<u32>(std::strtoul(trimmed.c_str(), nullptr, 10));
}

std::string VU1MemoryLayoutConfig::ExtractValue(const std::string& json, const std::string& key)
{
	// Simple JSON value extraction (handles strings and numbers)
	std::string searchKey = "\"" + key + "\"";
	size_t keyPos = json.find(searchKey);
	if (keyPos == std::string::npos)
		return "";
	
	size_t colonPos = json.find(':', keyPos + searchKey.length());
	if (colonPos == std::string::npos)
		return "";
	
	// Skip whitespace after colon
	size_t valueStart = colonPos + 1;
	while (valueStart < json.length() && (json[valueStart] == ' ' || json[valueStart] == '\t' || json[valueStart] == '\n' || json[valueStart] == '\r'))
		valueStart++;
	
	if (valueStart >= json.length())
		return "";
	
	// Determine value type and extract
	if (json[valueStart] == '"')
	{
		// String value
		size_t valueEnd = json.find('"', valueStart + 1);
		if (valueEnd == std::string::npos)
			return "";
		return json.substr(valueStart + 1, valueEnd - valueStart - 1);
	}
	else
	{
		// Number or other value - find end (comma, }, or newline)
		size_t valueEnd = valueStart;
		while (valueEnd < json.length() && json[valueEnd] != ',' && json[valueEnd] != '}' && 
		       json[valueEnd] != '\n' && json[valueEnd] != '\r')
			valueEnd++;
		
		std::string result = json.substr(valueStart, valueEnd - valueStart);
		// Trim whitespace
		while (!result.empty() && (result.back() == ' ' || result.back() == '\t'))
			result.pop_back();
		return result;
	}
}

bool VU1MemoryLayoutConfig::ParseDefaultLayout(const std::string& jsonContent, VU1MemoryLayout& layout)
{
	// Find "defaults" section
	size_t defaultsPos = jsonContent.find("\"defaults\"");
	if (defaultsPos == std::string::npos)
		return false;
	
	size_t braceStart = jsonContent.find('{', defaultsPos);
	if (braceStart == std::string::npos)
		return false;
	
	// Find matching closing brace
	int braceCount = 1;
	size_t braceEnd = braceStart + 1;
	while (braceEnd < jsonContent.length() && braceCount > 0)
	{
		if (jsonContent[braceEnd] == '{')
			braceCount++;
		else if (jsonContent[braceEnd] == '}')
			braceCount--;
		braceEnd++;
	}
	
	std::string defaultsSection = jsonContent.substr(braceStart, braceEnd - braceStart);
	
	std::string value;
	
	value = ExtractValue(defaultsSection, "mvp_offset");
	if (!value.empty())
		layout.mvpOffset = ParseHexValue(value);
	
	value = ExtractValue(defaultsSection, "mvp_format");
	layout.mvpColumnMajor = (value.find("column_major") != std::string::npos);
	
	value = ExtractValue(defaultsSection, "bone_start_offset");
	if (!value.empty())
		layout.boneStartOffset = ParseHexValue(value);
	
	value = ExtractValue(defaultsSection, "bone_stride");
	if (!value.empty())
		layout.boneStride = ParseHexValue(value);
	
	value = ExtractValue(defaultsSection, "max_bones");
	if (!value.empty())
		layout.maxBones = ParseHexValue(value);
	
	value = ExtractValue(defaultsSection, "bone_format");
	layout.boneColumnMajor = (value.find("column_major") != std::string::npos);
	layout.bone4x4 = (value.find("4x4") != std::string::npos);
	
	value = ExtractValue(defaultsSection, "vertex_data_offset");
	if (!value.empty())
		layout.vertexDataOffset = ParseHexValue(value);
	
	value = ExtractValue(defaultsSection, "vertex_stride");
	if (!value.empty())
		layout.vertexStride = ParseHexValue(value);
	
	return true;
}

bool VU1MemoryLayoutConfig::ParseGameLayout(const std::string& jsonContent, const std::string& gameSerial, VU1MemoryLayout& layout)
{
	// Find "games" section
	size_t gamesPos = jsonContent.find("\"games\"");
	if (gamesPos == std::string::npos)
		return false;
	
	// Find the specific game serial
	std::string serialKey = "\"" + gameSerial + "\"";
	size_t serialPos = jsonContent.find(serialKey, gamesPos);
	if (serialPos == std::string::npos)
		return false;
	
	// Find the opening brace for this game's config
	size_t braceStart = jsonContent.find('{', serialPos);
	if (braceStart == std::string::npos)
		return false;
	
	// Find matching closing brace
	int braceCount = 1;
	size_t braceEnd = braceStart + 1;
	while (braceEnd < jsonContent.length() && braceCount > 0)
	{
		if (jsonContent[braceEnd] == '{')
			braceCount++;
		else if (jsonContent[braceEnd] == '}')
			braceCount--;
		braceEnd++;
	}
	
	std::string gameSection = jsonContent.substr(braceStart, braceEnd - braceStart);
	
	std::string value;
	
	value = ExtractValue(gameSection, "_name");
	if (!value.empty())
		layout.gameName = value;
	
	value = ExtractValue(gameSection, "mvp_offset");
	if (!value.empty())
		layout.mvpOffset = ParseHexValue(value);
	
	value = ExtractValue(gameSection, "mvp_format");
	if (!value.empty())
		layout.mvpColumnMajor = (value.find("column_major") != std::string::npos);
	
	value = ExtractValue(gameSection, "scale_offset");
	if (!value.empty())
	{
		layout.scaleOffset = ParseHexValue(value);
		layout.hasScale = true;
	}
	
	value = ExtractValue(gameSection, "bone_start_offset");
	if (!value.empty())
		layout.boneStartOffset = ParseHexValue(value);
	
	value = ExtractValue(gameSection, "bone_stride");
	if (!value.empty())
		layout.boneStride = ParseHexValue(value);
	
	value = ExtractValue(gameSection, "max_bones");
	if (!value.empty())
		layout.maxBones = ParseHexValue(value);
	
	value = ExtractValue(gameSection, "bone_format");
	if (!value.empty())
	{
		layout.boneColumnMajor = (value.find("column_major") != std::string::npos);
		layout.bone4x4 = (value.find("4x4") != std::string::npos);
	}
	
	value = ExtractValue(gameSection, "notes");
	if (!value.empty())
		layout.notes = value;
	
	return true;
}

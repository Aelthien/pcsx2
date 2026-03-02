// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "GS/Renderers/Common/GSVertex.h"
#include "GS/GSVector.h"

#include <string>
#include <unordered_map>
#include <vector>

/// Key for identifying a draw call that should be replaced.
/// Uses texture hash as the primary stable identifier.
struct GSMeshReplaceKey
{
	u32 texture_tbp0;    // Texture base pointer (most stable identifier)
	u32 texture_tw;      // Texture width log2
	u32 texture_th;      // Texture height log2
	u32 texture_psm;     // Texture pixel storage mode

	bool operator==(const GSMeshReplaceKey& other) const
	{
		return texture_tbp0 == other.texture_tbp0 &&
		       texture_tw == other.texture_tw &&
		       texture_th == other.texture_th &&
		       texture_psm == other.texture_psm;
	}
};

struct GSMeshReplaceKeyHash
{
	std::size_t operator()(const GSMeshReplaceKey& key) const
	{
		// Simple hash combining all fields
		std::size_t h = std::hash<u32>{}(key.texture_tbp0);
		h ^= std::hash<u32>{}(key.texture_tw) << 1;
		h ^= std::hash<u32>{}(key.texture_th) << 2;
		h ^= std::hash<u32>{}(key.texture_psm) << 3;
		return h;
	}
};

/// Stores replacement mesh data in GSVertex format ready for rendering.
struct GSMeshReplacement
{
	std::vector<GSVertex> vertices;
	std::vector<u16> indices;
	std::string source_file;  // For debugging/logging

	// Transform parameters to position the mesh correctly
	GSVector4 position_offset = GSVector4::zero();  // XYZ offset (in GS coordinates, <<4 for subpixel)
	GSVector4 position_scale = GSVector4(1.0f);     // Scale factor
	GSVector2 uv_offset = GSVector2(0.0f, 0.0f);    // UV offset
	GSVector2 uv_scale = GSVector2(1.0f, 1.0f);     // UV scale

	// Rendering options
	bool preserve_original_color = true;   // Use original draw's vertex color
	bool preserve_original_uv = false;     // Use original draw's UVs (useful for animated textures)
	u32 override_color = 0x80808080;       // RGBA color if not preserving original
};

/// Registry for mesh replacements.
/// Maps draw call identifiers to replacement meshes.
class GSMeshReplaceRegistry
{
public:
	GSMeshReplaceRegistry();
	~GSMeshReplaceRegistry();

	/// Initialize the registry, loading all replacements from the configured directory.
	void Initialize(const std::string& game_serial);

	/// Shutdown and clear all loaded replacements.
	void Shutdown();

	/// Reload all replacements (useful for hot-reloading during development).
	void Reload();

	/// Check if a replacement exists for the given key.
	bool HasReplacement(const GSMeshReplaceKey& key) const;

	/// Get the replacement for the given key, or nullptr if none exists.
	const GSMeshReplacement* GetReplacement(const GSMeshReplaceKey& key) const;

	/// Register a replacement manually (for programmatic use).
	void RegisterReplacement(const GSMeshReplaceKey& key, GSMeshReplacement&& replacement);

	/// Get the number of loaded replacements.
	size_t GetReplacementCount() const { return m_replacements.size(); }

	/// Check if the registry is enabled.
	bool IsEnabled() const { return m_enabled; }

	/// Enable/disable the registry.
	void SetEnabled(bool enabled) { m_enabled = enabled; }

	/// Dump current draw info for capture (helps identify what to replace).
	void DumpDrawInfo(const GSMeshReplaceKey& key, const GSVertex* vertices, u32 num_vertices,
	                  const u16* indices, u32 num_indices);

private:
	/// Load replacements from a directory.
	void LoadReplacementsFromDirectory(const std::string& directory);

	/// Load a single replacement from an OBJ file.
	bool LoadOBJReplacement(const std::string& filepath, const GSMeshReplaceKey& key);

	/// Load a replacement configuration file (.ini alongside .obj).
	bool LoadReplacementConfig(const std::string& filepath, GSMeshReplacement& replacement);

	/// Parse a mesh key from filename (format: TBP0_TW_TH_PSM.obj).
	static bool ParseKeyFromFilename(const std::string& filename, GSMeshReplaceKey& key);

	std::unordered_map<GSMeshReplaceKey, GSMeshReplacement, GSMeshReplaceKeyHash> m_replacements;
	std::string m_game_serial;
	std::string m_replacements_directory;
	bool m_enabled = false;
	bool m_dump_enabled = false;
};

/// Global mesh replacement registry instance.
extern GSMeshReplaceRegistry g_mesh_replace_registry;

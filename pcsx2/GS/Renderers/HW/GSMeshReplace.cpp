// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/StringUtil.h"

#include "Config.h"
#include "GS/Renderers/HW/GSMeshReplace.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string_view>

#define MESH_REPLACEMENT_SUBDIRECTORY_NAME "meshes"
#define MESH_DUMP_SUBDIRECTORY_NAME "mesh_dumps"

GSMeshReplaceRegistry g_mesh_replace_registry;

GSMeshReplaceRegistry::GSMeshReplaceRegistry() = default;
GSMeshReplaceRegistry::~GSMeshReplaceRegistry() = default;

void GSMeshReplaceRegistry::Initialize(const std::string& game_serial)
{
	m_game_serial = game_serial;
	m_replacements.clear();

	if (m_game_serial.empty())
	{
		Console.WriteLn("GSMeshReplace: No game serial, mesh replacement disabled.");
		return;
	}

	// Use the textures directory structure for meshes too
	m_replacements_directory = Path::Combine(EmuFolders::Textures, m_game_serial);
	m_replacements_directory = Path::Combine(m_replacements_directory, MESH_REPLACEMENT_SUBDIRECTORY_NAME);

	if (!FileSystem::DirectoryExists(m_replacements_directory.c_str()))
	{
		// Create the directory structure if it doesn't exist
		const std::string game_dir = Path::Combine(EmuFolders::Textures, m_game_serial);
		if (FileSystem::DirectoryExists(game_dir.c_str()) || FileSystem::CreateDirectoryPath(game_dir.c_str(), false))
		{
			FileSystem::EnsureDirectoryExists(m_replacements_directory.c_str(), false);
			FileSystem::EnsureDirectoryExists(Path::Combine(game_dir, MESH_DUMP_SUBDIRECTORY_NAME).c_str(), false);
		}
	}

	LoadReplacementsFromDirectory(m_replacements_directory);

	if (!m_replacements.empty())
	{
		m_enabled = true;
		Console.WriteLn("GSMeshReplace: Loaded %zu mesh replacement(s) for %s",
		                m_replacements.size(), m_game_serial.c_str());
	}
}

void GSMeshReplaceRegistry::Shutdown()
{
	m_replacements.clear();
	m_game_serial.clear();
	m_replacements_directory.clear();
	m_enabled = false;
}

void GSMeshReplaceRegistry::Reload()
{
	const std::string serial = m_game_serial;
	Shutdown();
	Initialize(serial);
}

bool GSMeshReplaceRegistry::HasReplacement(const GSMeshReplaceKey& key) const
{
	if (!m_enabled)
		return false;
	return m_replacements.find(key) != m_replacements.end();
}

const GSMeshReplacement* GSMeshReplaceRegistry::GetReplacement(const GSMeshReplaceKey& key) const
{
	if (!m_enabled)
		return nullptr;

	auto it = m_replacements.find(key);
	if (it != m_replacements.end())
		return &it->second;
	return nullptr;
}

void GSMeshReplaceRegistry::RegisterReplacement(const GSMeshReplaceKey& key, GSMeshReplacement&& replacement)
{
	m_replacements[key] = std::move(replacement);
	Console.WriteLn("GSMeshReplace: Registered replacement for TBP0=0x%04X TW=%u TH=%u PSM=%u (%zu verts, %zu indices)",
	                key.texture_tbp0, key.texture_tw, key.texture_th, key.texture_psm,
	                m_replacements[key].vertices.size(), m_replacements[key].indices.size());
}

void GSMeshReplaceRegistry::LoadReplacementsFromDirectory(const std::string& directory)
{
	if (!FileSystem::DirectoryExists(directory.c_str()))
		return;

	FileSystem::FindResultsArray files;
	if (!FileSystem::FindFiles(directory.c_str(), "*.obj", FILESYSTEM_FIND_FILES, &files))
		return;

	for (const FILESYSTEM_FIND_DATA& fd : files)
	{
		const std::string filename(Path::GetFileName(fd.FileName));
		GSMeshReplaceKey key;

		if (ParseKeyFromFilename(filename, key))
		{
			if (LoadOBJReplacement(fd.FileName, key))
			{
				Console.WriteLn("GSMeshReplace: Loaded %s", filename.c_str());
			}
			else
			{
				Console.Warning("GSMeshReplace: Failed to load %s", filename.c_str());
			}
		}
		else
		{
			Console.Warning("GSMeshReplace: Invalid filename format: %s (expected TBP0_TW_TH_PSM.obj)", filename.c_str());
		}
	}
}

bool GSMeshReplaceRegistry::ParseKeyFromFilename(const std::string& filename, GSMeshReplaceKey& key)
{
	// Expected format: TBP0_TW_TH_PSM.obj (all values in hex)
	// Example: 0x1234_5_6_00.obj or 1234_5_6_0.obj
	u32 tbp0, tw, th, psm;

	// Try with 0x prefix first
	if (std::sscanf(filename.c_str(), "0x%x_%u_%u_%x.obj", &tbp0, &tw, &th, &psm) == 4 ||
	    std::sscanf(filename.c_str(), "%x_%u_%u_%x.obj", &tbp0, &tw, &th, &psm) == 4)
	{
		key.texture_tbp0 = tbp0;
		key.texture_tw = tw;
		key.texture_th = th;
		key.texture_psm = psm;
		return true;
	}

	return false;
}

bool GSMeshReplaceRegistry::LoadOBJReplacement(const std::string& filepath, const GSMeshReplaceKey& key)
{
	std::ifstream file(filepath);
	if (!file.is_open())
		return false;

	GSMeshReplacement replacement;
	replacement.source_file = filepath;

	// Parse OBJ file
	std::vector<GSVector4> positions;  // x, y, z, w
	std::vector<GSVector2> texcoords;  // u, v
	std::vector<GSVector4> colors;     // r, g, b, a (from vertex colors if present)

	// Temporary face data
	struct FaceVertex
	{
		int pos_idx = -1;
		int tex_idx = -1;
	};

	std::string line;
	while (std::getline(file, line))
	{
		// Skip comments and empty lines
		if (line.empty() || line[0] == '#')
			continue;

		std::istringstream iss(line);
		std::string prefix;
		iss >> prefix;

		if (prefix == "v")
		{
			// Vertex position: v x y z [w]
			float x, y, z, w = 1.0f;
			iss >> x >> y >> z;
			if (!(iss >> w))
				w = 1.0f;
			positions.push_back(GSVector4(x, y, z, w));
		}
		else if (prefix == "vt")
		{
			// Texture coordinate: vt u v [w]
			float u, v;
			iss >> u >> v;
			texcoords.push_back(GSVector2(u, v));
		}
		else if (prefix == "f")
		{
			// Face: f v1/vt1 v2/vt2 v3/vt3 ...
			std::vector<FaceVertex> face_verts;
			std::string vertex_str;

			while (iss >> vertex_str)
			{
				FaceVertex fv;

				// Parse vertex index and optional texture index
				size_t slash1 = vertex_str.find('/');
				if (slash1 == std::string::npos)
				{
					// Just position index
					fv.pos_idx = std::stoi(vertex_str) - 1;  // OBJ is 1-indexed
				}
				else
				{
					fv.pos_idx = std::stoi(vertex_str.substr(0, slash1)) - 1;
					size_t slash2 = vertex_str.find('/', slash1 + 1);
					if (slash2 != slash1 + 1)  // Not empty between slashes
					{
						std::string tex_str = vertex_str.substr(slash1 + 1, slash2 - slash1 - 1);
						if (!tex_str.empty())
							fv.tex_idx = std::stoi(tex_str) - 1;
					}
				}

				face_verts.push_back(fv);
			}

			// Triangulate the face (fan triangulation)
			for (size_t i = 2; i < face_verts.size(); i++)
			{
				// Create triangle from vertices 0, i-1, i
				for (int vi : {0, static_cast<int>(i - 1), static_cast<int>(i)})
				{
					const FaceVertex& fv = face_verts[vi];

					GSVertex vertex = {};

					// Position - convert from OBJ space to GS space
					// GS uses fixed point: multiply by 16 for subpixel precision
					// Also apply any transforms from config
					if (fv.pos_idx >= 0 && fv.pos_idx < static_cast<int>(positions.size()))
					{
						const GSVector4& pos = positions[fv.pos_idx];
						// Scale and offset can be applied here or at render time
						// For now, store raw positions scaled to GS coordinates
						// Typical GS screen is 640x448, positions are in 12.4 fixed point
						vertex.XYZ.X = static_cast<u16>((pos.x * 16.0f) + 0.5f);
						vertex.XYZ.Y = static_cast<u16>((pos.y * 16.0f) + 0.5f);
						vertex.XYZ.Z = static_cast<u32>(pos.z * 65535.0f);  // Depth
					}

					// Texture coordinates - GS uses 12.4 fixed point for UV
					if (fv.tex_idx >= 0 && fv.tex_idx < static_cast<int>(texcoords.size()))
					{
						const GSVector2& tc = texcoords[fv.tex_idx];
						// UV in GS is typically in texture pixels, scaled by texture size
						// Store as 12.4 fixed point
						vertex.U = static_cast<u16>(tc.x * 16.0f);
						vertex.V = static_cast<u16>(tc.y * 16.0f);
					}

					// Default color (white, full alpha)
					vertex.RGBAQ.R = 128;
					vertex.RGBAQ.G = 128;
					vertex.RGBAQ.B = 128;
					vertex.RGBAQ.A = 128;
					vertex.RGBAQ.Q = 1.0f;

					// ST coordinates (floating point version)
					if (fv.tex_idx >= 0 && fv.tex_idx < static_cast<int>(texcoords.size()))
					{
						vertex.ST.S = texcoords[fv.tex_idx].x;
						vertex.ST.T = texcoords[fv.tex_idx].y;
					}

					// Add vertex and index
					replacement.indices.push_back(static_cast<u16>(replacement.vertices.size()));
					replacement.vertices.push_back(vertex);
				}
			}
		}
	}

	if (replacement.vertices.empty())
	{
		Console.Warning("GSMeshReplace: No vertices loaded from %s", filepath.c_str());
		return false;
	}

	// Try to load config file (.ini with same name)
	std::string config_path = Path::ReplaceExtension(filepath, "ini");
	if (FileSystem::FileExists(config_path.c_str()))
	{
		LoadReplacementConfig(config_path, replacement);
	}

	m_replacements[key] = std::move(replacement);
	return true;
}

bool GSMeshReplaceRegistry::LoadReplacementConfig(const std::string& filepath, GSMeshReplacement& replacement)
{
	std::ifstream file(filepath);
	if (!file.is_open())
		return false;

	std::string line;
	while (std::getline(file, line))
	{
		// Skip comments and empty lines
		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;

		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;

		std::string_view key_sv = StringUtil::StripWhitespace(line.substr(0, eq));
		std::string_view value_sv = StringUtil::StripWhitespace(line.substr(eq + 1));
		std::string key(key_sv);
		std::string value(value_sv);

		if (key == "position_offset_x")
			replacement.position_offset.x = std::stof(value);
		else if (key == "position_offset_y")
			replacement.position_offset.y = std::stof(value);
		else if (key == "position_offset_z")
			replacement.position_offset.z = std::stof(value);
		else if (key == "position_scale_x")
			replacement.position_scale.x = std::stof(value);
		else if (key == "position_scale_y")
			replacement.position_scale.y = std::stof(value);
		else if (key == "position_scale_z")
			replacement.position_scale.z = std::stof(value);
		else if (key == "uv_offset_u")
			replacement.uv_offset.x = std::stof(value);
		else if (key == "uv_offset_v")
			replacement.uv_offset.y = std::stof(value);
		else if (key == "uv_scale_u")
			replacement.uv_scale.x = std::stof(value);
		else if (key == "uv_scale_v")
			replacement.uv_scale.y = std::stof(value);
		else if (key == "preserve_original_color")
			replacement.preserve_original_color = (value == "true" || value == "1");
		else if (key == "preserve_original_uv")
			replacement.preserve_original_uv = (value == "true" || value == "1");
		else if (key == "override_color")
			replacement.override_color = static_cast<u32>(std::stoul(value, nullptr, 16));
	}

	return true;
}

void GSMeshReplaceRegistry::DumpDrawInfo(const GSMeshReplaceKey& key, const GSVertex* vertices, u32 num_vertices,
                                          const u16* indices, u32 num_indices)
{
	if (m_game_serial.empty())
		return;

	// Create dump directory if needed
	const std::string game_dir = Path::Combine(EmuFolders::Textures, m_game_serial);
	const std::string dump_dir = Path::Combine(game_dir, MESH_DUMP_SUBDIRECTORY_NAME);

	if (!FileSystem::DirectoryExists(dump_dir.c_str()))
	{
		if (!FileSystem::CreateDirectoryPath(game_dir.c_str(), false) ||
		    !FileSystem::EnsureDirectoryExists(dump_dir.c_str(), false))
		{
			return;
		}
	}

	// Generate filename from key
	std::string filename = StringUtil::StdStringFromFormat("0x%04X_%u_%u_%02X.obj",
	                                                        key.texture_tbp0, key.texture_tw,
	                                                        key.texture_th, key.texture_psm);
	std::string filepath = Path::Combine(dump_dir, filename);

	// Don't overwrite existing dumps
	if (FileSystem::FileExists(filepath.c_str()))
		return;

	std::ofstream file(filepath);
	if (!file.is_open())
		return;

	file << "# PCSX2 Mesh Dump\n";
	file << "# TBP0=0x" << std::hex << key.texture_tbp0 << std::dec << "\n";
	file << "# TW=" << key.texture_tw << " TH=" << key.texture_th << "\n";
	file << "# PSM=" << key.texture_psm << "\n";
	file << "# Vertices: " << num_vertices << " Indices: " << num_indices << "\n\n";

	// Write vertices
	for (u32 i = 0; i < num_vertices; i++)
	{
		const GSVertex& v = vertices[i];
		// Convert from GS fixed point back to float
		float x = static_cast<float>(v.XYZ.X) / 16.0f;
		float y = static_cast<float>(v.XYZ.Y) / 16.0f;
		float z = static_cast<float>(v.XYZ.Z) / 65535.0f;
		file << "v " << x << " " << y << " " << z << "\n";
	}

	file << "\n";

	// Write texture coordinates
	for (u32 i = 0; i < num_vertices; i++)
	{
		const GSVertex& v = vertices[i];
		float u = v.ST.S;
		float vt = v.ST.T;
		file << "vt " << u << " " << vt << "\n";
	}

	file << "\n";

	// Write faces (triangles)
	for (u32 i = 0; i + 2 < num_indices; i += 3)
	{
		// OBJ is 1-indexed
		int i0 = indices[i] + 1;
		int i1 = indices[i + 1] + 1;
		int i2 = indices[i + 2] + 1;
		file << "f " << i0 << "/" << i0 << " "
		     << i1 << "/" << i1 << " "
		     << i2 << "/" << i2 << "\n";
	}

	Console.WriteLn("GSMeshReplace: Dumped mesh to %s", filepath.c_str());
}

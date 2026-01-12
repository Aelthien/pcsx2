// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "RemixConfig.h"

#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Path.h"

#include "Config.h"
#include "INISettingsInterface.h"

#include <fmt/format.h>

std::string RemixConfig::GetConfigPath(const std::string_view serial, u32 crc)
{
	std::string sanitized_serial(Path::SanitizeFileName(serial));

	if (serial.empty())
		return Path::Combine(EmuFolders::GameSettings, fmt::format("remix_{:08X}.ini", crc));
	else
		return Path::Combine(EmuFolders::GameSettings, fmt::format("remix_{}_{:08X}.ini", sanitized_serial, crc));
}

RemixConfig RemixConfig::Load(const std::string_view serial, u32 crc)
{
	RemixConfig config;

	if (crc == 0)
		return config;

	const std::string path = GetConfigPath(serial, crc);
	if (!FileSystem::FileExists(path.c_str()))
	{
		DevCon.WriteLn("Remix: No config found at '%s', using defaults", path.c_str());
		return config;
	}

	INISettingsInterface si(path);
	if (!si.Load())
	{
		Console.Warning("Remix: Failed to load config from '%s'", path.c_str());
		return config;
	}

	Console.WriteLn("Remix: Loading config from '%s'", path.c_str());

	config.NearPlane = si.GetFloatValue("Remix", "NearPlane", config.NearPlane);
	config.FarPlane = si.GetFloatValue("Remix", "FarPlane", config.FarPlane);
	config.FOV = si.GetFloatValue("Remix", "FOV", config.FOV);

	return config;
}

bool RemixConfig::Save(const std::string_view serial, u32 crc) const
{
	if (crc == 0)
		return false;

	const std::string path = GetConfigPath(serial, crc);
	Console.WriteLn("Remix: Saving config to '%s'", path.c_str());

	INISettingsInterface si(path);

	si.SetFloatValue("Remix", "NearPlane", NearPlane);
	si.SetFloatValue("Remix", "FarPlane", FarPlane);
	si.SetFloatValue("Remix", "FOV", FOV);

	return si.Save();
}

float RemixConfig::ReconstructLinearZ(float normalizedDepth) const
{
	// Inverse perspective depth: z_eye = (near * far) / (far - d * (far - near))
	const float range = FarPlane - NearPlane;
	return (NearPlane * FarPlane) / (FarPlane - normalizedDepth * range);
}

// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Types.h"

#include <string>
#include <string_view>

struct RemixConfig
{
	// Camera/projection settings for depth reconstruction
	float NearPlane = 1.0f;
	float FarPlane = 10000.0f;
	float FOV = 60.0f; // Vertical FOV in degrees

	// Returns the path to the Remix config file for the given game
	static std::string GetConfigPath(const std::string_view serial, u32 crc);

	// Loads the Remix config for the given game, returns defaults if not found
	static RemixConfig Load(const std::string_view serial, u32 crc);

	// Saves the current config to disk
	bool Save(const std::string_view serial, u32 crc) const;

	// Reconstructs linear eye-space Z from a normalized depth buffer value
	float ReconstructLinearZ(float normalizedDepth) const;
};

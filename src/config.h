/**
 * ======================================================
 * Accelerator local
 * Written by komashchenko (Phoenix) 2023-2026, Asher Baker (asherkin) 2011.
 * ======================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 */

#pragma once

#include <filesystem>
#include <string>

namespace acceleratorlocal
{
	struct Config
	{
		bool bGenerateSymbols = true;
		int nProcessorTimeoutSeconds = 30;
		bool bIncludeCommandLine = true;
		bool bIncludeConsoleHistory = true;
		int nConsoleStackFrames = 15;
		int nDumpRetentionDays = 0;
	};

	struct GameVersion
	{
		std::string strPatchVersion = "unknown";
		std::string strServerVersion = "unknown";
	};

	bool LoadConfig(const std::filesystem::path& pathConfig, Config& config, std::string& strError);
	GameVersion ReadGameVersion(const std::filesystem::path& pathGame);
} // namespace acceleratorlocal

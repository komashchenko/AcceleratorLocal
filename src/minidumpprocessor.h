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

#include "config.h"

#include <filesystem>

namespace acceleratorlocal
{
	bool ProcessMinidump(const char* pszDumpPath, const std::filesystem::path& pathSymbols, const Config& config);
	void RemoveOldDumps(const std::filesystem::path& pathDumps, int nRetentionDays);
} // namespace acceleratorlocal

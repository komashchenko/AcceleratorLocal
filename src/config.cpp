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

#include "config.h"

#include <charconv>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>

namespace acceleratorlocal
{
	namespace
	{
		std::string_view Trim(std::string_view strValue)
		{
			auto nFirst = strValue.find_first_not_of(" \t\r\n");
			if (nFirst == std::string_view::npos)
				return {};

			auto nLast = strValue.find_last_not_of(" \t\r\n");

			return strValue.substr(nFirst, nLast - nFirst + 1);
		}

		bool ParseIni(const std::filesystem::path& pathConfig, std::unordered_map<std::string, std::string>& mapValues, bool bStrict, std::string& strError)
		{
			std::ifstream fileInput(pathConfig);
			if (!fileInput)
			{
				strError = "Cannot read " + pathConfig.string();
				return false;
			}

			std::string strLine;
			std::size_t nLineNumber = 0;
			while (std::getline(fileInput, strLine))
			{
				nLineNumber++;
				std::string_view strEntry = Trim(strLine);
				if (strEntry.empty() || strEntry.front() == ';' || strEntry.front() == '#')
					continue;

				auto nSeparator = strEntry.find('=');
				std::string_view strKey = Trim(strEntry.substr(0, nSeparator));
				if (nSeparator == std::string_view::npos || strKey.empty())
				{
					if (!bStrict)
						continue;

					strError = pathConfig.string() + ": invalid entry on line " + std::to_string(nLineNumber) + " (expected key=value)";
					return false;
				}

				mapValues[std::string(strKey)] = Trim(strEntry.substr(nSeparator + 1));
			}

			if (fileInput.bad())
			{
				strError = "Failed to read " + pathConfig.string();
				return false;
			}

			return true;
		}

		bool ParseBoolean(std::string_view strValue, bool& bResult)
		{
			if (strValue != "true" && strValue != "false")
				return false;

			bResult = strValue == "true";

			return true;
		}

		bool ParseInteger(std::string_view strValue, int& nResult, int nMinimum)
		{
			int nParsed = 0;
			auto conversion = std::from_chars(strValue.data(), strValue.data() + strValue.size(), nParsed);
			if (conversion.ec != std::errc{} || conversion.ptr != strValue.data() + strValue.size() || nParsed < nMinimum)
				return false;

			nResult = nParsed;

			return true;
		}
	} // namespace

	bool LoadConfig(const std::filesystem::path& pathConfig, Config& config, std::string& strError)
	{
		Config loadedConfig;
		std::error_code ecError;
		bool bExists = std::filesystem::exists(pathConfig, ecError);
		if (ecError)
		{
			strError = "Cannot access " + pathConfig.string() + ": " + ecError.message();
			return false;
		}

		if (!bExists)
		{
			config = loadedConfig;
			return true;
		}

		std::unordered_map<std::string, std::string> mapValues;
		if (!ParseIni(pathConfig, mapValues, true, strError))
			return false;

		for (const auto& [strKey, strValue] : mapValues)
		{
			bool bValid;
			std::string_view strExpected = "true or false";
			if (strKey == "GenerateSymbols")
			{
				bValid = ParseBoolean(strValue, loadedConfig.bGenerateSymbols);
			}
			else if (strKey == "IncludeCommandLine")
			{
				bValid = ParseBoolean(strValue, loadedConfig.bIncludeCommandLine);
			}
			else if (strKey == "IncludeConsoleHistory")
			{
				bValid = ParseBoolean(strValue, loadedConfig.bIncludeConsoleHistory);
			}
			else if (strKey == "ProcessorTimeoutSeconds")
			{
				bValid = ParseInteger(strValue, loadedConfig.nProcessorTimeoutSeconds, 5);
				strExpected = "an integer >= 5";
			}
			else if (strKey == "ConsoleStackFrames")
			{
				bValid = ParseInteger(strValue, loadedConfig.nConsoleStackFrames, 0);
				strExpected = "a nonnegative integer";
			}
			else if (strKey == "DumpRetentionDays")
			{
				bValid = ParseInteger(strValue, loadedConfig.nDumpRetentionDays, 0);
				strExpected = "a nonnegative integer";
			}
			else
			{
				strError = pathConfig.string() + ": unknown option '" + strKey + "'";
				return false;
			}

			if (!bValid)
			{
				strError = pathConfig.string() + ": invalid value for '" + strKey + "' (expected " + std::string(strExpected) + ")";
				return false;
			}
		}

		config = loadedConfig;

		return true;
	}

	GameVersion ReadGameVersion(const std::filesystem::path& pathGame)
	{
		GameVersion gameVersion;
		std::unordered_map<std::string, std::string> mapValues;
		std::string strError;
		if (!ParseIni(pathGame / "steam.inf", mapValues, false, strError))
			return gameVersion;

		if (auto itEntry = mapValues.find("PatchVersion"); itEntry != mapValues.end())
			gameVersion.strPatchVersion = itEntry->second;

		if (auto itEntry = mapValues.find("ServerVersion"); itEntry != mapValues.end())
			gameVersion.strServerVersion = itEntry->second;

		return gameVersion;
	}
} // namespace acceleratorlocal

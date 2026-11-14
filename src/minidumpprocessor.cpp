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

#include "minidumpprocessor.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <system_error>

#include <elf.h>
#include <unistd.h>

#include "common/linux/dump_symbols.h"
#include "common/module.h"
#include "google_breakpad/processor/basic_source_line_resolver.h"
#include "google_breakpad/processor/call_stack.h"
#include "google_breakpad/processor/code_module.h"
#include "google_breakpad/processor/minidump.h"
#include "google_breakpad/processor/minidump_processor.h"
#include "google_breakpad/processor/process_state.h"
#include "google_breakpad/processor/stack_frame.h"
#include "processor/pathname_stripper.h"
#include "processor/simple_symbol_supplier.h"
#include "processor/stackwalk_common.h"

namespace acceleratorlocal
{
	static bool RegularFileExists(const std::filesystem::path& pathFile);
	static std::optional<std::filesystem::path> GetSymbolPathForModule(const google_breakpad::CodeModule& module, const std::filesystem::path& pathSymbols);
	static bool GenerateSymbolFileForModule(const google_breakpad::CodeModule& module, const std::filesystem::path& pathSymbol);
	static void PrintStackFrame(std::FILE* pConsole, const google_breakpad::StackFrame& frame, std::size_t iFrame);

	namespace
	{
		class CSymbolSupplier final : public google_breakpad::SimpleSymbolSupplier
		{
		public:
			CSymbolSupplier(const std::filesystem::path& pathSymbols, google_breakpad::Minidump& miniDump, bool bGenerateSymbols) :
				SimpleSymbolSupplier(pathSymbols.string()), m_pathSymbols(pathSymbols), m_pModules(miniDump.GetModuleList()), m_bGenerateSymbols(bGenerateSymbols)
			{
			}

			using SimpleSymbolSupplier::GetSymbolFile;

			SymbolResult GetSymbolFile(const google_breakpad::CodeModule* pModule, const google_breakpad::SystemInfo* pSystemInfo, std::string* pSymbolFile) override
			{
				pSymbolFile->clear();
				auto pathSymbol = GetSymbolPathForModule(*pModule, m_pathSymbols);
				if (!pathSymbol)
					return NOT_FOUND;

				if (!RegularFileExists(*pathSymbol) && (!m_bGenerateSymbols || !m_AttemptedSymbols.insert(*pathSymbol).second || !GenerateSymbols(*pModule, *pathSymbol)))
					return NOT_FOUND;

				return SimpleSymbolSupplier::GetSymbolFile(pModule, pSystemInfo, pSymbolFile);
			}

		private:
			bool GenerateSymbols(const google_breakpad::CodeModule& module, const std::filesystem::path& pathSymbol)
			{
				if (GenerateSymbolFileForModule(module, pathSymbol))
					return true;

				if (!m_pModules)
					return false;

				// A duplicate mapping can have an unusable code path but the same symbols.
				std::set<std::string> attemptedCodeFiles{module.code_file()};
				unsigned int nModuleCount = m_pModules->module_count();
				for (unsigned int iModule = 0; iModule < nModuleCount; iModule++)
				{
					const auto* pCandidate = m_pModules->GetModuleAtIndex(iModule);
					if (GetSymbolPathForModule(*pCandidate, m_pathSymbols) != pathSymbol || !attemptedCodeFiles.insert(pCandidate->code_file()).second)
						continue;

					if (GenerateSymbolFileForModule(*pCandidate, pathSymbol))
						return true;
				}

				return false;
			}

			std::filesystem::path m_pathSymbols;
			const google_breakpad::MinidumpModuleList* m_pModules;
			bool m_bGenerateSymbols;
			std::set<std::filesystem::path> m_AttemptedSymbols;
		};
	} // namespace

	bool ProcessMinidump(const char* pszDumpPath, const std::filesystem::path& pathSymbols, const Config& config)
	{
		// Increase the maximum number of threads and regions.
		google_breakpad::MinidumpThreadList::set_max_threads(std::numeric_limits<std::uint32_t>::max());
		google_breakpad::MinidumpMemoryList::set_max_regions(std::numeric_limits<std::uint32_t>::max());

		google_breakpad::Minidump miniDump(pszDumpPath);
		if (!miniDump.Read())
		{
			std::fprintf(stderr, "Failed to read minidump\n");
			return false;
		}

		CSymbolSupplier symbolSupplier(pathSymbols, miniDump, config.bGenerateSymbols);
		google_breakpad::BasicSourceLineResolver resolver;
		google_breakpad::MinidumpProcessor minidumpProcessor(&symbolSupplier, &resolver);

		google_breakpad::ProcessState processState;
		auto result = minidumpProcessor.Process(&miniDump, &processState);
		if (result != google_breakpad::PROCESS_OK)
		{
			std::fprintf(stderr, "MinidumpProcessor::Process failed\n");
			return false;
		}

		std::unique_ptr<std::FILE, decltype(&std::fclose)> pConsole(nullptr, std::fclose);
		if (config.nConsoleStackFrames > 0)
		{
			int nConsole = dup(STDOUT_FILENO);
			if (nConsole != -1)
			{
				pConsole.reset(fdopen(nConsole, "w"));
				if (!pConsole)
					close(nConsole);
			}
		}

		std::filesystem::path pathReport(pszDumpPath);
		pathReport += ".txt";
		bool bReportWritten = false;
		if (std::freopen(pathReport.c_str(), "a", stdout))
		{
			PrintProcessState(processState, true, false, false, -1, &resolver);
			int nFlushResult = std::fflush(stdout);
			bReportWritten = nFlushResult == 0 && !std::ferror(stdout);
		}

		if (pConsole)
		{
			std::fprintf(pConsole.get(), "\n");
			int iRequestingThread = processState.requesting_thread();
			if (iRequestingThread == -1)
				iRequestingThread = 0;

			const auto& vecFrames = *processState.threads()->at(iRequestingThread)->frames();
			std::size_t nFrameCount = std::min(vecFrames.size(), static_cast<std::size_t>(config.nConsoleStackFrames));
			for (std::size_t iFrame = 0; iFrame < nFrameCount; iFrame++)
				PrintStackFrame(pConsole.get(), *vecFrames[iFrame], iFrame);

			std::fflush(pConsole.get());
		}

		return bReportWritten;
	}

	void RemoveOldDumps(const std::filesystem::path& pathDumps, int nRetentionDays)
	{
		if (nRetentionDays <= 0)
			return;

		auto now = std::filesystem::file_time_type::clock::now();
		std::error_code ecIteration;
		std::filesystem::directory_iterator itFile(pathDumps, ecIteration);
		for (; !ecIteration && itFile != std::filesystem::directory_iterator{}; itFile.increment(ecIteration))
		{
			const auto& entry = *itFile;
			if (entry.path().extension() != ".dmp")
				continue;

			std::error_code ecError;
			auto status = entry.symlink_status(ecError);
			if (ecError || !std::filesystem::is_regular_file(status))
				continue;

			auto modified = entry.last_write_time(ecError);
			if (ecError)
				continue;

			auto nAgeDays = std::chrono::duration_cast<std::chrono::days>(now - modified).count();
			if (nAgeDays < nRetentionDays)
				continue;

			std::filesystem::path pathReport = entry.path();
			pathReport += ".txt";
			auto reportStatus = std::filesystem::symlink_status(pathReport, ecError);
			if (ecError && ecError != std::errc::no_such_file_or_directory)
			{
				std::fprintf(stderr, "Cannot inspect %s: %s\n", pathReport.c_str(), ecError.message().c_str());
				continue;
			}

			if (std::filesystem::is_regular_file(reportStatus))
			{
				std::filesystem::remove(pathReport, ecError);
				if (ecError)
				{
					std::fprintf(stderr, "Cannot remove %s: %s\n", pathReport.c_str(), ecError.message().c_str());
					continue;
				}
			}

			std::filesystem::remove(entry.path(), ecError);
			if (ecError)
				std::fprintf(stderr, "Cannot remove %s: %s\n", entry.path().c_str(), ecError.message().c_str());
		}

		if (ecIteration)
			std::fprintf(stderr, "Cannot scan %s: %s\n", pathDumps.c_str(), ecIteration.message().c_str());
	}

	static bool RegularFileExists(const std::filesystem::path& pathFile)
	{
		std::error_code ecError;
		if (!std::filesystem::is_regular_file(pathFile, ecError))
			return false;

		auto nFileSize = std::filesystem::file_size(pathFile, ecError);

		return !ecError && nFileSize > 0;
	}

	static bool IsElfFile(const std::filesystem::path& pathFile)
	{
		std::ifstream fileInput(pathFile, std::ios::binary);
		std::array<char, SELFMAG> szMagic{};

		return fileInput.read(szMagic.data(), szMagic.size()) && std::memcmp(szMagic.data(), ELFMAG, szMagic.size()) == 0;
	}

	static std::optional<std::filesystem::path> GetSymbolPathForModule(const google_breakpad::CodeModule& module, const std::filesystem::path& pathSymbols)
	{
		std::string strDebugFile = google_breakpad::PathnameStripper::File(module.debug_file());
		std::string strDebugIdentifier = module.debug_identifier();
		if (strDebugFile.empty() || strDebugIdentifier.empty())
			return std::nullopt;

		return pathSymbols / strDebugFile / strDebugIdentifier / (strDebugFile + ".sym");
	}

	static bool GenerateSymbolFileForModule(const google_breakpad::CodeModule& module, const std::filesystem::path& pathSymbol)
	{
		std::string strCodeFile = module.code_file();
		if (strCodeFile.empty() || strCodeFile[0] != '/')
			return false;

		if (!IsElfFile(strCodeFile))
			return false;

		google_breakpad::DumpOptions options(ALL_SYMBOL_DATA, true, false, false);
		google_breakpad::Module* pModule;
		bool bReadSymbols = google_breakpad::ReadSymbolData(strCodeFile, strCodeFile, "Linux", {}, {}, options, &pModule);
		if (!bReadSymbols)
			return false;

		std::unique_ptr<google_breakpad::Module> pSymbols(pModule);
		if (pSymbols->identifier() != module.debug_identifier())
			return false;

		std::error_code ecDirectory;
		std::filesystem::create_directories(pathSymbol.parent_path(), ecDirectory);
		if (ecDirectory)
			return false;

		std::filesystem::path pathTemporarySymbol = pathSymbol;
		pathTemporarySymbol += ".tmp";

		std::error_code ecIgnored;
		std::filesystem::remove(pathTemporarySymbol, ecIgnored);

		std::ofstream outputStream(pathTemporarySymbol, std::ios::binary | std::ios::trunc);
		if (!outputStream)
			return false;

		bool bWroteSymbols = pSymbols->Write(outputStream, options.symbol_data, options.preserve_load_address);
		outputStream.close();
		if (!bWroteSymbols || !outputStream || !RegularFileExists(pathTemporarySymbol))
		{
			std::filesystem::remove(pathTemporarySymbol, ecIgnored);
			return false;
		}

		std::error_code ecRename;
		std::filesystem::rename(pathTemporarySymbol, pathSymbol, ecRename);
		if (ecRename)
		{
			std::filesystem::remove(pathTemporarySymbol, ecIgnored);
			return false;
		}

		return true;
	}

	static void PrintStackFrame(std::FILE* pConsole, const google_breakpad::StackFrame& frame, std::size_t iFrame)
	{
		std::uint64_t nInstructionAddress = frame.ReturnAddress();
		std::fprintf(pConsole, "%2zu  ", iFrame);

		if (!frame.module)
		{
			std::fprintf(pConsole, "0x%" PRIx64 "\n", nInstructionAddress);
			return;
		}

		std::string strModuleFile = google_breakpad::PathnameStripper::File(frame.module->code_file());
		std::uint64_t nModuleOffset = nInstructionAddress - frame.module->base_address();

		std::fprintf(pConsole, "%s + 0x%" PRIx64, strModuleFile.c_str(), nModuleOffset);
		if (frame.function_name.empty())
		{
			std::fprintf(pConsole, "\n");
			return;
		}

		std::fprintf(pConsole, " <%s + 0x%" PRIx64 ">", frame.function_name.c_str(), nInstructionAddress - frame.function_base);
		if (!frame.source_file_name.empty())
		{
			std::string strSourceFile = google_breakpad::PathnameStripper::File(frame.source_file_name);
			std::fprintf(pConsole, " [%s : %d + 0x%" PRIx64 "]", strSourceFile.c_str(), frame.source_line, nInstructionAddress - frame.source_line_base);
		}

		std::fprintf(pConsole, "\n");
	}
} // namespace acceleratorlocal

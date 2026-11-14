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

#include "crashhandler.h"

#include "config.h"
#include "minidumpprocessor.h"

#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
#include <system_error>

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "client/linux/handler/exception_handler.h"
#include "client/linux/handler/minidump_descriptor.h"
#include "common/linux/linux_libc_support.h"
#include "third_party/lss/linux_syscall_support.h"

namespace acceleratorlocal
{
	namespace
	{
		char g_szCrashMap[256]{};
		char g_szCrashGamePath[512]{};
		char g_szCrashPatchVersion[64]{};
		char g_szCrashServerVersion[32]{};
		char g_szCrashCommandLine[1024]{};

		Config g_Settings;
		std::unique_ptr<google_breakpad::ExceptionHandler> g_pExceptionHandler;
		const char* (*g_pfnCaptureConsoleHistory)();
		int g_nProcessorSocket = -1;
		pid_t g_nProcessorPid = -1;
		constexpr std::size_t MAX_DUMP_PATH_LENGTH = 1024;

		void (*g_pfnSignalHandler)(int, siginfo_t*, void*);
		constexpr auto EXCEPTION_SIGNALS = std::to_array({SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS, SIGTRAP});

		bool StartMinidumpProcessor(const std::filesystem::path& pathDumps, const std::filesystem::path& pathSymbols);
		void RestoreDefaultExceptionHandlers();
		void WriteCrashMetadata(const char* pszDumpPath);
		bool CloseInheritedDescriptors(int nSocket);

		enum class ProcessorResult
		{
			Succeeded,
			Failed,
			TimedOut
		};

		ProcessorResult FinishMinidumpProcessor(int nTimeoutSeconds);

		bool DumpCallback(const google_breakpad::MinidumpDescriptor& descriptor, void* pContext, bool bSucceeded)
		{
			if (bSucceeded)
				sys_write(STDOUT_FILENO, "Wrote minidump to: ", 19);
			else
				sys_write(STDOUT_FILENO, "Failed to write minidump to: ", 29);

			sys_write(STDOUT_FILENO, descriptor.path(), my_strlen(descriptor.path()));
			sys_write(STDOUT_FILENO, "\n", 1);

			if (!bSucceeded)
				return bSucceeded;

			constexpr char PROCESSOR_FAILED[] = "Minidump processor failed; dump retained\n";
			std::size_t nPathLength = my_strlen(descriptor.path()) + 1;
			if (nPathLength > MAX_DUMP_PATH_LENGTH)
			{
				sys_write(STDOUT_FILENO, PROCESSOR_FAILED, 41);
				return bSucceeded;
			}

			WriteCrashMetadata(descriptor.path());
			ssize_t nSent;
			do
			{
				nSent = send(g_nProcessorSocket, descriptor.path(), nPathLength, MSG_DONTWAIT | MSG_NOSIGNAL);
			} while (nSent == -1 && errno == EINTR);

			sys_close(g_nProcessorSocket);
			g_nProcessorSocket = -1;

			if (nSent != static_cast<ssize_t>(nPathLength))
			{
				sys_write(STDOUT_FILENO, PROCESSOR_FAILED, 41);
				return bSucceeded;
			}

			auto result = FinishMinidumpProcessor(g_Settings.nProcessorTimeoutSeconds);
			if (result == ProcessorResult::TimedOut)
				sys_write(STDOUT_FILENO, "Minidump processor timed out; dump retained\n", 44);
			else if (result == ProcessorResult::Failed)
				sys_write(STDOUT_FILENO, PROCESSOR_FAILED, 41);

			return bSucceeded;
		}

		void WriteCrashMetadata(const char* pszDumpPath)
		{
			char szReportPath[MAX_DUMP_PATH_LENGTH + 4];
			my_strlcpy(szReportPath, pszDumpPath, sizeof(szReportPath));
			my_strlcat(szReportPath, ".txt", sizeof(szReportPath));

			int nExtra = sys_open(szReportPath, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
			if (nExtra == -1)
			{
				sys_write(STDOUT_FILENO, "Failed to open metadata file!\n", 30);
				return;
			}

			sys_write(nExtra, "-------- CONFIG BEGIN --------", 30);
			sys_write(nExtra, "\nMap=", 5);
			sys_write(nExtra, g_szCrashMap, my_strlen(g_szCrashMap));
			sys_write(nExtra, "\nGamePath=", 10);
			sys_write(nExtra, g_szCrashGamePath, my_strlen(g_szCrashGamePath));
			sys_write(nExtra, "\nPatchVersion=", 14);
			sys_write(nExtra, g_szCrashPatchVersion, my_strlen(g_szCrashPatchVersion));
			sys_write(nExtra, "\nServerVersion=", 15);
			sys_write(nExtra, g_szCrashServerVersion, my_strlen(g_szCrashServerVersion));
			if (g_Settings.bIncludeCommandLine)
			{
				sys_write(nExtra, "\nCommandLine=", 13);
				sys_write(nExtra, g_szCrashCommandLine, my_strlen(g_szCrashCommandLine));
			}
			sys_write(nExtra, "\n-------- CONFIG END --------\n", 30);
			sys_write(nExtra, "\n", 1);

			if (g_Settings.bIncludeConsoleHistory)
			{
				const char* pszConsoleHistory = g_pfnCaptureConsoleHistory();
				if (pszConsoleHistory[0])
				{
					sys_write(nExtra, "-------- CONSOLE HISTORY BEGIN --------\n", 40);
					sys_write(nExtra, pszConsoleHistory, my_strlen(pszConsoleHistory));
					sys_write(nExtra, "-------- CONSOLE HISTORY END --------\n", 38);
					sys_write(nExtra, "\n", 1);
				}
			}

			sys_close(nExtra);
		}

		pid_t WaitForChild(pid_t nPid, int nTimeoutSeconds, int& nStatus)
		{
			struct timespec deadline;
			if (clock_gettime(CLOCK_MONOTONIC, &deadline) == -1)
				return -1;

			deadline.tv_sec += nTimeoutSeconds;

			while (true)
			{
				pid_t nResult = sys_waitpid(nPid, &nStatus, WNOHANG);
				if (nResult > 0 || (nResult == -1 && errno != EINTR))
					return nResult;

				struct timespec now;
				if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)
					return -1;

				if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec))
					return 0;

				if (sys_poll(nullptr, 0, 10) == -1 && errno != EINTR)
					return -1;
			}
		}

		ProcessorResult FinishMinidumpProcessor(int nTimeoutSeconds)
		{
			int nStatus = 0;
			pid_t nResult = WaitForChild(g_nProcessorPid, nTimeoutSeconds, nStatus);
			bool bSucceeded = nResult == g_nProcessorPid && WIFEXITED(nStatus) && WEXITSTATUS(nStatus) == 0;
			if (nResult == 0 || (nResult == -1 && errno != ECHILD))
			{
				sys_kill(g_nProcessorPid, SIGKILL);
				WaitForChild(g_nProcessorPid, 1, nStatus);
			}

			g_nProcessorPid = -1;

			if (nResult == 0)
				return ProcessorResult::TimedOut;

			return bSucceeded ? ProcessorResult::Succeeded : ProcessorResult::Failed;
		}

		bool StartMinidumpProcessor(const std::filesystem::path& pathDumps, const std::filesystem::path& pathSymbols)
		{
			int nSockets[2];
			if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, nSockets) == -1)
				return false;

			pid_t nPid = fork();
			if (nPid == -1)
			{
				int nForkError = errno;
				close(nSockets[0]);
				close(nSockets[1]);
				errno = nForkError;
				return false;
			}

			if (nPid == 0)
			{
				close(nSockets[0]);
				RestoreDefaultExceptionHandlers();

				sigset_t emptyMask;
				sigemptyset(&emptyMask);
				sigprocmask(SIG_SETMASK, &emptyMask, nullptr);

				if (!CloseInheritedDescriptors(nSockets[1]))
				{
					std::fprintf(stderr, "Failed to close inherited worker descriptors\n");
					_exit(1);
				}

				RemoveOldDumps(pathDumps, g_Settings.nDumpRetentionDays);

				char szDumpPath[MAX_DUMP_PATH_LENGTH];
				ssize_t nReceived;
				do
				{
					nReceived = recv(nSockets[1], szDumpPath, sizeof(szDumpPath), 0);
				} while (nReceived == -1 && errno == EINTR);

				close(nSockets[1]);

				if (nReceived <= 1)
					_exit(nReceived < 0 ? 1 : 0);

				if (szDumpPath[nReceived - 1] != '\0')
					_exit(1);

				bool bProcessed = ProcessMinidump(szDumpPath, pathSymbols, g_Settings);
				_exit(bProcessed ? 0 : 1);
			}

			close(nSockets[1]);
			g_nProcessorSocket = nSockets[0];
			g_nProcessorPid = nPid;

			return true;
		}

		void RestoreDefaultExceptionHandlers()
		{
			struct sigaction act;
			std::memset(&act, 0, sizeof(act));
			sigemptyset(&act.sa_mask);
			act.sa_handler = SIG_DFL;

			for (int nExceptionSignal : EXCEPTION_SIGNALS)
				sigaction(nExceptionSignal, &act, nullptr);
		}

		bool CloseInheritedDescriptors(int nSocket)
		{
			std::unique_ptr<DIR, decltype(&closedir)> pDirectory(opendir("/proc/self/fd"), closedir);
			if (!pDirectory)
				return false;

			int nDirectory = dirfd(pDirectory.get());

			while (true)
			{
				errno = 0;
				auto* pEntry = readdir(pDirectory.get());
				if (!pEntry)
					return errno == 0;

				int nDescriptor;
				const char* pszEnd = pEntry->d_name + std::strlen(pEntry->d_name);
				auto conversion = std::from_chars(pEntry->d_name, pszEnd, nDescriptor);
				if (conversion.ec != std::errc{} || conversion.ptr != pszEnd)
					continue;

				// Preserve stdio, IPC, and the directory being scanned.
				if (nDescriptor > STDERR_FILENO && nDescriptor != nSocket && nDescriptor != nDirectory)
					close(nDescriptor);
			}
		}

	} // namespace

	bool StartCrashHandler(const CrashHandlerConfig& config, std::string& strError)
	{
		const auto pathPlugin = std::filesystem::path(config.pszGamePath) / "addons" / "acceleratorlocal";
		const auto pathDumps = pathPlugin / "dumps";
		const auto pathSymbols = pathPlugin / "symbols";
		if (!LoadConfig(pathPlugin / "acceleratorlocal.ini", g_Settings, strError))
			return false;

		std::snprintf(g_szCrashGamePath, sizeof(g_szCrashGamePath), "%s", config.pszGamePath);
		if (g_Settings.bIncludeCommandLine)
			std::snprintf(g_szCrashCommandLine, sizeof(g_szCrashCommandLine), "%s", config.pszCommandLine);

		g_pfnCaptureConsoleHistory = config.pfnConsoleHistory;

		GameVersion gameVersion = ReadGameVersion(config.pszGamePath);
		std::snprintf(g_szCrashPatchVersion, sizeof(g_szCrashPatchVersion), "%s", gameVersion.strPatchVersion.c_str());
		std::snprintf(g_szCrashServerVersion, sizeof(g_szCrashServerVersion), "%s", gameVersion.strServerVersion.c_str());

		for (const auto& pathDirectory : {pathDumps, pathSymbols})
		{
			std::error_code ecError;
			std::filesystem::create_directories(pathDirectory, ecError);
			if (ecError)
			{
				strError = "Cannot create " + pathDirectory.string() + ": " + ecError.message();
				return false;
			}
		}

		// Fork before Breakpad installs its signal handler and alternate stack.
		if (!StartMinidumpProcessor(pathDumps, pathSymbols))
		{
			strError = "Failed to start minidump processor (errno " + std::to_string(errno) + ")";
			return false;
		}

		google_breakpad::MinidumpDescriptor descriptor(pathDumps.string());
		g_pExceptionHandler = std::make_unique<google_breakpad::ExceptionHandler>(descriptor, nullptr, DumpCallback, nullptr, true, -1);

		struct sigaction action;
		sigaction(SIGSEGV, nullptr, &action);
		g_pfnSignalHandler = action.sa_sigaction;

		return true;
	}

	void StopCrashHandler()
	{
		g_pExceptionHandler.reset();
		if (g_nProcessorSocket != -1)
		{
			send(g_nProcessorSocket, "", 1, MSG_DONTWAIT | MSG_NOSIGNAL);
			close(g_nProcessorSocket);
			g_nProcessorSocket = -1;
		}

		if (g_nProcessorPid > 0)
			FinishMinidumpProcessor(2);
	}

	void CheckCrashHandler()
	{
		bool bHandlerChanged = false;
		struct sigaction oact;

		for (int nExceptionSignal : EXCEPTION_SIGNALS)
		{
			sigaction(nExceptionSignal, nullptr, &oact);

			if (oact.sa_sigaction != g_pfnSignalHandler)
			{
				bHandlerChanged = true;
				break;
			}
		}

		if (!bHandlerChanged)
			return;

		struct sigaction act;
		std::memset(&act, 0, sizeof(act));
		sigemptyset(&act.sa_mask);

		for (int nExceptionSignal : EXCEPTION_SIGNALS)
			sigaddset(&act.sa_mask, nExceptionSignal);

		act.sa_sigaction = g_pfnSignalHandler;
		act.sa_flags = SA_ONSTACK | SA_SIGINFO;

		for (int nExceptionSignal : EXCEPTION_SIGNALS)
			sigaction(nExceptionSignal, &act, nullptr);
	}

	void SetCrashMap(const char* pszMapName)
	{
		std::snprintf(g_szCrashMap, sizeof(g_szCrashMap), "%s", pszMapName);
	}
} // namespace acceleratorlocal

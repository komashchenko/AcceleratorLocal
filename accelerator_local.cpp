/**
 * ======================================================
 * Accelerator local
 * Written by Phoenix (˙·٠●Феникс●٠·˙) 2023-2025, Asher Baker (asherkin) 2011.
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

#include "accelerator_local.h"
#include "CMiniDumpComment.hpp"

#include "client/linux/handler/exception_handler.h"
#include "common/linux/linux_libc_support.h"
#include "third_party/lss/linux_syscall_support.h"

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <dirent.h>
#include <unistd.h>
#include <limits>

#include "common/path_helper.h"
#include "common/using_std_string.h"
#include "google_breakpad/processor/basic_source_line_resolver.h"
#include "google_breakpad/processor/minidump_processor.h"
#include "google_breakpad/processor/process_state.h"
#include "processor/simple_symbol_supplier.h"
#include "processor/stackwalk_common.h"
#include <google_breakpad/processor/call_stack.h>
#include <google_breakpad/processor/stack_frame.h>
#include <processor/pathname_stripper.h>

AcceleratorLocal g_AcceleratorLocal;
PLUGIN_EXPOSE(AcceleratorLocal, g_AcceleratorLocal);

char crashMap[256];
char crashGamePath[512];
char crashCommandLine[1024];
char dumpStoragePath[512];

google_breakpad::ExceptionHandler* exceptionHandler = nullptr;
CMiniDumpComment g_MiniDumpComment(95000);

void (*SignalHandler)(int, siginfo_t*, void*);
const int kExceptionSignals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};
const int kNumHandledSignals = std::size(kExceptionSignals);

class GameSessionConfiguration_t { };
SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK3_void(INetworkServerService, StartupServer, SH_NOATTRIB, 0, const GameSessionConfiguration_t&, ISource2WorldSession*, const char*);

static bool dumpCallback(const google_breakpad::MinidumpDescriptor& descriptor, void* context, bool succeeded)
{
	if (succeeded)
		sys_write(STDOUT_FILENO, "Wrote minidump to: ", 19);
	else
		sys_write(STDOUT_FILENO, "Failed to write minidump to: ", 29);

	sys_write(STDOUT_FILENO, descriptor.path(), my_strlen(descriptor.path()));
	sys_write(STDOUT_FILENO, "\n", 1);

	if (!succeeded)
		return succeeded;

	my_strlcpy(dumpStoragePath, descriptor.path(), sizeof(dumpStoragePath));
	my_strlcat(dumpStoragePath, ".txt", sizeof(dumpStoragePath));

	int extra = sys_open(dumpStoragePath, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
	if (extra == -1)
	{
		sys_write(STDOUT_FILENO, "Failed to open metadata file!\n", 30);
		return succeeded;
	}

	sys_write(extra, "-------- CONFIG BEGIN --------", 30);
	sys_write(extra, "\nMap=", 5);
	sys_write(extra, crashMap, my_strlen(crashMap));
	sys_write(extra, "\nGamePath=", 10);
	sys_write(extra, crashGamePath, my_strlen(crashGamePath));
	sys_write(extra, "\nCommandLine=", 13);
	sys_write(extra, crashCommandLine, my_strlen(crashCommandLine));
	sys_write(extra, "\n-------- CONFIG END --------\n", 30);
	sys_write(extra, "\n", 1);
	
	LoggingSystem_GetLogCapture(&g_MiniDumpComment, false);
	const char* pszConsoleHistory = g_MiniDumpComment.GetStartPointer();
	
	if (pszConsoleHistory[0])
	{
		sys_write(extra, "-------- CONSOLE HISTORY BEGIN --------\n", 40);
		sys_write(extra, pszConsoleHistory, my_strlen(pszConsoleHistory));
		sys_write(extra, "-------- CONSOLE HISTORY END --------\n", 38);
		sys_write(extra, "\n", 1);
	}
	
	google_breakpad::scoped_ptr<google_breakpad::SimpleSymbolSupplier> symbolSupplier;
	google_breakpad::BasicSourceLineResolver resolver;
	google_breakpad::MinidumpProcessor minidump_processor(symbolSupplier.get(), &resolver);

	// Increase the maximum number of threads and regions.
	google_breakpad::MinidumpThreadList::set_max_threads(std::numeric_limits<uint32_t>::max());
	google_breakpad::MinidumpMemoryList::set_max_regions(std::numeric_limits<uint32_t>::max());
	// Process the minidump.
	google_breakpad::Minidump miniDump(descriptor.path());
	if (!miniDump.Read())
	{
		sys_write(STDOUT_FILENO, "Failed to read minidump\n", 24);
	}
	else
	{
		google_breakpad::ProcessState processState;
		if (minidump_processor.Process(&miniDump, &processState) !=  google_breakpad::PROCESS_OK)
		{
			sys_write(STDOUT_FILENO, "MinidumpProcessor::Process failed\n", 34);
		}
		else
		{
			int requestingThread = processState.requesting_thread();
			if (requestingThread == -1)
				requestingThread = 0;

			const google_breakpad::CallStack* stack = processState.threads()->at(requestingThread);
			size_t frameCount = MIN(stack->frames()->size(), 15);

			auto signal_safe_hex_print = [](uint64_t num)
			{
				char buffer[18];
				char* ptr = buffer + sizeof(buffer);

				if (num == 0)
					*(--ptr) = '0';
				else
				{
					while (num > 0)
					{
						*(--ptr) = "0123456789abcdef"[num % 16];
						num /= 16;
					}
				}

				*(--ptr) = 'x';
				*(--ptr) = '0';

				size_t length = buffer + sizeof(buffer) - ptr;
				sys_write(STDOUT_FILENO, ptr, length);
			};

			sys_write(STDOUT_FILENO, "\n", 1);
			for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
			{
				const google_breakpad::StackFrame* frame = stack->frames()->at(frameIndex);

				uint64_t moduleOffset = frame->ReturnAddress();
				if (frame->module)
				{
					const std::string moduleFile = google_breakpad::PathnameStripper::File(frame->module->code_file());
					moduleOffset -= frame->module->base_address();
					sys_write(STDOUT_FILENO, moduleFile.c_str(), moduleFile.size());
					sys_write(STDOUT_FILENO, " + ", 3);
					signal_safe_hex_print(moduleOffset);
					sys_write(STDOUT_FILENO, "\n", 1);
				}
				else
				{
					sys_write(STDOUT_FILENO, "unknown + ", 10);
					signal_safe_hex_print(moduleOffset);
					sys_write(STDOUT_FILENO, "\n", 1);
				}
			}

			freopen(dumpStoragePath, "a", stdout);
			PrintProcessState(processState, true, false, &resolver);
			fflush(stdout);
		}
	}

	sys_close(extra);

	return succeeded;
}

bool AcceleratorLocal::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	
	GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	
	strncpy(crashGamePath, ismm->GetBaseDir(), sizeof(crashGamePath) - 1);
	ismm->Format(dumpStoragePath, sizeof(dumpStoragePath), "%s/addons/accelerator_local/dumps", ismm->GetBaseDir());
	
	struct stat st = {0};
	if (stat(dumpStoragePath, &st) == -1)
	{
		if(mkdir(dumpStoragePath, 0777) == -1)
		{
			ismm->Format(error, maxlen, "%s didn't exist and we couldn't create it :(", dumpStoragePath);
			return false;
		}
	}
	else
		chmod(dumpStoragePath, 0777);
	
	google_breakpad::MinidumpDescriptor descriptor(dumpStoragePath);
	exceptionHandler = new google_breakpad::ExceptionHandler(descriptor, NULL, dumpCallback, NULL, true, -1);

	struct sigaction oact;
	sigaction(SIGSEGV, NULL, &oact);
	SignalHandler = oact.sa_sigaction;

	SH_ADD_HOOK(IServerGameDLL, GameFrame, g_pSource2Server, SH_MEMBER(this, &AcceleratorLocal::GameFrame), true);
	SH_ADD_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService, SH_MEMBER(this, &AcceleratorLocal::StartupServer), true);

	strncpy(crashCommandLine, CommandLine()->GetCmdLine(), sizeof(crashCommandLine) - 1);

	if (late)
		StartupServer({}, nullptr, g_pNetworkServerService->GetIGameServer()->GetMapName());
	
	return true;
}

bool AcceleratorLocal::Unload(char* error, size_t maxlen)
{
	SH_REMOVE_HOOK(IServerGameDLL, GameFrame, g_pSource2Server, SH_MEMBER(this, &AcceleratorLocal::GameFrame), true);
	SH_REMOVE_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService, SH_MEMBER(this, &AcceleratorLocal::StartupServer), true);

	delete exceptionHandler;
	
	return true;
}

void AcceleratorLocal::GameFrame(bool simulating, bool bFirstTick, bool bLastTick)
{
	bool weHaveBeenFuckedOver = false;
	struct sigaction oact;

	for (int i = 0; i < kNumHandledSignals; ++i)
	{
		sigaction(kExceptionSignals[i], NULL, &oact);

		if (oact.sa_sigaction != SignalHandler)
		{
			weHaveBeenFuckedOver = true;
			break;
		}
	}

	if (!weHaveBeenFuckedOver)
		return;

	struct sigaction act;
	memset(&act, 0, sizeof(act));
	sigemptyset(&act.sa_mask);

	for (int i = 0; i < kNumHandledSignals; ++i)
		sigaddset(&act.sa_mask, kExceptionSignals[i]);

	act.sa_sigaction = SignalHandler;
	act.sa_flags = SA_ONSTACK | SA_SIGINFO;

	for (int i = 0; i < kNumHandledSignals; ++i)
		sigaction(kExceptionSignals[i], &act, NULL);
}

void AcceleratorLocal::StartupServer(const GameSessionConfiguration_t& config, ISource2WorldSession*, const char* pszMapName)
{
	strncpy(crashMap, pszMapName, sizeof(crashMap) - 1);
}

///////////////////////////////////////
const char* AcceleratorLocal::GetLicense()
{
	return "GPL";
}

const char* AcceleratorLocal::GetVersion()
{
	return "1.0.3";
}

const char* AcceleratorLocal::GetDate()
{
	return __DATE__;
}

const char *AcceleratorLocal::GetLogTag()
{
	return "AcceleratorLocal";
}

const char* AcceleratorLocal::GetAuthor()
{
	return "Phoenix (˙·٠●Феникс●٠·˙), Asher Baker (asherkin)";
}

const char* AcceleratorLocal::GetDescription()
{
	return "Crash Handler";
}

const char* AcceleratorLocal::GetName()
{
	return "Accelerator local";
}

const char* AcceleratorLocal::GetURL()
{
	return "https://github.com/komashchenko/AcceleratorLocal";
}

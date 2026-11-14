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

#include "acceleratorlocal.h"

#include "crashhandler.h"

#include <cstdio>
#include <string>

#include <eiface.h>
#include <iserver.h>
#include <tier0/icommandline.h>
#include <tier0/logging.h>
#include <tier0/minidump.h>

CAcceleratorLocal g_AcceleratorLocal;
PLUGIN_EXPOSE(CAcceleratorLocal, g_AcceleratorLocal);

static CMiniDumpComment g_MiniDumpComment(95000);

static const char* CaptureConsoleHistory()
{
	LoggingSystem_GetLogCapture(&g_MiniDumpComment, true);

	return g_MiniDumpComment.GetStartPointer();
}

CAcceleratorLocal::CAcceleratorLocal() :
	m_GameFrameHook(&ISource2Server::GameFrame, this, nullptr, &CAcceleratorLocal::GameFrame),
	m_StartupServerHook(&INetworkServerService::StartupServer, this, nullptr, &CAcceleratorLocal::StartupServer)
{
}

bool CAcceleratorLocal::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool bLate)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);

	std::string strHandlerError;
	const acceleratorlocal::CrashHandlerConfig config{ismm->GetBaseDir(), CommandLine()->GetCmdLine(), CaptureConsoleHistory};
	if (!acceleratorlocal::StartCrashHandler(config, strHandlerError))
	{
		std::snprintf(error, maxlen, "%s", strHandlerError.c_str());
		return false;
	}

	m_GameFrameHook.Add(g_pSource2Server);
	m_StartupServerHook.Add(g_pNetworkServerService);

	if (bLate)
		acceleratorlocal::SetCrashMap(g_pNetworkServerService->GetIGameServer()->GetMapName());

	return true;
}

bool CAcceleratorLocal::Unload(char* pszError, size_t nMaxlen)
{
	m_StartupServerHook.Remove(g_pNetworkServerService);
	m_GameFrameHook.Remove(g_pSource2Server);

	acceleratorlocal::StopCrashHandler();

	return true;
}

KHook::Return<void> CAcceleratorLocal::GameFrame(ISource2Server*, bool bSimulating, bool bFirstTick, bool bLastTick)
{
	acceleratorlocal::CheckCrashHandler();

	return {KHook::Action::Ignore};
}

KHook::Return<void> CAcceleratorLocal::StartupServer(INetworkServerService*, const GameSessionConfiguration_t& config, ISource2WorldSession*, const char* pszMapName)
{
	acceleratorlocal::SetCrashMap(pszMapName);

	return {KHook::Action::Ignore};
}

const char* CAcceleratorLocal::GetLicense()
{
	return "GPL";
}

const char* CAcceleratorLocal::GetVersion()
{
	return ACCELERATORLOCAL_VERSION;
}

const char* CAcceleratorLocal::GetDate()
{
	return __DATE__;
}

const char* CAcceleratorLocal::GetLogTag()
{
	return "AcceleratorLocal";
}

const char* CAcceleratorLocal::GetAuthor()
{
	return "komashchenko (Phoenix), Asher Baker (asherkin)";
}

const char* CAcceleratorLocal::GetDescription()
{
	return "Local crash handler for Counter-Strike 2";
}

const char* CAcceleratorLocal::GetName()
{
	return "Accelerator local";
}

const char* CAcceleratorLocal::GetURL()
{
	return "https://github.com/komashchenko/AcceleratorLocal";
}

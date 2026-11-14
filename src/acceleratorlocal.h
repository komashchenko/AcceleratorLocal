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

#include <cstddef>

#include <ISmmPlugin.h>
#include <khook.hpp>

class GameSessionConfiguration_t;
class INetworkServerService;
class ISource2Server;
class ISource2WorldSession;

class CAcceleratorLocal final : public ISmmPlugin
{
public:
	CAcceleratorLocal();

	bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool bLate) override;
	bool Unload(char* pszError, size_t nMaxlen) override;

private:
	const char* GetAuthor() override;
	const char* GetName() override;
	const char* GetDescription() override;
	const char* GetURL() override;
	const char* GetLicense() override;
	const char* GetVersion() override;
	const char* GetDate() override;
	const char* GetLogTag() override;

private: // Hooks
	KHook::Return<void> GameFrame(ISource2Server*, bool bSimulating, bool bFirstTick, bool bLastTick);
	KHook::Return<void> StartupServer(INetworkServerService*, const GameSessionConfiguration_t& config, ISource2WorldSession*, const char* pszMapName);

	KHook::Virtual<ISource2Server, void, bool, bool, bool> m_GameFrameHook;
	KHook::Virtual<INetworkServerService, void, const GameSessionConfiguration_t&, ISource2WorldSession*, const char*> m_StartupServerHook;
};

#define _CRT_SECURE_NO_WARNINGS
#include <WinSDKVer.h>
#define _WIN32_WINNT 0x0A00
#include <SDKDDKVer.h>
//Don't include unnecessary APIs
#define WIN32_LEAN_AND_MEAN
#define NOGDICAPMASKS
#define NOVIRTUALKEYCODES
#define NOWINMESSAGES
#define NOWINSTYLES
#define NOSYSMETRICS
#define NOMENUS
#define NOICONS
#define NOKEYSTATES
#define NOSYSCOMMANDS
#define NORASTEROPS
#define NOSHOWWINDOW
#define OEMRESOURCE
#define NOATOM
#define NOCLIPBOARD
#define NOCOLOR
#define NOCTLMGR
#define NODRAWTEXT
#define NOGDI
#define NOKERNEL
#define NOUSER
#define NOMB
#define NOMEMMGR
#define NOMETAFILE
#define NOMINMAX
#define NOOPENFILE
#define NOSCROLL
#define NOSERVICE
#define NOSOUND
#define NOTEXTMETRIC
#define NOWH
#define NOWINOFFSETS
#define NOCOMM
#define NOKANJI
#define NOHELP
#define NOPROFILER
#define NODEFERWINDOWPOS
#define NOMCX
#include <iostream>
#include <WS2tcpip.h>
#include "steam/steam_gameserver.h"

using namespace std;

struct Rule
{
	const char* Key;
	const char* Value;
};
struct QueryResult
{
	const char* Name;
	const char* Map;
	CHAR Players;
	int MaxPlayers;
	bool Visibility;
	bool VAC;
	uint16 GamePort;
	int NumRules;
	Rule* Rules;
};
struct Callback
{
	bool LoggedOn = false;
	void Run(SteamServersConnected_t* arg)
	{
		LoggedOn = true;
	}
};

alignas(16) CHAR Buffer[4096];
alignas(16) CHAR GameTagsAndDataBuffer[k_cbMaxGameServerGameData];
alignas(16) CHAR PlayerDataBuffer[2048];
alignas(16) CHAR ResponseQueryDataBuffer[4096];
QueryResult LastQuery;
sockaddr_in ParentAddress{ AF_INET, 0x8769, { { { 127, 0, 0, 1 } } } };
DWORD PlayerDataSize;
uint32 AppId = 346110;
uint16 GamePort = 7777;
char ServerName[k_cbMaxGameServerName]{ "ArkServerMirror" };
CSteamID SteamId;
constexpr const BYTE A2S_INFO[]
{
	0xFF, 0xFF, 0xFF, 0xFF, 0x54, 0x53, 0x6F, 0x75, 0x72, 0x63, 0x65, 0x20, 0x45, 0x6E,
	0x67, 0x69, 0x6E, 0x65, 0x20, 0x51, 0x75, 0x65, 0x72, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00
};
bool ParentActive;
bool ShutdownRequested;

static bool QueryParentInfo(_In_ SOCKET socket, _Inout_ OVERLAPPED& overlapped, _Out_ QueryResult& result)
{
	const auto address = reinterpret_cast<const sockaddr*>(&ParentAddress);
	//A2S_INFO
	WSABUF wsaBuf{ sizeof(A2S_INFO), reinterpret_cast<CHAR*>(const_cast<BYTE*>(A2S_INFO)) };
	WSASendTo(socket, &wsaBuf, 1, nullptr, 0, address, sizeof(sockaddr), &overlapped, nullptr);
	if (WaitForSingleObject(overlapped.hEvent, 2000))
		return false;
	wsaBuf = { sizeof(Buffer), Buffer };
	DWORD transferred, flags = 0;
	WSARecvFrom(socket, &wsaBuf, 1, nullptr, &flags, nullptr, nullptr, &overlapped, nullptr);
	if (WaitForSingleObject(overlapped.hEvent, 2000))
		return false;
	WSAGetOverlappedResult(socket, &overlapped, &transferred, FALSE, &flags);
	if (transferred == 9)
	{
		*reinterpret_cast<uint32*>(wsaBuf.buf + 25) = *reinterpret_cast<const uint32*>(wsaBuf.buf + 5);
		memcpy(wsaBuf.buf, A2S_INFO, sizeof(A2S_INFO) - 4);
		wsaBuf.len = sizeof(A2S_INFO);
		WSASendTo(socket, &wsaBuf, 1, nullptr, 0, address, sizeof(sockaddr), &overlapped, nullptr);
		if (WaitForSingleObject(overlapped.hEvent, 2000))
			return false;
		wsaBuf.len = sizeof(Buffer);
		flags = 0;
		WSARecvFrom(socket, &wsaBuf, 1, nullptr, &flags, nullptr, nullptr, &overlapped, nullptr);
		if (WaitForSingleObject(overlapped.hEvent, 2000))
			return false;
	}
	const CHAR* cur = wsaBuf.buf + 6;
	result.Name = cur;
	while (*cur)
		++cur;
	result.Map = ++cur;
	while (*cur)
		++cur;
	cur += 46;
	result.Players = *cur;
	result.MaxPlayers = *++cur;
	cur += 4;
	result.Visibility = *cur;
	result.VAC = *++cur;
	++cur;
	while (*cur)
		++cur;
	if (*++cur & 0x80)
		result.GamePort = *reinterpret_cast<const uint16*>(++cur);
	const auto rulesBufferSize = static_cast<ULONG>(Buffer + sizeof(Buffer) - cur);
	//A2S_RULES
	wsaBuf = { 9, const_cast<CHAR*>(cur) };
	*reinterpret_cast<uint64*>(wsaBuf.buf) = 0xFFFFFF56FFFFFFFF;
	wsaBuf.buf[8] = 0xFF;
	WSASendTo(socket, &wsaBuf, 1, nullptr, 0, address, sizeof(sockaddr), &overlapped, nullptr);
	if (WaitForSingleObject(overlapped.hEvent, 2000))
		return false;
	flags = 0;
	WSARecvFrom(socket, &wsaBuf, 1, nullptr, &flags, nullptr, nullptr, &overlapped, nullptr);
	if (WaitForSingleObject(overlapped.hEvent, 2000))
		return false;
	wsaBuf.buf[4] = 0x56;
	WSASendTo(socket, &wsaBuf, 1, nullptr, 0, address, sizeof(sockaddr), &overlapped, nullptr);
	if (WaitForSingleObject(overlapped.hEvent, 2000))
		return false;
	wsaBuf.len = rulesBufferSize;
	flags = 0;
	WSARecvFrom(socket, &wsaBuf, 1, nullptr, &flags, nullptr, nullptr, &overlapped, nullptr);
	if (WaitForSingleObject(overlapped.hEvent, 2000))
		return false;
	result.NumRules = *reinterpret_cast<const uint16*>(wsaBuf.buf + 5);
	auto rule = reinterpret_cast<Rule*>(Buffer + sizeof(Buffer) - sizeof(Rule) * result.NumRules);
	result.Rules = rule;
	cur = wsaBuf.buf + 6;
	for (int i = 0; i < result.NumRules; i++, rule++)
	{
		rule->Key = ++cur;
		while (*cur)
			++cur;
		rule->Value = ++cur;
		while (*cur)
			++cur;
	}
	//A2S_PLAYER
	wsaBuf = { 9, PlayerDataBuffer };
	*reinterpret_cast<uint64*>(wsaBuf.buf) = 0xFFFFFF55FFFFFFFF;
	wsaBuf.buf[8] = 0xFF;
	WSASendTo(socket, &wsaBuf, 1, nullptr, 0, address, sizeof(sockaddr), &overlapped, nullptr);
	if (WaitForSingleObject(overlapped.hEvent, 2000))
		return false;
	flags = 0;
	WSARecvFrom(socket, &wsaBuf, 1, nullptr, &flags, nullptr, nullptr, &overlapped, nullptr);
	if (WaitForSingleObject(overlapped.hEvent, 2000))
		return false;
	wsaBuf.buf[4] = 0x55;
	WSASendTo(socket, &wsaBuf, 1, nullptr, 0, address, sizeof(sockaddr), &overlapped, nullptr);
	if (WaitForSingleObject(overlapped.hEvent, 2000))
		return false;
	wsaBuf.len = sizeof(PlayerDataBuffer);
	flags = 0;
	WSARecvFrom(socket, &wsaBuf, 1, nullptr, &flags, nullptr, nullptr, &overlapped, nullptr);
	if (WaitForSingleObject(overlapped.hEvent, 2000))
		return false;
	WSAGetOverlappedResult(socket, &overlapped, &PlayerDataSize, FALSE, &flags);
	return true;
}
static DWORD QueryPortListen(LPVOID parameter)
{
	const auto socket = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	if (socket == INVALID_SOCKET)
	{
		wcerr << L"WSASocketW@QueryPortListen failed with error code " << WSAGetLastError() << endl;
		return 7;
	}
	const sockaddr_in localAddress{ AF_INET, htons(static_cast<uint16>(reinterpret_cast<uint64>(parameter))), { { { 0, 0, 0, 0 } } } };
	const auto errCode = bind(socket, reinterpret_cast<const sockaddr*>(&localAddress), sizeof(sockaddr));
	if (errCode)
	{
		wcerr << L"bind@QueryPortListen failed with error code " << WSAGetLastError() << endl;
		return 8;
	}
	OVERLAPPED overlapped{};
	overlapped.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	WSABUF wsaBuf{ sizeof(A2S_INFO), ResponseQueryDataBuffer };
	DWORD transferred, flags = 0;
	for (;;)
	{
		wsaBuf.len = sizeof(A2S_INFO);
		sockaddr address;
		INT addressLength = sizeof(sockaddr);
		WSARecvFrom(socket, &wsaBuf, 1, nullptr, &flags, &address, &addressLength, &overlapped, nullptr);
		WaitForSingleObject(overlapped.hEvent, INFINITE);
		WSAGetOverlappedResult(socket, &overlapped, &transferred, FALSE, &flags);
		const auto ip = reinterpret_cast<const sockaddr_in*>(&address)->sin_addr.S_un.S_addr;
		const auto head = *reinterpret_cast<const DWORD*>(wsaBuf.buf);
		if (transferred == 4 && (ip == 0 || ip == 0x0100007F) && !head)
		{
			ShutdownRequested = true;
			closesocket(socket);
			CloseHandle(overlapped.hEvent);
			return 0;
		}
		if (!ParentActive || transferred < 9 || head != 0xFFFFFFFF)
			continue;
		switch (wsaBuf.buf[4])
		{
			case 0x54:
			{
				*reinterpret_cast<uint16*>(wsaBuf.buf + 4) = 0x1149;
				auto cur = wsaBuf.buf + 6;
				auto length = strlen(ServerName) + 1;
				memcpy(cur, ServerName, length);
				cur += length;
				length = strlen(LastQuery.Map) + 1;
				memcpy(cur, LastQuery.Map, length);
				cur += length;
				memcpy(cur, "ark_survival_evolved\0ARK: Survival Evolved", 43);
				cur += 43;
				*reinterpret_cast<uint16*>(cur) = 0;
				cur += 2;
				*cur++ = LastQuery.Players;
				*cur++ = LastQuery.MaxPlayers;
				*cur++ = 0;
				*reinterpret_cast<uint16*>(cur) = 0x7764;
				cur += 2;
				*cur++ = LastQuery.Visibility;
				*cur++ = LastQuery.VAC;
				memcpy(cur, "1.0.0.0", 8);
				cur += 8;
				*cur++ = 0xB1;
				*reinterpret_cast<uint16*>(cur) = GamePort;
				cur += 2;
				*reinterpret_cast<CSteamID*>(cur) = SteamId;
				cur += 8;
				length = strlen(GameTagsAndDataBuffer) + 1;
				memcpy(cur, GameTagsAndDataBuffer, length);
				cur += length;
				*reinterpret_cast<uint64*>(cur) = AppId;
				cur += 8;
				wsaBuf.len = cur - wsaBuf.buf;
				WSASendTo(socket, &wsaBuf, 1, nullptr, 0, &address, addressLength, &overlapped, nullptr);
				WaitForSingleObject(overlapped.hEvent, 2000);
				break;
			}
			case 0x55:
			{
				wsaBuf.len = 9;
				wsaBuf.buf[4] = 0x41;
				*reinterpret_cast<uint32*>(wsaBuf.buf + 5) = *reinterpret_cast<const uint32*>(PlayerDataBuffer + 5);
				WSASendTo(socket, &wsaBuf, 1, nullptr, 0, &address, addressLength, &overlapped, nullptr);
				if (WaitForSingleObject(overlapped.hEvent, 2000))
					break;
				WSARecvFrom(socket, &wsaBuf, 1, nullptr, &flags, nullptr, nullptr, &overlapped, nullptr);
				if (WaitForSingleObject(overlapped.hEvent, 2000))
					break;
				wsaBuf = { PlayerDataSize, PlayerDataBuffer };
				WSASendTo(socket, &wsaBuf, 1, nullptr, 0, &address, addressLength, &overlapped, nullptr);
				WaitForSingleObject(overlapped.hEvent, 2000);
				wsaBuf.buf = ResponseQueryDataBuffer;
				break;
			}
			case 0x56:
			{
				wsaBuf.len = 9;
				wsaBuf.buf[4] = 0x41;
				*reinterpret_cast<uint32*>(wsaBuf.buf + 5) = *reinterpret_cast<const uint32*>(PlayerDataBuffer + 5);
				WSASendTo(socket, &wsaBuf, 1, nullptr, 0, &address, addressLength, &overlapped, nullptr);
				if (WaitForSingleObject(overlapped.hEvent, 2000))
					break;
				WSARecvFrom(socket, &wsaBuf, 1, nullptr, &flags, nullptr, nullptr, &overlapped, nullptr);
				if (WaitForSingleObject(overlapped.hEvent, 2000))
					break;
				wsaBuf.buf[4] = 0x45;
				*reinterpret_cast<uint16*>(wsaBuf.buf + 5) = LastQuery.NumRules;
				auto cur = wsaBuf.buf + 7;
				for (int i = 0; i < LastQuery.NumRules; i++)
				{
					const auto rule = LastQuery.Rules[i];
					auto length = strlen(rule.Key) + 1;
					memcpy(cur, rule.Key, length);
					cur += length;
					length = strlen(rule.Value) + 1;
					memcpy(cur, rule.Value, length);
					cur += length;
				}
				wsaBuf.len = cur - wsaBuf.buf;
				WSASendTo(socket, &wsaBuf, 1, nullptr, 0, &address, addressLength, &overlapped, nullptr);
				WaitForSingleObject(overlapped.hEvent, 2000);
				break;
			}
		}
	}
	return 0;
}

int wmain(int argc, wchar_t* argv[])
{
	//Variable definitions
	auto queryPort = 27015;
	char serverNameLower[k_cbMaxGameServerName]{};
	auto pIp = L"127.0.0.1";
	auto appId = L"346110";
	DWORD updateInterval = 10000;
	//Load arguments
	for (int i = 1; i < argc; i++)
	{
		if (!wcscmp(argv[i], L"--gamePort"))
			GamePort = wcstoul(argv[++i], nullptr, 10);
		else if (!wcscmp(argv[i], L"--queryPort"))
			queryPort = wcstoul(argv[++i], nullptr, 10);
		else if (!wcscmp(argv[i], L"--displayName"))
			WideCharToMultiByte(CP_UTF8, 0, argv[++i], -1, ServerName, sizeof(ServerName), nullptr, nullptr);
		else if (!wcscmp(argv[i], L"--parentIP"))
			pIp = argv[++i];
		else if (!wcscmp(argv[i], L"--parentQueryPort"))
			ParentAddress.sin_port = htons(wcstoul(argv[++i], nullptr, 10));
		else if (!wcscmp(argv[i], L"--appIdOverride"))
		{
			appId = argv[++i];
			AppId = wcstoul(appId, nullptr, 10);
		}
		else if (!wcscmp(argv[i], L"--updateInterval"))
			updateInterval = wcstoul(argv[++i], nullptr, 10) * 1000;
	}
	//Query parent server
	WSAData wsaData;
	if (WSAStartup(0x0202, &wsaData))
	{
		wcerr << L"WSAStartup failed with error code " << WSAGetLastError() <<endl;
		return 1;
	}
	if (!InetPtonW(AF_INET, pIp, &ParentAddress.sin_addr))
	{
		wcerr << L"InetPtonW failed with error code " << WSAGetLastError() << endl;
		WSACleanup();
		return 2;
	}
	const auto pSocket = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	if (pSocket == INVALID_SOCKET)
	{
		wcerr << L"WSASocketW failed with error code " << WSAGetLastError() << endl;
		WSACleanup();
		return 3;
	}
	OVERLAPPED overlapped{};
	overlapped.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	ParentActive = QueryParentInfo(pSocket, overlapped, LastQuery);
	if (!ParentActive)
	{
		wcerr << L"Parent server doesn't respond" << endl;
		closesocket(pSocket);
		CloseHandle(overlapped.hEvent);
		WSACleanup();
		return 4;
	}
	//Make some strings
	const auto serverNameLength = strlen(ServerName);
	for (size_t i = 0; i < serverNameLength; i++)
		serverNameLower[i] = tolower(ServerName[i]);
	serverNameLower[serverNameLength] = '\0';
	strcpy_s(ServerName + serverNameLength, sizeof(ServerName) - serverNameLength, strrchr(LastQuery.Name, '-') - 1);
	char portStr[6];
	_ultoa_s(GamePort, portStr, 10);
	//Initialize Steam GS
	SetEnvironmentVariableW(L"SteamAppId", appId);
	SetEnvironmentVariableW(L"GameAppId", appId);
	if (!SteamGameServer_Init(0, GamePort + 1, 0, 0, eServerModeAuthenticationAndSecure, "1.0.0.0"))
	{
		wcerr << L"SteamGameServer_Init failed" << endl;
		closesocket(pSocket);
		CloseHandle(overlapped.hEvent);
		WSACleanup();
		return 5;
	}
	const auto steamGameServer = SteamGameServer();
	steamGameServer->SetProduct("ark_survival_evolved");
	steamGameServer->SetModDir("ark_survival_evolved");
	steamGameServer->SetGameDescription("ARK: Survival Evolved");
	steamGameServer->SetDedicatedServer(true);
	steamGameServer->SetPasswordProtected(LastQuery.Visibility);
	steamGameServer->SetServerName(ServerName);
	steamGameServer->SetMapName(LastQuery.Map);
	steamGameServer->SetMaxPlayerCount(LastQuery.MaxPlayers);
	Callback callback;
	CCallback<Callback, SteamServersConnected_t, true> cCallback(&callback, &Callback::Run);
	steamGameServer->LogOnAnonymous();
	while (!callback.LoggedOn)
	{
		SteamGameServer_RunCallbacks();
		Sleep(50);
	}
	steamGameServer->EnableHeartbeats(true);
	SteamId = steamGameServer->GetSteamID();
	char steamIdStr[21];
	_ui64toa_s(*reinterpret_cast<const uint64*>(&SteamId), steamIdStr, sizeof(steamIdStr), 10);
	const char* numOpenPubConn= "10";
	bool useTekWrapper = false;
	for (int i = 0; i < LastQuery.NumRules; i++)
	{
		if (!strcmp(LastQuery.Rules[i].Key, "OWNINGID"))
			LastQuery.Rules[i].Value = steamIdStr;
		if (!strcmp(LastQuery.Rules[i].Key, "OWNINGNAME"))
			LastQuery.Rules[i].Value = steamIdStr;
		if (!strcmp(LastQuery.Rules[i].Key, "NUMOPENPUBCONN"))
			numOpenPubConn = LastQuery.Rules[i].Value;
		if (!strcmp(LastQuery.Rules[i].Key, "P2PADDR"))
			LastQuery.Rules[i].Value = steamIdStr;
		if (!strcmp(LastQuery.Rules[i].Key, "P2PPORT"))
			LastQuery.Rules[i].Value = portStr;
		if (!strcmp(LastQuery.Rules[i].Key, "CUSTOMSERVERNAME_s"))
			LastQuery.Rules[i].Value = serverNameLower;
		if (!strcmp(LastQuery.Rules[i].Key, "SEARCHKEYWORDS_s") && !strncmp(LastQuery.Rules[i].Value, "TEKWrapper", 10))
			useTekWrapper = true;
	}
	auto lastNumOpenPubConn = strtoul(numOpenPubConn, nullptr, 10);
	char* cur = GameTagsAndDataBuffer;
	for (int i = 0; i < LastQuery.NumRules; i++)
	{
		if (i)
			*cur++ = ',';
		const auto rule = LastQuery.Rules[i];
		auto length = strlen(rule.Key);
		memcpy(cur, rule.Key, length);
		cur += length;
		*cur++ = ':';
		length = strlen(rule.Value);
		memcpy(cur, rule.Value, length);
		cur += length;
		steamGameServer->SetKeyValue(rule.Key, rule.Value);
	}
	*cur = '\0';
	steamGameServer->SetGameData(GameTagsAndDataBuffer);
	sprintf_s(GameTagsAndDataBuffer, ",OWNINGID:%s,OWNINGNAME:%s,NUMOPENPUBCONN:%s,P2PADDR:%s,P2PPORT:%s,LEGACY_i:0", steamIdStr, steamIdStr, numOpenPubConn, steamIdStr, portStr);
	steamGameServer->SetGameTags(GameTagsAndDataBuffer);
	const auto thread = CreateThread(nullptr, 0, QueryPortListen, reinterpret_cast<LPVOID>(queryPort), 0, nullptr);
	while (!ShutdownRequested)
	{
		Sleep(updateInterval);
		ParentActive = QueryParentInfo(pSocket, overlapped, LastQuery);
		for (int i = 0; i < LastQuery.NumRules; i++)
		{
			if (!strcmp(LastQuery.Rules[i].Key, "OWNINGID"))
				LastQuery.Rules[i].Value = steamIdStr;
			if (!strcmp(LastQuery.Rules[i].Key, "OWNINGNAME"))
				LastQuery.Rules[i].Value = steamIdStr;
			if (!strcmp(LastQuery.Rules[i].Key, "NUMOPENPUBCONN"))
				numOpenPubConn = LastQuery.Rules[i].Value;
			if (!strcmp(LastQuery.Rules[i].Key, "P2PADDR"))
				LastQuery.Rules[i].Value = steamIdStr;
			if (!strcmp(LastQuery.Rules[i].Key, "P2PPORT"))
				LastQuery.Rules[i].Value = portStr;
			if (!strcmp(LastQuery.Rules[i].Key, "CUSTOMSERVERNAME_s"))
				LastQuery.Rules[i].Value = serverNameLower;
		}
		auto newNumOpenPubConn = strtoul(numOpenPubConn, nullptr, 10);
		if (newNumOpenPubConn != lastNumOpenPubConn)
		{
			lastNumOpenPubConn = newNumOpenPubConn;
			cur = GameTagsAndDataBuffer;
			for (int i = 0; i < LastQuery.NumRules; i++)
			{
				if (i)
					*cur++ = ',';
				const auto rule = LastQuery.Rules[i];
				auto length = strlen(rule.Key);
				memcpy(cur, rule.Key, length);
				cur += length;
				*cur++ = ':';
				length = strlen(rule.Value);
				memcpy(cur, rule.Value, length);
				cur += length;
				if (!strcmp(rule.Key, "NUMOPENPUBCONN"))
					steamGameServer->SetKeyValue(rule.Key, rule.Value);
			}
			if (useTekWrapper)
				memcpy(cur, ",TEKWrapper:1", 14);
			else
				*cur = '\0';
			steamGameServer->SetGameData(GameTagsAndDataBuffer);
			sprintf_s(GameTagsAndDataBuffer, ",OWNINGID:%s,OWNINGNAME:%s,NUMOPENPUBCONN:%s,P2PADDR:%s,P2PPORT:%s,LEGACY_i:0", steamIdStr, steamIdStr, numOpenPubConn, steamIdStr, portStr);
			steamGameServer->SetGameTags(GameTagsAndDataBuffer);
		}
	}
	WaitForSingleObject(thread, INFINITE);
	CloseHandle(thread);
	closesocket(pSocket);
	CloseHandle(overlapped.hEvent);
	WSACleanup();
	steamGameServer->LogOff();
	SteamGameServer_Shutdown();
	return 0;
}
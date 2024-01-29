#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
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
	char Players;
	int MaxPlayers;
	bool Visibility;
	bool VAC;
	uint16 GamePort;
	unsigned int NumRules;
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

alignas(16) char Buffer[4096];
alignas(16) char GameTagsAndDataBuffer[k_cbMaxGameServerGameData];
alignas(16) char PlayerDataBuffer[2048];
alignas(16) char ResponseQueryDataBuffer[4096];
QueryResult LastQuery;
ssize_t PlayerDataSize;
sockaddr_in ParentAddress{ AF_INET, 0x8769, { 0 } };
uint32 AppId = 346110;
uint16 GamePort = 7777;
char ServerName[k_cbMaxGameServerName]{ "ArkServerMirror" };
CSteamID SteamId;
constexpr const unsigned char A2S_INFO[]
{
	0xFF, 0xFF, 0xFF, 0xFF, 0x54, 0x53, 0x6F, 0x75, 0x72, 0x63, 0x65, 0x20, 0x45, 0x6E,
	0x67, 0x69, 0x6E, 0x65, 0x20, 0x51, 0x75, 0x65, 0x72, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00
};
bool ParentActive;
bool ShutdownRequested;

static bool QueryParentInfo(int socket, QueryResult& result)
{
	const auto address = reinterpret_cast<const sockaddr*>(&ParentAddress);
	//A2S_INFO
	sendto(socket, A2S_INFO, sizeof(A2S_INFO), 0, address, sizeof(sockaddr_in));
	auto transferred = recvfrom(socket, Buffer, sizeof(Buffer), 0, nullptr, nullptr);
	if (transferred == 9)
	{
		*reinterpret_cast<uint32*>(Buffer + 25) = *reinterpret_cast<const uint32*>(Buffer + 5);
		memcpy(Buffer, A2S_INFO, sizeof(A2S_INFO) - 4);
		sendto(socket, Buffer, sizeof(A2S_INFO), 0, address, sizeof(sockaddr_in));
		if (recvfrom(socket, Buffer, sizeof(Buffer), 0, nullptr, nullptr) < 0)
			return false;
	}
	auto cur = Buffer + 6;
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
	const auto rulesBufferSize = static_cast<size_t>(Buffer + sizeof(Buffer) - cur);
	//A2S_RULES
	*reinterpret_cast<uint64*>(cur) = 0xFFFFFF56FFFFFFFF;
	cur[8] = '\xFF';
	sendto(socket, cur, 9, 0, address, sizeof(sockaddr_in));
	if (recvfrom(socket, cur, 9, 0, nullptr, nullptr) < 0)
		return false;
	cur[4] = 0x56;
	sendto(socket, cur, 9, 0, address, sizeof(sockaddr_in));
	if (recvfrom(socket, cur, rulesBufferSize, 0, nullptr, nullptr) < 0)
		return false;
	cur += 5;
	result.NumRules = *reinterpret_cast<const uint16*>(cur);
	auto rule = reinterpret_cast<Rule*>(Buffer + sizeof(Buffer) - sizeof(Rule) * static_cast<unsigned long>(result.NumRules));
	result.Rules = rule;
	++cur;
	for (unsigned int i = 0; i < result.NumRules; i++, rule++)
	{
		rule->Key = ++cur;
		while (*cur)
			++cur;
		rule->Value = ++cur;
		while (*cur)
			++cur;
	}
	//A2S_PLAYER
	*reinterpret_cast<uint64*>(PlayerDataBuffer) = 0xFFFFFF55FFFFFFFF;
	PlayerDataBuffer[8] = '\xFF';
	sendto(socket, PlayerDataBuffer, 9, 0, address, sizeof(sockaddr_in));
	if (recvfrom(socket, PlayerDataBuffer, 9, 0, nullptr, nullptr) < 0)
		return false;
	PlayerDataBuffer[4] = 0x55;
	sendto(socket, PlayerDataBuffer, 9, 0, address, sizeof(sockaddr_in));
	PlayerDataSize = recvfrom(socket, PlayerDataBuffer, sizeof(PlayerDataBuffer), 0, nullptr, nullptr);
	return PlayerDataSize >= 0;
}
static void* QueryPortListen(void* parameter)
{
	printf("ts\n");
	const auto lSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (lSocket < 0)
	{
		cerr << "socket@QueryPortListen failed with error code " << errno << endl;
		return reinterpret_cast<void*>(7);
	}
	const sockaddr_in localAddress{ AF_INET, htons(static_cast<uint16>(reinterpret_cast<uint64>(parameter))), { 0 } };
	if (bind(lSocket, reinterpret_cast<const sockaddr*>(&localAddress), sizeof(sockaddr_in)) < 0)
	{
		cerr << "bind@QueryPortListen failed with error code " << errno << endl;
		return reinterpret_cast<void*>(8);
	}
	printf("b\n");
	for (;;)
	{
		sockaddr address;
		socklen_t addressLength = sizeof(sockaddr);
		timeval timeout{ 0, 0 };
		setsockopt(lSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeval));
		const auto transferred = recvfrom(lSocket, ResponseQueryDataBuffer, sizeof(A2S_INFO), 0, &address, &addressLength);
		timeout = { 2, 0 };
		setsockopt(lSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeval));
		const auto ip = reinterpret_cast<const sockaddr_in*>(&address)->sin_addr.s_addr;
		const auto head = *reinterpret_cast<const uint32*>(ResponseQueryDataBuffer);
		if (transferred == 4 && (ip == 0 || ip == 0x0100007F) && !head)
		{
			ShutdownRequested = true;
			close(lSocket);
			return 0;
		}
		if (!ParentActive || transferred < 9 || head != 0xFFFFFFFF)
			continue;
		switch (ResponseQueryDataBuffer[4])
		{
			case 0x54:
			{
				*reinterpret_cast<uint16*>(ResponseQueryDataBuffer + 4) = 0x1149;
				auto cur = ResponseQueryDataBuffer + 6;
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
				*cur++ = static_cast<char>(LastQuery.MaxPlayers);
				*cur++ = 0;
				*reinterpret_cast<uint16*>(cur) = 0x7764;
				cur += 2;
				*cur++ = LastQuery.Visibility;
				*cur++ = LastQuery.VAC;
				memcpy(cur, "1.0.0.0", 8);
				cur += 8;
				*cur++ = '\xB1';
				*reinterpret_cast<uint16*>(cur) = GamePort;
				cur += 2;
				*reinterpret_cast<CSteamID*>(cur) = SteamId;
				cur += 8;
				length = strlen(GameTagsAndDataBuffer) + 1;
				memcpy(cur, GameTagsAndDataBuffer, length);
				cur += length;
				*reinterpret_cast<uint64*>(cur) = AppId;
				cur += 8;
				sendto(lSocket, ResponseQueryDataBuffer, static_cast<size_t>(cur - ResponseQueryDataBuffer), 0, &address, addressLength);
				break;
			}
			case 0x55:
			{
				ResponseQueryDataBuffer[4] = 0x41;
				*reinterpret_cast<uint32*>(ResponseQueryDataBuffer + 5) = *reinterpret_cast<const uint32*>(PlayerDataBuffer + 5);
				sendto(lSocket, ResponseQueryDataBuffer, 9, 0, &address, addressLength);
				if (recvfrom(lSocket, ResponseQueryDataBuffer, 9, 0, nullptr, nullptr) > 0)
					sendto(lSocket, PlayerDataBuffer, static_cast<size_t>(PlayerDataSize), 0, &address, addressLength);
				break;
			}
			case 0x56:
			{
				ResponseQueryDataBuffer[4] = 0x41;
				*reinterpret_cast<uint32*>(ResponseQueryDataBuffer + 5) = *reinterpret_cast<const uint32*>(PlayerDataBuffer + 5);
				sendto(lSocket, ResponseQueryDataBuffer, 9, 0, &address, addressLength);
				if (recvfrom(lSocket, ResponseQueryDataBuffer, 9, 0, nullptr, nullptr) < 0)
					break;
				ResponseQueryDataBuffer[4] = 0x45;
				*reinterpret_cast<uint16*>(ResponseQueryDataBuffer + 5) = static_cast<uint16>(LastQuery.NumRules);
				auto cur = ResponseQueryDataBuffer + 7;
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
				sendto(lSocket, ResponseQueryDataBuffer, static_cast<size_t>(cur - ResponseQueryDataBuffer), 0, &address, addressLength);
				break;
			}
		}
	}
	return 0;
}

int main(int argc, char* argv[])
{
	//Variable definitions
	auto queryPort = 27015u;
	char serverNameLower[k_cbMaxGameServerName]{};
	auto pIp = "127.0.0.1";
	auto appId = "346110";
	unsigned int updateInterval = 10;
	//Load arguments
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--gamePort"))
			GamePort = static_cast<uint16>(atoi(argv[++i]));
		else if (!strcmp(argv[i], "--queryPort"))
			queryPort = static_cast<unsigned int>(atoi(argv[++i]));
		else if (!strcmp(argv[i], "--displayName"))
			strcpy(ServerName, argv[++i]);
		else if (!strcmp(argv[i], "--parentIP"))
			pIp = argv[++i];
		else if (!strcmp(argv[i], "--parentQueryPort"))
			ParentAddress.sin_port = htons(static_cast<uint16_t>(atoi(argv[++i])));
		else if (!strcmp(argv[i], "--appIdOverride"))
		{
			appId = argv[++i];
			AppId = static_cast<uint32>(atoi(appId));
		}
		else if (!strcmp(argv[i], "--updateInterval"))
			updateInterval = static_cast<unsigned int>(atoi(argv[++i]));
	}
	//Query parent server
	if (!inet_pton(AF_INET, pIp, &ParentAddress.sin_addr))
	{
		cerr << "inet_pton failed with error code " << errno << endl;
		return 2;
	}
	const auto pSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (pSocket < 0)
	{
		cerr << "socket failed with error code " << errno << endl;
		return 3;
	}
	const timeval timeout{ 2, 0 };
	setsockopt(pSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeval));
	ParentActive = QueryParentInfo(pSocket, LastQuery);
	if (!ParentActive)
	{
		cerr << "Parent server doesn't respond" << endl;
		close(pSocket);
		return 4;
	}
	//Make some strings
	const auto serverNameLength = strlen(ServerName);
	for (size_t i = 0; i < serverNameLength; i++)
		serverNameLower[i] = static_cast<char>(tolower(ServerName[i]));
	serverNameLower[serverNameLength] = '\0';
	strcpy(ServerName + serverNameLength, strrchr(LastQuery.Name, '-') - 1);
	char portStr[6];
	sprintf(portStr, "%hu", GamePort);
	//Initialize Steam GS
	setenv("SteamAppId", appId, 1);
	setenv("GameAppId", appId, 1);
	if (!SteamGameServer_Init(0, GamePort + 1, 0, 0, eServerModeAuthenticationAndSecure, "1.0.0.0"))
	{
		cerr << "SteamGameServer_Init failed" << endl;
		close(pSocket);
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
		sleep(1);
	}
	steamGameServer->EnableHeartbeats(true);
	SteamId = steamGameServer->GetSteamID();
	char steamIdStr[21];
	sprintf(steamIdStr, "%llu", SteamId);
	const char* numOpenPubConn = "10";
	bool useTekWrapper = false;
	for (unsigned int i = 0; i < LastQuery.NumRules; i++)
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
	sprintf(GameTagsAndDataBuffer, ",OWNINGID:%s,OWNINGNAME:%s,NUMOPENPUBCONN:%s,P2PADDR:%s,P2PPORT:%s,LEGACY_i:0", steamIdStr, steamIdStr, numOpenPubConn, steamIdStr, portStr);
	steamGameServer->SetGameTags(GameTagsAndDataBuffer);
	pthread_t thread;
	auto wer = pthread_create(&thread, nullptr, QueryPortListen, reinterpret_cast<void*>(queryPort));
	while (!ShutdownRequested)
	{
		sleep(updateInterval);
		ParentActive = QueryParentInfo(pSocket, LastQuery);
		for (unsigned int i = 0; i < LastQuery.NumRules; i++)
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
			sprintf(GameTagsAndDataBuffer, ",OWNINGID:%s,OWNINGNAME:%s,NUMOPENPUBCONN:%s,P2PADDR:%s,P2PPORT:%s,LEGACY_i:0", steamIdStr, steamIdStr, numOpenPubConn, steamIdStr, portStr);
			steamGameServer->SetGameTags(GameTagsAndDataBuffer);
		}
	}
	pthread_join(thread, nullptr);
	close(pSocket);
	steamGameServer->LogOff();
	SteamGameServer_Shutdown();
	return 0;
}
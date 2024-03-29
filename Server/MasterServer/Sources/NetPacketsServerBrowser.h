#pragma once

#include "r3dNetwork.h"
#include "../../ServerNetPackets/NetPacketsGameInfo.h"
#include "../../ServerNetPackets/NetPacketsWeaponInfo.h"

namespace NetPacketsServerBrowser
{
#pragma pack(push)
#pragma pack(1)

#define SBNET_VERSION		(0x00000105 + GBWEAPINFO_VERSION + GBGAMEINFO_VERSION)
#define SBNET_KEY1		0x1fb2a86e

//
// Server Browser Packet IDs
// 
enum sbpktType_e
{
  SBPKT_ValidateConnectingPeer = r3dNetwork::FIRST_FREE_PACKET_ID,
  
  // supervisor <-> master
  SBPKT_S2M_RegisterMachine,
  SBPKT_M2S_RegisterAns,
  SBPKT_M2S_StartGameReq,
  
  // game <-> master
  SBPKT_G2M_RegisterGame,		// register game session in master server
  SBPKT_M2G_ShutdownNote,
  SBPKT_M2G_KillGame,			// game need to be closed because of master error
  SBPKT_G2M_AddPlayer,			// update info about game session
  SBPKT_G2M_RemovePlayer,		// update info about game session
  SBPKT_G2M_FinishGame,			// game is finished
  SBPKT_G2M_CloseGame,			// close session
  

  SBPKT_M2G_SendPlayerReq,
  SBPKT_M2G_SendPlayerList,
  SBPKT_G2M_SendNoticeMsg,
  SBPKT_M2G_SendPlayerKick,
  SBPKT_M2G_SendPlayerListEnd,
  SBPKT_M2G_BanPlayer,

  // data update packets
  SBPKT_G2M_DataUpdateReq,
  SBPKT_M2G_UpdateWeaponData,
  SBPKT_M2G_UpdateGearData,
  SBPKT_M2G_UpdateItemData,
  SBPKT_M2G_UpdateDataEnd,
  
  SBPKT_LAST_PACKET_ID
};

#if SBPKT_LAST_PACKET_ID > 255
  #error Shit happens, more that 255 packet ids
#endif

#ifndef CREATE_PACKET
#define CREATE_PACKET(PKT_ID, VAR) PKT_ID##_s VAR
#endif

struct SBPKT_ValidateConnectingPeer_s : public r3dNetPacketMixin<SBPKT_ValidateConnectingPeer>
{
	DWORD		version;
	DWORD		key1;
};

struct SBPKT_S2M_RegisterMachine_s  : public r3dNetPacketMixin<SBPKT_S2M_RegisterMachine>
{
	BYTE		region; // GBNET_REGION_Unknown
	char		serverName[64];
	WORD		maxGames;
	WORD		maxPlayers;
	WORD		portStart;
	DWORD		externalIpAddr;
};

struct SBPKT_M2S_RegisterAns_s : public r3dNetPacketMixin<SBPKT_M2S_RegisterAns>
{
	DWORD		id;
};

struct SBPKT_M2S_StartGameReq_s : public r3dNetPacketMixin<SBPKT_M2S_StartGameReq>
{
	DWORD		gameId;
	__int64		sessionId;
	WORD		port;
	uint32_t	creatorID;
	GBGameInfo	ginfo;
};

struct SBPKT_G2M_RegisterGame_s : public r3dNetPacketMixin<SBPKT_G2M_RegisterGame>
{
	DWORD		gameId;
};

struct SBPKT_M2G_KillGame_s : public r3dNetPacketMixin<SBPKT_M2G_KillGame>
{
	BYTE		reason;
};

struct SBPKT_M2G_ShutdownNote_s: public r3dNetPacketMixin<SBPKT_M2G_ShutdownNote>
{
	BYTE		reason;
	float		timeLeft;
};

struct SBPKT_G2M_AddPlayer_s : public r3dNetPacketMixin<SBPKT_G2M_AddPlayer>
{
	DWORD		CustomerID;
};

struct SBPKT_G2M_RemovePlayer_s : public r3dNetPacketMixin<SBPKT_G2M_RemovePlayer>
{
	DWORD		CustomerID;
};

struct SBPKT_G2M_FinishGame_s : public r3dNetPacketMixin<SBPKT_G2M_FinishGame>
{
};

struct SBPKT_G2M_CloseGame_s : public r3dNetPacketMixin<SBPKT_G2M_CloseGame>
{
};

struct SBPKT_G2M_DataUpdateReq_s : public r3dNetPacketMixin<SBPKT_G2M_DataUpdateReq>
{
};

struct SBPKT_M2G_UpdateWeaponData_s : public r3dNetPacketMixin<SBPKT_M2G_UpdateWeaponData>
{
	DWORD		itemId;
	GBWeaponInfo	wi;
};

struct SBPKT_M2G_UpdateGearData_s : public r3dNetPacketMixin<SBPKT_M2G_UpdateGearData>
{
	DWORD		itemId;
	GBGearInfo	gi;
};

struct SBPKT_M2G_UpdateItemData_s : public r3dNetPacketMixin<SBPKT_M2G_UpdateItemData>
{
	DWORD		itemId;
	// for now it'll be used only for lootbox RequiredLevel update
	BYTE		LevelRequired;
};

struct SBPKT_M2G_UpdateDataEnd_s : public r3dNetPacketMixin<SBPKT_M2G_UpdateDataEnd>
{
};

struct SBPKT_M2G_SendPlayerReq_s : public r3dNetPacketMixin<SBPKT_M2G_SendPlayerReq>
{
	DWORD ClientPeerId;
};
struct SBPKT_M2G_SendPlayerList_s : public r3dNetPacketMixin<SBPKT_M2G_SendPlayerList>
{
	DWORD ClientPeerId;
    char		gamertag[32*2];
	int alive;
	int rep;
	int xp;
};
struct SBPKT_G2M_SendNoticeMsg_s : public r3dNetPacketMixin<SBPKT_G2M_SendNoticeMsg>
{
	char msg[512];
};
struct SBPKT_M2G_SendPlayerKick_s : public r3dNetPacketMixin<SBPKT_M2G_SendPlayerKick>
{
	char name[32*2];
};
struct SBPKT_M2G_SendPlayerListEnd_s : public r3dNetPacketMixin<SBPKT_M2G_SendPlayerListEnd>
{
	DWORD ClientPeerId;
};
struct SBPKT_M2G_BanPlayer_s : public r3dNetPacketMixin<SBPKT_M2G_BanPlayer>
{
     char	gamertag[512];
	 int customerid;
};

#pragma pack(pop)

}; // namespace NetPacketsServerBrowser

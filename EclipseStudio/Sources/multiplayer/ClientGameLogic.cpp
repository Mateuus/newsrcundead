#include "r3dPCH.h"
#include "r3d.h"

#include "ClientGameLogic.h"

#include "GameObjects/ObjManag.h"
#include "GameObjects/EventTransport.h"
#include "MasterServerLogic.h"

#include "multiplayer/P2PMessages.h"

#include "ObjectsCode/AI/AI_Player.h"
#include "ObjectsCode/Gameplay/BasePlayerSpawnPoint.h"
#include "ObjectsCode/Gameplay/obj_DroppedItem.h"
#include "ObjectsCode/Gameplay/obj_Note.h"
#include "ObjectsCode/Gameplay/obj_Grave.h"
//#include "ObjectsCode/Gameplay/obj_Repair.h"
#include "ObjectsCode/Gameplay/obj_SafeLock.h"
#include "ObjectsCode/Gameplay/obj_IndustGen.h"
#include "ObjectsCode/Gameplay/obj_BlockLight.h"
#include "ObjectsCode/Gameplay/obj_ClayMore.h"
#include "ObjectsCode/Gameplay/obj_Zombie.h"
#include "ObjectsCode/weapons/Weapon.h"
#include "ObjectsCode/weapons/WeaponArmory.h"
#include "ObjectsCode/weapons/Ammo.h"
#include "ObjectsCode/weapons/Barricade.h"

#include "Rendering/Deffered/D3DMiscFunctions.h"

#include "GameCode/UserProfile.h"
#include "GameCode/UserSettings.h"
#include "Gameplay_Params.h"
#include "GameLevel.h"

#include "ui/m_LoadingScreen.h"
#include "ui/HUDDisplay.h"

#include "GameObjects/obj_Vehicle.h"

extern HUDDisplay*	hudMain;
extern int g_RenderScopeEffect;

// VMProtect code block
#if USE_VMPROTECT
  #pragma optimize("g", off)
#endif

static r3dSec_type<ClientGameLogic*, 0xC7FA1DB5> g_pClientLogic = NULL;

void ClientGameLogic::CreateInstance()
{
	VMPROTECT_BeginVirtualization("ClientGameLogic::CreateInstance");
	r3d_assert(g_pClientLogic == NULL);
	g_pClientLogic = new ClientGameLogic();
	VMPROTECT_End();
}

void ClientGameLogic::DeleteInstance()
{
	VMPROTECT_BeginVirtualization("ClientGameLogic::DeleteInstance");
	SAFE_DELETE(g_pClientLogic);
	VMPROTECT_End();
}

ClientGameLogic* ClientGameLogic::GetInstance()
{
	VMPROTECT_BeginMutation("ClientGameLogic::GetInstance");
	r3d_assert(g_pClientLogic);
	return g_pClientLogic;
	VMPROTECT_End();
}

obj_Player* ClientGameLogic::GetPlayer(int idx) const
{
	VMPROTECT_BeginMutation("ClientGameLogic::GetPlayer");
	r3d_assert(idx < MAX_NUM_PLAYERS);
	return players2_[idx].ptr;
	VMPROTECT_End();
}

void ClientGameLogic::SetPlayerPtr(int idx, obj_Player* ptr)
{
	VMPROTECT_BeginMutation("ClientGameLogic::SetPlayerPtr");
	r3d_assert(idx < MAX_NUM_PLAYERS);
	players2_[idx].ptr  = ptr;

	if(idx >= CurMaxPlayerIdx)
		CurMaxPlayerIdx = idx + 1;

	VMPROTECT_End();
}

#if USE_VMPROTECT
  #pragma optimize("g", on)
#endif



static const int HOST_TIME_SYNC_SAMPLES	= 20;

static void preparePacket(const GameObject* from, DefaultPacket* packetData)
{
	r3d_assert(packetData);
	//r3d_assert(packetData->EventID >= 0);

	if(from) {
		r3d_assert(from->GetNetworkID());
		// r3d_assert(from->NetworkLocal);

		packetData->FromID = toP2pNetId(from->GetNetworkID());
	} else {
		packetData->FromID = 0; // world event
	}

	return;
}

bool g_bDisableP2PSendToHost = false;
void p2pSendToHost(const GameObject* from, DefaultPacket* packetData, int packetSize, bool guaranteedAndOrdered)
{
	extern bool g_bEditMode;
	if(g_bEditMode)
		return;
	if(g_bDisableP2PSendToHost)
		return;
	if(!gClientLogic().serverConnected_)
		return;

	preparePacket(from, packetData);
	gClientLogic().net_->SendToHost(packetData, packetSize, guaranteedAndOrdered);
}

ClientGameLogic::ClientGameLogic()
{
	Reset();
}

void ClientGameLogic::Reset()
{
	net_lastFreeId    = NETID_OBJECTS_START;

	serverConnected_ = false;
	gameShuttedDown_ = false;

	disconnectStatus_ = 0;

	cheatAttemptReceived_ = false;
	cheatAttemptCheatId_  = 0;
	nextSecTimeReport_    = 0xFFFFFFFF;
	gppDataSeed_          = 0;
	d3dCheatSent_         = false;
	d3dCheatSent2_		  = false;

	m_highPingTimer		  = 0;

	gameJoinAnswered_ = false;
	gameStartAnswered_ = false;
	serverVersionStatus_ = 0;
	m_gameInfo = GBGameInfo();
	m_sessionId = 0;

	localPlayerIdx_   = -1;
	localPlayer_      = NULL;
	localPlayerConnectedTime = 0;

	CurMaxPlayerIdx = 0;
	for(int i=0; i<MAX_NUM_PLAYERS; i++) {
		SetPlayerPtr(i, NULL);
	}
	CurMaxPlayerIdx = 0; // reset it after setting to NULLs

	for(int i=0; i<R3D_ARRAYSIZE(playerNames); i++)
	{
		playerNames[i].Gamertag[0] = 0;
		playerNames[i].plrRep = 0;
		playerNames[i].isLegend = false;
		playerNames[i].isDev = false;
		playerNames[i].isPunisher = false;
		playerNames[i].isInvitePending = false;
		playerNames[i].MeCustomerID = 0;
		playerNames[i].ShowCustomerID = 0;
		playerNames[i].IsPremium = false;
	}

	for(int i=0; i<R3D_ARRAYSIZE(HelpCalls); i++)
	{
		HelpCalls[i].DistressText[0] = 0;
		HelpCalls[i].pos = r3dPoint3D(0,0,0);
		HelpCalls[i].rewardText[0] = 0;
	//	playerNames[i].isGroups = false;
	}
	// clearing scoping.  Particularly important for Spectator modes.
	g_RenderScopeEffect = 0;
}

ClientGameLogic::~ClientGameLogic()
{
	g_net.Deinitialize();
}

void ClientGameLogic::OnNetPeerConnected(DWORD peerId)
{
#ifndef FINAL_BUILD
	r3dOutToLog("peer%02d connected\n", peerId);
#endif
	serverConnected_ = true;
	return;
}

void ClientGameLogic::OnNetPeerDisconnected(DWORD peerId)
{
	r3dOutToLog("***** disconnected from game server\n");
	serverConnected_ = false;
	return;
}
void ClientGameLogic::SendBuyItemReq(int ItemID, int GP, int GD)
{
	isBuyAns = false;

    PKT_C2S_BuyItemReq_s n;
	n.ItemID = ItemID;
	n.ansCode = 0;
	n.GP = GP;
	n.GD = GD;
	p2pSendToHost(gClientLogic().localPlayer_, &n, sizeof(n));
}
void ClientGameLogic::OnNetData(DWORD peerId, const r3dNetPacketHeader* packetData, int packetSize)
{
	R3DPROFILE_FUNCTION("ClientGameLogic::OnNetData");

	r3d_assert(packetSize >= sizeof(DefaultPacket));
	const DefaultPacket* evt = static_cast<const DefaultPacket*>(packetData);

	GameObject* fromObj = NULL;
	if(evt->FromID != 0)
	{
		fromObj = GameWorld().GetNetworkObject(evt->FromID);
	}

	//r3dOutToLog("OnNetData from peer%d, obj:%d(%s), event:%d\n", peerId, evt->FromID, fromObj ? fromObj->Name.c_str() : "", evt->EventID);

	// pass to world event processor first.
	if(ProcessWorldEvent(fromObj, evt->EventID, peerId, packetData, packetSize))
		return;

	if(evt->FromID && fromObj == NULL)
	{
		r3dError("bad event %d sent from non registered object %d\n", evt->EventID, evt->FromID);
		return;
	}

	if(fromObj)
	{
		r3d_assert(!(fromObj->ObjFlags & OBJFLAG_JustCreated)); // just to make sure

		if(!fromObj->OnNetReceive(evt->EventID, packetData, packetSize))
			r3dError("bad event %d for %s", evt->EventID, fromObj->Class->Name.c_str());
		return;
	}

	r3dError("bad world event %d", evt->EventID);
	return;
}

r3dPoint3D ClientGameLogic::AdjustSpawnPositionToGround(const r3dPoint3D& pos)
{
	//
	// detect 'ground' under spawn position.
	// because server now send exact position and it might be under geometry if it was changed
	//
	PxRaycastHit hit;
	PxSceneQueryFilterData filter(PxFilterData(COLLIDABLE_PLAYER_COLLIDABLE_MASK,0,0,0), PxSceneQueryFilterFlags(PxSceneQueryFilterFlag::eSTATIC));
	if(!g_pPhysicsWorld->raycastSingle(PxVec3(pos.x, pos.y+1.0f, pos.z), PxVec3(0,-1,0), 1.2f, PxSceneQueryFlags(PxSceneQueryFlag::eIMPACT), hit, filter))
		return pos + r3dPoint3D(0, 1.0f, 0);

	return r3dPoint3D(hit.impact.x, hit.impact.y + 0.1f, hit.impact.z);
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_C2S_ValidateConnectingPeer)
{
	serverVersionStatus_ = 1;
	if(n.protocolVersion != P2PNET_VERSION)
	{
		r3dOutToLog("Version mismatch our:%d, server:%d\n", P2PNET_VERSION, n.protocolVersion);
		serverVersionStatus_ = 2;
	}

	return;
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_C2C_PacketBarrier)
{
	// sanity check: must be only for local networked objects
	if(fromObj && !fromObj->NetworkLocal) {
		r3dError("PKT_C2C_PacketBarrier for %s, %s, %d\n", fromObj->Name.c_str(), fromObj->Class->Name.c_str(), fromObj->GetNetworkID());
	}

	// reply back
	PKT_C2C_PacketBarrier_s n2;
	p2pSendToHost(fromObj, &n2, sizeof(n2));
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_JoinGameAns)
{
#ifndef FINAL_BUILD
	r3dOutToLog("PKT_S2C_JoinGameAns: %d %d\n", n.success, n.playerIdx);
#endif

	if(n.success != 1) {
		r3dOutToLog("Can't join to game server - session is full");
		return;
	}

	localPlayerIdx_   = n.playerIdx;
	gameJoinAnswered_ = true;

	m_gameInfo        = n.gameInfo;
	gameStartUtcTime_ = n.gameTime;
	gameStartTime_    = r3dGetTime();

	lastShadowCacheReset_ = -1;
	UpdateTimeOfDay();

	g_num_matches_played->SetInt(g_num_matches_played->GetInt()+1);
	void writeGameOptionsFile();
	writeGameOptionsFile();
	if(!gUserSettings.isInRecentGamesList(n.gameInfo.gameServerId))
	{
		gUserSettings.addGameToRecent(n.gameInfo.gameServerId);
	}
	gUserSettings.saveSettings();

	return;
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_ShutdownNote)
{
	gameShuttedDown_ = true;

	char msg[128];
	sprintf(msg, "SERVER SHUTDOWN in %.0f sec", n.timeLeft);
	if(hudMain)
	{
		hudMain->showChat(true);
		hudMain->addChatMessage(1, "<system>", msg, 0);
	}
	return;
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_SetGamePlayParams)
{
	// replace our game parameters with ones from server.
	r3d_assert(GPP);
	*const_cast<CGamePlayParams*>(GPP) = n.GPP_Data;
	gppDataSeed_ = n.GPP_Seed;

	return;
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_StartGameAns)
{
#ifndef FINAL_BUILD
	r3dOutToLog("OnPKT_S2C_StartGameAns, %d\n", n.result);
#endif

	r3d_assert(gameStartAnswered_ == false);
	gameStartAnswered_ = true;
	gameStartResult_   = n.result;
	return;
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_PositionVehicle) // Server Vehicles
{
	GameObject* from = GameWorld().GetNetworkObject(n.spawnID);
	if(from)
	{
		const float fTimePassed = r3dGetFrameTime();
		obj_Vehicle* ResPawnCar = (obj_Vehicle*)from;
		if (n.RespawnCar==true)
		{
			if (!ResPawnCar)
				return;
			r3dOutToLog("VehicleID: %i is Respawned\n",n.FromID);
		if (ResPawnCar->m_ParticleTracer)
		{
			ResPawnCar->m_ParticleTracer->SetPosition(n.spawnPos);
			ResPawnCar->m_ParticleTracer->bKill=true;
			ResPawnCar->m_ParticleTracer = NULL;
		}

		if (ResPawnCar->m_Particlebomb)
			ResPawnCar->m_Particlebomb = NULL;

		if (ResPawnCar->CollisionCar != NULL)
		{
			ResPawnCar->CollisionCar->SetPosition(n.spawnPos);
			ResPawnCar->CollisionCar->SetRotationVector(n.RotationPos + r3dPoint3D(90,0,0));
		}
			ResPawnCar->SetPosition(n.spawnPos);
			ResPawnCar->SetRotationVector(n.RotationPos);
			ResPawnCar->DamageCar=n.DamageCar;
			ResPawnCar->GasolineCar=n.GasolineCar;
			ResPawnCar->Occupants=n.OccupantsVehicle;
			ResPawnCar->DestroyOnWater=false;
			ResPawnCar->curTime=0;
			ResPawnCar->BombTime=0;
			ResPawnCar->bOn=false;
			if (ResPawnCar->Light)
				ResPawnCar->Light->SetPosition(n.spawnPos+r3dPoint3D(0,2,0));
		}
		else {
			if (ResPawnCar)
			{
			   if (!ResPawnCar->NetworkLocal)
			   {
				
				if (ResPawnCar->CollisionCar != NULL)
				{
					if (n.DamageCar>0)
					{
					ResPawnCar->CollisionCar->SetPosition(n.spawnPos);
					ResPawnCar->CollisionCar->SetRotationVector(n.RotationPos + r3dPoint3D(90,0,0));
					}
				}
				if (n.DamageCar>0)
				{
					ResPawnCar->SetRotationVector(n.RotationPos);
					ResPawnCar->SetPosition(n.spawnPos);
				}
				ResPawnCar->Occupants=n.OccupantsVehicle;
				ResPawnCar->GasolineCar=n.GasolineCar;
				ResPawnCar->DamageCar=n.DamageCar;
				ResPawnCar->RPMPlayer=n.RPMPlayer;
				ResPawnCar->RotationSpeed=n.RotationSpeed;
				ResPawnCar->bOn=n.bOn;
				//r3dOutToLog("VehicleID: %i is have %i passengers\n",n.FromID,n.OccupantsVehicle);
				g_pPhysicsWorld->m_VehicleManager->DoUserCarControl(n.timeStep,true,n.controlData,n.spawnID);
			   }
			}
		}
	}
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_GroupData)
{
	switch (n.State)
	{
	default:
		break;
	case 1:
		{
			break;
		}
	case 2:
		{
			break;
		}
	case 3:
		{
			char text[128];
			sprintf(text, "%s $InfoMsg_GroupInviteDecline",n.intogamertag);
			hudMain->showMessage(gLangMngr.getString(text));	
			break;
		}
	case 4:
		{
			if (!hudMain || !gClientLogic().localPlayer_)
				return;
			if (hudMain)
			{
				gClientLogic().localPlayer_->MaxGroupPlayers=n.PlayersOnGroup;

				for(int u=0;u<11;u++)
				{
					strcpy(gClientLogic().localPlayer_->PlayersNames[u],"");
					gClientLogic().localPlayer_->IDSPlayers[u]=0;
					gClientLogic().localPlayer_->PlayersOnGroup[u]=false;
				}

				for(int i=0;i<11;i++)
				{
					if (n.NetworkIDPlayer[i]>0)
					{
							if (strcmp(hudMain->GametagName,"") == 0 && !gClientLogic().localPlayer_->imOnGruop)
							{
								gClientLogic().localPlayer_->imOnGruop=true;
								hudMain->TimeToWaitGroup=r3dGetTime();
							}
							strcpy(gClientLogic().localPlayer_->PlayersNames[i],n.gametag[i]);
							gClientLogic().localPlayer_->IDSPlayers[i]=n.NetworkIDPlayer[i];
							if (strcmp(gClientLogic().localPlayer_->PlayersNames[i],"") != 0)
								gClientLogic().localPlayer_->PlayersOnGroup[i]=true;
							else
								gClientLogic().localPlayer_->PlayersOnGroup[i]=false;

								hudMain->addPlayerToGroup(n.gametag[i],n.GameLeader[i]);
								gClientLogic().localPlayer_->CheckGruoupPlayer(n.NetworkIDPlayer[i],n.PlayerPosition[i]);
					}
				}
			}
			break;
		}
	case 5:
		{
			if (n.status == 2)
			{
				hudMain->currentinvite = true;
			}
			if (strcmp(n.GametagLeader,"") != 0)
			{
				for(int i=0; i<R3D_ARRAYSIZE(gClientLogic().playerNames); i++)
				{
					if (strcmp(gClientLogic().playerNames[i].Gamertag,n.GametagLeader) == 0 )
					{
						if (!playerNames[i].isInvitePending)
						{
							if (!gClientLogic().localPlayer_->imOnGruop)
							{
								playerNames[i].isInvitePending = true;						
								char text[128];
								sprintf(text,"$InfoMsg_GroupRcvdInviteFrom %s",n.GametagLeader);
								hudMain->showMessage(gLangMngr.getString(text));
								return;
							}
							else {
								char text[128];
								sprintf(text,"$InfoMsg_YouHaveBeenInvited");
								hudMain->showMessage(gLangMngr.getString(text));
								return;
							}
						}
						break;
					}
				}
			}
			break;
		}
	case 6: // leave player group
		{
			if (!gClientLogic().localPlayer_)
				return;

			if (strcmp(n.MyNickName,gClientLogic().localPlayer_->CurLoadout.Gamertag) == 0)
			{
				gClientLogic().localPlayer_->PlayerIDGroupLeave=true;
				strcpy(hudMain->GametagName,"");
				gClientLogic().localPlayer_->imOnGruop=false;
				gClientLogic().localPlayer_->CurLoadout.GroupID=0;

				for (int i=0;i<11;i++)
				{
					hudMain->removePlayerFromGroup(gClientLogic().localPlayer_->PlayersNames[i],false);
					strcpy(gClientLogic().localPlayer_->PlayersNames[i],"");
					gClientLogic().localPlayer_->IDSPlayers[i]=0;
					gClientLogic().localPlayer_->PlayersOnGroup[i]=false;
				}

				gClientLogic().localPlayer_->CurLoadout.GroupID=0;
				gClientLogic().localPlayer_->MaxGroupPlayers=0;
				hudMain->TimeToWaitGroup=0;
				hudMain->showMessage(gLangMngr.getString("$InfoMsg_YouLeaveGroup"));
				return;
			}

			if (gClientLogic().localPlayer_->CurLoadout.GroupID==n.MyID && gClientLogic().localPlayer_->CurLoadout.GroupID != 0)
			{
				bool PlayerOnlist=false;

				for (int i=0;i<11;i++)
				{
					if (strcmp(gClientLogic().localPlayer_->PlayersNames[i],n.MyNickName) == 0)
					{
						strcpy(gClientLogic().localPlayer_->PlayersNames[i],"");
						gClientLogic().localPlayer_->IDSPlayers[i]=0;
						gClientLogic().localPlayer_->PlayersOnGroup[i]=false;
						PlayerOnlist=true;
						break;
					}
				}

				if (!PlayerOnlist)
					  return;

				hudMain->removePlayerFromGroup(n.MyNickName,false);

				GameObject* from = GameWorld().GetNetworkObject(n.IDPlayerOutGroup);

				if (from)
				{
					obj_Player* targetPlr = (obj_Player*)from;
					targetPlr->CurLoadout.GroupID = 0;
				}
				
				gClientLogic().localPlayer_->MaxGroupPlayers=0;
				
				for (int u=0;u<10;u++)
				{
					if (strcmp(gClientLogic().localPlayer_->PlayersNames[u],"") != 0)
						gClientLogic().localPlayer_->MaxGroupPlayers++;
				}

				char text[128];
				sprintf(text,"%s $InfoMsg_LeaveGroup_msg",n.MyNickName);
				hudMain->showMessage(gLangMngr.getString(text));

				char NewList[11][512];
				int NewIDS[11];
				bool PlayersGRP[11];
				int count = 0;
			    
				for (int po=0;po<11;po++)
				{
					strcpy(NewList[po],"");
					NewIDS[po] = 0;
					PlayersGRP[po] = false;
				}

				for (int e=0;e<10;e++)
				{
					if (strcmp(gClientLogic().localPlayer_->PlayersNames[e],"") != 0)
					{
						strcpy(NewList[count],gClientLogic().localPlayer_->PlayersNames[e]);
						NewIDS[count]=gClientLogic().localPlayer_->IDSPlayers[e];
						PlayersGRP[count]=gClientLogic().localPlayer_->PlayersOnGroup[e];
						count++;
					}
					strcpy(gClientLogic().localPlayer_->PlayersNames[e],"");
					gClientLogic().localPlayer_->IDSPlayers[e]=0;
					gClientLogic().localPlayer_->PlayersOnGroup[e]=0;
				}
				for (int i=0;i<10;i++)
				{
					strcpy(gClientLogic().localPlayer_->PlayersNames[i],NewList[i]);
					gClientLogic().localPlayer_->IDSPlayers[i]=NewIDS[i];
					gClientLogic().localPlayer_->PlayersOnGroup[i]=PlayersGRP[i];
				}

				if (strcmp(gClientLogic().localPlayer_->PlayersNames[0],gClientLogic().localPlayer_->CurLoadout.Gamertag) != 0)
				{
					// update leader
					strcpy(hudMain->GametagName,gClientLogic().localPlayer_->PlayersNames[0]);
					hudMain->updateLeaderGroup(gClientLogic().localPlayer_->PlayersNames[0]);
				}
				else {
					strcpy(hudMain->GametagName,"");
					hudMain->updateLeaderGroup(gClientLogic().localPlayer_->CurLoadout.Gamertag);
				}
			}
			break;
		}
	case 7:
		{
			gClientLogic().localPlayer_->CheckGruoupPlayer(n.IDPlayer,n.Position);
			break;
		}
	case 8:
		{
			char text[128];
			sprintf(text, "%s $InfoMsg_InOtherGroup",n.pName);
			hudMain->showMessage(gLangMngr.getString(text));
			break;
		}
	case 9:
		{
			if (gClientLogic().localPlayer_->CurLoadout.GroupID==n.MyID && gClientLogic().localPlayer_->CurLoadout.GroupID != 0)
			{
				char text[128];
				sprintf(text,"%s $InfoMsg_GroupWantLeave",n.fromgamertag);
				if (strcmp(gClientLogic().localPlayer_->CurLoadout.Gamertag,n.fromgamertag) != 0)
					hudMain->showMessage(gLangMngr.getString(text));

				if (strcmp(gClientLogic().localPlayer_->CurLoadout.Gamertag,n.fromgamertag) == 0)
				{
					gClientLogic().localPlayer_->LeavingGroup=r3dGetTime();
					gClientLogic().localPlayer_->StartToLeaveGroup=true;
					hudMain->aboutToLeavePlayerFromGroup(n.fromgamertag);
				}
				else {
					hudMain->aboutToLeavePlayerFromGroup(n.fromgamertag);
				}
			}
			break;
		}
	}
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_C2C_ChatMessage)
{
	hudMain->showChat(true);
	hudMain->addChatMessage(n.msgChannel, n.gamertag, n.msg, n.userFlag);
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_C2S_SendHelpCall)
{
		gClientLogic().HelpCalls[n.FromID].pos = n.pos;
		hudMain->showMessage(gLangMngr.getString("HUD_CallForHelp_RequestSent"));
}
IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_SendHelpCallData)
{
		sprintf(gClientLogic().HelpCalls[n.peerid].DistressText,n.DistressText);
	 	sprintf(gClientLogic().HelpCalls[n.peerid].rewardText,n.rewardText);
		gClientLogic().HelpCalls[n.peerid].pos = n.pos;
		hudMain->showMessage(gLangMngr.getString("HUD_CallForHelp_RequestSent"));
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_C2S_SafelockData)
{
	switch(n.Status)
	{
	case 4:
		{
			if (n.GameServerID == gClientLogic().m_gameInfo.gameServerId)
			{
				GameObject* obj = GameWorld().GetNetworkObject(n.SafelockID);

				if (!obj)
					return;

				if (obj->GetNetworkID() != n.SafelockID)
					return;

				if (obj->Class->Name == "obj_SafeLock")
				{
					obj_SafeLock* Safelock = (obj_SafeLock*)obj;
					strcpy(Safelock->Password,n.Password);
					Safelock->IDSafe = n.SafeID;
				}
			}
			break;
		}
	case 5:
		{
			if (n.State==1)
			{
				const wchar_t* text=gLangMngr.getString("$SafelockInUse");
				hudMain->showMessage(text);
				return;
			}
			if (gClientLogic().localPlayer_)
			{
				if (gClientLogic().localPlayer_->GetNetworkID() == n.CustomerID)
				{
					gClientLogic().localPlayer_->OpenSafelock(n.iDSafeLock,n.Password,n.DateExpire);
				}
			}
			break;
		}
	case 7:
		{
			GameObject* obj = GameWorld().GetNetworkObject(n.IDNetwork);

			if (!obj)
				return;

			obj->setActiveFlag(0);
			break;
		}
	}

	//r3dOutToLog("###### SAFELOCK ID %i PASSWORD %s\n",n.SafeID,n.Password);
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_UpdateWeaponData)
{
	WeaponConfig* wc = const_cast<WeaponConfig*>(g_pWeaponArmory->getWeaponConfig(n.itemId));
#ifdef FINAL_BUILD
	if(wc)
		wc->copyParametersFrom(n.wi);
	return;
#endif

	if(wc == NULL) {
		r3dOutToLog("!!! got update for not existing weapon %d\n", n.itemId);
		return;
	}

	wc->copyParametersFrom(n.wi);

	//r3dOutToLog("got update for weapon %s\n", wc->m_StoreName);

	static float lastMsgTime = 0;
	if(r3dGetTime() > lastMsgTime + 1.0f) {
		lastMsgTime = r3dGetTime();
		hudMain->showMessage(gLangMngr.getString("InfoMsg_WeaponDataUpdated"));
	}

	return;
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_UpdateGearData)
{
	GearConfig* gc = const_cast<GearConfig*>(g_pWeaponArmory->getGearConfig(n.itemId));
#ifdef FINAL_BUILD
	if(gc)
		gc->copyParametersFrom(n.gi);
	return;
#endif

	if(gc == NULL) {
		r3dOutToLog("!!! got update for not existing gear %d\n", n.itemId);
		return;
	}

	gc->copyParametersFrom(n.gi);

	r3dOutToLog("got update for gear %s\n", gc->m_StoreName);
	return;
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_CreateNetObject)
{
	if(n.itemID == WeaponConfig::ITEMID_BarbWireBarricade ||
		n.itemID == WeaponConfig::ITEMID_WoodShieldBarricade ||
		n.itemID == WeaponConfig::ITEMID_RiotShieldBarricade ||
		n.itemID == WeaponConfig::ITEMID_SandbagBarricade ||
		n.itemID == WeaponConfig::ITEMID_BlockDoorWood ||
        //n.itemID == WeaponConfig::ITEMID_BlockLight ||
        n.itemID == WeaponConfig::ITEMID_BlockSolarWater ||
        n.itemID == WeaponConfig::ITEMID_BlockWallBrickShort ||
        n.itemID == WeaponConfig::ITEMID_BlockWallBrick ||
        n.itemID == WeaponConfig::ITEMID_BlockWallMetal ||
        n.itemID == WeaponConfig::ITEMID_BlockWallWood ||
        n.itemID == WeaponConfig::ITEMID_BlockDoorWood2M01 ||
        n.itemID == WeaponConfig::ITEMID_BlockWallMetal2M01 ||
        n.itemID == WeaponConfig::ITEMID_BlockWallBrickTall ||
        n.itemID == WeaponConfig::ITEMID_BlockWallWood2M01 ||
        n.itemID == WeaponConfig::ITEMID_BlockWallBrickShort2 ||
        n.itemID == WeaponConfig::ITEMID_BlockFarm ||
        n.itemID == WeaponConfig::ITEMID_BlockSolarWater2 ||
        n.itemID == WeaponConfig::ITEMID_BlockLight2 ||
        n.itemID == WeaponConfig::ITEMID_BlockFarmCrate ||
		//n.itemID == WeaponConfig::ITEMID_IndustGenerator ||
		n.itemID == WeaponConfig::ITEMID_BlockEnergy)
	{
		obj_Barricade* shield= (obj_Barricade*)srv_CreateGameObject("obj_Barricade", "shield", n.pos);
		shield->m_ItemID	= n.itemID;
		shield->SetNetworkID(n.spawnID);
		shield->m_RotX		= n.var1;
		shield->SetRotationVector(r3dVector(n.var1, 0, 0));
		shield->OnCreate();
		//SoundSys.PlayAndForget(SoundSys.GetEventIDByPath("Sounds/WarZ/PlayerSounds/PLAYER_DROPITEM"), n.pos);
	}
	if (n.itemID == WeaponConfig::ITEMID_PersonalLocker)
	{
	    obj_SafeLock* obj = (obj_SafeLock*)srv_CreateGameObject("obj_SafeLock", "obj_SafeLock", n.pos);
		obj->SetNetworkID(n.spawnID);
		obj->SetPosition(n.pos);
		obj->SetRotationVector(r3dVector(n.var1, 0, 0));
		obj->OnCreate();
		//SoundSys.PlayAndForget(SoundSys.GetEventIDByPath("Sounds/WarZ/PlayerSounds/PLAYER_DROPITEM"), n.pos);
	} 
	if (n.itemID == WeaponConfig::ITEMID_IndustGenerator)
	{
	    obj_IndustGen* obj = (obj_IndustGen*)srv_CreateGameObject("obj_IndustGen", "obj_IndustGen", n.pos);
		obj->SetNetworkID(n.spawnID);
		obj->SetPosition(n.pos);
		obj->SetRotationVector(r3dVector(n.var1, 0, 0));
		obj->OnCreate();
	}
	if (n.itemID == WeaponConfig::ITEMID_ClayMore)
	{
		obj_ClayMore* obj = (obj_ClayMore*)srv_CreateGameObject("obj_ClayMore", "obj_ClayMore", n.pos);
		obj->SetNetworkID(n.spawnID);
		obj->m_RotX		= n.var1;
		obj->SetRotationVector(r3dVector(n.var1, 0, 0));
		obj->OnCreate();
	}
	if (n.itemID == WeaponConfig::ITEMID_BlockLight)
	{
	    obj_BlockLight* obj = (obj_BlockLight*)srv_CreateGameObject("obj_BlockLight", "obj_BlockLight", n.pos);
		obj->SetNetworkID(n.spawnID);
		obj->SetPosition(n.pos);
		obj->SetRotationVector(r3dVector(n.var1, 0, 0));
		obj->OnCreate();
	}

	return;
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_DestroyNetObject)
{
	GameObject* obj = GameWorld().GetNetworkObject(n.spawnID);
	r3d_assert(obj);

	if(obj->isObjType(OBJTYPE_Human))
	{
		int playerIdx = obj->GetNetworkID() - NETID_PLAYERS_START;
		SetPlayerPtr(playerIdx, NULL);
	}
	obj->setActiveFlag(0);

	//@TODO: do something when local player was dropped
	if(obj == localPlayer_)
	{
		r3dOutToLog("local player dropped by server\n");
	}

	return;
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_CreateDroppedItem)
{
	//r3dOutToLog("obj_DroppedItem %d %d\n", n.spawnID, n.Item.itemID);
	r3d_assert(GameWorld().GetNetworkObject(n.spawnID) == NULL);

	obj_DroppedItem* obj = (obj_DroppedItem*)srv_CreateGameObject("obj_DroppedItem", "obj_DroppedItem", n.pos);
	obj->SetNetworkID(n.spawnID);
	obj->m_Item    = n.Item;
	obj->SpawnedItem = n.SpawnedItem;
	obj->LootID = n.LootID;
	obj->OnCreate();
}

/*IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_CreateRepairBench)
{
	r3d_assert(GameWorld().GetNetworkObject(n.spawnID) == NULL);

	obj_Repair* obj = (obj_Repair*)srv_CreateGameObject("obj_Repair", "obj_Repair", n.pos);
	obj->SetNetworkID(n.spawnID);
	obj->SetPosition(n.pos);
	obj->SetRotationVector(n.rot);
	obj->OnCreate();
	r3dOutToLog("CreateBench\n");
}
*/
IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_CreateGrave)
{
	r3d_assert(GameWorld().GetNetworkObject(n.spawnID) == NULL);

	obj_Grave* obj = (obj_Grave*)srv_CreateGameObject("obj_Grave", "obj_Grave", n.pos);
	obj->SetNetworkID(n.spawnID);
	obj->OnCreate();
	r3dOutToLog("CreateGrave\n");
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_CreateNote)
{
	r3dOutToLog("obj_Note %d\n", n.spawnID);
	r3d_assert(GameWorld().GetNetworkObject(n.spawnID) == NULL);

	obj_Note* obj = (obj_Note*)srv_CreateGameObject("obj_Note", "obj_Note", n.pos);
	obj->SetNetworkID(n.spawnID);
	obj->OnCreate();
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_SetGraveData)
{
	obj_Grave* obj = (obj_Grave*)fromObj;
	r3d_assert(obj);

	obj->m_GotData = true;
	obj->m_plr1 = n.plr1;
	obj->m_plr2 = n.plr2;

	hudMain->showGraveNote(obj->m_plr1.c_str(),obj->m_plr2.c_str());
	r3dOutToLog("SetGraveData\n");
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_SetNoteData)
{

			obj_Note* obj = (obj_Note*)fromObj;
			r3d_assert(obj);

			obj->m_GotData = true;
			obj->m_TextFrom = n.TextFrom;
			obj->m_Text = n.TextSubj;

			hudMain->showReadNote(obj->m_Text.c_str());
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_CreateVehicle) // Server Vehicles
{
#if VEHICLES_ENABLED
	r3d_assert(GameWorld().GetNetworkObject(n.spawnID) == NULL);

	r3dOutToLog("Created vehicle -> %s\n",n.vehicle);

	obj_Vehicle* obj = (obj_Vehicle*)srv_CreateGameObject("obj_Vehicle", n.vehicle, n.spawnPos);
	obj->DisablePhysX = true;
	obj->SetNetworkID(n.spawnID);
	obj->NetworkLocal = false;
	obj->m_isSerializable = true;
	obj->SetRotationVector(n.spawnDir);
	obj->GasolineCar=n.Gasolinecar;
	obj->Occupants=n.Ocuppants;
	obj->DamageCar=n.DamageCar;
	obj->FirstRotationVector=n.FirstRotationVector;
	obj->FirstPosition=n.FirstPosition;
	obj->OnCreate();

	// set base cell for movement data (must do it AFTER OnCreate())
	obj->netMover.SetStartCell(n.moveCell);
#endif
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_CreateZombie)
{
	//r3dOutToLog("obj_Zombie %d\n", n.spawnID);
	r3d_assert(GameWorld().GetNetworkObject(n.spawnID) == NULL);

	obj_Zombie* obj = (obj_Zombie*)srv_CreateGameObject("obj_Zombie", "obj_Zombie", n.spawnPos);
	obj->SetNetworkID(n.spawnID);
	obj->NetworkLocal = false;
	memcpy(&obj->CreateParams, &n, sizeof(n));
	obj->OnCreate();

	// set base cell for movement data (must do it AFTER OnCreate())
	obj->netMover.SetStartCell(n.moveCell);
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_PlayerNameJoined)
{
	r3d_assert(n.peerId < R3D_ARRAYSIZE(playerNames));
	r3dscpy(playerNames[n.peerId].Gamertag, n.gamertag);
	playerNames[n.peerId].plrRep = n.Reputation;
	playerNames[n.peerId].isLegend = (n.flags & 1)?true:false;
	playerNames[n.peerId].isDev = (n.flags & 2)?true:false;
	playerNames[n.peerId].IsPremium = n.IsPremium;

}
IMPL_PACKET_FUNC(ClientGameLogic, PKT_C2S_DamageCar)
{
	if (gClientLogic().localPlayer_)
	{
		if (gClientLogic().localPlayer_->ActualVehicle != NULL)
		{
			if (gClientLogic().localPlayer_->ActualVehicle->GetNetworkID() == n.CarID)
			{
				if (n.WeaponID != NULL)
					gClientLogic().localPlayer_->ActualVehicle->ApplyDamage(gClientLogic().localPlayer_->ActualVehicle->GetNetworkID(),n.WeaponID);
			}
		}
	}
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_PlayerNameLeft)
{
	r3d_assert(n.peerId < R3D_ARRAYSIZE(playerNames));
	playerNames[n.peerId].Gamertag[0] = 0;

}
IMPL_PACKET_FUNC(ClientGameLogic, PKT_C2S_PlayerSetObStatus)
{
	if (n.id == 1)
	{
		if (n.status > 4)
		{
					PKT_C2s_PlayerSetMissionStatus_s n1;
					n1.id = 1;
					n1.status = 3;
					p2pSendToHost(gClientLogic().localPlayer_, &n1, sizeof(n1));
					hudMain->setMissionObjectiveCompleted(0,1);
					hudMain->removeMissionInfo(0);
					hudMain->showMissionInfo(false);
					gClientLogic().localPlayer_->CurLoadout.Mission1 = 3;
					wchar_t tmpStr[64];
					swprintf(tmpStr, 64, gLangMngr.getString("InfoMsg_XPAdded"), 200);
					hudMain->showMessage(tmpStr);
					return;	
		}
		if (n.ob == 1)
		{
			char text[512];
			sprintf(text,"%d/5",n.status);
			hudMain->setMissionObjectiveNumbers(0,1,text);
		}
	}
	if (n.id == 2)
	{
		if (n.status > 4)
		{
        PKT_C2s_PlayerSetMissionStatus_s n1;
					n1.id = 2;
					n1.status = 3;
					p2pSendToHost(gClientLogic().localPlayer_, &n1, sizeof(n1));
					hudMain->setMissionObjectiveCompleted(0,1);
					hudMain->removeMissionInfo(0);
					hudMain->showMissionInfo(false);
					gClientLogic().localPlayer_->CurLoadout.Mission2 = 3;
					wchar_t tmpStr[64];
					swprintf(tmpStr, 64, gLangMngr.getString("InfoMsg_XPAdded"), 200);
					hudMain->showMessage(tmpStr);
					return;	
		}
		if (n.ob == 1)
		{
			char text[512];
			sprintf(text,"%d/5",n.status);
			hudMain->setMissionObjectiveNumbers(0,1,text);
		}
	}
}
IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_CreatePlayer)
{
	R3DPROFILE_FUNCTION("ClientGameLogic::OnPKT_S2C_CreatePlayer");
	//r3dOutToLog("PKT_S2C_CreatePlayer: %d at %f %f %f\n", n.playerIdx, n.spawnPos.x, n.spawnPos.y, n.spawnPos.z);
#ifndef FINAL_BUILD
	r3dOutToLog("Create player: %s\n", n.gamertag);
#endif

	r3dPoint3D spawnPos = AdjustSpawnPositionToGround(n.spawnPos);

	wiCharDataFull slot;

	if(n.playerIdx == localPlayerIdx_)
	{
		slot = gUserProfile.ProfileData.ArmorySlots[gUserProfile.SelectedCharID];

		if(hudMain && n.ClanID != 0)
			hudMain->enableClanChannel();

		if(hudMain && n.GroupID != 0)
			hudMain->enableGroupChannel();

		// make sure that our loadout is same as server reported. if not - disconnect and try again
		if(slot.Items[wiCharDataFull::CHAR_LOADOUT_WEAPON1 ].itemID != n.WeaponID0 ||
		   slot.Items[wiCharDataFull::CHAR_LOADOUT_WEAPON2 ].itemID != n.WeaponID1 ||
		   slot.Items[wiCharDataFull::CHAR_LOADOUT_ARMOR   ].itemID != n.ArmorID ||
		   slot.Items[wiCharDataFull::CHAR_LOADOUT_HEADGEAR].itemID != n.HeadGearID ||
		   slot.Items[wiCharDataFull::CHAR_LOADOUT_ITEM1].itemID != n.Item0 ||
		   slot.Items[wiCharDataFull::CHAR_LOADOUT_ITEM2].itemID != n.Item1 ||
		   slot.Items[wiCharDataFull::CHAR_LOADOUT_ITEM3].itemID != n.Item2 ||
		   slot.Items[wiCharDataFull::CHAR_LOADOUT_ITEM4].itemID != n.Item3)
		{
			r3dOutToLog("!!! server reported loadout is different with local\n");

			Disconnect();
			return;
		}

		//@ FOR NOW, attachment are RESET on entry. need to detect if some of them was dropped
		// (SERVER CODE SYNC POINT)
		slot.Attachment[0] = wiWeaponAttachment();
		if(slot.Items[0].Var2 > 0)
			slot.Attachment[0].attachments[WPN_ATTM_CLIP] = slot.Items[0].Var2;

		slot.Attachment[1] = wiWeaponAttachment();
		if(slot.Items[1].Var2 > 0)
			slot.Attachment[1].attachments[WPN_ATTM_CLIP] = slot.Items[1].Var2;

	}
	else
	{
		slot.HeroItemID = n.HeroItemID;
		slot.HeadIdx    = n.HeadIdx;
		slot.BodyIdx    = n.BodyIdx;
		slot.LegsIdx    = n.LegsIdx;
		slot.BackpackID = n.BackpackID;
		slot.Items[wiCharDataFull::CHAR_LOADOUT_WEAPON1 ].itemID = n.WeaponID0;
		slot.Items[wiCharDataFull::CHAR_LOADOUT_WEAPON2 ].itemID = n.WeaponID1;
		slot.Items[wiCharDataFull::CHAR_LOADOUT_ARMOR   ].itemID = n.ArmorID;
		slot.Items[wiCharDataFull::CHAR_LOADOUT_HEADGEAR].itemID = n.HeadGearID;
		slot.Items[wiCharDataFull::CHAR_LOADOUT_ITEM1].itemID = n.Item0;
		slot.Items[wiCharDataFull::CHAR_LOADOUT_ITEM2].itemID = n.Item1;
		slot.Items[wiCharDataFull::CHAR_LOADOUT_ITEM3].itemID = n.Item2;
		slot.Items[wiCharDataFull::CHAR_LOADOUT_ITEM4].itemID = n.Item3;
		slot.Attachment[0].attachments[WPN_ATTM_MUZZLE]    = n.Attm0.MuzzleID;
		slot.Attachment[0].attachments[WPN_ATTM_LEFT_RAIL] = n.Attm0.LeftRailID;
		slot.Attachment[1].attachments[WPN_ATTM_MUZZLE]    = n.Attm1.MuzzleID;
		slot.Attachment[1].attachments[WPN_ATTM_LEFT_RAIL] = n.Attm1.LeftRailID;
	}

	char name[128];
	sprintf(name, "player%02d", n.playerIdx);
	obj_Player* plr = (obj_Player*)srv_CreateGameObject("obj_Player", name, spawnPos);
	plr->ViewAngle.Assign(-n.spawnDir, 0, 0);
	plr->m_fPlayerRotationTarget = n.spawnDir;
	plr->m_fPlayerRotation = n.spawnDir;
	plr->m_SelectedWeapon = n.weapIndex;
	r3d_assert(plr->m_SelectedWeapon>=0 && plr->m_SelectedWeapon < NUM_WEAPONS_ON_PLAYER);
	plr->m_PrevSelectedWeapon = -1;
	plr->CurLoadout   = slot;
//#ifndef FINAL_BUILD // decode CustomerID here
	plr->CustomerID   = n.CustomerID ^ 0x54281162;
//#endif
	plr->bDead        = false;
	plr->SetNetworkID(n.playerIdx + NETID_PLAYERS_START);
	plr->NetworkLocal = false;
	plr->m_EncryptedUserName.set(n.gamertag);
	// should be safe to use playerIdx, as it should be uniq to each player
	sprintf_s(plr->m_MinimapTagIconName, 64, "pl_%u", n.playerIdx);
	plr->ClanID = n.ClanID;
	plr->GroupID = n.GroupID;
	r3dscpy(plr->ClanTag, n.ClanTag);
	plr->ClanTagColor = n.ClanTagColor;

	plr->CurLoadout.Stats.Reputation = n.Reputation;

	if(n.playerIdx == localPlayerIdx_)
	{
		localPlayer_      = plr;
		plr->NetworkLocal = true;

		localPlayerConnectedTime = r3dGetTime();

		// start time reports for speedhack detection
		nextSecTimeReport_ = GetTickCount();

		// add chat msg
//		hudMain->AddChatMessage(0, NULL, gLangMngr.getString("$HUD_Msg_ChatTypeHelp"));
	}
	plr->OnCreate(); // call OnCreate manually to init player right away
	// call change weapon manually because UpdateLoadoutSlot that is called from OnCreate always resets CurWeapon to 0
	plr->ChangeWeaponByIndex(n.weapIndex);
	plr->forcedEmptyHands = n.isFreeHands>0?true:false; // set it right before sync animation, as in ChangeWeaponByIndex it will be reset
	plr->SyncAnimation(true);

	// set base cell for movement data (must do it AFTER OnCreate())
	if(!plr->NetworkLocal) plr->netMover.SetStartCell(n.moveCell);

	r3d_assert(GetPlayer(n.playerIdx) == NULL);
	SetPlayerPtr(n.playerIdx, plr);
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_Damage)
{
	GameObject* targetObj = GameWorld().GetNetworkObject(n.targetId);
	if(!targetObj)
		return;
	r3d_assert(fromObj);

	//r3dOutToLog("PKT_S2C_Damage: from:%s, to:%s, damage:%d\n", fromObj->Name.c_str(), targetObj->Name.c_str(), n.damage);

	if(targetObj->isObjType(OBJTYPE_Human))
	{
		obj_Player* targetPlr = (obj_Player*)targetObj;
		targetPlr->ApplyDamage(n.dmgPos, n.damage, fromObj, n.bodyBone, n.dmgType);
	}
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_ZombieAttack)
{
	GameObject* targetObj = GameWorld().GetNetworkObject(n.targetId);
	if(!targetObj)
		return;

	if(targetObj == localPlayer_)
	{
		//TODO: display blood or something
		obj_Player* plr = (obj_Player*)targetObj;
		plr->lastTimeHit = r3dGetTime(); // for blood effect to work
	}

	r3d_assert(fromObj && fromObj->isObjType(OBJTYPE_Zombie));
	((obj_Zombie*)fromObj)->OnNetAttackStart();
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_KillPlayer)
{
	R3DPROFILE_FUNCTION("ClientGameLogic::OnPKT_S2C_KillPlayer");
	GameObject* killerObj = fromObj;
	r3d_assert(killerObj);

	GameObject* targetObj = GameWorld().GetNetworkObject(n.targetId);
	if(!targetObj)
		return;


	//r3dOutToLog("PKT_S2C_KillPlayer: killed %s by %s\n", targetObj->Name.c_str(), killerObj->Name.c_str());

	r3d_assert(targetObj->isObjType(OBJTYPE_Human));
	obj_Player* targetPlr = (obj_Player*)targetObj;

	int killedID = invalidGameObjectID;
	if(fromObj != targetPlr && (fromObj->isObjType(OBJTYPE_Human) || fromObj->isObjType(OBJTYPE_Zombie)))
		killedID = fromObj->GetNetworkID();
	targetPlr->DoDeath(killedID, n.forced_by_server, (STORE_CATEGORIES)n.killerWeaponCat);

	if(n.forced_by_server)
		return;
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_C2S_DisconnectReq)
{
	//r3dOutToLog("PKT_C2S_DisconnectReq\n");
	disconnectStatus_ = 2;
	GameWorld().OnGameEnded();
}
IMPL_PACKET_FUNC(ClientGameLogic, PKT_C2S_BuyItemReq)
{
	//r3dOutToLog("isBuyAns\n");

	isBuyAns = true;
	BuyAnsCode = n.ansCode;
}

IMPL_PACKET_FUNC(ClientGameLogic, PKT_S2C_CheatWarning)
{
	cheatAttemptReceived_ = true;
	cheatAttemptCheatId_  = n.cheatId;
}

int ClientGameLogic::ProcessWorldEvent(GameObject* fromObj, DWORD eventId, DWORD peerId, const void* packetData, int packetSize)
{
	R3DPROFILE_FUNCTION("ClientGameLogic::ProcessWorldEvent");
	switch(eventId)
	{
		DEFINE_PACKET_HANDLER(PKT_C2S_ValidateConnectingPeer);
		DEFINE_PACKET_HANDLER(PKT_C2C_PacketBarrier);
		DEFINE_PACKET_HANDLER(PKT_C2S_DisconnectReq);
		DEFINE_PACKET_HANDLER(PKT_S2C_JoinGameAns);
		DEFINE_PACKET_HANDLER(PKT_S2C_ShutdownNote);
		DEFINE_PACKET_HANDLER(PKT_S2C_SetGamePlayParams);
		DEFINE_PACKET_HANDLER(PKT_S2C_StartGameAns);
		DEFINE_PACKET_HANDLER(PKT_S2C_PlayerNameJoined);
		DEFINE_PACKET_HANDLER(PKT_S2C_PlayerNameLeft);
		DEFINE_PACKET_HANDLER(PKT_S2C_CreatePlayer);
		DEFINE_PACKET_HANDLER(PKT_S2C_Damage);
		DEFINE_PACKET_HANDLER(PKT_S2C_ZombieAttack);
		DEFINE_PACKET_HANDLER(PKT_S2C_KillPlayer);
		DEFINE_PACKET_HANDLER(PKT_C2C_ChatMessage);
		DEFINE_PACKET_HANDLER(PKT_S2C_UpdateWeaponData);
		DEFINE_PACKET_HANDLER(PKT_S2C_UpdateGearData);
		DEFINE_PACKET_HANDLER(PKT_S2C_CreateNetObject);
		DEFINE_PACKET_HANDLER(PKT_S2C_DestroyNetObject);
		DEFINE_PACKET_HANDLER(PKT_S2C_CreateDroppedItem);
		DEFINE_PACKET_HANDLER(PKT_S2C_CreateGrave);
		DEFINE_PACKET_HANDLER(PKT_S2C_CreateNote);
		DEFINE_PACKET_HANDLER(PKT_S2C_SetGraveData);
		DEFINE_PACKET_HANDLER(PKT_S2C_SetNoteData);
		DEFINE_PACKET_HANDLER(PKT_S2C_CreateVehicle); // Server Vehicles
		DEFINE_PACKET_HANDLER(PKT_S2C_CreateZombie);
		DEFINE_PACKET_HANDLER(PKT_S2C_CheatWarning);
		DEFINE_PACKET_HANDLER(PKT_S2C_SendHelpCallData);
		DEFINE_PACKET_HANDLER(PKT_C2S_SendHelpCall);
		DEFINE_PACKET_HANDLER(PKT_S2C_PositionVehicle);
		DEFINE_PACKET_HANDLER(PKT_C2S_DamageCar);
		DEFINE_PACKET_HANDLER(PKT_C2S_SafelockData);
		DEFINE_PACKET_HANDLER(PKT_C2S_BuyItemReq);
		DEFINE_PACKET_HANDLER(PKT_S2C_GroupData);
	//	DEFINE_PACKET_HANDLER(PKT_S2C_CreateRepairBench);
	}

	return FALSE;
}

int ClientGameLogic::WaitFunc(fn_wait fn, float timeout, const char* msg)
{
	float endWait = r3dGetTime() + timeout;
	while(1)
	{
		r3dEndFrame();
		r3dStartFrame();

		extern void tempDoMsgLoop();
		tempDoMsgLoop();

		if((this->*fn)())
			break;

		r3dRenderer->StartRender();
		r3dRenderer->StartFrame();
		r3dRenderer->SetRenderingMode(R3D_BLEND_ALPHA);
		Font_Label->PrintF(10, 10, r3dColor::white, "%s", msg);
		r3dRenderer->EndFrame();
		r3dRenderer->EndRender( true );

		if(r3dGetTime() > endWait) {
			return 0;
		}
	}

	return 1;
}

bool ClientGameLogic::Connect(const char* host, int port)
{
	r3d_assert(!serverConnected_);
	r3d_assert(disconnectStatus_ == 0);

	g_net.Initialize(this, "p2pNet");
	g_net.CreateClient();
	g_net.Connect(host, port);

	if( !DoConnectScreen( this, &ClientGameLogic::wait_IsConnected, gLangMngr.getString("WaitConnectingToServer"), 30.f ) )
		return false;

	return true;
}

void  ClientGameLogic::Disconnect()
{
	g_net.Deinitialize();
	serverConnected_ = false;
}

int ClientGameLogic::ValidateServerVersion(__int64 sessionId)
{
	serverVersionStatus_ = 0;

	PKT_C2S_ValidateConnectingPeer_s n;
	n.protocolVersion = P2PNET_VERSION;
	n.sessionId       = sessionId;
	p2pSendToHost(NULL, &n, sizeof(n));

	if( !DoConnectScreen( this, &ClientGameLogic::wait_ValidateVersion, gLangMngr.getString("WaitValidatingClientVersion"), 30.f ) )
	{
		r3dOutToLog("can't check game server version");
		return 0;
	}

	// if invalid version
	if(serverVersionStatus_ == 2)
	{
		return 0;
	}

	m_sessionId = sessionId;

	return 1;
}

int ClientGameLogic::RequestToJoinGame()
{
	r3d_assert(!gameJoinAnswered_);
	r3d_assert(localPlayer_ == NULL);
	r3d_assert(localPlayerIdx_ == -1);

	PKT_C2S_JoinGameReq_s n;
	n.CustomerID  = gUserProfile.CustomerID;
	n.SessionID   = gUserProfile.SessionID;
	n.CharID      = gUserProfile.ProfileData.ArmorySlots[gUserProfile.SelectedCharID].LoadoutID;
	r3d_assert(n.CharID);
	p2pSendToHost(NULL, &n, sizeof(n));

	if( !DoConnectScreen( this, &ClientGameLogic::wait_GameJoin, gLangMngr.getString("WaitJoinGame"), 10.f ) )
	{
		r3dOutToLog("RequestToJoinGame failed\n");
		return 0;
	}

	r3d_assert(localPlayerIdx_ != -1);
#ifndef FINAL_BUILD
	r3dOutToLog("joined as player %d\n", localPlayerIdx_);
#endif

	return 1;
}

bool ClientGameLogic::wait_GameStart()
{
	if(!gameStartAnswered_)
		return false;

	if(gameStartResult_ == PKT_S2C_StartGameAns_s::RES_Pending)
	{
		gameStartAnswered_ = false; // reset flag to force wait for next answer

		//TODO: make a separate timer to send new queries
		r3dOutToLog("retrying start game request\n");
		::Sleep(500);

		PKT_C2S_StartGameReq_s n;
		n.lastNetID = net_lastFreeId;
		p2pSendToHost(NULL, &n, sizeof(n));
		return false;
	}

	return true;
}

int ClientGameLogic::RequestToStartGame()
{
	r3d_assert(localPlayerIdx_ != -1);
	r3d_assert(localPlayer_ == NULL);

	PKT_C2S_StartGameReq_s n;
	n.lastNetID = net_lastFreeId;
	p2pSendToHost(NULL, &n, sizeof(n));

	if( !DoConnectScreen( this, &ClientGameLogic::wait_GameStart, gLangMngr.getString("WaitGameStart"), 20.f ) )
	{
		r3dOutToLog("can't start game, timeout\n");
		gameStartResult_ = PKT_S2C_StartGameAns_s::RES_Timeout;
		return 0;
	}

	if(gameStartResult_ != PKT_S2C_StartGameAns_s::RES_Ok)
	{
		r3dOutToLog("can't start game, res: %d\n", gameStartResult_);
		return 0;
	}

	return 1;
}

//@ MUST BE CALLED in in-game PLAYER "exit" menu, then we should wait until server ack it
void ClientGameLogic::RequestToDisconnect()
{
	r3d_assert(localPlayer_);
	r3d_assert(disconnectStatus_ == 0);

	PKT_C2S_DisconnectReq_s n;
	p2pSendToHost(NULL, &n, sizeof(n));

	disconnectStatus_  = 1;
	disconnectReqTime_ = r3dGetTime();

	return;
}

void ClientGameLogic::SendScreenshotFailed(int code)
{
	r3d_assert(code > 0);

	PKT_C2S_ScreenshotData_s n;
	n.errorCode = (BYTE)code;
	n.dataSize  = 0;
	p2pSendToHost(NULL, &n, sizeof(n));
}

extern IDirect3DTexture9* _r3d_screenshot_copy;
void ClientGameLogic::SendScreenshot(IDirect3DTexture9* texture)
{
	r3d_assert(texture);

	HRESULT hr;
	IDirect3DSurface9* pSurf0 = NULL;
	hr = texture->GetSurfaceLevel(0, &pSurf0);

	ID3DXBuffer* pData = NULL;
	hr = D3DXSaveSurfaceToFileInMemory(&pData, D3DXIFF_JPG, pSurf0, NULL, NULL);
	SAFE_RELEASE(pSurf0);

	if(pData == NULL || pData->GetBufferSize() > 0xF000) {
		SAFE_RELEASE(pData);
		SendScreenshotFailed(4);
		return;
	}

  // assemble packet and send it to host
	int   pktsize = sizeof(PKT_C2S_ScreenshotData_s) + pData->GetBufferSize();
	char* pktdata = new char[pktsize + 1];

	PKT_C2S_ScreenshotData_s n;
	n.errorCode = 0;
	n.dataSize  = (WORD)pData->GetBufferSize();
	memcpy(pktdata, &n, sizeof(n));
	memcpy(pktdata + sizeof(n), pData->GetBufferPointer(), n.dataSize);
	p2pSendToHost(NULL, (DefaultPacket*)pktdata, pktsize);

	delete[] pktdata;
	SAFE_RELEASE(pData);
	return;
}

__int64 ClientGameLogic::GetServerGameTime() const
{
	float secs = (r3dGetTime() - gameStartTime_) * (float)GPP->c_iGameTimeCompression;
	__int64 gameUtcTime = gameStartUtcTime_ + __int64(secs);
	return gameUtcTime;
}

void ClientGameLogic::UpdateTimeOfDay()
{
	R3DPROFILE_FUNCTION("ClientGameLogic::UpdateTimeOfDay");
	if(!gameJoinAnswered_)
		return;

	__int64 gameUtcTime = GetServerGameTime();
	struct tm* tm = _gmtime64(&gameUtcTime);
	r3d_assert(tm);

	r3dGameLevel::Environment.__CurTime  = (float)tm->tm_hour + (float)tm->tm_min / 59.0f;
	//do not add seconds, much shadow flicker
	//r3dGameLevel::Environment.__CurTime += (float)tm->tm_sec / (59.0f * 59.0f);

	// reset shadow cache if environment time is changed
	if(fabs(r3dGameLevel::Environment.__CurTime - lastShadowCacheReset_) > 0.01f)
	{
		//r3dOutToLog("Server time: %f %d %d %d %d:%d\n", r3dGameLevel::Environment.__CurTime, tm->tm_mon, tm->tm_mday, 1900 + tm->tm_year, tm->tm_hour, tm->tm_min);
		lastShadowCacheReset_ = r3dGameLevel::Environment.__CurTime;
		ResetShadowCache();
	}
}

void ClientGameLogic::Tick()
{
	R3DPROFILE_FUNCTION("ClientGameLogic::Tick");
	if(net_)
	{
		R3DPROFILE_FUNCTION("Net update");
		net_->Update();
	}

	if(!serverConnected_)
		return;

	// every <N> sec client must send his security report
	const DWORD curTicks = GetTickCount();
	if(curTicks >= nextSecTimeReport_)
	{
		nextSecTimeReport_ = curTicks + (PKT_C2S_SecurityRep_s::REPORT_PERIOD * 1000);
		PKT_C2S_SecurityRep_s n;
		// store current time for speed hack detection
		n.gameTime = (float)curTicks / 1000.0f;
		n.detectedWireframeCheat = 0;
		n.GPP_Crc32 = GPP->GetCrc32() ^ gppDataSeed_;

		p2pSendToHost(NULL, &n, sizeof(n), true);
	}

	if(localPlayer_ && net_ && net_->lastPing_>500)
	{
		m_highPingTimer += r3dGetFrameTime();
		if(m_highPingTimer>60) // ping > 500 for more than 60 seconds -> disconnect
		{
			Disconnect();
			return;
		}
	}
	else
		m_highPingTimer = 0;

	/*if(showCoolThingTimer > 0 && m_gameHasStarted && localPlayer_ && !localPlayer_->bDead && ((r3dGetTime()-m_gameLocalStartTime))>showCoolThingTimer && m_gameInfo.practiceGame == false )
	{
		showCoolThingTimer = 0;
		char titleID[64];
		char textID[64];
		int tID = int(u_GetRandom(1.0f, 10.99f));
		int txtID = int(u_GetRandom(1.0f, 2.99f));
		sprintf(titleID, "tmpYouGotCoolThingTitle%d", tID);
		sprintf(textID, "tmpYouGotCoolThingText%d_%d", tID, txtID);
		hudMain->showYouGotCoolThing(gLangMngr.getString(titleID), "$Data/Weapons/StoreIcons/WPIcon.dds");
	}*/

	// change time of day
	UpdateTimeOfDay();

	// send d3d cheat screenshot once per game
	if(_r3d_screenshot_copy && !d3dCheatSent_)
	{
		d3dCheatSent_ = true;
		SendScreenshot(_r3d_screenshot_copy);
		// release saved screenshot copy
		SAFE_RELEASE(_r3d_screenshot_copy);
	}

	if(GetCheatScreenshot() && !d3dCheatSent2_)
	{
		d3dCheatSent2_ = true;
		SendScreenshot(GetCheatScreenshot());
		ReleaseCheatScreenshot();
	}

	return;
}

bool canDamageTarget(const GameObject* obj);
// applies damage from local player
// Note: Direction tells us if it's a directional attack as well as how much. Remember to half the arc you want. (180 degree arc, is 90 degrees)
// 		ForwVector is the direction.  ForwVector needs to be normalized if used.
void ClientGameLogic::ApplyExplosionDamage( const r3dVector& pos, float radius, int wpnIdx, const r3dVector& forwVector /*= FORWARDVECTOR*/, float direction /*= 360*/ ,int ItemID)
{

	// WHEN WE MOVE THIS TO THE SERVER: Check the piercability of the gameobjects hit, and apply that to the explosion.
	if(localPlayer_ == NULL)
		return;

	float radius_incr = 1.0f;

	// apply damage within a radius
	ObjectManager& GW = GameWorld();
	for(GameObject *obj = GW.GetFirstObject(); obj; obj = GW.GetNextObject(obj))
	{
		//r3dOutToLog("####### ENTRA 1 %s \n",obj->Class->Name);
/////////////////////////////////
		bool zombiedetect=false;
		if(obj->isObjType(OBJTYPE_Zombie))
		{
			obj_Zombie* zombie = (obj_Zombie*)obj;
			float dist_to_obj_sq = (pos - obj->GetPosition()).LengthSq();
			if(dist_to_obj_sq < 15 )
			{
				if (!zombie->bDead)
				{
					PKT_C2S_Temp_Damage_s n2;
					n2.targetId = toP2pNetId(zombie->GetNetworkID());
					n2.wpnIdx = (BYTE)wpnIdx;
					n2.damagePercentage = 0;
					n2.explosion_pos = pos;
					p2pSendToHost(localPlayer_, &n2, sizeof(n2));

					obj_Player* plr = gClientLogic().localPlayer_;
					PKT_C2S_CarKill_s n;
					n.weaponID = 0;
					n.DieForExplosion = false;
					n.targetId = zombie->GetNetworkID();
					p2pSendToHost(plr, &n, sizeof(n));
					zombiedetect=true;
				}
			}
		}
		if (obj->Class->Name == "obj_Barricade")
		{
			float dist_to_obj_sq = (pos - obj->GetPosition()).LengthSq();
			if(dist_to_obj_sq < 15 )
			{
					PKT_C2S_Temp_Damage_s n;
					n.targetId = toP2pNetId(obj->GetNetworkID());
					n.wpnIdx = (BYTE)wpnIdx;
					if (ItemID == 101310)
						n.damagePercentage = 100;
					else
						n.damagePercentage = 0;
					n.explosion_pos = pos;
					p2pSendToHost(localPlayer_, &n, sizeof(n));
			}
		}
		if (obj->isObjType(OBJTYPE_Vehicle))
		{
			obj_Vehicle* Vehicle = (obj_Vehicle*)obj;
			float dist_to_obj_sq = (pos - obj->GetPosition()).LengthSq();
			if(dist_to_obj_sq < 15 )
			{
				if (Vehicle->DamageCar>0)
				{
					Vehicle->ApplyDamage(Vehicle->GetNetworkID(),100999);
				}
			}
		}
////////////////////////////////////////////////////////
		if(canDamageTarget(obj) && zombiedetect==false)
		{
			r3d_assert(obj->GetNetworkID());
			float dist_to_obj_sq = (pos - obj->GetPosition()).LengthSq();
			if(dist_to_obj_sq < 15 )
			{
				if(obj->isObjType(OBJTYPE_Human))
				{
					PKT_C2S_Temp_Damage_s n;
					n.targetId = toP2pNetId(obj->GetNetworkID());
					n.wpnIdx = (BYTE)wpnIdx;
					if (ItemID == 101310)
						n.damagePercentage = 50;
					else
						n.damagePercentage = 0;
					n.explosion_pos = pos;
					p2pSendToHost(localPlayer_, &n, sizeof(n));
				}
			}
		}
	}
}

#include "r3dPCH.h"
#include "r3d.h"
#include "r3dNetwork.h"
#include <shellapi.h>
#include <signal.h>
#include "CkString.h"

#pragma warning (disable: 4244)
#pragma warning (disable: 4305)
#pragma warning (disable: 4101)

#include "ServerMain.h"

#include "ServerGameLogic.h"
#include "MasterServerLogic.h"
#include "../../ServerNetPackets/NetPacketsGameInfo.h"
#include "KeepAliveReporter.h"
#include "../../MasterServer/Sources/SrvCrashHandler.h"

extern	void 		PlayGameServer();

extern	HANDLE		r3d_CurrentProcess;
extern	void		r3dChangeLogFile(const char* fname);

static	DWORD		cfg_gameId   = -1;
static	int		cfg_hostPort = 0;
static	char		cfg_masterip[512] = "";
static	GBGameInfo	cfg_ginfo;
static  uint32_t	cfg_creatorID; // customerID if any of creator, 0 if permanent game
	__int64		cfg_sessionId = 0;
	int		cfg_uploadLogs = 0;

	int		RUS_CLIENT = 0;

void game::Shutdown(void) { r3dError("not a gfx app"); }
void game::MainLoop(void) { r3dError("not a gfx app"); }
void game::Init(void)     { r3dError("not a gfx app"); }
void game::PreInit(void)  { r3dError("not a gfx app"); }

static bool downloadGameRewards()
{
	// receive game rewards from web
	r3dOutToLog("Reading game rewards\n");

	g_GameRewards = new CGameRewards();
	if(g_GameRewards->ApiGetDataGameRewards() != 0)
		r3dError("failed to get game rewards");

	return true;
}

#include "CkByteData.h"
#include "CkHttp.h"
#include "../../EclipseStudio/Sources/backend/WOBackendAPI.h"
#include "../../EclipseStudio/Sources/ObjectsCode/weapons/WeaponArmory.h"
static bool downloadLootBoxData()
{
	r3dOutToLog("Reading loot boxes\n");
	r3d_assert(g_pWeaponArmory);
  
	CkHttp http;
	int success = http.UnlockComponent("ARKTOSHttp_decCLPWFQXmU");
	if (success != 1) 
		r3dError("Internal error!!!");

	CkHttpRequest req;
	req.UsePost();
	req.put_Path("/conexao/api/php/api_GetLootBoxConfig.php");
	req.AddParam("serverkey", "9F179EB9-C74E-4933-85B5-EB135E16F5EF");

	CkHttpResponse* resp = http.SynchronousRequest("127.0.0.1", 80, false, req);
	if(!resp)
		r3dError("timeout getting lootbox db");
			
	// we can't use getBosyStr() because it'll fuckup characters inside UTF-8 xml
	CkByteData bodyData;
	resp->get_Body(bodyData);
	delete resp;
		
	pugi::xml_document xmlFile;
	pugi::xml_parse_result parseResult = xmlFile.load_buffer_inplace(
		(void*)bodyData.getBytes(), 
		bodyData.getSize(), 
		pugi::parse_default, 
		pugi::encoding_utf8);
	if(!parseResult)
		r3dError("Failed to parse lootbox db, error: %s", parseResult.description());
  
	g_pWeaponArmory->updateLootBoxContent(xmlFile.child("LootBoxDB"));
	
	return true;
}

void gameServerLoop()
{
  if(cfg_hostPort == 0) throw "no cfg_hostPort";
  if(cfg_gameId == -1) throw "no cfg_gameId";
  if(!cfg_ginfo.IsValid()) throw "invalid cfg_ginfo";
  if(cfg_masterip[0] == 0) throw "no cfg_masterip";

  // make unique log name for each game server
  _mkdir("logss");
  char fname[MAX_PATH];
  sprintf(fname, "logss\\GS_%I64x.txt", cfg_sessionId);
  r3dChangeLogFile(fname);
  
  sprintf(fname, "logss\\GS_%I64x.dmp", cfg_sessionId);
  SrvSetCrashHandler(fname);

  sprintf(fname, "WarZ GameServer, port %d", cfg_hostPort);
  SetConsoleTitle(fname);

  if(cfg_ginfo.region == GBNET_REGION_Russia)
  {
    r3dOutToLog("------- Russian server\n");
    RUS_CLIENT = 1;
  }
  
  // use archive filesystem
  r3dFileManager_OpenArchive("wo");
  
  if(cfg_gameId) {
    gMasterServerLogic.Init(cfg_gameId);
    gMasterServerLogic.Connect(cfg_masterip, SBNET_MASTER_PORT);
  }
  
  gServerLogic.Init(cfg_ginfo, cfg_creatorID);
  gServerLogic.CreateHost(cfg_hostPort);
  gMasterServerLogic.RegisterGame();

  // overwrite server behavior - he'll connect to different API IIS server
  
  //if(stricmp(g_api_ip->GetString(), "localhost") != 0)
  //{
  //  r3dOutToLog("*** OVERRIDING API TO LA-GAME2\n");
  //  extern int  gDomainPort;
  //  extern bool gDomainUseSSL;
	
  //  g_api_ip->SetString("localhost");
  //  gDomainUseSSL = false;
  //  gDomainPort   = 80;
  //}


  // get shop data
  r3dOutToLog("Getting shop data\n");
  CUserProfile tempProfile;
  tempProfile.CustomerID = 100;
  tempProfile.ApiGetShopData();
  
  downloadGameRewards();

  r3dOutToLog("Loading armory config\n");
  r3d_assert(g_pWeaponArmory == NULL);
  g_pWeaponArmory = new WeaponArmory();
  g_pWeaponArmory->Init();

  downloadLootBoxData();

  r3dOutToLog("Starting game server\n");
  PlayGameServer();
  
  r3dOutToLog("reporting game close\n");
  gMasterServerLogic.CloseGame();
  
  return;
}

static LRESULT CALLBACK serverWndFunc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch(message) 
  {
    case WM_CLOSE:
    {
      r3dOutToLog("alt-f4 pressed\n");
      r3dOutToLog("...terminating application\n");

      HRESULT res = TerminateProcess(r3d_CurrentProcess, 0);
      break;
    }
  }

  return DefWindowProc(hWnd, message, wParam, lParam);
}

static void serverCreateTempD3DWindow()
{
static	char*		ClassName = "r3dGameServerWin";
	WNDCLASS    	wndclass;

  wndclass.style         = CS_DBLCLKS | CS_GLOBALCLASS;
  wndclass.lpfnWndProc   = serverWndFunc;		// window function
  wndclass.cbClsExtra    = 0;				// no extra count of bytes
  wndclass.cbWndExtra    = 0;				// no extra count of bytes
  wndclass.hInstance     = GetModuleHandle(NULL);	// this instance
  wndclass.hIcon         = NULL;
  wndclass.hCursor       = LoadCursor(NULL, IDC_ARROW);
  wndclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  wndclass.lpszMenuName  = NULL;
  wndclass.lpszClassName = ClassName;
  RegisterClass(&wndclass);

  win::hWnd = CreateWindowEx(
    0,
    ClassName,             			// window class name
    "temp d3d window",				// window caption
    WS_OVERLAPPEDWINDOW, 			// window style
    0,						// initial x position
    0,						// initial y position
    16,						// initial x size
    16,						// initial y size
    NULL,                  			// parent window handle
    NULL,                  			// window menu handle
    GetModuleHandle(NULL), 			// program instance handle
    NULL);                 			// creation parameters

  if(!win::hWnd) {
    r3dError("unable to create window");
    return;
  }

  ShowWindow(win::hWnd, FALSE);
  return;
}

static BOOL WINAPI ConsoleHandlerRoutine(DWORD dwCtrlType)
{
  switch(dwCtrlType) 
  {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
      r3dOutToLog("Control-c ...\n");
      // do not do any network deinitialization here
      // because this handler is invoked from other thread

      HRESULT res = TerminateProcess(r3d_CurrentProcess, 0);
      break;
  }

  return FALSE;
}

static void ParseArgs(int argc, char* argv[])
{
  r3dOutToLog("cmd: %d\n", argc);
  for(int i=1; i<argc; i++) r3dOutToLog("%d: %s\n", i, argv[i]);

  switch(argc)
  {
    default:
      throw "invalid number of arguments";
    
    case 6: // normal start
    {
      if(3 != sscanf(argv[1], "%u %d %u", &cfg_gameId, &cfg_hostPort, &cfg_creatorID))
        throw "can't parse argv[1]";
      
      if(!cfg_ginfo.FromString(argv[2]))
        throw "Can't parse game info";
        
      r3dscpy(cfg_masterip, argv[3]);
      
      CkString ckGameName;
      ckGameName.setString(argv[4]);
      ckGameName.base64Decode("utf-8");
      r3dscpy(cfg_ginfo.name, ckGameName.getString());
      
      if(1 != sscanf(argv[5], "%d", &cfg_uploadLogs))
        throw "can't parse argv[5]";

      break;
    }
  }

  return;
}

// move console window to specified corner
static void moveWindowToCorner()
{
  HDC disp_dc  = CreateIC("DISPLAY", NULL, NULL, NULL);
  int curDispWidth  = GetDeviceCaps(disp_dc, HORZRES);
  int curDispHeight = GetDeviceCaps(disp_dc, VERTRES);
  DeleteDC(disp_dc);

  HWND cwhdn = GetConsoleWindow();  

  RECT rect;
  GetWindowRect(cwhdn, &rect);
  
  SetWindowPos(cwhdn, 
    0, 
    curDispWidth - (rect.right - rect.left) - 20,
    rect.top,
    rect.right,
    rect.bottom,
    SWP_NOSIZE);
}

//
// server entry
//
int main(int argc, char* argv[])
{
//	MessageBox(0, "Time to connect debugger", "Debugger", MB_OK);
  win::hInstance = GetModuleHandle(NULL);

  extern int _r3d_bLogToConsole;
  extern int _r3d_bLogToDebug;
  _r3d_bLogToDebug   = 0;
  _r3d_bLogToConsole = 0;

  extern int _r3d_bSilent_r3dError;
  _r3d_bSilent_r3dError = 1;
  
  // do not close on debug terminate key
  extern int _r3d_bTerminateOnZ;
  _r3d_bTerminateOnZ = 0;
  
  // skip filling mesh buffers
  extern int r3dMeshObject_SkipFillBuffers;
  r3dMeshObject_SkipFillBuffers = 1;
  
  // do not use textures
  extern int r3dTexture_UseEmpty;
  r3dTexture_UseEmpty = 1;

  extern int _r3d_MatLib_SkipAllMaterials;
  _r3d_MatLib_SkipAllMaterials = 1;
  
  r3d_CurrentProcess = GetCurrentProcess();
  
#ifdef _DEBUG
  if(1)
  {
    AllocConsole();
    freopen("CONOUT$", "wb", stdout);

    _r3d_bLogToConsole = 1;
    SetConsoleCtrlHandler(ConsoleHandlerRoutine, TRUE);
    
    moveWindowToCorner();
  } 
#endif

  try
  {
    ParseArgs(argc, argv);
    r3dOutToLog("gameId: %x, port: %d\n", cfg_gameId, cfg_hostPort);

    r3dRenderer = NULL;

    gKeepAliveReporter.Init(cfg_gameId);
    cfg_sessionId = gKeepAliveReporter.GetSessionId();
    
    ClipCursor(NULL);

    // from SF config.cpp, bah.
    extern void RegisterAllVars();
    RegisterAllVars();
    
    gameServerLoop();
  } 
  catch(const char* what)
  {
    r3dOutToLog("!!! Exception: %s\n", what);
    gKeepAliveReporter.SetCrashed();
    // fall thru
  }
 
  r3dOutToLog("stopping server\n");

  gKeepAliveReporter.Close();
  
  gServerLogic.Disconnect();
  gMasterServerLogic.Disconnect();

  DestroyWindow(win::hWnd);
  ExitProcess(0);

  return 0;
}

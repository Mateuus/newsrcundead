#include "r3dPCH.h"
#include "r3d.h"
#include "r3dDebug.h"
static unsigned antihackNotFound = 1;
#include <ShellAPI.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <DbgHelp.h>
#pragma comment(lib,"Psapi.lib")
#pragma comment(lib, "version.lib")  // for "VerQueryValue"
#include <string>
#include <iostream>  
#include <Sddl.h> 

#ifdef FINAL_BUILD
#include "F:\Undead\src\EclipseStudio\Sources\resource.h"
#include "HShield.h"
#include "HSUpChk.h"
#pragma comment(lib,"HShield.lib")
#pragma comment(lib,"HSUpChk.lib")
#include <assert.h>
#include <stdio.h>
#include <winsock2.h>
#include <process.h>
#include "F:/Undead/src/External/Fury/Fury/Fury.h"
#endif
#include "../SF/Console/EngineConsole.h"

typedef void (*HackCheck)();

#ifdef FINAL_BUILD
typedef NTSTATUS (WINAPI *pNtQIT)(HANDLE, LONG, PVOID, ULONG, PULONG);
#endif

#define STATUS_SUCCESS    ((NTSTATUS)0x000 00000L)
// Global Variables:
HINSTANCE hInst;											// current instance
TCHAR	szTitle[500];								// The title bar text
TCHAR	szWindowClass[500];						// The title bar text
DWORD	g_dwMainThreadID;
TCHAR	g_szHShieldPath[MAX_PATH] = {0,};
TCHAR	g_szIniPath[MAX_PATH] = {0,};
int		g_nServerPort = 0;
char	g_szServerIP[20] = {0,};
#define ThreadQuerySetWin32StartAddress 9
char	__r3dCmdLine[1024];
typedef bool (*Win32MsgProc_fn)(UINT uMsg, WPARAM wParam, LPARAM lParam);

//hack: add temp handler for external message processing

namespace
{
	const int		NUM_MSG_PROC_MAX = 8;
	Win32MsgProc_fn	r3dApp_MsgProc3[NUM_MSG_PROC_MAX] = {0};
}

static	int		StartWinHeight = 300;
static	int		StartWinWidth  = 300;
static	int		curDispWidth   = 0;
static	int		curDispHeight  = 0;

namespace win
{
	HINSTANCE	hInstance  = NULL;
	HWND		hWnd       = NULL;
	const char 	*szWinName = "$o, yeah!$";
	HICON		hWinIcon   = 0;
	int		bSuspended	= 0;
	int		bNeedRender	= 1;
	
	void		HandleActivate();
	void		HandleDeactivate();
	void		HandleMinimize();
	void		HandleRestore();

	int		Init();
}

void RegisterMsgProc (Win32MsgProc_fn proc)
{
	for (int i = 0; i<NUM_MSG_PROC_MAX; i++ )
	{
		if (!r3dApp_MsgProc3[i])
		{
			r3dApp_MsgProc3[i] = proc;
			return;
		}
	}
	r3d_assert ( 0 && "RegisterMsgProc error; register more than NUM_MSG_PROC_MAX MsgProc." );
}

void UnregisterMsgProc (Win32MsgProc_fn proc)
{
	for (int i = 0; i<NUM_MSG_PROC_MAX; i++ )
	{
		if (r3dApp_MsgProc3[i] == proc)
		{
			r3dApp_MsgProc3[i] = NULL;
			return;
		}
	}
	r3d_assert ( 0 && "UnregisterMsgProc error." );
}

#define INPUT_KBD_STACK	32
volatile static  int   input_ScanStack[INPUT_KBD_STACK + 2];
volatile static  int   * volatile input_StackHead = input_ScanStack;
volatile static  int   * volatile input_StackTail = input_ScanStack;


bool g_bExit = false;

bool IsNeedExit()
{
	return g_bExit;
}

int win::input_Flush()
{
  input_StackHead = input_ScanStack;
  input_StackTail = input_ScanStack;

  return 1;
}

int win::kbhit()
{
  if(input_StackHead == input_StackTail)
    return 0;
  else
    return 1;
}

int win::getch( bool bPop )
{
	int	ch;

  if(!win::kbhit())
    return 0;

  ch = *input_StackTail;
  if ( bPop )
	  input_StackTail++;

  if(input_StackTail >= input_ScanStack + INPUT_KBD_STACK)
    input_StackTail = input_ScanStack;

  return ch;
}


static 
void wnd__DrawLogo()
{
#if TSG_BITMAP
	HDC		hDC;
	HANDLE		hBmp;

  // load background image and center our window
  hBmp = LoadImage(
    GetModuleHandle(NULL), 
    MAKEINTRESOURCE(TSG_BITMAP),
    IMAGE_BITMAP,
    win__Width,
    win__Height,
    LR_DEFAULTCOLOR
    );
  if(hBmp == NULL) return;

  if(hDC = GetDC(win::hWnd))
  {
	HDC	dc;

    dc = CreateCompatibleDC(hDC);
    SelectObject(dc, hBmp);
    BitBlt(hDC, 0, 0, win__Width, win__Height, dc, 0, 0, SRCCOPY);
    DeleteDC(dc);

    ReleaseDC(win::hWnd, hDC);
  }

  DeleteObject(hBmp);

#endif

  return;
}

bool winMouseWasVisible = true;
extern bool r3dMouse_visible;
void win::HandleActivate()
{
	//  if(!win::bSuspended)
	//		return;

	win::bSuspended = 0;
	win::bNeedRender = 1;

	if(Mouse)    Mouse->SetCapture();
	if(Keyboard) Keyboard->ClearPressed();

	if( winMouseWasVisible || g_cursor_mode->GetInt() )
		r3dMouse::Show(true);
	else 
		r3dMouse::Hide(true);
}

void win::HandleDeactivate()
{
	//  if(win::bSuspended)
	//    return;
	win::bSuspended = 1;

	if( !(r_render_on_deactivation && r_render_on_deactivation->GetInt()) )
		win::bNeedRender = 0;
	else
		win::bNeedRender = 1;

	winMouseWasVisible = r3dMouse_visible;

	if(Mouse)    Mouse->ReleaseCapture();
	if(Keyboard) Keyboard->ClearPressed();
}

void win::HandleMinimize()
{
	win::bNeedRender = 0;
}

void win::HandleRestore()
{
	win::bNeedRender = 1;
}


//
//
//
// main Callback function
//
//

/*
namespace 
{
  //
  // some vars to simulate move moving by ourselves.
  //
  
  static bool bOnCaption = false;	// true if we're on caption
  static bool bDragging  = true;	// true if we're dragging our window
  static int  dragX      = 0;		// offset of drag point, relative to window origin
  static int  dragY      = 0;		// offset of drag point, relative to window origin
  
  static void dragDisable()
  {
    if(!bDragging) return;
  
    ReleaseCapture();
    bDragging = false;

    return;
  }  
};
*/

void (*OnDblClick)() = 0;

static 
LRESULT CALLBACK win__WndFunc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static int userProcActive = 0;

  if(userProcActive) {
    // in very rare occasions we can reenter this function
    // one of cases is that when error is thrown from processing user or UI functions
    // so, in this case we'll enter to never ending loop
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
  }
 
  userProcActive = true;
  bool bAnyCompleted = false;
  for(int i = 0; i < NUM_MSG_PROC_MAX; i++ )
  {
	  if ( r3dApp_MsgProc3 [i] )
		  bAnyCompleted |= r3dApp_MsgProc3[i](uMsg, wParam, lParam);
  }
  userProcActive = false;
  if ( bAnyCompleted ) return 0;


  //r3dOutToLog("uMsg %x\n", uMsg);
  switch(uMsg) 
  {
    case WM_CLOSE:
    {
      r3dOutToLog("alt-f4 pressed\n");
      r3dOutToLog("...terminating application\n");

      ClipCursor(NULL);
      
      //HRESULT res = TerminateProcess(r3d_CurrentProcess, 0);

	  g_bExit = true;
      return 0;
    }
    
    case WM_CONTEXTMENU:
      // disable context menu
      return 0;
      
    case WM_ENTERMENULOOP:
      r3dOutToLog("!!!warning!!! r3dApp entered to modal menu loop\n");
      break;
      
    case WM_ENTERSIZEMOVE:
      //r3d_assert(0 && "WM_ENTERSIZEMOVE");
      win::HandleDeactivate();
      return 0;
      
    case WM_EXITSIZEMOVE:
      r3dOutToLog("WM_EXITSIZEMOVE: %d\n", win::bSuspended);
      win::HandleActivate();
      break;

/*
    case WM_CAPTURECHANGED:
      // if we lose capture from our window - disable dragging
      if(bDragging && (HWND)lParam != hWnd) {
        // note: do not call dragDisable(), because it will call ReleaseCapture()
        bDragging = false;
        break;
      }
      break;

    case WM_LBUTTONUP:
    {
      if(bDragging) {
        dragDisable();
      }
      break;
    }

    case WM_LBUTTONDOWN:
    {
      if(bOnCaption) {
        // start dragging our window, calc drag anchor point, relative to window corner
        POINT pp;        
        GetCursorPos(&pp);
        RECT rr;
        GetWindowRect(hWnd, &rr);

        dragX = pp.x - rr.left;
        dragY = pp.y - rr.top;
        
        ::SetCapture(hWnd);
        bDragging = true;
      }
      
      break;
    }
    
    case WM_MOUSEMOVE:
    {
      if(bDragging) {
        POINT pp;        
        GetCursorPos(&pp);
        SetWindowPos(hWnd, 0, pp.x - dragX, pp.y - dragY, 0, 0, SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOZORDER);
      }

      break;
    }

    case WM_NCHITTEST:
    {
      if(!r3dRenderer || !r3dRenderer->d3dpp.Windowed) 
        break;
      if(win::bSuspended)
        break;
        
      // if cursor is clipped somewhere, don't allow this window dragging at all.
      RECT clipRect;
      ::GetClipCursor(&clipRect);
      if(clipRect.left != 0 || clipRect.top != 0 || clipRect.right != curDispWidth || clipRect.bottom != curDispHeight) {
        return HTCLIENT;
      }
        
      LONG hitTest = DefWindowProc(hWnd, uMsg, wParam, lParam);
      if(hitTest == HTCLIENT && bOnCaption) {
        if(Mouse) Mouse->Hide(); 
        if(Mouse) Mouse->SetCapture();
        
        bOnCaption = false;
        return hitTest;
      }
      
      if(hitTest == HTCAPTION && bOnCaption) {
        // return that we still in client area - so windows will not start windows moving by itself
        return HTCLIENT;
      }
      
      if(hitTest == HTCAPTION && !bOnCaption) {
        if(Mouse) Mouse->Show(); 
        if(Mouse) Mouse->ReleaseCapture();

        // we will simuate dragging by ourselves, otherwise game will be in modal dragging loop!
        bOnCaption = true;
        return HTCLIENT;
      }

      return hitTest;
    }
*/    
    
    // disable menu calling when ALT pressed
    case WM_SYSCOMMAND:
    {
      // disable those system commands
      switch(wParam & 0xFFFF) {
        case 0xF093:		// system menu on caption bar
        case SC_KEYMENU:
        case SC_MOVE:
          return 0;
        case SC_MINIMIZE:
          win::HandleMinimize();
          break;
        case SC_RESTORE:
          win::HandleRestore();
          break;

      }

      r3dOutToLog("r3dapp: SysCmd: %x\n", wParam & 0xFFFF);
      break;
    }

    case WM_NCACTIVATE: 
      //dragDisable();

      if((wParam & 0xFFFF) == TRUE) 
        win::HandleActivate();
      break;

    case WM_ACTIVATE:
      //dragDisable();

      if((wParam & 0xFFFF) == WA_INACTIVE) {
        win::HandleDeactivate();
      } else {
        win::HandleActivate();
      }
      break;

	case WM_KEYDOWN:
	{
		EngineConsole::ProcessKey( wParam );
		break;
	}

	case WM_LBUTTONDBLCLK:
		if( OnDblClick )
			OnDblClick() ;
		break;

    // store char to input stream
    case WM_CHAR:
    {
		EngineConsole::ProcessChar( wParam );

        int	ch;

      ch              = (TCHAR)wParam;
      *(input_StackHead++) = ch;
      if(input_StackHead >= input_ScanStack + INPUT_KBD_STACK)
	input_StackHead = input_ScanStack;
      break;
    }

    case WM_PAINT:
    {
	HDC         hDC;
	PAINTSTRUCT ps;

      hDC = BeginPaint(hWnd,&ps);
      wnd__DrawLogo();
      EndPaint(hWnd,&ps);
      break;
    }

    case WM_DESTROY:
      //PostQuitMessage (0);
		g_bExit = true;
      break;
  }

  return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void r3dWinStyleModify(HWND hWnd, int add, DWORD style)
{
  if(add)
    SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(win::hWnd, GWL_STYLE) | style);
  else
    SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(win::hWnd, GWL_STYLE) & ~style);
}


BOOL win::Init()
{
  static const char* szWinClassName = "r3dWin";

  r3d_assert(hInstance != NULL);

  WNDCLASS  wndclass;
  wndclass.style         = CS_DBLCLKS | CS_GLOBALCLASS;
  wndclass.lpfnWndProc   = win__WndFunc;		// window function
  wndclass.cbClsExtra    = 0;				// no extra count of bytes
  wndclass.cbWndExtra    = 0;				// no extra count of bytes
  wndclass.hInstance     = GetModuleHandle(NULL);	// this instance
  wndclass.hIcon         = (hWinIcon) ? hWinIcon : LoadIcon(NULL, IDI_APPLICATION);
  wndclass.hCursor       = LoadCursor(NULL, IDC_ARROW);
  wndclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  wndclass.lpszMenuName  = NULL;
  wndclass.lpszClassName = szWinClassName;
  RegisterClass(&wndclass);

	HDC		disp_dc;

  disp_dc  = CreateIC("DISPLAY", NULL, NULL, NULL);
  curDispWidth  = GetDeviceCaps(disp_dc, HORZRES);
  curDispHeight = GetDeviceCaps(disp_dc, VERTRES);
  //disp_bpp = GetDeviceCaps(disp_dc, BITSPIXEL);
  DeleteDC(disp_dc);

  hWnd = CreateWindow(
    /* window class name */       szWinClassName,            			
    /* window caption*/           szWinName,					
    /* window style */            WS_VISIBLE | WS_OVERLAPPEDWINDOW,
    /* initial x position */      (curDispWidth - StartWinWidth) / 2,		
    /* initial y position */      (curDispHeight - StartWinHeight) / 2,		
    /* initial x size */          StartWinWidth,				
    /* initial y size */          StartWinHeight,				
    /* parent window handle */    NULL,                 			
    /* window menu handle*/       NULL,                 			
    /* program instance handle */ GetModuleHandle(NULL), 			
    /* creation parameters */     NULL);                 			

  if(!hWnd) {
    MessageBox(GetActiveWindow(), "Window Creation Failed", "Error", MB_OK);
    return FALSE;
  }

/*
  //WinStyleModify(0, WS_SYSMENU);
  WinStyleModify(0, WS_BORDER);
  WinStyleModify(0, WS_CAPTION);
  WinStyleModify(1, WS_DLGFRAME);
*/

  // set icon
  ::SendMessage(hWnd, WM_SETICON, TRUE,  (LPARAM)wndclass.hIcon);
  ::SendMessage(hWnd, WM_SETICON, FALSE, (LPARAM)wndclass.hIcon);
  
  r3dWinStyleModify(hWnd, 0, WS_THICKFRAME);	// prevent resize
  r3dWinStyleModify(hWnd, 0, WS_MAXIMIZEBOX);	// prevent maximize
  //r3dWinStyleModify(hWnd, 0, WS_MINIMIZEBOX);	// prevent minimize
  

  ShowWindow(win::hWnd, FALSE);
  wnd__DrawLogo();
  InvalidateRect(win::hWnd, NULL, FALSE);
  UpdateWindow(win::hWnd);
  SetFocus(win::hWnd);

  return TRUE;
}



int win::ProcessSuspended()
{
  if(!bSuspended)
    return FALSE;

  MSG msg;
  while(PeekMessage(&msg, NULL,0,0,PM_NOREMOVE))
  {
    if(!GetMessage (&msg, NULL, 0, 0)) 
      return 1;

    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  return TRUE;
}
// Anti Inject

// ANTI PAUSE
DWORD WINAPI AntiPause() 
{
    DWORD TimeTest1=0, TimeTest2=0;
    while(true)
    {
        TimeTest1 = TimeTest2;
        TimeTest2 = GetTickCount();
        if(TimeTest1 != 0)
        {
            Sleep(1000);
            if((TimeTest2-TimeTest1) > 5000)
            {
                ExitProcess(0);
                TerminateProcess(GetCurrentProcess(),0);
            }
        }        
    }
    return 0;
}  
// ANTI DEBUG - ANTI SANDBOXIE THANKS TO JOHAN - N0v0c
bool inDebugger()
{
        ("inDebugger::ACInDebugger");
    return ( GetModuleHandle( "dbghelp" ) != NULL );
}
 
bool inSandbox()
{
        ("inSandbox::ACInSandBox");
    return ( FindWindow( "TCPViewClass", NULL ) != NULL || FindWindow( "RegFromApp", NULL ) != NULL );
}
 
bool inSandboxie()
{
        ("inSandboxie::ACinSandboxie");
    return ( GetModuleHandle( "SbieDll" ) != NULL || FindWindow( "SandboxieControlWndClass", NULL ) != NULL );
}
 
bool inVMWare()
{
        ("inVMWare::ACInVMWare");
    bool rc = true;
 
    __try
    {
        __asm
        {
            push   edx
            push   ecx
            push   ebx
 
 
            mov    eax, 'VMXh'
            mov    ebx, 0 // any value but not the MAGIC VALUE
            mov    ecx, 10 // get VMWare version
            mov    edx, 'VX' // port number
 
 
            in     eax, dx // read port
            // on return EAX returns the VERSION
            cmp    ebx, 'VMXh' // is it a reply from VMWare?
            setz   [rc] // set return value
 
 
            pop    ebx
            pop    ecx
            pop    edx
        }
    }
    __except( EXCEPTION_EXECUTE_HANDLER )
    {
        rc = false;
    }
 
 
    return rc;
}
 
 
DWORD __forceinline inVPC_exceptionFilter( LPEXCEPTION_POINTERS ep )
{
        ("__forceinline::ACForceinline");
    PCONTEXT ctx = ep->ContextRecord;
 
    ctx->Ebx = -1;
    ctx->Eip += 4;
 
    return EXCEPTION_CONTINUE_EXECUTION;
}
 
bool inVirtualPC()
{
        ("inVirtualPc::ACInVirtualPc");
    bool rc = false;
 
    __try
    {
        _asm push ebx
        _asm mov  ebx, 0
        _asm mov  eax, 1
 
 
        _asm __emit 0Fh
        _asm __emit 3Fh
        _asm __emit 07h
        _asm __emit 0Bh
 
 
        _asm test ebx, ebx
        _asm setz [rc]
        _asm pop ebx
    }
    __except( inVPC_exceptionFilter( GetExceptionInformation() ) )
    {
    }
 
 
    return rc;
}
 
bool inWinJail()
{
        ("inWinJail::ACInWinJail");
    return ( FindWindow( "Afx:400000:0", NULL ) != NULL );
}
 
bool isHooked()
{
        ("isHooked::ACIshooked");
    unsigned char bBuffering;
    unsigned long aCreateProcesses = ( unsigned long )GetProcAddress( GetModuleHandle( "kernel32.dll" ), "CreateProcessA" );
 
    ReadProcessMemory( GetCurrentProcess(), ( void* )aCreateProcesses, &bBuffering, 1, 0 );
 
    return ( bBuffering == 0xE8 || bBuffering == 0xE9 || bBuffering == 0xEB );
}
 
bool isSandboxed()
{
        ("isSandBoxed::ACIsSandboxed");
     return ( inSandbox() || inSandboxie() || inVMWare() || inVirtualPC() || inWinJail() || inDebugger() || isHooked() );
}
////////////// Anti Inject /////////////////
#ifndef Duno_Dump_H
#define Dump_Dump_H



#define MAX_DUMP_OFFSETS 66 // << Atualizar Quantidade de Hacks
#define MAX_DUMP_SIZE 16      // << Quantidade de Hex contados
#define MAX_PROCESS_DUMP 66  // << Atualizar Quantidade de Hacks

typedef struct ANITHACK_PROCDUMP {
	unsigned int m_aOffset;
	unsigned char m_aMemDump[MAX_DUMP_SIZE];
} *PANITHACK_PROCDUMP;

extern ANITHACK_PROCDUMP g_ProcessesDumps[MAX_PROCESS_DUMP];

void SystemProcessesScan();
void ProtectionMain();
bool ScanProcessMemory(HANDLE hProcess);
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <tlhelp32.h>
//#include "dump.h" //AntiDump


/////////////////////TEST ANTI DUMP///////////////////////

char NameProcess[100]={0}; // << log.txt ainda nao pega


ANITHACK_PROCDUMP g_ProcessesDumps[MAX_PROCESS_DUMP] = {    
	// AO ADICIONAR HACKS N�O ESQUE�ER DE ATUALIZAR A QUANTIDADE DE HACKS NO Dump.h
	{0x44EB02, {0xE8, 0xC5, 0xC0, 0x00, 0x00, 0xE9, 0x78, 0xFE, 0xFF, 0xFF, 0xCC, 0xCC, 0xCC, 0xCC, 0x51, 0x8D}}, //Process Explorer 
	{0xCD3546, {0x6A, 0x1C, 0x89, 0x4C, 0x24, 0x2C, 0x8B, 0xC8, 0x51, 0x6A, 0x01, 0x52, 0x89, 0x6C, 0x24, 0x30}}, //Process Explorer
	{0x41F525, {0xe8, 0xb5, 0x8b, 0x00, 0x00, 0xe9, 0x78, 0xfe, 0xff, 0xff, 0x33, 0xc0, 0xf6, 0xc3, 0x10, 0x74}}, //deskhedron
	{0x81E810, {0xc6, 0x05, 0x30, 0x09, 0x82, 0x00, 0x00, 0xe8, 0xb4, 0xff, 0xff, 0xff, 0xb8, 0x40, 0x3d, 0xa4}}, //Cheat Engine 6.3 1
	{0x434D90, {0xc6, 0x05, 0x40, 0x55, 0x43, 0x00, 0x00, 0xe8, 0xb4, 0xff, 0xff, 0xff, 0xb8, 0xf0, 0x17, 0x44}}, //Cheat Engine 6.3 2
	{0x574EC0, {0x55, 0x8B, 0xEC, 0x83, 0xC4, 0xEC, 0x53, 0x33, 0xC0, 0x89, 0x45, 0xEC, 0xB8, 0xE0, 0x49, 0x57}}, //Cheat Engine
	{0x574EEC, {0xE8, 0x8B, 0xEA, 0xF1, 0xFF, 0x8D, 0x45, 0xEC, 0xE8, 0x33, 0x56, 0xFF, 0xFF, 0xE8, 0x5A, 0x1F}}, //Cheat Engine
	{0x4CBD70, {0x8D, 0x85, 0x7C, 0xFE, 0xFF, 0xFF, 0xBA, 0x03, 0x00, 0x00, 0x00, 0xE8, 0xB0, 0x8F, 0xF3, 0xFF}}, //Cheat Engine
	{0x591F94, {0x55, 0x8B, 0xEC, 0x83, 0xC4, 0xEC, 0x53, 0x33, 0xC0, 0x89, 0x45, 0xEC, 0xB8, 0x5C, 0x1A, 0x59}}, //Cheat Engine
	{0x591FC0, {0xE8, 0x07, 0x23, 0xF0, 0xFF, 0x8D, 0x45, 0xEC, 0xE8, 0x1F, 0x4B, 0xFF, 0xFF, 0xE8, 0x76, 0x99}}, //Cheat Engine
	{0x5839E7, {0x8D, 0x45, 0xB0, 0x50, 0x6A, 0x08, 0x8D, 0x85, 0x78, 0xFF, 0xFF, 0xFF, 0x50, 0xA1, 0xB0, 0xA1}}, //Cheat Engine
	{0x4CBE2B, {0x8D, 0x55, 0xF0, 0xB9, 0x04, 0x00, 0x00, 0x00, 0x8B, 0xC7, 0xE8, 0x02, 0x15, 0xF5, 0xFF, 0x8B}}, //Cheat Engine
	{0x5CF354, {0x55, 0x8B, 0xEC, 0x83, 0xC4, 0xEC, 0x53, 0x33, 0xC0, 0x89, 0x45, 0xEC, 0xB8, 0x44, 0xED, 0x5C}}, //Cheat Engine
	{0x5CF440, {0xE8, 0x37, 0xA3, 0xFC, 0xFF, 0xE8, 0x8E, 0x96, 0xF9, 0xFF, 0x8B, 0x03, 0xBA, 0xA8, 0xF5, 0x5C}}, //Chear Engine
	{0x5CF43D, {0x8D, 0x45, 0xEC, 0xE8, 0x37, 0xA3, 0xFC, 0xFF, 0xE8, 0x8E, 0x96, 0xF9, 0xFF, 0x8B, 0x03, 0xBA}}, //Cheat Engine
	{0x5FECF4, {0x55, 0x8B, 0xEC, 0x83, 0xC4, 0xEC, 0x53, 0x33, 0xC0, 0x89, 0x45, 0xEC, 0xB8, 0xE4, 0xE4, 0x5F}}, //Cheat Engine
	{0x6105D4, {0x55, 0x8B, 0xEC, 0x83, 0xC4, 0xE8, 0x53, 0x33, 0xC0, 0x89, 0x45, 0xE8, 0x89, 0x45, 0xEC, 0xB8}}, //Cheat Engine
	{0x5FED5B, {0xE8, 0x10, 0xC3, 0xE9, 0xFF, 0x8B, 0x0D, 0x64, 0x5D, 0x60, 0x00, 0x8B, 0x03, 0x8B, 0x15, 0x00}}, //Cheat Engine
	{0x434460, {0xc6, 0x05, 0x60, 0xf0, 0x43, 0x00, 0x00, 0xe8, 0xb4, 0xff, 0xff, 0xff, 0xb8, 0x10, 0x18, 0x44}}, //Cheat Engine
	{0x5674D4, {0x55, 0x8B, 0xEC, 0x83, 0xC4, 0xEC, 0x53, 0x33, 0xC0, 0x89, 0x45, 0xEC, 0xB8, 0x2C, 0x70, 0x56}}, //Cheat Engine
	{0x5AA16C, {0xE8, 0x13, 0x40, 0xFF, 0xFF, 0xE8, 0x86, 0x2C, 0xFC, 0xFF, 0x8B, 0x03, 0xBA, 0xD4, 0xA2, 0x5A}}, //Cheat Engine
	{0x591F94, {0x55, 0x8B, 0xEC, 0x83, 0xC4, 0xEC, 0x53, 0x33, 0xC0, 0x89, 0x45, 0xEC, 0xB8, 0x5C, 0x1A, 0x59}}, //Cheat Engine 
	{0x5CF354, {0x78, 0xAA, 0x4A, 0x00, 0x48, 0xAA, 0x4A, 0x00, 0xFC, 0x17, 0x4A, 0x00, 0xCC, 0x17, 0x4A, 0x00}}, //Cheat Engine
	{0x606140, {0x8C, 0x79, 0x60, 0x00, 0xE0, 0xA8, 0x60, 0x00, 0x18, 0x70, 0x4D, 0x00, 0xE8, 0x18, 0x60, 0x00}}, //Cheat Engine
	{0x574EC0, {0x55, 0x8B, 0xEC, 0x83, 0xC4, 0xEC, 0x53, 0x33, 0xC0, 0x89, 0x45, 0xEC, 0xB8, 0xE0, 0x49, 0x57}}, //Cheat Engine 
	{0x40C484, {0x8B, 0x45, 0x08, 0xFF, 0x70, 0x0C, 0xFF, 0x70, 0x08, 0xE8, 0xD6, 0xF8, 0xFF, 0xFF, 0x0F, 0xB7}}, //Cheat Engine
	{0x408771, {0xEB, 0x07, 0x8B, 0x45, 0x0C, 0x33, 0xD2, 0x89, 0x10, 0x83, 0x3F, 0x00, 0x74, 0x18, 0x85, 0xDB}}, //Cheat Engine
	{0x41D39A, {0xEB, 0x0B, 0x0B, 0xEB, 0x0B, 0x0B, 0xEB, 0x35, 0x2B, 0xCA, 0x61, 0x4C, 0xA7, 0x41, 0x35, 0xC0}}, //SpeederXP
	{0x42FAA4, {0xA7, 0x62, 0x62, 0x62, 0x68, 0x68, 0x68, 0x7B, 0x7B, 0x7B, 0x98, 0x98, 0x98, 0xB9, 0xB9, 0xB9}}, //SpeederXP
	{0x50541A, {0xBE, 0xDE, 0xEE, 0xE2, 0x52, 0xE8, 0x6B, 0xFA, 0xFF, 0xFF, 0x68, 0x3A, 0x84, 0xE5, 0xDD, 0xE8}}, //SpeederXP
	{0x42727A, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xA8, 0x7A, 0x43, 0x00, 0x68, 0xE4, 0x73, 0x42, 0x00, 0x64}}, //SpeederXP
	{0x5053C8, {0x68, 0x3A, 0x38, 0x21, 0xDB, 0xE8, 0xA9, 0xAB, 0x07, 0x00, 0x72, 0xAA, 0x00, 0x72, 0x5C, 0xC2}}, //SpeederXP
	{0x410086, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x98, 0x3D, 0x41, 0x00, 0x68, 0x0C, 0x02, 0x41, 0x00, 0x64}}, //Game Speed Changer
	{0x40FBB6, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x48, 0x3D, 0x41, 0x00, 0x68, 0x3C, 0xFD, 0x40, 0x00, 0x64}}, //Game Speed Changer
	{0x40C0B0, {0x70, 0x6C, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6F, 0x6E, 0x31, 0x5C, 0x6F, 0x62, 0x6A, 0x5C, 0x52}}, //Speed Hack Simplifier
	{0x40E04E, {0x53, 0x68, 0x61, 0x64, 0x6F, 0x77, 0x42, 0x65, 0x61, 0x73, 0x74, 0x2E, 0x41, 0x53, 0x41, 0x46}}, //Speed Hack Simplifier
	{0x402868, {0x52, 0x92, 0x3B, 0x8D, 0xD5, 0x33, 0x78, 0xB7, 0x32, 0x71, 0xA3, 0x37, 0x73, 0xBF, 0x45, 0x85}}, //Speed Hack Simplifier
	{0x417259, {0x89, 0x42, 0xBC, 0xBA, 0x14, 0x00, 0x00, 0x80, 0xB8, 0x0F, 0x00, 0x00, 0x80, 0xE8, 0x65, 0xB7}}, //Speed Hack
	{0x40134A, {0xA1, 0x8B, 0x50, 0x48, 0x00, 0xC1, 0xE0, 0x02, 0xA3, 0x8F, 0x50, 0x48, 0x00, 0x52, 0x6A, 0x00}}, //Speed Hack
	{0x40134F, {0xC1, 0xE0, 0x02, 0xA3, 0x8F, 0x40, 0x47, 0x00, 0x52, 0x6A, 0x00, 0xE8, 0xEF, 0x14, 0x07, 0x00}}, //Speed Hack
	{0x401338, {0xEB, 0x10, 0x66, 0x62, 0x3A, 0x43, 0x2B, 0x2B, 0x48, 0x4F, 0x4F, 0x4B, 0x90, 0xE9, 0x98, 0x50}}, //Speed Hack
	{0x401414, {0x68, 0xA4, 0x22, 0x40, 0x00, 0xE8, 0xEE, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}, //Speed Hack
	{0x4217E0, {0x60, 0xBE, 0x00, 0xD0, 0x41, 0x00, 0x8D, 0xBE, 0x00, 0x40, 0xFE, 0xFF, 0x57, 0xEB, 0x0B, 0x90}}, //!xSpeed.net
	{0x41F04A, {0x8D, 0x5D, 0x5E, 0x53, 0x50, 0xFF, 0x95, 0x49, 0x0F, 0x00, 0x00, 0x89, 0x85, 0x4D, 0x05, 0x00}}, //!xSpeed.net
	{0x401100, {0x33, 0xC0, 0x8B, 0x04, 0x9D, 0x21, 0x79, 0x40, 0x00, 0x36, 0x8A, 0x54, 0x2A, 0x80, 0x03, 0xC2}}, //!xSpeed.net 
	{0x41D4FE, {0x30, 0xB3, 0x7F, 0x4E, 0xB8, 0x85, 0x54, 0xBC, 0x8B, 0x59, 0xBD, 0x8C, 0x5A, 0xBD, 0x8C, 0x5A}}, //!xSpeed.net
	{0x41F001, {0x60, 0xE8, 0x03, 0x00, 0x00, 0x00, 0xE9, 0xEB, 0x04, 0x5D, 0x45, 0x55, 0xC3, 0xE8, 0x01, 0x00}}, //!xSpeed.net
	{0x4217E0, {0x60, 0xBE, 0x00 ,0xD0, 0x41, 0x00, 0x8D, 0xBE, 0x00, 0x40, 0xFE, 0xFF, 0x57, 0xEB, 0x0B, 0x90}}, //!xSpeed.net
	{0x420630, {0x60, 0xBE, 0x00, 0xC0, 0x41, 0x00, 0x8D, 0xBE, 0x00, 0x50, 0xFE, 0xFF, 0x57, 0xEB, 0x0B, 0x90}}, //!xSpeed.net
	{0x420001, {0x60, 0xE8, 0x03, 0x00, 0x00, 0x00, 0xE9, 0xEB, 0x04, 0x5D, 0x45, 0x55, 0xC3, 0xE8, 0x01, 0x00}}, //!xSpeed.Pro
	{0x426ECA, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0x90, 0x7A, 0x43, 0x00, 0x68, 0x34, 0x70, 0x42, 0x00, 0x64}}, //Speed Gear 
	{0x568E9A, {0x68, 0xB8, 0xF9, 0x85, 0x13, 0xE8, 0x9D, 0x53, 0x01, 0x00, 0xB6, 0x94, 0x70, 0x4B, 0xE8, 0x87}}, //Speed Gear
	{0x40970E, {0x68, 0xB4, 0x98, 0x40, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x64, 0x89, 0x25, 0x00}}, //Speed Gear
	{0x568E9A, {0x68, 0xB8, 0xF9, 0x85, 0x13, 0xE8, 0x9D, 0x53, 0x01, 0x00, 0xB6, 0x94, 0x70, 0x4B, 0xE8, 0x87}}, //Speed Gear 
	{0x4011D4, {0x68, 0x50, 0x8E, 0x40, 0x00, 0xE8, 0xF0, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}, //HackSpeed
	{0x416B41, {0xE8, 0xBC, 0x57, 0x00, 0x00, 0xA3, 0xA4, 0xB2, 0x44, 0x00, 0xE8, 0x65, 0x55, 0x00, 0x00, 0xE8}}, //Game Speed Adjuster
	{0x416AB0, {0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68, 0xC0, 0xC0, 0x43, 0x00, 0x68, 0x24, 0xA0, 0x41, 0x00, 0x64}}, //Game Speed Adjuster
	{0x4BCFA4, {0x55, 0x8B, 0xEC, 0x83, 0xC4, 0xF0, 0x53, 0x56, 0x57, 0xB8, 0xC4, 0xCC, 0x4B, 0x00, 0xE8, 0xB1}}, //Xelerator
	{0x430A27, {0xE9, 0x06, 0x01, 0x00, 0x00, 0x3B, 0xD1, 0x72, 0x6C, 0x8B, 0x55, 0x08, 0x83, 0x7A, 0x54, 0x00}}, //Speed Wizard
	{0x401027, {0x73, 0xE3, 0xBC, 0x49, 0x73, 0x62, 0x72, 0x4D, 0x73, 0xFA, 0x00, 0x4C, 0x73, 0x0F, 0x9E, 0x4A}}, //Game Acelerator
	{0x41F525, {0xe8, 0xb5, 0x8b, 0x00, 0x00, 0xe9, 0x78, 0xfe, 0xff, 0xff, 0x33, 0xc0, 0xf6, 0xc3, 0x10, 0x74}}, //deskhedron
	{0x7C4001, {0x60, 0xe8, 0x03, 0x00, 0x00, 0x00, 0xe9, 0xeb, 0x04, 0x5d, 0x45, 0x55, 0xc3, 0xe8, 0x01, 0x00}}, //ArtMoney PRO v7.41
	{0x4067CF, {0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x08, 0xd2, 0x40, 0x00, 0x68, 0xf4, 0x8a, 0x40, 0x00, 0x64}}, //Winject
	{0x2E46D2, {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x28, 0x03, 0x00, 0x00, 0xA3, 0x58, 0x94, 0x2F, 0x00, 0x89, 0x0D}}, //Dktz
	{0x3BF54C, {0xC8, 0xF5, 0x3B, 0x00, 0x86, 0x7D, 0xA2, 0x5B, 0x20, 0x24, 0x24, 0x04, 0x00, 0x00, 0x00, 0x00}}, //Configurable_Injector
	{0x2CFB54, {0x64, 0xFB, 0x2C, 0x00, 0x4C, 0x69, 0x34, 0x00, 0x8A, 0x33, 0x29, 0x76, 0x00, 0xE0, 0xFD, 0x7E}}, // PlayLoader_original
};	

bool ScanProcessMemory(HANDLE hProcess) {
	for(int i = 0; i < MAX_PROCESS_DUMP; i++)
	{
		char aTmpBuffer[MAX_DUMP_SIZE];
		SIZE_T aBytesRead = 0;
		ReadProcessMemory(hProcess, (LPCVOID)g_ProcessesDumps[i].m_aOffset, (LPVOID)aTmpBuffer, sizeof(aTmpBuffer), &aBytesRead);

		if(memcmp(aTmpBuffer, g_ProcessesDumps[i].m_aMemDump, MAX_DUMP_SIZE) == 0)
		{
			return true;
			break;
		}
	}
	return false;
}

void SystemProcessesScan() {
	HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if(hProcessSnap != INVALID_HANDLE_VALUE)
	{
		PROCESSENTRY32 pe32;
		pe32.dwSize = sizeof(PROCESSENTRY32);

		if(Process32First(hProcessSnap, &pe32))
		{
			do
			{
				HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pe32.th32ProcessID);
				if(hProcess != NULL)
				{
					if(ScanProcessMemory(hProcess))
					{
						Sleep (100); 
						ExitProcess(0);	
					}
				}
			}
			while(Process32Next(hProcessSnap, &pe32));
		}
	}
	CloseHandle(hProcessSnap);
}
void D_Scan(){
again:
	SystemProcessesScan();
	Sleep (100);
	goto again;
}

void ProtectionMain(){
	CreateThread(NULL,NULL,LPTHREAD_START_ROUTINE(D_Scan),NULL,0,0);
	SystemProcessesScan();
}

static void startupFunc(DWORD in)
{
//  in = in;
  
	SystemProcessesScan();//Dump
	ProtectionMain();//Dump 
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) AntiPause, NULL, 0, NULL);

  game::PreInit();  

  win::Init();

  game::Init();

  game::MainLoop();

  game::Shutdown();
}

//
//
// the One And Only - WinMain!
//
//
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
#ifdef FINAL_BUILD

	TCHAR	*pEnd = NULL;
	TCHAR	szFullFileName[MAX_PATH] = { 0, };
	TCHAR	szMsg[255];
	int		nRet;

	g_dwMainThreadID = GetCurrentThreadId();
	LoadString(hInstance, 103, szTitle, 500);
	GetModuleFileName(NULL, szFullFileName, MAX_PATH);
	pEnd = _tcsrchr( szFullFileName, _T('\\')) + 1;
	if (!pEnd)
	{
		return FALSE;	
	}
	*pEnd = _T('\0');

	_stprintf(g_szIniPath, _T("%s"), szFullFileName);				
	_stprintf(g_szHShieldPath, _T("%s\\HackShield"), szFullFileName);
	_tcscat(szFullFileName, _T("HackShield\\EhSvc.dll"));			
	_tcscat(g_szIniPath, _T("MiniAEnv.INI"));		

	AHNHS_EXT_ERRORINFO HsExtError;
	sprintf( HsExtError.szServer, "%s", "167.114.32.63" );	
	sprintf( HsExtError.szUserId, "%s", "GameUser" );			
	sprintf( HsExtError.szGameVersion, "%s", "1.0.0.1" );

	DWORD dwResult ;

	dwResult = _AhnHS_HSUpdateEx(g_szHShieldPath, 1000 * 600,1000 ,AHNHSUPDATE_CHKOPT_HOSTFILE| AHNHSUPDATE_CHKOPT_GAMECODE,  HsExtError, 1000* 20 );

	nRet = _AhnHS_StartMonitor(HsExtError,g_szHShieldPath);

	if (nRet != HS_ERR_OK && nRet != 257 && nRet != 12159)
	{
		_stprintf(szMsg, _T("HackShield Load Error. Folder HShield is missing from your computer(Error Code = %d)."), nRet);
		MessageBox(NULL, szMsg, szTitle, MB_OK);
		return FALSE;
	}

	extern int __stdcall AhnHS_Callback(long lCode, long lParamSize, void* pParam);
	nRet = _AhnHS_Initialize(szFullFileName, AhnHS_Callback, 
		1000, "B228F2916A48AC24", 
		AHNHS_CHKOPT_ALL|AHNHS_DISPLAY_HACKSHIELD_TRAYICON|AHNHS_CHKOPT_LOADLIBRARY|AHNHS_CHKOPT_PROTECT_D3DX|AHNHS_CHKOPT_LOCAL_MEMORY_PROTECTION,
		AHNHS_SPEEDHACK_SENSING_RATIO_HIGHEST);


	if (nRet != HS_ERR_OK && nRet != 257 && nRet != 12159)
	{
		_stprintf(szMsg, _T("HackShield Initialize Error. Folder HShield is missing from your computer(Error Code = %d)."), nRet);
		MessageBox(NULL, szMsg, szTitle, MB_OK);
		return FALSE;
	}

	DWORD version;
	nRet = _AhnHS_GetSDKVersion(&version);

	if (nRet != HS_ERR_OK)
	{
		_stprintf(szMsg, _T("HackShield Api Error. (Error Code = %d)."), nRet);
		MessageBox(NULL, szMsg, szTitle, MB_OK);
	}
	else
	{
		if (version != 1446617492)
		{
			_stprintf(szMsg, _T("HackShield Load Failed. Wrong Version."));
			MessageBox(NULL, szMsg, szTitle, MB_OK);
			return FALSE;
		}
	}

	nRet = _AhnHS_StartService();
	assert(nRet != HS_ERR_NOT_INITIALIZED);
	assert(nRet != HS_ERR_ALREADY_SERVICE_RUNNING);

	if (nRet != HS_ERR_OK && nRet != 257 && nRet != 12159)
	{
		_stprintf(szMsg, _T("HackShield Start Error. Folder HShield is missing from your computer(Error Code = %d)."), nRet);
		MessageBox(NULL, szMsg, szTitle, MB_OK);
		return FALSE;
	}

	antihackNotFound = 0;
#endif
#ifdef _DEBUG
#ifdef ENABLE_MEMORY_DEBUG
	//_CrtSetReportMode( _CRT_WARN, _CRTDBG_MODE_FILE );
	//_CrtSetReportFile( _CRT_WARN, _CRTDBG_FILE_STDOUT );

	_CrtMemState _ms;
	_CrtMemCheckpoint(&_ms);
#endif
#endif

#if 0
	char* __internal_gnrt_lkey(const char* name, int exp_year, int exp_month, int exp_day);
	void r3dLibraryInit(char* license);
	void checkLicenseKey();

	char* lic = __internal_gnrt_lkey("testing", 2010, 7, 10);
	r3dLibraryInit(lic);
	checkLicenseKey();
#endif

	// set our game to run only one processor (we're not using multithreading)
	// that will help with timer
	//DWORD_PTR oldmask = ::SetThreadAffinityMask(::GetCurrentThread(), 0);


  r3dscpy(__r3dCmdLine, lpCmdLine);
  
#ifdef _DEBUG
  //DWORD NewMask = ~(_EM_ZERODIVIDE | _EM_OVERFLOW | _EM_INVALID);
  //_controlfp(NewMask, _MCW_EM);
#endif 

  win::hInstance = hInstance;

  r3dThreadEntryHelper(startupFunc, 0);

  PostQuitMessage(0);

#ifdef _DEBUG
#ifdef ENABLE_MEMORY_DEBUG
   _CrtMemDumpAllObjectsSince(&_ms);
#endif
#endif
  return 0;
}

#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <iostream>
#include <boost/bind.hpp>
#include "../AppConfig.h"
#include "../MIPSAssembler.h"
#include "../Ps2Const.h"
#include "../PS2OS.h"
#include "win32/AcceleratorTableGenerator.h"
#include "win32/InputBox.h"
#include "Debugger.h"
#include "resource.h"
#include "PtrMacro.h"
#include "string_cast.h"

#define CLSNAME			_T("CDebugger")

#define ID_EDIT_COPY    (0xE001)
#define WM_EXECUNLOAD	(WM_USER + 0)
#define WM_EXECCHANGE	(WM_USER + 1)

using namespace Framework;
using namespace std;
using namespace boost;

CDebugger::CDebugger(CPS2VM& virtualMachine) :
m_pTestEngineView(NULL),
m_virtualMachine(virtualMachine)
{
	RECT rc;

	RegisterPreferences();

	if(!DoesWindowClassExist(CLSNAME))
	{
		WNDCLASSEX wc;
		memset(&wc, 0, sizeof(WNDCLASSEX));
		wc.cbSize			= sizeof(WNDCLASSEX);
		wc.hCursor			= LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground	= (HBRUSH)GetStockObject(GRAY_BRUSH); 
		wc.hInstance		= GetModuleHandle(NULL);
		wc.lpszClassName	= CLSNAME;
		wc.lpfnWndProc		= CWindow::WndProc;
		RegisterClassEx(&wc);
	}
	
	SetRect(&rc, 0, 0, 640, 480);

	Create(NULL, CLSNAME, _T(""), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, &rc, NULL, NULL);
	SetClassPtr();

	SetMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_DEBUGGER)));

	CreateClient(NULL);

	//Show(SW_MAXIMIZE);

	SetRect(&rc, 0, 0, 320, 240);

	//ELF View Initialization
	m_pELFView = new CELFView(m_pMDIClient->m_hWnd);
	m_pELFView->Show(SW_HIDE);

	//Functions View Initialization
	m_pFunctionsView = new CFunctionsView(m_pMDIClient->m_hWnd, &m_virtualMachine.m_EE);
	m_pFunctionsView->Show(SW_HIDE);
	m_pFunctionsView->m_OnFunctionDblClick.connect(bind(&CDebugger::OnFunctionViewFunctionDblClick, this, _1));
	m_pFunctionsView->m_OnFunctionsStateChange.connect(bind(&CDebugger::OnFunctionViewFunctionsStateChange, this));

	//OS Events View Initialization
	m_pOsEventView = new COsEventViewWnd(m_pMDIClient->m_hWnd);
	m_pOsEventView->Show(SW_HIDE);
	m_pOsEventView->m_OnEventDblClick.connect(bind(&CDebugger::OnEventViewEventDblClick, this, _1));

    //Instruction Test Console View Initialization
//    m_pTestEngineView = new CTestEngineWnd(m_pMDIClient->m_hWnd);
//    m_pTestEngineView->Show(SW_HIDE);
//    m_pTestEngineView->m_OnTestCaseLoad.connect(bind(&CDebugger::OnTestEngineViewTestCaseLoad, this, _1));

	//Debug Views Initialization
	m_nCurrentView = -1;

    memset(m_pView, 0, sizeof(m_pView));
    m_pView[DEBUGVIEW_EE]	= new CDebugView(m_pMDIClient->m_hWnd, virtualMachine, &m_virtualMachine.m_EE, 
        bind(&CPS2VM::StepEe, &m_virtualMachine), "EmotionEngine");
	m_pView[DEBUGVIEW_VU0]	= new CDebugView(m_pMDIClient->m_hWnd, virtualMachine, &m_virtualMachine.m_VU0, 
        bind(&CPS2VM::StepEe, &m_virtualMachine), "Vector Unit 0");
	m_pView[DEBUGVIEW_VU1]	= new CDebugView(m_pMDIClient->m_hWnd, virtualMachine, &m_virtualMachine.m_VU1, 
        bind(&CPS2VM::StepEe, &m_virtualMachine), "Vector Unit 1");
    m_pView[DEBUGVIEW_IOP]  = new CDebugView(m_pMDIClient->m_hWnd, virtualMachine, &m_virtualMachine.m_iop.m_cpu, 
        bind(&CPS2VM::StepIop, &m_virtualMachine), "IO Processor");

	m_virtualMachine.m_os->m_OnExecutableChange.connect(bind(&CDebugger::OnExecutableChange, this));
	m_virtualMachine.m_os->m_OnExecutableUnloading.connect(bind(&CDebugger::OnExecutableUnloading, this));

	ActivateView(DEBUGVIEW_EE);
	LoadSettings();

	if(GetDisassemblyWindow()->IsVisible())
	{
		GetDisassemblyWindow()->SetFocus();
	}

	UpdateLoggingMenu();
	CreateAccelerators();
}

CDebugger::~CDebugger()
{
    OnExecutableUnloadingMsg();

	DestroyAccelerators();

	SaveSettings();

    for(unsigned int i = 0; i < DEBUGVIEW_MAX; i++)
    {
        delete m_pView[i];
    }

	DELETEPTR(m_pELFView);
	DELETEPTR(m_pFunctionsView);
    DELETEPTR(m_pTestEngineView);
}

HACCEL CDebugger::GetAccelerators()
{
	return m_nAccTable;
}

void CDebugger::RegisterPreferences()
{
    CAppConfig& config(CAppConfig::GetInstance());

    config.RegisterPreferenceInteger("debugger.log.posx",           0);
    config.RegisterPreferenceInteger("debugger.log.posy",           0);
    config.RegisterPreferenceInteger("debugger.log.sizex",          0);
    config.RegisterPreferenceInteger("debugger.log.sizey",          0);
    config.RegisterPreferenceBoolean("debugger.log.visible",        true);

    config.RegisterPreferenceInteger("debugger.disasm.posx",            0);
    config.RegisterPreferenceInteger("debugger.disasm.posy",            0);
    config.RegisterPreferenceInteger("debugger.disasm.sizex",           0);
    config.RegisterPreferenceInteger("debugger.disasm.sizey",           0);
    config.RegisterPreferenceBoolean("debugger.disasm.visible",         true);

    config.RegisterPreferenceInteger("debugger.regview.posx",           0);
    config.RegisterPreferenceInteger("debugger.regview.posy",           0);
    config.RegisterPreferenceInteger("debugger.regview.sizex",          0);
    config.RegisterPreferenceInteger("debugger.regview.sizey",          0);
    config.RegisterPreferenceBoolean("debugger.regview.visible",        true);

    config.RegisterPreferenceInteger("debugger.memoryview.posx",        0);
    config.RegisterPreferenceInteger("debugger.memoryview.posy",        0);
    config.RegisterPreferenceInteger("debugger.memoryview.sizex",       0);
    config.RegisterPreferenceInteger("debugger.memoryview.sizey",       0);
    config.RegisterPreferenceBoolean("debugger.memoryview.visible",     true);

    config.RegisterPreferenceInteger("debugger.callstack.posx",         0);
    config.RegisterPreferenceInteger("debugger.callstack.posy",         0);
    config.RegisterPreferenceInteger("debugger.callstack.sizex",        0);
    config.RegisterPreferenceInteger("debugger.callstack.sizey",        0);
    config.RegisterPreferenceBoolean("debugger.callstack.visible",      true);
}

void CDebugger::UpdateLoggingMenu()
{
	HMENU hMenu;
	MENUITEMINFO mii;
	bool nState[7];

	hMenu = GetMenu(m_hWnd);

	hMenu = GetSubMenu(hMenu, 2);

//	nState[0] = m_virtualMachine.m_Logging.GetGSLoggingStatus();
//	nState[1] = m_virtualMachine.m_Logging.GetDMACLoggingStatus();
//	nState[2] = m_virtualMachine.m_Logging.GetIPULoggingStatus();
//	nState[3] = m_virtualMachine.m_Logging.GetOSLoggingStatus();
//	nState[4] = m_virtualMachine.m_Logging.GetOSRecordingStatus();
//	nState[5] = m_virtualMachine.m_Logging.GetSIFLoggingStatus();
//	nState[6] = m_virtualMachine.m_Logging.GetIOPLoggingStatus();
    memset(nState, 0, sizeof(nState));

	for(unsigned int i = 0; i < 7; i++)
	{
		memset(&mii, 0, sizeof(MENUITEMINFO));
		mii.cbSize		= sizeof(MENUITEMINFO);
		mii.fMask		= MIIM_STATE;
		mii.fState		= nState[i] ? MFS_CHECKED : 0;

		SetMenuItemInfo(hMenu, i, TRUE, &mii);
	}
}

void CDebugger::UpdateTitle()
{
	tstring sTitle(tcond("Purei! - Debugger", L"プレイ! - Debugger"));

	if(GetCurrentView() != NULL)
	{
		sTitle += 
			_T(" - [ ") + 
			string_cast<tstring>(GetCurrentView()->GetName()) +
			_T(" ]");
	}

	SetText(sTitle.c_str());
}

void CDebugger::LoadSettings()
{
	LoadViewLayout();
}

void CDebugger::SaveSettings()
{
	SaveViewLayout();
}

void CDebugger::SerializeWindowGeometry(CWindow* pWindow, const char* sPosX, const char* sPosY, const char* sSizeX, const char* sSizeY, const char* sVisible)
{
	RECT rc;
    CAppConfig& config(CAppConfig::GetInstance());

	pWindow->GetWindowRect(&rc);
	ScreenToClient(m_pMDIClient->m_hWnd, (POINT*)&rc + 0);
	ScreenToClient(m_pMDIClient->m_hWnd, (POINT*)&rc + 1);

	config.SetPreferenceInteger(sPosX, rc.left);
	config.SetPreferenceInteger(sPosY, rc.top);

	if(sSizeX != NULL && sSizeY != NULL)
	{
		config.SetPreferenceInteger(sSizeX, (rc.right - rc.left));
		config.SetPreferenceInteger(sSizeY, (rc.bottom - rc.top));
	}

	config.SetPreferenceBoolean(sVisible, pWindow->IsVisible());
}

void CDebugger::UnserializeWindowGeometry(CWindow* pWindow, const char* sPosX, const char* sPosY, const char* sSizeX, const char* sSizeY, const char* sVisible)
{
    CAppConfig& config(CAppConfig::GetInstance());

	pWindow->SetPosition(config.GetPreferenceInteger(sPosX), config.GetPreferenceInteger(sPosY));
	pWindow->SetSize(config.GetPreferenceInteger(sSizeX), config.GetPreferenceInteger(sSizeY));

	if(!config.GetPreferenceBoolean(sVisible))
	{
		pWindow->Show(SW_HIDE);
	}
	else
	{
		pWindow->Show(SW_SHOW);
	}
}

void CDebugger::Resume()
{
	m_virtualMachine.Resume();
}

void CDebugger::StepCPU1()
{
    if(m_virtualMachine.GetStatus() == CVirtualMachine::RUNNING)
	{
		MessageBeep(-1);
		return;
	}
	
	if(::GetParent(GetFocus()) != GetDisassemblyWindow()->m_hWnd)
	{
		GetDisassemblyWindow()->SetFocus();
	}

    GetCurrentView()->Step();
//    m_virtualMachine.Step();
//	GetContext()->Step();
//	m_virtualMachine.m_OnMachineStateChange();
}

void CDebugger::FindValue()
{
	uint32 nValue;
	const TCHAR* sValue;
	unsigned int i;
	Win32::CInputBox Input(_T("Find Value in Memory"), _T("Enter value to find:"), _T("00000000"));
	
	sValue = Input.GetValue(m_hWnd);
	if(sValue == NULL) return;

	_stscanf(sValue, _T("%x"), &nValue);
	if(nValue == 0) return;

	printf("Search results for 0x%0.8X\r\n", nValue);
	printf("-----------------------------\r\n");
	for(i = 0; i < PS2::EERAMSIZE; i += 4)
	{
		if(*(uint32*)&m_virtualMachine.m_pRAM[i] == nValue)
		{
			printf("0x%0.8X\r\n", i);
		}
	}
}

void CDebugger::AssembleJAL()
{
	uint32 nValueTarget, nValueAssemble;
	const TCHAR* sTarget;
	const TCHAR* sAssemble;

	Win32::CInputBox InputTarget(_T("Assemble JAL"), _T("Enter jump target:"), _T("00000000"));
	Win32::CInputBox InputAssemble(_T("Assemble JAL"), _T("Enter address to assemble JAL to:"), _T("00000000"));

	sTarget = InputTarget.GetValue(m_hWnd);
	if(sTarget == NULL) return;

	sAssemble = InputAssemble.GetValue(m_hWnd);
	if(sAssemble == NULL) return;

	_stscanf(sTarget, _T("%x"), &nValueTarget);
	_stscanf(sAssemble, _T("%x"), &nValueAssemble);

	*(uint32*)&m_virtualMachine.m_pRAM[nValueAssemble] = 0x0C000000 | (nValueTarget / 4);
}

void CDebugger::Layout1024()
{
	GetDisassemblyWindow()->SetPosition(0, 0);
	GetDisassemblyWindow()->SetSize(700, 435);
	GetDisassemblyWindow()->Show(SW_SHOW);

	GetRegisterViewWindow()->SetPosition(700, 0);
	GetRegisterViewWindow()->SetSize(324, 572);
	GetRegisterViewWindow()->Show(SW_SHOW);

	GetMemoryViewWindow()->SetPosition(0, 435);
	GetMemoryViewWindow()->SetSize(700, 265);
	GetMemoryViewWindow()->Show(SW_SHOW);

	GetCallStackWindow()->SetPosition(700, 572);
	GetCallStackWindow()->SetSize(324, 128);
	GetCallStackWindow()->Show(SW_SHOW);
}

void CDebugger::Layout1280()
{
	GetDisassemblyWindow()->SetPosition(0, 0);
	GetDisassemblyWindow()->SetSize(900, 540);
	GetDisassemblyWindow()->Show(SW_SHOW);

	GetRegisterViewWindow()->SetPosition(900, 0);
	GetRegisterViewWindow()->SetSize(380, 784);
	GetRegisterViewWindow()->Show(SW_SHOW);

	GetMemoryViewWindow()->SetPosition(0, 540);
	GetMemoryViewWindow()->SetSize(900, 416);
	GetMemoryViewWindow()->Show(SW_SHOW);

	GetCallStackWindow()->SetPosition(900, 784);
	GetCallStackWindow()->SetSize(380, 172);
	GetCallStackWindow()->Show(SW_SHOW);
}

void CDebugger::Layout1600()
{
	GetDisassemblyWindow()->SetPosition(0, 0);
	GetDisassemblyWindow()->SetSize(1094, 725);
	GetDisassemblyWindow()->Show(SW_SHOW);

    GetRegisterViewWindow()->SetPosition(1094, 0);
	GetRegisterViewWindow()->SetSize(506, 725);
	GetRegisterViewWindow()->Show(SW_SHOW);

	GetMemoryViewWindow()->SetPosition(0, 725);
	GetMemoryViewWindow()->SetSize(1094, 407);
	GetMemoryViewWindow()->Show(SW_SHOW);

	GetCallStackWindow()->SetPosition(1094, 725);
	GetCallStackWindow()->SetSize(506, 407);
	GetCallStackWindow()->Show(SW_SHOW);
}

void CDebugger::InitializeConsole()
{
	AllocConsole();

	CONSOLE_SCREEN_BUFFER_INFO ScreenBufferInfo;

	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ScreenBufferInfo);
	ScreenBufferInfo.dwSize.Y = 1000;
	SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), ScreenBufferInfo.dwSize);

	(*stdout) = *_fdopen(_open_osfhandle(
		reinterpret_cast<intptr_t>(GetStdHandle(STD_OUTPUT_HANDLE)),
		_O_TEXT), "w");

	setvbuf(stdout, NULL, _IONBF, 0);
	ios::sync_with_stdio();	
}

void CDebugger::ActivateView(unsigned int nView)
{
	if(m_nCurrentView == nView) return;

	if(m_nCurrentView != -1)
	{
		SaveViewLayout();
		GetCurrentView()->Hide();
	}

	m_nCurrentView = nView;
	LoadViewLayout();
	UpdateTitle();

	if(GetDisassemblyWindow()->IsVisible())
	{
		GetDisassemblyWindow()->SetFocus();
	}
}

void CDebugger::SaveViewLayout()
{
	SerializeWindowGeometry(GetDisassemblyWindow(), \
		"debugger.disasm.posx", \
		"debugger.disasm.posy", \
		"debugger.disasm.sizex", \
		"debugger.disasm.sizey", \
		"debugger.disasm.visible");

	SerializeWindowGeometry(GetRegisterViewWindow(), \
		"debugger.regview.posx", \
		"debugger.regview.posy", \
		"debugger.regview.sizex", \
		"debugger.regview.sizey", \
		"debugger.regview.visible");

	SerializeWindowGeometry(GetMemoryViewWindow(), \
		"debugger.memoryview.posx", \
		"debugger.memoryview.posy", \
		"debugger.memoryview.sizex", \
		"debugger.memoryview.sizey", \
		"debugger.memoryview.visible");

	SerializeWindowGeometry(GetCallStackWindow(), \
		"debugger.callstack.posx", \
		"debugger.callstack.posy", \
		"debugger.callstack.sizex", \
		"debugger.callstack.sizey", \
		"debugger.callstack.visible");
}

void CDebugger::LoadViewLayout()
{
	UnserializeWindowGeometry(GetDisassemblyWindow(), \
		"debugger.disasm.posx", \
		"debugger.disasm.posy", \
		"debugger.disasm.sizex", \
		"debugger.disasm.sizey", \
		"debugger.disasm.visible");

	UnserializeWindowGeometry(GetRegisterViewWindow(), \
		"debugger.regview.posx", \
		"debugger.regview.posy", \
		"debugger.regview.sizex", \
		"debugger.regview.sizey", \
		"debugger.regview.visible");

	UnserializeWindowGeometry(GetMemoryViewWindow(), \
		"debugger.memoryview.posx", \
		"debugger.memoryview.posy", \
		"debugger.memoryview.sizex", \
		"debugger.memoryview.sizey", \
		"debugger.memoryview.visible");

	UnserializeWindowGeometry(GetCallStackWindow(), \
		"debugger.callstack.posx", \
		"debugger.callstack.posy", \
		"debugger.callstack.sizex", \
		"debugger.callstack.sizey", \
		"debugger.callstack.visible");
}

CDebugView* CDebugger::GetCurrentView()
{
	if(m_nCurrentView == -1) return NULL;
	return m_pView[m_nCurrentView];
}

CMIPS* CDebugger::GetContext()
{
	return GetCurrentView()->GetContext();
}

CDisAsmWnd* CDebugger::GetDisassemblyWindow()
{
	return GetCurrentView()->GetDisassemblyWindow();
}

CMemoryViewMIPSWnd* CDebugger::GetMemoryViewWindow()
{
	return GetCurrentView()->GetMemoryViewWindow();
}

CRegViewWnd* CDebugger::GetRegisterViewWindow()
{
	return GetCurrentView()->GetRegisterViewWindow();
}

CCallStackWnd* CDebugger::GetCallStackWindow()
{
	return GetCurrentView()->GetCallStackWindow();
}

void CDebugger::CreateAccelerators()
{
    Win32::CAcceleratorTableGenerator generator;
    generator.Insert(ID_VM_SAVESTATE,           VK_F7,  FVIRTKEY);
    generator.Insert(ID_VM_LOADSTATE,           VK_F8,  FVIRTKEY);
    generator.Insert(ID_VIEW_FUNCTIONS,         'F',    FCONTROL | FVIRTKEY);
    generator.Insert(ID_VM_STEP1,               VK_F10, FVIRTKEY);
    generator.Insert(ID_VM_RESUME,              VK_F5,  FVIRTKEY);
    generator.Insert(ID_VIEW_CALLSTACK,         'A',    FCONTROL | FVIRTKEY);
    generator.Insert(ID_VIEW_EEVIEW,            '1',    FALT | FVIRTKEY);
    generator.Insert(ID_VIEW_VU0VIEW,           '2',    FALT | FVIRTKEY);
    generator.Insert(ID_VIEW_VU1VIEW,           '3',    FALT | FVIRTKEY);
    generator.Insert(ID_VIEW_IOPVIEW,           '4',    FALT | FVIRTKEY);
    generator.Insert(ID_EDIT_COPY,              'C',    FCONTROL | FVIRTKEY);
    generator.Insert(ID_VIEW_TESTENGINECONSOLE, 'T',    FCONTROL | FVIRTKEY);
    m_nAccTable = generator.Create();
}

void CDebugger::DestroyAccelerators()
{
	DestroyAcceleratorTable(m_nAccTable);
}

long CDebugger::OnCommand(unsigned short nID, unsigned short nMsg, HWND hFrom)
{
	switch(nID)
	{
	case ID_VM_STEP1:
		StepCPU1();
		break;
	case ID_VM_RESUME:
		Resume();
		break;
	case ID_VM_SAVESTATE:
		m_virtualMachine.SaveState("./config/state.sta");
		break;
	case ID_VM_LOADSTATE:
		m_virtualMachine.LoadState("./config/state.sta");
		break;
	case ID_VM_DUMPTHREADS:
		m_virtualMachine.DumpEEThreadSchedule();
		break;
	case ID_VM_DUMPINTCHANDLERS:
		m_virtualMachine.DumpEEIntcHandlers();
		break;
	case ID_VM_DUMPDMACHANDLERS:
		m_virtualMachine.DumpEEDmacHandlers();
		break;
	case ID_VM_ASMJAL:
		AssembleJAL();
		break;
	case ID_VM_TEST_SHIFT:
		StartShiftOpTest();
		break;
	case ID_VM_TEST_SHIFT64:
		StartShift64OpTest();
		break;
	case ID_VM_TEST_SPLITLOAD:
		StartSplitLoadOpTest();
		break;
	case ID_VM_TEST_ADD64:
		StartAddition64OpTest();
		break;
	case ID_VM_TEST_SLT:
		StartSetLessThanOpTest();
		break;
	case ID_VM_FINDVALUE:
		FindValue();
		break;
	case ID_VIEW_MEMORY:
		GetMemoryViewWindow()->Show(SW_SHOW);
		GetMemoryViewWindow()->SetFocus();
		return FALSE;
		break;
	case ID_VIEW_CALLSTACK:
		GetCallStackWindow()->Show(SW_SHOW);
		GetCallStackWindow()->SetFocus();
		return FALSE;
		break;
	case ID_VIEW_FUNCTIONS:
		m_pFunctionsView->Show(SW_SHOW);
		m_pFunctionsView->SetFocus();
		return FALSE;
		break;
	case ID_VIEW_ELF:
		m_pELFView->Show(SW_SHOW);
		m_pELFView->SetFocus();
		return FALSE;
		break;
	case ID_VIEW_OSEVENTS:
		m_pOsEventView->Show(SW_SHOW);
		m_pOsEventView->SetFocus();
		return FALSE;
		break;
    case ID_VIEW_TESTENGINECONSOLE:
        m_pTestEngineView->Show(SW_SHOW);
        m_pTestEngineView->SetFocus();
        return FALSE;
        break;
	case ID_VIEW_DISASSEMBLY:
		GetDisassemblyWindow()->Show(SW_SHOW);
		GetDisassemblyWindow()->SetFocus();
		return FALSE;
		break;
	case ID_VIEW_EEVIEW:
		ActivateView(DEBUGVIEW_EE);
		break;
    case ID_VIEW_VU0VIEW:
        ActivateView(DEBUGVIEW_VU0);
        break;
	case ID_VIEW_VU1VIEW:
		ActivateView(DEBUGVIEW_VU1);
		break;
    case ID_VIEW_IOPVIEW:
        ActivateView(DEBUGVIEW_IOP);
        break;
	case ID_LOGGING_GS:
//		m_virtualMachine.m_Logging.SetGSLoggingStatus(!m_virtualMachine.m_Logging.GetGSLoggingStatus());
		UpdateLoggingMenu();
		break;
	case ID_LOGGING_DMAC:
//		m_virtualMachine.m_Logging.SetDMACLoggingStatus(!m_virtualMachine.m_Logging.GetDMACLoggingStatus());
		UpdateLoggingMenu();
		break;
	case ID_LOGGING_IPU:
//		m_virtualMachine.m_Logging.SetIPULoggingStatus(!m_virtualMachine.m_Logging.GetIPULoggingStatus());
		UpdateLoggingMenu();
		break;
	case ID_LOGGING_OS:
//		m_virtualMachine.m_Logging.SetOSLoggingStatus(!m_virtualMachine.m_Logging.GetOSLoggingStatus());
		UpdateLoggingMenu();
		break;
	case ID_RECORDING_OS:
//		m_virtualMachine.m_Logging.SetOSRecordingStatus(!m_virtualMachine.m_Logging.GetOSRecordingStatus());
		UpdateLoggingMenu();
		break;
	case ID_LOGGING_SIF:
//		m_virtualMachine.m_Logging.SetSIFLoggingStatus(!m_virtualMachine.m_Logging.GetSIFLoggingStatus());
		UpdateLoggingMenu();
		break;
	case ID_LOGGING_IOP:
//		m_virtualMachine.m_Logging.SetIOPLoggingStatus(!m_virtualMachine.m_Logging.GetIOPLoggingStatus());
		UpdateLoggingMenu();
		break;
	case ID_WINDOW_CASCAD:
		m_pMDIClient->Cascade();
		return FALSE;
		break;
	case ID_WINDOW_TILEHORIZONTAL:
		m_pMDIClient->TileHorizontal();
		return FALSE;
		break;
	case ID_WINDOW_TILEVERTICAL:
		m_pMDIClient->TileVertical();
		return FALSE;
		break;
	case ID_WINDOW_LAYOUT1024:
		Layout1024();
		return FALSE;
		break;
	case ID_WINDOW_LAYOUT1280:
		Layout1280();
		return FALSE;
		break;
    case ID_WINDOW_LAYOUT1600:
        Layout1600();
        return FALSE;
        break;
    case ID_EDIT_COPY:
        SendMessage(m_pMDIClient->GetActiveWindow(), WM_COPY, 0, 0);
        break;
	}
	return TRUE;
}

long CDebugger::OnSysCommand(unsigned int nCmd, LPARAM lParam)
{
	switch(nCmd)
	{
	case SC_CLOSE:
		Show(SW_HIDE);
		return FALSE;
	}
	return TRUE;
}

long CDebugger::OnWndProc(unsigned int nMsg, WPARAM wParam, LPARAM lParam)
{
	switch(nMsg)
	{
	case WM_EXECUNLOAD:
		OnExecutableUnloadingMsg();
		return FALSE;
		break;
	case WM_EXECCHANGE:
		OnExecutableChangeMsg();
		return FALSE;
		break;
	}
	return CMDIFrame::OnWndProc(nMsg, wParam, lParam);
}

void CDebugger::OnFunctionViewFunctionDblClick(uint32 nAddress)
{
	GetDisassemblyWindow()->SetAddress(nAddress);
	//GetDisassemblyWindow()->SetFocus();
}

void CDebugger::OnFunctionViewFunctionsStateChange()
{
	GetDisassemblyWindow()->Refresh();
}

void CDebugger::OnTestEngineViewTestCaseLoad(uint32 nAddress)
{
    GetDisassemblyWindow()->SetAddress(nAddress);
}

void CDebugger::OnEventViewEventDblClick(uint32 nAddress)
{
	GetDisassemblyWindow()->SetAddress(nAddress);
}

void CDebugger::OnExecutableChange()
{
	SendMessage(m_hWnd, WM_EXECCHANGE, 0, 0);
}

void CDebugger::OnExecutableUnloading()
{
	SendMessage(m_hWnd, WM_EXECUNLOAD, 0, 0);
}

void CDebugger::OnExecutableChangeMsg()
{
	m_pELFView->SetELF(m_virtualMachine.m_os->GetELF());
	m_pFunctionsView->SetELF(m_virtualMachine.m_os->GetELF());

    LoadDebugTags();

	GetDisassemblyWindow()->Refresh();
	m_pFunctionsView->Refresh();
}

void CDebugger::OnExecutableUnloadingMsg()
{
    SaveDebugTags();
	m_pELFView->SetELF(NULL);
	m_pFunctionsView->SetELF(NULL);
}

void CDebugger::LoadDebugTags()
{
    m_virtualMachine.LoadDebugTags(m_virtualMachine.m_os->GetExecutableName());
}

void CDebugger::SaveDebugTags()
{
    if(m_virtualMachine.m_os->GetELF() != NULL)
    {
        m_virtualMachine.SaveDebugTags(m_virtualMachine.m_os->GetExecutableName());
    }
}

void CDebugger::StartShiftOpTest()
{
	const int nBaseAddress = 0x00100000;

	CMIPSAssembler Assembler((uint32*)&m_virtualMachine.m_pRAM[nBaseAddress]);

	Assembler.LUI(CMIPS::T0, 0x8080);
	Assembler.ORI(CMIPS::T0, CMIPS::T0, 0x80FF);

	//SLLV
	for(unsigned int i = 0; i <= 32; i += 4)
	{
		Assembler.ADDIU(CMIPS::T1, CMIPS::R0, i);
		Assembler.SLLV(CMIPS::T2, CMIPS::T0, CMIPS::T1);
	}

	//SRAV
	for(unsigned int i = 0; i <= 32; i += 4)
	{
		Assembler.ADDIU(CMIPS::T1, CMIPS::R0, i);
		Assembler.SRAV(CMIPS::T2, CMIPS::T0, CMIPS::T1);
	}

	//SRLV
	for(unsigned int i = 0; i <= 32; i += 4)
	{
		Assembler.ADDIU(CMIPS::T1, CMIPS::R0, i);
		Assembler.SRLV(CMIPS::T2, CMIPS::T0, CMIPS::T1);
	}

	//SLL
	for(unsigned int i = 0; i <= 32; i += 4)
	{
		Assembler.SLL(CMIPS::T2, CMIPS::T0, i);
	}
	
	//SRA
	for(unsigned int i = 0; i <= 32; i += 4)
	{
		Assembler.SRA(CMIPS::T2, CMIPS::T0, i);
	}

	//SRL
	for(unsigned int i = 0; i <= 32; i += 4)
	{
		Assembler.SRL(CMIPS::T2, CMIPS::T0, i);
	}

	m_virtualMachine.m_EE.m_State.nPC = nBaseAddress;
}

void CDebugger::StartShift64OpTest()
{
	const int nBaseAddress = 0x00100000;

	CMIPSAssembler Assembler((uint32*)&m_virtualMachine.m_pRAM[nBaseAddress]);

	Assembler.LUI(CMIPS::T0, 0x8080);
	Assembler.ADDIU(CMIPS::T0, CMIPS::T0, 0xFFFF);
	Assembler.DSLL32(CMIPS::T0, CMIPS::T0, 0);
	Assembler.ORI(CMIPS::T0, CMIPS::T0, 0x80FF);

	//DSLLV
	for(unsigned int i = 0; i <= 64; i += 4)
	{
		Assembler.ADDIU(CMIPS::T1, CMIPS::R0, i);
		Assembler.DSLLV(CMIPS::T2, CMIPS::T0, CMIPS::T1);
	}

	//DSRAV
	//for(unsigned int i = 0; i <= 64; i += 4)
	//{
	//	Assembler.ADDIU(CMIPS::T1, CMIPS::R0, i);
	//	Assembler.DSRAV(CMIPS::T2, CMIPS::T0, CMIPS::T1);
	//}

	//DSRLV
	for(unsigned int i = 0; i <= 64; i += 4)
	{
		Assembler.ADDIU(CMIPS::T1, CMIPS::R0, i);
		Assembler.DSRLV(CMIPS::T2, CMIPS::T0, CMIPS::T1);
	}

	//DSRA32
	for(unsigned int i = 0; i <= 32; i += 4)
	{
		Assembler.DSRA32(CMIPS::T2, CMIPS::T0, i);
	}

	//DSRL32
	for(unsigned int i = 0; i <= 32; i += 4)
	{
		Assembler.DSRL32(CMIPS::T2, CMIPS::T0, i);
	}

	//DSLL32
	for(unsigned int i = 0; i <= 32; i += 4)
	{
		Assembler.DSLL32(CMIPS::T2, CMIPS::T0, i);
	}

	//DSLL
	for(unsigned int i = 0; i <= 32; i += 4)
	{
		Assembler.DSLL(CMIPS::T2, CMIPS::T0, i);
	}
	
	//DSRA
	for(unsigned int i = 0; i <= 32; i += 4)
	{
		Assembler.DSRA(CMIPS::T2, CMIPS::T0, i);
	}

	//DSRL
	for(unsigned int i = 0; i <= 32; i += 4)
	{
		Assembler.DSRL(CMIPS::T2, CMIPS::T0, i);
	}

	m_virtualMachine.m_EE.m_State.nPC = nBaseAddress;
}

void CDebugger::StartSplitLoadOpTest()
{
	const int nBaseAddress = 0x00100000;

	CMIPSAssembler Assembler((uint32*)&m_virtualMachine.m_pRAM[nBaseAddress]);

	Assembler.LUI(CMIPS::T0, 0x0302);
	Assembler.ORI(CMIPS::T0, CMIPS::T0, 0x0100);
	Assembler.SW(CMIPS::T0, 0x00, CMIPS::R0);

	Assembler.LUI(CMIPS::T0, 0x0706);
	Assembler.ORI(CMIPS::T0, CMIPS::T0, 0x0504);
	Assembler.SW(CMIPS::T0, 0x04, CMIPS::R0);

	Assembler.LUI(CMIPS::T0, 0x0B0A);
	Assembler.ORI(CMIPS::T0, CMIPS::T0, 0x0908);
	Assembler.SW(CMIPS::T0, 0x08, CMIPS::R0);

	Assembler.LUI(CMIPS::T0, 0x0F0E);
	Assembler.ORI(CMIPS::T0, CMIPS::T0, 0x0D0C);
	Assembler.SW(CMIPS::T0, 0x0C, CMIPS::R0);

	for(unsigned int i = 0; i < 4; i++)
	{
		Assembler.ADDIU(CMIPS::T0, CMIPS::R0, i);
		Assembler.LWL(CMIPS::T1, 3, CMIPS::T0);
		Assembler.LWR(CMIPS::T1, 0, CMIPS::T0);
	}

	for(unsigned int i = 0; i < 8; i++)
	{
		Assembler.ADDIU(CMIPS::T0, CMIPS::R0, i);
		Assembler.LDL(CMIPS::T1, 0x7, CMIPS::T0);
		Assembler.LDR(CMIPS::T1, 0x0, CMIPS::T0);
	}

	m_virtualMachine.m_EE.m_State.nPC = nBaseAddress;
}

void CDebugger::StartAddition64OpTest()
{
	const int nBaseAddress = 0x00100000;

	CMIPSAssembler Assembler((uint32*)&m_virtualMachine.m_pRAM[nBaseAddress]);

	Assembler.DADDU(CMIPS::T0, CMIPS::R0, CMIPS::R0);
	Assembler.ORI(CMIPS::T0, CMIPS::T0, 0xFFFF);
	Assembler.DSLL(CMIPS::T0, CMIPS::T0, 16);
	Assembler.ORI(CMIPS::T0, CMIPS::T0, 0xFFF0);
	Assembler.DSLL(CMIPS::T0, CMIPS::T0, 16);
	Assembler.ORI(CMIPS::T0, CMIPS::T0, 0xFFFF);
	Assembler.DSLL(CMIPS::T0, CMIPS::T0, 16);
	Assembler.ORI(CMIPS::T0, CMIPS::T0, 0xFFFF);

	Assembler.DADDIU(CMIPS::T1, CMIPS::T0, 1);
	Assembler.ADDIU(CMIPS::T2, CMIPS::R0, 1);
	Assembler.DADDU(CMIPS::T0, CMIPS::T2, CMIPS::T0);

	Assembler.DSUBU(CMIPS::T0, CMIPS::T0, CMIPS::T2);
	Assembler.DADDIU(CMIPS::T1, CMIPS::T1, 0xFFFF);

	Assembler.DADDU(CMIPS::T0, CMIPS::R0, CMIPS::R0);
	Assembler.ORI(CMIPS::T0, CMIPS::T0, 0x8FFF);
	Assembler.DSLL(CMIPS::T0, CMIPS::T0, 16);
	Assembler.ORI(CMIPS::T0, CMIPS::T0, 0xFFFF);
	Assembler.DSLL32(CMIPS::T0, CMIPS::T0, 0);
//	Assembler.ORI(CMIPS::T0, CMIPS::T0, 0x0000);
//	Assembler.DSLL(CMIPS::T0, CMIPS::T0, 16);
//	Assembler.ORI(CMIPS::T0, CMIPS::T0, 0x0000);

	//Assembler.BLTZ(CMIPS::T0, 0xFFFF);
	//Assembler.BGEZ(CMIPS::T0, 0xFFFF);
	Assembler.BLEZ(CMIPS::T0, 0xFFFF);

	m_virtualMachine.m_EE.m_State.nPC = nBaseAddress;
}

void CDebugger::StartSetLessThanOpTest()
{
	const int nBaseAddress = 0x00100000;

	CMIPSAssembler Assembler((uint32*)&m_virtualMachine.m_pRAM[nBaseAddress]);

	Assembler.LUI(CMIPS::K0, 0xFFFF);
	Assembler.DSLL(CMIPS::K1, CMIPS::K0, 16);
	Assembler.DSRL32(CMIPS::GP, CMIPS::K0, 16);
	Assembler.DADDIU(CMIPS::V0, CMIPS::R0, CMIPS::R0);

	Assembler.SLTU(CMIPS::T0, CMIPS::V0, CMIPS::K0);
	Assembler.SLTU(CMIPS::T0, CMIPS::V0, CMIPS::K1);
	Assembler.SLTU(CMIPS::T0, CMIPS::V0, CMIPS::GP);
	Assembler.SLTU(CMIPS::T0, CMIPS::K0, CMIPS::GP);

	m_virtualMachine.m_EE.m_State.nPC = nBaseAddress;
}

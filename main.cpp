#ifndef _WIN32_IE
#define _WIN32_IE 0x0400    //for commctrl, datetime
#endif
#include <windows.h>
#include <Powrprof.h>
#include <commctrl.h>
#include <string.h>
#include <assert.h>
#include <algorithm>	// needed by Utils::in_group
#include "Utils.h"
#include <string>
#include <fstream>
#include <json/json.h>
#include "main.h"
#include "shutdown.h"
#include "resource.h"

#define _EXPORTING
#include "..\tSIP\tSIP\phone\Phone.h"
#include "..\tSIP\tSIP\phone\PhoneSettings.h"
#include "..\tSIP\tSIP\phone\PhoneCapabilities.h"


HANDLE Thread;
HANDLE SessionMutex;

HWND hwndDP = NULL;
SYSTEMTIME stShutdown;      ///< requested time of shutdown
bool bDeployed = false;     ///< are you ready to shutdown?
bool bVisible = false;
HANDLE hwndGlobalDialog = NULL;
const char *myname = "System Shutdown";
const char *errorCaption = "SystemShutdown plugin";
char *inifile = NULL;

HINSTANCE hInst;

SYSTEM_POWER_CAPABILITIES powerCapabilities;

static const struct S_PHONE_DLL_INTERFACE dll_interface =
{DLL_INTERFACE_MAJOR_VERSION, DLL_INTERFACE_MINOR_VERSION};

// callback ptrs
CALLBACK_LOG lpLogFn = NULL;
CALLBACK_CONNECT lpConnectFn = NULL;
CALLBACK_KEY lpKeyFn = NULL;
CALLBACK_ADD_TRAY_MENU_ITEM lpAddTrayMenuItemFn = NULL;

void *callbackCookie;	///< used by upper class to distinguish library instances when receiving callbacks

enum ACTION
{
    ACTION_NONE = 0,
    ACTION_S3,
    ACTION_S4,
    ACTION_SHUTDOWN
} action = ACTION_NONE;

BOOL CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    INITCOMMONCONTROLSEX icex;
    HWND hCtl;
    LRESULT lResult;
    //char lpDateTime[64];
    char tmpbuf[10];
    hwndGlobalDialog = hwndDlg;
    switch (uMsg)
    {
    case WM_INITDIALOG:
        icex.dwSize = sizeof(icex);
        icex.dwICC = ICC_DATE_CLASSES;

        InitCommonControlsEx(&icex);
        if (hwndDlg)
        {
            hwndDP = CreateWindowEx(0,
                                    DATETIMEPICK_CLASS,
                                    TEXT("DateTime"),
                                    WS_BORDER|WS_CHILD|WS_VISIBLE|DTS_UPDOWN|DTS_TIMEFORMAT,
                                    110, 5, 110, 25,
                                    hwndDlg,
                                    NULL,
                                    NULL,
                                    NULL);
            ::SendMessage(hwndDP, DTM_SETFORMAT, 0, (LPARAM)"[MM.dd]   HH:mm:ss");
        }

        if (bDeployed)
        {
            ::SendMessage(hwndDP, DTM_SETSYSTEMTIME, 0, (LPARAM)&stShutdown);
        }

        // disable S3, S4 options
        hCtl = GetDlgItem(hwndDlg, IDC_S3);
        EnableWindow(hCtl, FALSE);
        hCtl = GetDlgItem(hwndDlg, IDC_S4);
        EnableWindow(hCtl, FALSE);
        hCtl = GetDlgItem(hwndDlg, IDC_SHUTDOWN);
        EnableWindow(hCtl, FALSE);
        if (GetPwrCapabilities(&powerCapabilities))
        {
            if (powerCapabilities.SystemS3)
            {
                hCtl = GetDlgItem(hwndDlg, IDC_S3);
                EnableWindow(hCtl, TRUE);
            }
            if (powerCapabilities.SystemS4 && powerCapabilities.HiberFilePresent)
            {
                hCtl = GetDlgItem(hwndDlg, IDC_S4);
                EnableWindow(hCtl, TRUE);
            }
            if (powerCapabilities.SystemS5)
            {
                hCtl = GetDlgItem(hwndDlg, IDC_SHUTDOWN);
                EnableWindow(hCtl, TRUE);
            }
        }
        else
            DisplayError( GetLastError(), errorCaption, "Error fetching system power capabilities.\n");

        if (action == ACTION_S3)
            SendMessage(GetDlgItem(hwndDlg, IDC_S3), BM_SETCHECK, 1, 0);
        else if (action == ACTION_S4)
            SendMessage(GetDlgItem(hwndDlg, IDC_S3), BM_SETCHECK, 1, 0);
        else
            SendMessage(GetDlgItem(hwndDlg, IDC_SHUTDOWN), BM_SETCHECK, 1, 0);

        return TRUE;

    case WM_CLOSE:
        bVisible = false;
        hwndGlobalDialog = NULL;
        EndDialog(hwndDlg, 0);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
            /*
             * TODO: Add more control ID's, when needed.
             */
        case IDC_BTN_ABORT:
            bDeployed = false;
            AbortSystemShutdown(NULL);
            bVisible = false;
            hwndGlobalDialog = NULL;
            EndDialog(hwndDlg, 0);
            return TRUE;

        case IDC_BTN_OK:
            // which radio button is checked?
            lResult = SendMessage(GetDlgItem(hwndDlg, IDC_S3), BM_GETCHECK, 0, 0);
            if (lResult == BST_CHECKED)
            {
                ::SendMessage(hwndDP, DTM_GETSYSTEMTIME, 0, (LPARAM)&stShutdown);
                bDeployed = true;
                action = ACTION_S3;
                if (inifile)
                {
                    itoa(action, tmpbuf, 10);
                    WritePrivateProfileString("Action", "Type", tmpbuf, inifile);
                }
                bVisible = false;
                hwndGlobalDialog = NULL;
                EndDialog(hwndDlg, 0);
                return TRUE;
            }
            lResult = SendMessage(GetDlgItem(hwndDlg, IDC_S4), BM_GETCHECK, 0, 0);
            if (lResult == BST_CHECKED)
            {
                ::SendMessage(hwndDP, DTM_GETSYSTEMTIME, 0, (LPARAM)&stShutdown);
                bDeployed = true;
                action = ACTION_S4;
                if (inifile)
                {
                    itoa(action, tmpbuf, 10);
                    WritePrivateProfileString("Action", "Type", tmpbuf, inifile);
                }
                bVisible = false;
                hwndGlobalDialog = NULL;
                EndDialog(hwndDlg, 0);
                return TRUE;
            }
            lResult = SendMessage(GetDlgItem(hwndDlg, IDC_SHUTDOWN), BM_GETCHECK, 0, 0);
            if (lResult == BST_CHECKED)
            {
                if ( ! SetCurrentPrivilege( SE_SHUTDOWN_NAME, TRUE ) )
                    DisplayError( GetLastError(), errorCaption, "Insufficient privileges to shutdown system.\n");
                else
                {
                    ::SendMessage(hwndDP, DTM_GETSYSTEMTIME, 0, (LPARAM)&stShutdown);
                    bDeployed = true;
                    action = ACTION_SHUTDOWN;
                    if (inifile)
                    {
                        itoa(action, tmpbuf, 10);
                        WritePrivateProfileString("Action", "Type", tmpbuf, inifile);
                    }
                    bVisible = false;
                    hwndGlobalDialog = NULL;
                    EndDialog(hwndDlg, 0);
                }
                return TRUE;
            }

            MessageBox(NULL, "No option have been chosen", myname, MB_OK);
            return TRUE;
        }
    }

    return FALSE;
}


const __int64 nano100SecInSec=(__int64)10000000;
/*1. Diffence BETWEEN two datetime
////////////
//equivalent of DATEDIFF function from SQLServer
//Returns the number of date and time boundaries crossed
//between two specified dates.
////////////*/
double     /* return count of period between pst1 and pst2*/
DT_PeriodsBetween
(
    const __int64 datepart, /*datepart that we want to count, {nano100SecInDay ...}*/
    const SYSTEMTIME* pst1, /*valid datetime*/
    const SYSTEMTIME* pst2  /*valid datetime*/
)
{
    FILETIME ft1,ft2;
    __int64 *pi1,*pi2;

    /*convert SYSTEMTIME to FILETIME
    //SYSTEMTIME is only representation, so need convert to
    //FILETIME for make some calculation*/
    SystemTimeToFileTime (pst1,&ft1);
    SystemTimeToFileTime (pst2,&ft2);
    /*convert FILETIME to __int64
    //FILETIME is struct with two DWORD, for receive
    //ability calculation we must reference to FILETIME as to int*/
    pi1 = (__int64*)&ft1;
    pi2 = (__int64*)&ft2;
    /*compare two datetimes and (bigger date - smaller date)/period*/
    return (CompareFileTime(&ft1,&ft2)==1) ?
           (((*pi1)-(*pi2))/(double)datepart) : (((*pi2)-(*pi1))/(double)datepart);
}

DWORD WINAPI ThreadProc(LPVOID data)
{
    SYSTEMTIME st;
    DWORD rc;

    const int period = 3;
    while (1)
    {
        //OutputDebugStr("plugin System Shutdown is running");
        if (bDeployed)
        {
            GetLocalTime(&st);
            if (DT_PeriodsBetween(nano100SecInSec,&st,&stShutdown)<=period)
            {
                bDeployed = false;
                if (action == ACTION_S3)
                {
                    if (!SetSuspendState(FALSE, TRUE, FALSE))
                        DisplayError( GetLastError(), errorCaption, "Failed to change state to STANDBY (S3).\n" );
                }
                else if (action == ACTION_S4)
                {
                    if (!SetSuspendState(TRUE, TRUE, FALSE))
                        DisplayError( GetLastError(), errorCaption, "Failed to change state to HIBERNATE (S4).\n" );
                }
                else if (action == ACTION_SHUTDOWN)
                {
                    //ShellExecute(0, "open", "shutdown", "-s -t 30", NULL, 0);
                    rc = InitiateSystemShutdown( NULL,
                                                 "Shutting down initiated by SystemShutdown plugin... "
                                                 "Use \"Disarm\" from plugin to interrupt this. "
                                                 "Warning: plugin window may be shown under this window.",
                                                 (DWORD) 60, true, false );
                    if ( ! rc )
                        DisplayError( GetLastError(), errorCaption, "Shutdown failed\n" );
                }
                else
                    DisplayError( 0, errorCaption, "Unimplemented action.\n" );
            }
        }
        Sleep(period*1000);
    }
    return 0;
}

/** get handle to dll without knowing its name
*/
HMODULE GetCurrentModule()
{
    MEMORY_BASIC_INFORMATION mbi;
    static int dummy;
    VirtualQuery( &dummy, &mbi, sizeof(mbi) );
    return reinterpret_cast<HMODULE>(mbi.AllocationBase);
}

DWORD WINAPI ThreadDialog(LPVOID data)
{
    int ret = DialogBox(GetCurrentModule(),
                        MAKEINTRESOURCE(DLG_MAIN), NULL, DialogProc);
    if (ret </*=*/ 0)   ///< \todo poprawne wywolanie daje 0?
    {
        LPVOID lpMsgBuf;
        DWORD dw = GetLastError();

        FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            dw,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR) &lpMsgBuf,
            0, NULL );
        MessageBox(NULL, (LPCTSTR)lpMsgBuf, TEXT("Error"), MB_OK);
        LocalFree(lpMsgBuf);
        bVisible = false;
    }
    return 0;
}

/* extern "C" __declspec(dllexport) */ void GetPhoneInterfaceDescription(struct S_PHONE_DLL_INTERFACE* interf) {
    interf->majorVersion = dll_interface.majorVersion;
    interf->minorVersion = dll_interface.minorVersion;
}

void Log(const char* txt) {
    if (lpLogFn)
        lpLogFn(callbackCookie, const_cast<char*>(txt));
}

void SetCallbacks(void *cookie, CALLBACK_LOG lpLog, CALLBACK_CONNECT lpConnect, CALLBACK_KEY lpKey) {
    assert(cookie && lpLog && lpConnect && lpKey);
    lpLogFn = lpLog;
    lpConnectFn = lpConnect;
    lpKeyFn = lpKey;
    callbackCookie = cookie;

    Log("SystemShutdown plugin loaded\n");
}

void GetPhoneCapabilities(struct S_PHONE_CAPABILITIES **caps) {
    static struct S_PHONE_CAPABILITIES capabilities = {
        0
    };
    *caps = &capabilities;
}

void ShowSettings(HANDLE parent) {
    MessageBox((HWND)parent, "No additional settings.", "Device DLL", MB_ICONINFORMATION);
}

void __stdcall TrayMenuClick(void *menuItemClickCookie) {
    if (!bVisible)
    {
        DWORD dwtid;
        bVisible = true;
        CreateThread(NULL,0,ThreadDialog,0,0,&dwtid);
    }
    else
    {
        if (hwndGlobalDialog)
            SetWindowPos( (HWND__*)hwndGlobalDialog, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE );
    }
}

int Connect(void) {
    DWORD dwtid;
    Thread = CreateThread(NULL,0,ThreadProc,0,0,&dwtid);

    if (lpAddTrayMenuItemFn) {
        lpAddTrayMenuItemFn(callbackCookie, NULL, "System Shutdown", TrayMenuClick, NULL);
    }

    return 0;
}

int Disconnect(void)
{
    TerminateThread(Thread,0);
    return 0;
}

static bool bSettingsReaded = false;

static int GetDefaultSettings(struct S_PHONE_SETTINGS* settings) {
    settings->ring = 0;

    bSettingsReaded = true;
    return 0;
}

int GetPhoneSettings(struct S_PHONE_SETTINGS* settings) {

    std::string path = Utils::GetDllPath();
    path = Utils::ReplaceFileExtension(path, ".cfg");
    if (path == "")
        return GetDefaultSettings(settings);

    Json::Value root;   // will contains the root value after parsing.
    Json::Reader reader;

    std::ifstream ifs(path.c_str());
    std::string strConfig((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    //settings->iTriggerSrcChannel = 0;

    bool parsingSuccessful = reader.parse( strConfig, root );
    if ( !parsingSuccessful )
        return GetDefaultSettings(settings);

    GetDefaultSettings(settings);


    //int mode = root.get("TriggerMode", TRIGGER_MODE_AUTO).asInt();
    settings->ring = false;//root.get("ring", settings->ring).asInt();
    action = (ACTION)root.get("action", action).asInt();

    bSettingsReaded = true;
    return 0;
}

int SavePhoneSettings(struct S_PHONE_SETTINGS* settings) {
    Json::Value root;
    Json::StyledWriter writer;

    //root["ring"] = settings->ring;
    root["action"] = action;

    std::string outputConfig = writer.write( root );

    std::string path = Utils::GetDllPath();
    path = Utils::ReplaceFileExtension(path, ".cfg");
    if (path == "")
        return -1;

    std::ofstream ofs(path.c_str());
    ofs << outputConfig;
    ofs.close();

    return 0;
}

void SetAddTrayMenuItemCallback(CALLBACK_ADD_TRAY_MENU_ITEM lpFn)
{
    lpAddTrayMenuItemFn = lpFn;
}

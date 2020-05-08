#include "shutdown.h"

BOOL SetCurrentPrivilege( LPCTSTR Privilege, BOOL bEnablePrivilege )
{
	HANDLE hToken;
	LUID luid;
	TOKEN_PRIVILEGES tp, tpPrevious;
	DWORD cbPrevious = sizeof( TOKEN_PRIVILEGES );
	BOOL bSuccess = FALSE;

	if ( ! LookupPrivilegeValue( NULL, Privilege, &luid ) )
		return FALSE;

	if( ! OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken ) )
		return FALSE;

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = 0;

	AdjustTokenPrivileges( hToken, FALSE, &tp, sizeof( TOKEN_PRIVILEGES ), &tpPrevious, &cbPrevious );

	if ( GetLastError() == ERROR_SUCCESS )
	{
		tpPrevious.PrivilegeCount = 1;
		tpPrevious.Privileges[0].Luid = luid;

		if ( bEnablePrivilege )
			tpPrevious.Privileges[0].Attributes |= ( SE_PRIVILEGE_ENABLED );
		else
			tpPrevious.Privileges[0].Attributes &= ~( SE_PRIVILEGE_ENABLED );

		AdjustTokenPrivileges( hToken, FALSE, &tpPrevious, cbPrevious, NULL, NULL );

		if ( GetLastError() == ERROR_SUCCESS )
			bSuccess=TRUE;
	}

	CloseHandle( hToken );

	return bSuccess;
}



void DisplayError( DWORD err, const char *szCaption, const char *szComment )
{
	char msgbuf[4096];
	strcpy(msgbuf, szComment);
    itoa(err, msgbuf+strlen(msgbuf), 10);
    strcat(msgbuf, ":\n");
	FormatMessage( FORMAT_MESSAGE_FROM_SYSTEM, NULL, err,
		MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT),
		msgbuf+strlen(msgbuf), sizeof( msgbuf )-strlen(msgbuf), NULL );
	MessageBox(NULL, msgbuf, szCaption, MB_ICONEXCLAMATION);
}

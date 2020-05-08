#ifndef SHUTDOWN_H_INCLUDED
#define SHUTDOWN_H_INCLUDED

#include <windows.h>

BOOL SetCurrentPrivilege( LPCTSTR Privilege, BOOL bEnablePrivilege );
void DisplayError( DWORD err, const char *szCaption, const char *szComment );

#endif // SHUTDOWN_H_INCLUDED

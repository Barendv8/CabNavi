#pragma once
// STUB-windows.h -- alleen voor compileertests op Linux. Zie imgui.h.
#include <cstring>
#include <cstddef>

typedef void *HWND;
typedef void *HGLOBAL;
typedef void *HANDLE;
typedef void *HINSTANCE;
typedef long HRESULT;
typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef int BOOL;
typedef const char *LPCSTR;
typedef const wchar_t *LPCWSTR;

#define SUCCEEDED( hr ) ( ( (HRESULT)( hr ) ) >= 0 )
#define FAILED( hr ) ( ( (HRESULT)( hr ) ) < 0 )
#define GMEM_MOVEABLE 0x0002
#define CF_TEXT 1
#define SW_SHOWNORMAL 1

BOOL OpenClipboard( HWND );
BOOL EmptyClipboard();
BOOL CloseClipboard();
HANDLE SetClipboardData( UINT, HANDLE );
HGLOBAL GlobalAlloc( UINT, std::size_t );
void *GlobalLock( HGLOBAL );
BOOL GlobalUnlock( HGLOBAL );
DWORD GetLastError();
// MSVC-specifiek, hier alleen als declaratie zodat de _WIN32-tak compileert.
extern "C" int localtime_s( struct tm *, const long long * );
#define __declspec( x )
struct RECT { long left, top, right, bottom; };
BOOL GetClientRect( HWND, RECT * );

// Aanvullingen voor Plugin.cxx (venster zoeken) -- puur declaraties.
typedef long long LPARAM;
typedef unsigned long long WPARAM;
#define TRUE 1
#define FALSE 0
#define GW_OWNER 4
DWORD GetCurrentProcessId();
DWORD GetWindowThreadProcessId( HWND, DWORD * );
HWND GetWindow( HWND, UINT );
BOOL IsWindowVisible( HWND );
typedef BOOL ( *WNDENUMPROC )( HWND, LPARAM );
BOOL EnumWindows( WNDENUMPROC, LPARAM );
int MultiByteToWideChar( UINT, DWORD, LPCSTR, int, wchar_t *, int );
int WideCharToMultiByte( UINT, DWORD, const wchar_t *, int, char *, int, LPCSTR, BOOL * );
#define CP_UTF8 65001
HANDLE GetClipboardData( UINT );

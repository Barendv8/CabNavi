#pragma once
// STUB-winhttp -- alleen voor compileertests.
#include "windows.h"
typedef void *HINTERNET;
#define WINHTTP_ACCESS_TYPE_DEFAULT_PROXY 0
#define WINHTTP_NO_PROXY_NAME nullptr
#define WINHTTP_NO_PROXY_BYPASS nullptr
#define WINHTTP_NO_REFERER nullptr
#define WINHTTP_DEFAULT_ACCEPT_TYPES nullptr
#define WINHTTP_NO_ADDITIONAL_HEADERS nullptr
#define WINHTTP_FLAG_SECURE 0x00800000
#define WINHTTP_QUERY_STATUS_CODE 19
#define WINHTTP_QUERY_FLAG_NUMBER 0x20000000
#define WINHTTP_HEADER_NAME_BY_INDEX nullptr
#define INTERNET_SCHEME_HTTPS 2
#define ICU_ESCAPE 0x80000000

struct URL_COMPONENTS
{
    DWORD dwStructSize;
    wchar_t *lpszScheme; DWORD dwSchemeLength;
    int nScheme;
    wchar_t *lpszHostName; DWORD dwHostNameLength;
    unsigned short nPort;
    wchar_t *lpszUserName; DWORD dwUserNameLength;
    wchar_t *lpszPassword; DWORD dwPasswordLength;
    wchar_t *lpszUrlPath; DWORD dwUrlPathLength;
    wchar_t *lpszExtraInfo; DWORD dwExtraInfoLength;
};

HINTERNET WinHttpOpen( LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD );
HINTERNET WinHttpConnect( HINTERNET, LPCWSTR, unsigned short, DWORD );
HINTERNET WinHttpOpenRequest( HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD );
BOOL WinHttpSendRequest( HINTERNET, LPCWSTR, DWORD, void *, DWORD, DWORD, void * );
BOOL WinHttpReceiveResponse( HINTERNET, void * );
BOOL WinHttpQueryHeaders( HINTERNET, DWORD, LPCWSTR, void *, DWORD *, DWORD * );
BOOL WinHttpCrackUrl( LPCWSTR, DWORD, DWORD, URL_COMPONENTS * );
BOOL WinHttpCloseHandle( HINTERNET );
#define WINHTTP_NO_HEADER_INDEX nullptr
BOOL WinHttpQueryDataAvailable( HINTERNET, DWORD * );
BOOL WinHttpReadData( HINTERNET, void *, DWORD, DWORD * );

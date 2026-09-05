#pragma once
// ---------------------------------------------------------------------------
// HttpHulp -- one plain HTTPS GET via WinHTTP (built into Windows, no extra
// dependency), for the small downloads CabNavi does: the map table from the
// CabNavi repository. The TruckersMP Web API and the Discord webhook have
// their own variants because they need status handling of their own; this
// one is deliberately minimal. Never call it on the game thread.
// ---------------------------------------------------------------------------

#include <string>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <winhttp.h>
#endif

namespace Ritten
{
    // host without scheme ("raw.githubusercontent.com"), pad with leading '/'.
    // Returns false with a short reason in `fout` (no URL, no user data).
    inline bool HttpGet( const wchar_t *host, const wchar_t *pad, std::string &body, std::string &fout )
    {
#ifdef _WIN32
        body.clear();
        HINTERNET sessie = WinHttpOpen( L"CabNavi/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
        if( !sessie ) { fout = "WinHttpOpen failed"; return false; }
        HINTERNET verbinding = WinHttpConnect( sessie, host, 443, 0 );
        if( !verbinding ) { WinHttpCloseHandle( sessie ); fout = "connect failed"; return false; }
        HINTERNET request = WinHttpOpenRequest( verbinding, L"GET", pad, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE );
        bool ok = false;
        if( request && WinHttpSendRequest( request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0 ) && WinHttpReceiveResponse( request, nullptr ) )
        {
            DWORD status = 0, maat = sizeof( status );
            WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &maat, WINHTTP_NO_HEADER_INDEX );
            if( status == 200 )
            {
                DWORD beschikbaar = 0;
                do
                {
                    beschikbaar = 0;
                    if( !WinHttpQueryDataAvailable( request, &beschikbaar ) || beschikbaar == 0 ) break;
                    std::string blok( beschikbaar, '\0' );
                    DWORD gelezen = 0;
                    if( !WinHttpReadData( request, blok.data(), beschikbaar, &gelezen ) ) break;
                    body.append( blok.data(), gelezen );
                } while( beschikbaar > 0 );
                ok = true;
            }
            else fout = "status " + std::to_string( status );
        }
        else fout = "request failed (" + std::to_string( GetLastError() ) + ")";
        if( request ) WinHttpCloseHandle( request );
        WinHttpCloseHandle( verbinding );
        WinHttpCloseHandle( sessie );
        return ok;
#else
        (void)host; (void)pad; (void)body;
        fout = "no network on this platform";
        return false;
#endif
    }
}

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

    // Same as HttpGet, but with a method (GET/POST), extra request headers
    // ("Name: value\r\n" each) and an optional body. Any 2xx counts as ok;
    // the status code is returned in `status` so callers can tell 401 from
    // 429. Used by the VTC sources and dispatch; HttpGet above is left as is.
    inline bool HttpVerzoek( const wchar_t *methode, const wchar_t *host, const wchar_t *pad,
                             const std::wstring &headers, const std::string &verzoekBody,
                             std::string &body, int &status, std::string &fout,
                             int poort = 443, bool https = true )
    {
        body.clear(); status = 0;
#ifdef _WIN32
        HINTERNET sessie = WinHttpOpen( L"CabNavi/1.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
        if( !sessie ) { fout = "WinHttpOpen failed"; return false; }
        WinHttpSetTimeouts( sessie, 5000, 5000, 5000, 8000 );
        // No forced TLS protocol list here. Tried 15-09: forcing 1.2/1.3 gave
        // error 12152 on a request the plain WebApi code handled fine, so
        // the OS default wins (Windows 10/11 already leave 1.0/1.1 out).
        HINTERNET verbinding = WinHttpConnect( sessie, host, static_cast<INTERNET_PORT>( poort ), 0 );
        if( !verbinding ) { WinHttpCloseHandle( sessie ); fout = "connect failed"; return false; }
        HINTERNET request = WinHttpOpenRequest( verbinding, methode, pad, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0 );
        bool ok = false;
        if( request )
        {
            // Never follow a redirect: these requests carry a key, and a
            // redirect would hand that key to whatever host the server
            // points at. A 3xx simply counts as a failure.
            DWORD geenRedirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            WinHttpSetOption( request, WINHTTP_OPTION_REDIRECT_POLICY, &geenRedirect, sizeof( geenRedirect ) );
        }
        const LPCWSTR hdr = headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str();
        const DWORD hdrLen = headers.empty() ? 0 : (DWORD)-1L;
        LPVOID data = verzoekBody.empty() ? nullptr : (LPVOID)verzoekBody.data();
        const DWORD dataLen = (DWORD)verzoekBody.size();
        if( request && WinHttpSendRequest( request, hdr, hdrLen, data, dataLen, dataLen, 0 ) && WinHttpReceiveResponse( request, nullptr ) )
        {
            DWORD st = 0, maat = sizeof( st );
            WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st, &maat, WINHTTP_NO_HEADER_INDEX );
            status = (int)st;
            DWORD beschikbaar = 0;
            do
            {
                beschikbaar = 0;
                if( !WinHttpQueryDataAvailable( request, &beschikbaar ) || beschikbaar == 0 ) break;
                std::string blok( beschikbaar, '\0' );
                DWORD gelezen = 0;
                if( !WinHttpReadData( request, blok.data(), beschikbaar, &gelezen ) ) break;
                body.append( blok.data(), gelezen );
                // A company API reply is kilobytes. Anything past 4 MB is not
                // a reply, it is an attempt to fill the game's memory.
                if( body.size() > 4u * 1024u * 1024u ) { body.clear(); fout = "reply too large"; st = 0; break; }
            } while( beschikbaar > 0 );
            ok = ( st >= 200 && st < 300 );
            if( !ok && fout.empty() ) fout = "status " + std::to_string( st );
        }
        else fout = "request failed (" + std::to_string( GetLastError() ) + ")";
        if( request ) WinHttpCloseHandle( request );
        WinHttpCloseHandle( verbinding );
        WinHttpCloseHandle( sessie );
        return ok;
#else
        (void)methode; (void)host; (void)pad; (void)headers; (void)verzoekBody; (void)poort; (void)https;
        fout = "no network on this platform";
        return false;
#endif
    }
}

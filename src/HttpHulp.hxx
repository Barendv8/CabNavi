#pragma once

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
        HINTERNET verbinding = WinHttpConnect( sessie, host, static_cast<INTERNET_PORT>( poort ), 0 );
        if( !verbinding ) { WinHttpCloseHandle( sessie ); fout = "connect failed"; return false; }
        HINTERNET request = WinHttpOpenRequest( verbinding, methode, pad, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0 );
        bool ok = false;
        if( request )
        {
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

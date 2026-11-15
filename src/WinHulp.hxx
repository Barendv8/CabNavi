#pragma once

#include <windows.h>
#include <shellapi.h>
#pragma comment( lib, "shell32.lib" )

#include <string>

namespace Ritten
{
    inline void OpenUrlInBrowser( const std::string &url )
    {
        ShellExecuteA( nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL );
    }

    inline std::string LeesVanKlembord()
    {
        std::string uit;
        if( !OpenClipboard( nullptr ) ) return uit;

        if( HANDLE data = GetClipboardData( CF_TEXT ) )
        {
            if( const char *tekst = static_cast<const char *>( GlobalLock( data ) ) )
            {
                uit = tekst;
                GlobalUnlock( data );
            }
        }
        CloseClipboard();
        return uit;
    }

    inline void KopieerNaarKlembord( const std::string &tekst )
    {
        if( !OpenClipboard( nullptr ) ) return;
        EmptyClipboard();

        HGLOBAL geheugen = GlobalAlloc( GMEM_MOVEABLE, tekst.size() + 1 );
        if( geheugen != nullptr )
        {
            void *doel = GlobalLock( geheugen );
            if( doel != nullptr )
            {
                memcpy( doel, tekst.c_str(), tekst.size() + 1 );
                GlobalUnlock( geheugen );
                SetClipboardData( CF_TEXT, geheugen );
            }
        }
        CloseClipboard();
    }
}

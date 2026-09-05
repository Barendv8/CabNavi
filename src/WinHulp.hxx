#pragma once
// WinHulp.hxx
//
// Two simple standard Windows actions, used by the player context menu:
// open a URL in the default browser, and copy text to the clipboard. Both
// are plain Win32 APIs, not TruckersMP/SCS SDK functions -- so no risk of
// wrongly guessed names like earlier with the bus/cargo headers.

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

    // Get text FROM the clipboard. Used by the paste button next to the
    // webhook field: handier than Ctrl+V when you are not sure the game
    // lets that key combination through.
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

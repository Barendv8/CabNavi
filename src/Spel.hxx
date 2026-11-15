#pragma once

#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Ritten
{
    enum class Spel
    {
        ETS2 = 0,
        ATS  = 1,
    };

    class SpelInfo
    {
    public:
        static Spel Huidig()
        {
            static const Spel spel = Bepaal();
            return spel;
        }

        static bool IsAts() { return Huidig() == Spel::ATS; }

        static const char *Naam() { return IsAts() ? "ATS" : "ETS2"; }

        static const char *Munt() { return IsAts() ? "USD" : "EUR"; }

        static void Bevestig( const char *gameId, std::string &melding )
        {
            melding.clear();
            if( gameId == nullptr || *gameId == '\0' ) return;
            const std::string id( gameId );
            const bool zegtAts = ( id == "ats" );
            if( zegtAts != IsAts() )
                melding = "game_id '" + id + "' does not match executable (running as "
                          + std::string( Naam() ) + "); keeping the executable's answer";
        }

    private:
        static Spel Bepaal()
        {
#ifdef _WIN32
            wchar_t buf[ MAX_PATH ];
            const DWORD n = GetModuleFileNameW( nullptr, buf, MAX_PATH );
            if( n > 0 && n < MAX_PATH )
            {
                std::wstring pad( buf, n );
                for( auto &c : pad ) c = static_cast<wchar_t>( towlower( c ) );
                if( pad.find( L"amtrucks.exe" ) != std::wstring::npos ) return Spel::ATS;
            }
#endif
            return Spel::ETS2;
        }
    };
}

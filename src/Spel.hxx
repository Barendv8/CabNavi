#pragma once
// Spel.hxx
//
// Which game is this DLL loaded into: Euro Truck Simulator 2 or American
// Truck Simulator? One plugin serves both, the way the TruckersMP client
// itself does, so everything game-specific asks here instead of assuming.
//
// Detection: the executable name of the process we live in --
// eurotrucks2.exe or amtrucks.exe. That answer exists from the very first
// instruction, BEFORE any SDK init runs; the map-table cache is read at
// overlay start-up and needs the answer that early. scs_telemetry_init
// later reports common.game_id ("eut2"/"ats"), which Plugin.cxx passes to
// Bevestig() as a cross-check; a mismatch is logged, never trusted over
// the executable, because the executable is what actually runs.
//
// Unknown executable (renamed exe, future game): ETS2 is assumed, matching
// what every earlier version of this plugin silently did.

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

        // "ETS2" / "ATS" -- for logs, webhook payloads and the screen.
        static const char *Naam() { return IsAts() ? "ATS" : "ETS2"; }

        // Currency of the game economy. The game files price fuel per litre
        // in this currency (EUR in ETS2, USD in ATS); we keep every internal
        // amount in it and only ever LABEL it.
        static const char *Munt() { return IsAts() ? "USD" : "EUR"; }

        // scs_telemetry_init's common.game_id ("eut2"/"ats"). Pure
        // cross-check against the executable; see the header note.
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

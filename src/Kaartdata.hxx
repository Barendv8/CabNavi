#pragma once
// ---------------------------------------------------------------------------
// Kaartdata -- where am I, in map terms? Nearest city (and thus country),
// and whether the truck stands at a fuel station or at the pump of an own
// large garage.
//
// Two sources for the table, the newest wins:
//   1. KaartdataTabel.hxx, embedded at build time (always there).
//   2. %APPDATA%\CabNavi\kaartdata.json, downloaded from the CabNavi
//      repository when a map DLC added cities (see KaartdataUpdate). So a
//      user never has to install a new plugin version for a new map.
//
// Used for refuelling: the SDK gives neither the country nor the price, but
// the position is known (the radar uses it already). Nearest city gives the
// country; the country gives the price. The NEAREST PUMP decides the garage
// question: every large garage has its own pump as a separate map point, so
// a public station across the street is simply a different, nearer pump.
// MEASURED 04-09: own large garage = country price x (1 - 0.15), NL and DE.
// ---------------------------------------------------------------------------

#include <filesystem>
#include <string>

namespace Ritten
{
    class Kaartdata
    {
    public:
        struct Plaats
        {
            std::string stad;      // token, e.g. "duisburg"
            std::string land;      // token, e.g. "germany"
            double afstandStadM = -1.0;   // distance to that city centre, game metres
            bool bijGarage = false;       // nearest pump is an own-garage pump
            bool bijPomp = false;         // a pump within POMP_STRAAL_M at all
            double afstandGarageM = -1.0; // to the nearest large-garage point
            double afstandPompM = -1.0;   // to the nearest pump
        };

        // You refuel standing at the pump; the truck itself is 20 m long.
        static constexpr double POMP_STRAAL_M = 120.0;

        // World X/Z as the TruckersMP SDK reports them.
        static Plaats Bepaal( double x, double z );

        // Load a table from JSON text (format written by tools/kaartdata/
        // maak_tabel.py). Only replaces the active table when its version is
        // newer than the active one, or when `forceer` is set. Thread-safe.
        static bool LaadJson( const std::string &tekst, std::string &fout, bool forceer = false );
        static bool LaadBestand( const std::filesystem::path &pad, std::string &fout );

        // "1.60.1.7" style; true if a is newer than b.
        static bool VersieNieuwer( const std::string &a, const std::string &b );

        static std::string Versie();       // of the active table
        static int AantalSteden();
        static std::string Bron();         // "embedded" or "downloaded"

        // Background download of a newer table (see note below the class).
        static void StartUpdate( const std::filesystem::path &cacheMap );
        static void StopUpdate();   // joins the thread; call at shutdown
    };

    // KaartdataUpdate: see Kaartdata::StartUpdate/StopUpdate. Fetches
    // data/kaartdata.json from the CabNavi repository on a background thread,
    // once per session. A newer table than the active one is saved to
    // <cache>/kaartdata.json and activated; anything else is ignored. So when
    // a map DLC adds cities, the maintainer regenerates the table once
    // (tools/kaartdata/maak_tabel.py) and every user has it at the next start
    // -- no new plugin version needed. Off when the user disables it.
}

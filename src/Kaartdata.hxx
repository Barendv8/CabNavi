#pragma once

#include <filesystem>
#include <string>

namespace Ritten
{
    class Kaartdata
    {
    public:
        struct Plaats
        {
            std::string stad;
            std::string land;
            double afstandStadM = -1.0;
            bool bijGarage = false;
            bool bijPomp = false;
            double afstandGarageM = -1.0;
            double afstandPompM = -1.0;
        };

        static constexpr double POMP_STRAAL_M = 120.0;

        static Plaats Bepaal( double x, double z );

        static bool LaadJson( const std::string &tekst, std::string &fout, bool forceer = false );
        static bool LaadBestand( const std::filesystem::path &pad, std::string &fout );

        static bool VersieNieuwer( const std::string &a, const std::string &b );

        static std::string Versie();
        static const char *CacheBestandsnaam();
        static int AantalSteden();
        static std::string Bron();

        static void StartUpdate( const std::filesystem::path &cacheMap );
        static void StopUpdate();
    };
}

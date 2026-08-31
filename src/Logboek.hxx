#pragma once
// Logboek.hxx
//
// Eén plek waar alles heen geschreven wordt wat we bij een probleem willen
// terugzien. Schrijft naar %APPDATA%\CabNavi\debug.log.
//
// Waarom dit bestaat: de crash van 30 augustus was een leesfout buiten een
// tabel, en het enige spoor was een adres in het spellog. Met een spoor van
// waar de overlay mee bezig was, is dat soort dingen in één blik te vinden.
//
// Drie soorten regels:
//
//   [start]   eenmalig bij het opstarten -- versies, kanalen, bestanden
//   [gebeurt] iets noemenswaardigs: een rit, een fout, een instelling
//   [spoor]   waar de overlay was; wordt hooguit eens per twee seconden
//             weggeschreven, zodat het niet elke frame naar schijf gaat
//
// Bij een crash is de LAATSTE [spoor]-regel het startpunt van het zoeken.

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace Ritten
{
    class Logboek
    {
    public:
        // Waar het bestand staat. Naast de andere instellingen, zodat een
        // spel-update het niet opruimt.
        static std::filesystem::path Pad()
        {
            std::filesystem::path pad;
            if( const char *appdata = std::getenv( "APPDATA" ) ) pad = appdata;
            else pad = std::filesystem::current_path();
            pad /= "CabNavi";
            std::error_code ec;
            std::filesystem::create_directories( pad, ec );
            return pad / "debug.log";
        }

        // Uitgebreide diagnose aan of uit. Staat standaard UIT: de
        // categorieen "verbruik", "vlaggen" en "vtc" schrijven elke paar
        // seconden een regel, en dat hoef je alleen als er iets uitgezocht
        // moet worden. Fouten en gebeurtenissen worden ALTIJD gelogd.
        static bool &Uitgebreid()
        {
            static bool aan = false;
            return aan;
        }

        // Hoort deze categorie bij de uitgebreide diagnose?
        static bool IsDiagnose( const char *categorie )
        {
            if( categorie == nullptr ) return false;
            const std::string c = categorie;
            return ( c == "verbruik" || c == "vlaggen" || c == "vtc" );
        }

        // Eén regel wegschrijven. Kan vanaf elke thread; het slot voorkomt
        // dat twee regels door elkaar heen lopen.
        static void Schrijf( const char *categorie, const std::string &bericht )
        {
            if( IsDiagnose( categorie ) && !Uitgebreid() ) return;
            try
            {
                std::lock_guard<std::mutex> slot( Slot() );
                std::ofstream uit( Pad(), std::ios::app );
                if( !uit ) return;
                uit << Tijdstempel() << " [" << categorie << "] " << bericht << "\n";
            }
            catch( ... )
            {
                // Loggen mag NOOIT zelf een probleem worden. Lukt het niet,
                // dan gaat het spel gewoon door zonder logregel.
            }
        }

        // Het logboek leegmaken bij het opstarten, met een kopregel. Anders
        // groeit het bestand eindeloos en weet je niet welke sessie je leest.
        static void StartNieuweSessie( const std::string &kopregel )
        {
            try
            {
                std::lock_guard<std::mutex> slot( Slot() );
                std::ofstream uit( Pad(), std::ios::trunc );
                if( !uit ) return;
                uit << "=== " << Tijdstempel() << " " << kopregel << " ===\n";
            }
            catch( ... )
            {
            }
        }

        // Waar zijn we mee bezig? Kost niets: alleen een pointer opslaan.
        // Wordt hooguit eens per twee seconden naar schijf geschreven.
        static void Spoor( const char *waar )
        {
            HuidigSpoor() = waar;

            const auto nu = std::chrono::steady_clock::now();
            auto &laatst = LaatsteSpoorTijd();
            if( std::chrono::duration<double>( nu - laatst ).count() < 2.0 ) return;
            laatst = nu;
            Schrijf( "spoor", waar );
        }

        // De laatst bekende plek, voor in een foutmelding.
        static const char *LaatstBekend()
        {
            const char *s = HuidigSpoor();
            return s ? s : "(nog niets)";
        }

    private:
        static std::mutex &Slot()
        {
            static std::mutex m;
            return m;
        }
        static const char *&HuidigSpoor()
        {
            static const char *s = nullptr;
            return s;
        }
        static std::chrono::steady_clock::time_point &LaatsteSpoorTijd()
        {
            static std::chrono::steady_clock::time_point t{};
            return t;
        }

        static std::string Tijdstempel()
        {
            const std::time_t nu = std::time( nullptr );
            std::tm tmBuf{};
        #if defined( _WIN32 )
            localtime_s( &tmBuf, &nu );
        #else
            localtime_r( &nu, &tmBuf );
        #endif
            char buf[ 32 ];
            std::snprintf( buf, sizeof( buf ), "%02d:%02d:%02d",
                            tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec );
            return buf;
        }
    };
}

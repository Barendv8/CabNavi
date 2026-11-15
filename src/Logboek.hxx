#pragma once

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <thread>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace Ritten
{
    class Logboek
    {
    public:
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

        static bool &Uitgebreid()
        {
            static bool aan = false;
            return aan;
        }

        static unsigned long HuidigeThreadId()
        {
            return static_cast<unsigned long>(
                std::hash<std::thread::id>{}( std::this_thread::get_id() ) & 0xFFFFFFFFu );
        }

        static std::string KortPad( const std::filesystem::path &pad )
        {
            std::string s = pad.string();
            const char *omgeving = std::getenv( "APPDATA" );
            if( omgeving && *omgeving )
            {
                const std::string basis( omgeving );
                if( s.rfind( basis, 0 ) == 0 )
                {
                    return "%APPDATA%" + s.substr( basis.size() );
                }
            }

            const auto naam = pad.filename().string();
            const auto map = pad.parent_path().filename().string();
            return map.empty() ? naam : ( "..." + std::string( 1, pad.preferred_separator )
                                          + map + std::string( 1, pad.preferred_separator ) + naam );
        }

        static std::string KorteFout( const std::string &tekst )
        {
            std::string s = tekst;
            const char *omgeving = std::getenv( "USERPROFILE" );
            if( omgeving && *omgeving )
            {
                const std::string basis( omgeving );
                for( std::size_t p = s.find( basis ); p != std::string::npos;
                     p = s.find( basis ) )
                {
                    s.replace( p, basis.size(), "%USERPROFILE%" );
                }
            }
            return s;
        }

        static bool IsDiagnose( const char *categorie )
        {
            if( categorie == nullptr ) return false;
            const std::string c = categorie;
            return ( c == "fuel" || c == "flags" || c == "vtc" || c == "trace"
                     || c == "bus" || c == "eta" );
        }

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
            }
        }

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

        static void Spoor( const char *waar )
        {
            HuidigSpoor() = waar;

            const auto nu = std::chrono::steady_clock::now();
            auto &laatst = LaatsteSpoorTijd();
            if( std::chrono::duration<double>( nu - laatst ).count() < 2.0 ) return;
            laatst = nu;
            Schrijf( "trace", waar );
        }

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
            const auto nuKlok = std::chrono::system_clock::now();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                nuKlok.time_since_epoch() ).count() % 1000;

            char buf[ 32 ];
            std::snprintf( buf, sizeof( buf ), "%02d:%02d:%02d.%03d",
                            tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec,
                            static_cast<int>( ms ) );
            return buf;
        }
    };
}

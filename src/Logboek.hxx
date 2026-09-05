#pragma once
// Logboek.hxx
//
// One place where everything is written that we want to see again when
// something goes wrong. Writes to %APPDATA%\CabNavi\debug.log.
//
// Why this exists: the crash of 30 August was an out-of-bounds read in a
// table, and the only trace was an address in the game log. With a trail
// of what the overlay was doing, that kind of thing is found at a glance.
//
// Three kinds of lines:
//
//   [start]   once at startup -- versions, channels, files
//   [event] something noteworthy: a trip, an error, a setting
//   [trace]   where the overlay was; written at most once every two
//             seconds, so it does not hit the disk every frame
//
// After a crash the LAST [trace] line is where the search starts.

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
        // Where the file lives. Next to the other settings, so a game update
        // does not clean it up.
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

        // Verbose diagnostics on or off. Default OFF: the categories
        // "fuel", "flags" and "vtc" write a line every few seconds,
        // and you only need that while investigating something. Errors and
        // events are ALWAYS logged.
        static bool &Uitgebreid()
        {
            static bool aan = false;
            return aan;
        }

        // The ID of the thread we are running on right now. Only meant to
        // establish whether two pieces of code run on the same thread; the
        // number itself has no further meaning.
        static unsigned long HuidigeThreadId()
        {
            return static_cast<unsigned long>(
                std::hash<std::thread::id>{}( std::this_thread::get_id() ) & 0xFFFFFFFFu );
        }

        // Shorten a path for the log: everything up to and including the
        // user folder is replaced by %APPDATA%. The full path contains your
        // Windows user name, and debug.log gets shared easily -- on a forum,
        // in a bug report. To investigate a problem you only need to know
        // WHETHER the file is there.
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

            // No APPDATA, or the path is outside it: then only show the last
            // two parts, so a user name never ends up in the log.
            const auto naam = pad.filename().string();
            const auto map = pad.parent_path().filename().string();
            return map.empty() ? naam : ( "..." + std::string( 1, pad.preferred_separator )
                                          + map + std::string( 1, pad.preferred_separator ) + naam );
        }

        // Clean up an error message from the operating system. On file
        // errors Windows puts the FULL path in the text, including
        // C:\\Users\\<name>. Everything from that user folder onwards is cut.
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

        // Does this category belong to verbose diagnostics?
        static bool IsDiagnose( const char *categorie )
        {
            if( categorie == nullptr ) return false;
            const std::string c = categorie;
            // "trace" belongs here too. Without this, that category wrote a
            // line every two seconds for EVERYONE -- measured: 1186 lines in a
            // one-hour session. Pure diagnostics, so off by default.
            return ( c == "fuel" || c == "flags" || c == "vtc" || c == "trace"
                     || c == "bus" || c == "eta" );
        }

        // Write one line. May be called from any thread; the lock keeps two
        // lines from interleaving.
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
                // Logging must NEVER become a problem itself. If it fails, the game
                // simply continues without the log line.
            }
        }

        // Empty the log at startup, with a header line. Otherwise the file
        // grows forever and you cannot tell which session you are reading.
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

        // What are we doing? Costs nothing: only a pointer is stored.
        // Written to disk at most once every two seconds.
        static void Spoor( const char *waar )
        {
            HuidigSpoor() = waar;

            const auto nu = std::chrono::steady_clock::now();
            auto &laatst = LaatsteSpoorTijd();
            if( std::chrono::duration<double>( nu - laatst ).count() < 2.0 ) return;
            laatst = nu;
            Schrijf( "trace", waar );
        }

        // The last known location, for use in an error message.
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
            // Milliseconds included. Needed to line up a screen recording with
            // this log: a gear change takes about half a second, and without ms
            // you cannot tell which line belongs to which frame.
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

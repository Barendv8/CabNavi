#include "SpelConfig.hxx"
#include "Spel.hxx"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace Ritten
{
    namespace
    {
        const char *const SLEUTELS[] = {
            "g_fuel_simulation", "g_fatigue", "g_hardcore_simulation", "g_use_speed_limiter",
            "g_cargo_damage", "g_truck_stability", "g_trailer_stability", "g_brake_intensity",
            "g_road_events", "g_detours", "g_traffic", "g_bad_weather_factor",
            "g_suspension_auto_reset", "g_trailer_advanced_coupling", "g_hud_speed_limit",
            "g_police", "g_adviser_auto_parking", "g_pedestrian", "g_mandatory_break",
        };

        std::filesystem::path SpelDocumentenMap()
        {
#ifdef _WIN32
            PWSTR doc = nullptr;
            if( SHGetKnownFolderPath( FOLDERID_Documents, 0, nullptr, &doc ) != S_OK || !doc ) return {};
            std::filesystem::path basis = std::filesystem::path( doc )
                / ( SpelInfo::IsAts() ? L"American Truck Simulator" : L"Euro Truck Simulator 2" );
            CoTaskMemFree( doc );
            return basis;
#else
            if( const char *map = std::getenv( "CABNAVI_TEST_SPELMAP" ) ) return std::filesystem::path( map );
            return {};
#endif
        }

        std::filesystem::path NieuwsteProfiel( const std::filesystem::path &basis )
        {
            std::filesystem::path beste;
            std::filesystem::file_time_type besteTijd{};
            std::error_code ec;
            for( const auto &p : std::filesystem::directory_iterator( basis / L"profiles", ec ) )
            {
                if( !p.is_directory( ec ) ) continue;
                const auto cfg = p.path() / L"config_local.cfg";
                if( !std::filesystem::exists( cfg, ec ) ) continue;
                const auto t = std::filesystem::last_write_time( cfg, ec );
                if( beste.empty() || t > besteTijd ) { beste = p.path(); besteTijd = t; }
            }
            return beste;
        }
    }

    void SpelConfig::LeesBestand( const std::wstring &pad )
    {
        const std::filesystem::path p( pad );
        std::ifstream in( p );
        if( !in ) return;
        std::string regel;
        while( std::getline( in, regel ) )
        {
            if( regel.rfind( "uset ", 0 ) != 0 ) continue;
            const std::size_t sp = regel.find( ' ', 5 );
            if( sp == std::string::npos ) continue;
            const std::string sleutel = regel.substr( 5, sp - 5 );
            std::string waarde = regel.substr( sp + 1 );
            while( !waarde.empty() && ( waarde.back() == '\r' || waarde.back() == ' ' ) ) waarde.pop_back();
            if( waarde.size() >= 2 && waarde.front() == '"' && waarde.back() == '"' ) waarde = waarde.substr( 1, waarde.size() - 2 );
            if( m_waarden.find( sleutel ) == m_waarden.end() ) m_waarden[ sleutel ] = waarde;
        }
    }

    void SpelConfig::Laad()
    {
        m_waarden.clear();
        try
        {
            const std::filesystem::path basis = SpelDocumentenMap();
            if( basis.empty() ) return;
            const std::filesystem::path profiel = NieuwsteProfiel( basis );
            if( !profiel.empty() ) LeesBestand( ( profiel / L"config_local.cfg" ).wstring() );
            if( !profiel.empty() ) LeesBestand( ( profiel / L"config.cfg" ).wstring() );
            LeesBestand( ( basis / L"config.cfg" ).wstring() );
        }
        catch( ... ) { }
    }

    std::string SpelConfig::Waarde( const std::string &sleutel ) const
    {
        const auto it = m_waarden.find( sleutel );
        return it == m_waarden.end() ? std::string() : it->second;
    }

    bool SpelConfig::Aan( const std::string &sleutel, bool standaard ) const
    {
        const std::string w = Waarde( sleutel );
        if( w.empty() ) return standaard;
        return w != "0" && w != "0.0" && w != "false";
    }

    std::map<std::string, bool> SpelConfig::RealistischeInstellingen() const
    {
        std::map<std::string, bool> uit;
        for( const char *k : SLEUTELS )
        {
            const auto it = m_waarden.find( k );
            if( it == m_waarden.end() ) continue;
            const std::string naam = std::string( k ).substr( 2 );
            uit[ naam ] = it->second != "0" && it->second != "0.0" && it->second != "false";
        }
        return uit;
    }
}

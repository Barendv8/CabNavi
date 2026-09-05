#include "Kaartdata.hxx"
#include "KaartdataTabel.hxx"
#include "HttpHulp.hxx"
#include "Logboek.hxx"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace Ritten
{
    namespace
    {
        struct StadR { std::string token, land; float x, z; };
        struct PuntR { float x, z; };
        struct PompR { float x, z; bool garage; };

        // The active table. Empty vectors = use the embedded arrays.
        struct Tabel
        {
            bool geladen = false;
            std::string versie;
            std::vector<StadR> steden;
            std::vector<PuntR> garages;
            std::vector<PompR> pompen;
        };
        Tabel g_tabel;
        std::mutex g_slot;

        // Plain linear scan. Only runs at a refuelling event, a few thousand
        // points -- microseconds, not worth an index.
        template <typename T>
        int Dichtstbij( const T *punten, int aantal, double x, double z, double &afstand )
        {
            int beste = -1; double besteKw = 0.0;
            for( int i = 0; i < aantal; ++i )
            {
                const double dx = punten[ i ].x - x, dz = punten[ i ].z - z;
                const double kw = dx * dx + dz * dz;
                if( beste < 0 || kw < besteKw ) { beste = i; besteKw = kw; }
            }
            afstand = beste < 0 ? -1.0 : std::sqrt( besteKw );
            return beste;
        }

        std::vector<int> Versiedelen( const std::string &v )
        {
            std::vector<int> uit; std::stringstream ss( v ); std::string deel;
            while( std::getline( ss, deel, '.' ) ) uit.push_back( std::atoi( deel.c_str() ) );
            return uit;
        }
    }

    bool Kaartdata::VersieNieuwer( const std::string &a, const std::string &b )
    {
        const auto va = Versiedelen( a ), vb = Versiedelen( b );
        for( std::size_t i = 0; i < std::max( va.size(), vb.size() ); ++i )
        {
            const int x = i < va.size() ? va[ i ] : 0, y = i < vb.size() ? vb[ i ] : 0;
            if( x != y ) return x > y;
        }
        return false;
    }

    Kaartdata::Plaats Kaartdata::Bepaal( const double x, const double z )
    {
        std::lock_guard<std::mutex> lock( g_slot );
        Plaats p;
        double d = -1.0;
        if( g_tabel.geladen )
        {
            const int s = Dichtstbij( g_tabel.steden.data(), static_cast<int>( g_tabel.steden.size() ), x, z, d );
            if( s >= 0 ) { p.stad = g_tabel.steden[ s ].token; p.land = g_tabel.steden[ s ].land; p.afstandStadM = d; }
            if( Dichtstbij( g_tabel.garages.data(), static_cast<int>( g_tabel.garages.size() ), x, z, d ) >= 0 ) p.afstandGarageM = d;
            const int pomp = Dichtstbij( g_tabel.pompen.data(), static_cast<int>( g_tabel.pompen.size() ), x, z, d );
            if( pomp >= 0 ) { p.afstandPompM = d; p.bijPomp = d <= POMP_STRAAL_M; p.bijGarage = p.bijPomp && g_tabel.pompen[ pomp ].garage; }
            return p;
        }
        const int s = Dichtstbij( Kaart::STEDEN, Kaart::AANTAL_STEDEN, x, z, d );
        if( s >= 0 ) { p.stad = Kaart::STEDEN[ s ].token; p.land = Kaart::STEDEN[ s ].land; p.afstandStadM = d; }
        if( Dichtstbij( Kaart::GARAGES, Kaart::AANTAL_GARAGES, x, z, d ) >= 0 ) p.afstandGarageM = d;
        const int pomp = Dichtstbij( Kaart::POMPEN, Kaart::AANTAL_POMPEN, x, z, d );
        if( pomp >= 0 ) { p.afstandPompM = d; p.bijPomp = d <= POMP_STRAAL_M; p.bijGarage = p.bijPomp && Kaart::POMPEN[ pomp ].garage; }
        return p;
    }

    bool Kaartdata::LaadJson( const std::string &tekst, std::string &fout, const bool forceer )
    {
        Tabel t;
        try
        {
            const nlohmann::json j = nlohmann::json::parse( tekst );
            t.versie = j.value( "versie", std::string() );
            if( t.versie.empty() ) { fout = "no version"; return false; }
            if( !j.contains( "steden" ) || !j[ "steden" ].is_array() ) { fout = "no cities"; return false; }
            for( const auto &s : j[ "steden" ] )
            {
                // [token, country, x, z]
                StadR r; int n = 0;
                for( const auto &v : s )
                {
                    if( n == 0 && v.is_string() ) r.token = v.get<std::string>();
                    else if( n == 1 && v.is_string() ) r.land = v.get<std::string>();
                    else if( n == 2 && v.is_number() ) r.x = v.get<float>();
                    else if( n == 3 && v.is_number() ) r.z = v.get<float>();
                    ++n;
                }
                if( n == 4 && !r.token.empty() ) t.steden.push_back( r );
            }
            if( j.contains( "garages" ) && j[ "garages" ].is_array() )
                for( const auto &g : j[ "garages" ] )
                {
                    PuntR r{ 0, 0 }; int n = 0;
                    for( const auto &v : g ) { if( !v.is_number() ) { n = -1; break; } if( n == 0 ) r.x = v.get<float>(); else if( n == 1 ) r.z = v.get<float>(); ++n; }
                    if( n == 2 ) t.garages.push_back( r );
                }
            if( j.contains( "pompen" ) && j[ "pompen" ].is_array() )
                for( const auto &pp : j[ "pompen" ] )
                {
                    PompR r{ 0, 0, false }; int n = 0;
                    for( const auto &v : pp ) { if( !v.is_number() ) { n = -1; break; } if( n == 0 ) r.x = v.get<float>(); else if( n == 1 ) r.z = v.get<float>(); else if( n == 2 ) r.garage = v.get<int>() != 0; ++n; }
                    if( n == 3 ) t.pompen.push_back( r );
                }
        }
        catch( ... ) { fout = "not valid JSON"; return false; }
        if( t.steden.size() < 50 ) { fout = "table too small"; return false; }  // a real map has hundreds

        std::lock_guard<std::mutex> lock( g_slot );
        const std::string actief = g_tabel.geladen ? g_tabel.versie : std::string( Kaart::KAART_VERSIE );
        if( !forceer && !VersieNieuwer( t.versie, actief ) ) { fout = "not newer than " + actief; return false; }
        t.geladen = true;
        g_tabel = std::move( t );
        return true;
    }

    bool Kaartdata::LaadBestand( const std::filesystem::path &pad, std::string &fout )
    {
        std::ifstream in( pad, std::ios::binary );
        if( !in ) { fout = "not readable"; return false; }
        std::stringstream ss; ss << in.rdbuf();
        return LaadJson( ss.str(), fout );
    }

    std::string Kaartdata::Versie() { std::lock_guard<std::mutex> lock( g_slot ); return g_tabel.geladen ? g_tabel.versie : std::string( Kaart::KAART_VERSIE ); }
    int Kaartdata::AantalSteden() { std::lock_guard<std::mutex> lock( g_slot ); return g_tabel.geladen ? static_cast<int>( g_tabel.steden.size() ) : Kaart::AANTAL_STEDEN; }
    std::string Kaartdata::Bron() { std::lock_guard<std::mutex> lock( g_slot ); return g_tabel.geladen ? "downloaded" : "embedded"; }

    namespace
    {
        std::thread g_updateThread;
        constexpr const wchar_t *UPDATE_HOST = L"raw.githubusercontent.com";
        constexpr const wchar_t *UPDATE_PAD = L"/Barendv8/CabNavi/main/data/kaartdata.json";
    }

    void Kaartdata::StartUpdate( const std::filesystem::path &cacheMap )
    {
        StopUpdate();
        try
        {
            g_updateThread = std::thread( [ cacheMap ]()
            {
                std::string body, fout;
                if( !HttpGet( UPDATE_HOST, UPDATE_PAD, body, fout ) )
                {
                    Logboek::Schrijf( "event", "map table update: not fetched (" + fout + "), keeping " + Kaartdata::Versie() );
                    return;
                }
                const std::string voor = Kaartdata::Versie();
                if( Kaartdata::LaadJson( body, fout ) )
                {
                    // Only now write: a table that was accepted. OUR file in AppData.
                    std::ofstream uit( cacheMap / "kaartdata.json", std::ios::binary | std::ios::trunc );
                    if( uit ) uit << body;
                    Logboek::Schrijf( "event", "map table update: " + voor + " -> " + Kaartdata::Versie() + " (" + std::to_string( Kaartdata::AantalSteden() ) + " cities)" );
                }
                else
                {
                    Logboek::Schrijf( "start", "map table update: " + fout + ", keeping " + voor );
                }
            } );
        }
        catch( ... )
        {
            Logboek::Schrijf( "event", "map table update: thread could not start" );
        }
    }

    void Kaartdata::StopUpdate()
    {
        if( g_updateThread.joinable() ) g_updateThread.join();
    }
}

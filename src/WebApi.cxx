#include "WebApi.hxx"

#include "Logboek.hxx"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>

#include <windows.h>
#include <winhttp.h>

#pragma comment( lib, "winhttp.lib" )

using json = nlohmann::json;

namespace Ritten
{
    namespace
    {
        constexpr int SERVERS_ELKE_SECONDEN = 60;
        constexpr int EVENEMENTEN_ELKE_SECONDEN = 15 * 60;
        constexpr int VTC_ELKE_SECONDEN = 15 * 60;
        constexpr int OPZOEKINGEN_PER_SECONDE = 1;
        constexpr int RUST_NA_FOUT_SECONDEN = 10;
        constexpr int RUST_NA_429_SECONDEN = 60;

        bool LeesJaNee( const json &object, const char *sleutel, bool standaard )
        {
            if( !object.contains( sleutel ) ) return standaard;
            const json &v = object[ sleutel ];
            if( v.is_boolean() ) return v.get<bool>();
            if( v.is_number() )  return v.get<double>() != 0.0;
            if( v.is_string() )
            {
                const std::string t = v.get<std::string>();
                return t == "true" || t == "1" || t == "yes";
            }
            return standaard;
        }

        int LeesGetal( const json &object, const char *sleutel, int standaard )
        {
            if( !object.contains( sleutel ) ) return standaard;
            const json &v = object[ sleutel ];
            if( v.is_number() ) return static_cast<int>( v.get<double>() );
            if( v.is_string() )
            {
                try { return std::stoi( v.get<std::string>() ); }
                catch( ... ) { return standaard; }
            }
            return standaard;
        }

        std::string LeesTekst( const json &object, const char *sleutel )
        {
            if( !object.contains( sleutel ) ) return {};
            const json &v = object[ sleutel ];
            if( v.is_string() ) return v.get<std::string>();
            if( v.is_number() ) return std::to_string( v.get<double>() );
            return {};
        }

        std::wstring NaarWide( const std::string &tekst )
        {
            if( tekst.empty() ) return {};
            int nodig = MultiByteToWideChar( CP_UTF8, 0, tekst.c_str(), (int)tekst.size(), nullptr, 0 );
            std::wstring uit( nodig, L'\0' );
            MultiByteToWideChar( CP_UTF8, 0, tekst.c_str(), (int)tekst.size(), uit.data(), nodig );
            return uit;
        }
    }

    WebApi::WebApi()
    {
        LaadSpelerCache();
    }

    WebApi::~WebApi()
    {
        m_stoppen = true;
        if( m_thread.joinable() ) m_thread.join();
    }

    void WebApi::StartDraadIndienNodig()
    {
        if( m_thread.joinable() ) return;
        m_stoppen = false;
        m_thread = std::thread( [ this ] { WerkLus(); } );
        Logboek::Schrijf( "event", "Web API thread started" );
    }

    void WebApi::ZetIngeschakeld( bool aan )
    {
        m_aan = aan;
        if( aan )
        {
            StartDraadIndienNodig();
            Logboek::Schrijf( "event", "Web API enabled" );
        }
        else
        {
            Logboek::Schrijf( "event", "Web API disabled" );
        }
    }

    std::vector<ServerInfo> WebApi::Servers() const
    {
        std::lock_guard<std::mutex> slot( m_slot );
        return m_servers;
    }

    std::vector<EvenementInfo> WebApi::Evenementen() const
    {
        std::lock_guard<std::mutex> slot( m_slot );
        return m_evenementen;
    }

    std::string WebApi::Status() const
    {
        std::lock_guard<std::mutex> slot( m_slot );
        return m_status;
    }

    void WebApi::ZetVtc( bool aan, int vtcId )
    {
        const bool veranderd = ( m_vtcAan.load() != aan ) || ( m_vtcId.load() != vtcId );
        m_vtcAan = aan;
        m_vtcId = vtcId;
        if( aan && vtcId > 0 ) StartDraadIndienNodig();

        if( veranderd )
        {
            m_vtcNuOphalen = true;
            std::lock_guard<std::mutex> slot( m_slot );
            m_vtcStatus = aan ? "wordt opgehaald..." : "uit";
            if( !aan || vtcId <= 0 )
            {
                m_vtc = VtcInfo{};
                m_vtcEvenementen.clear();
                m_vtcNieuws.clear();
            }
        }
    }

    VtcInfo WebApi::Vtc() const
    {
        std::lock_guard<std::mutex> slot( m_slot );
        return m_vtc;
    }

    std::vector<EvenementInfo> WebApi::VtcEvenementen() const
    {
        std::lock_guard<std::mutex> slot( m_slot );
        return m_vtcEvenementen;
    }

    std::vector<VtcNieuwsInfo> WebApi::VtcNieuws() const
    {
        std::lock_guard<std::mutex> slot( m_slot );
        return m_vtcNieuws;
    }

    std::string WebApi::VtcStatus() const
    {
        std::lock_guard<std::mutex> slot( m_slot );
        return m_vtcStatus;
    }

    void WebApi::WerkLus()
    {
        int secondenSindsServers = SERVERS_ELKE_SECONDEN;
        int secondenSindsEvenementen = EVENEMENTEN_ELKE_SECONDEN;
        int secondenSindsVtc = VTC_ELKE_SECONDEN;

        while( !m_stoppen )
        {
            if( m_aan )
            {
                if( secondenSindsServers >= SERVERS_ELKE_SECONDEN )
                {
                    std::string antwoord;
                    if( HaalOp( "/v2/servers", antwoord ) ) VerwerkServers( antwoord );
                    secondenSindsServers = 0;
                }
                if( secondenSindsEvenementen >= EVENEMENTEN_ELKE_SECONDEN )
                {
                    std::string antwoord;
                    if( HaalOp( "/v2/events", antwoord ) ) VerwerkEvenementen( antwoord );
                    secondenSindsEvenementen = 0;
                }
            }

            const int vtcId = m_vtcId.load();
            if( m_vtcAan && vtcId > 0 && m_rustSeconden <= 0 )
            {
                bool nu = m_vtcNuOphalen.exchange( false );
                if( nu || secondenSindsVtc >= VTC_ELKE_SECONDEN )
                {
                    const std::string basis = "/v2/vtc/" + std::to_string( vtcId );
                    std::string antwoord;
                    if( HaalOp( basis, antwoord ) )
                    {
                        VerwerkVtc( antwoord );
                    }
                    else
                    {
                        std::lock_guard<std::mutex> slot( m_slot );
                        m_vtcStatus = m_status;
                    }
                    if( HaalOp( basis + "/events", antwoord ) ) VerwerkVtcEvenementen( antwoord );
                    if( HaalOp( basis + "/news", antwoord ) ) VerwerkVtcNieuws( antwoord );

                    if( HaalOp( basis + "/events/attending", antwoord ) )
                    {
                        VerwerkAangemeld( antwoord, true );
                    }
                    secondenSindsVtc = 0;
                }
            }

            if( m_vtcAan && m_rustSeconden <= 0 )
            {
                for( int n = 0; n < OPZOEKINGEN_PER_SECONDE; ++n )
                {
                    std::uint64_t id = 0;
                    {
                        std::lock_guard<std::mutex> slot( m_slot );
                        if( m_wachtrij.empty() ) break;
                        id = m_wachtrij.front();
                        m_wachtrij.pop_front();
                        m_bezig.insert( id );
                    }

                    std::string antwoord;
                    const bool gelukt = HaalOp( "/v2/player/" + std::to_string( id ), antwoord );
                    if( gelukt )
                    {
                        VerwerkSpelerVtc( id, antwoord );
                    }

                    {
                        std::lock_guard<std::mutex> slot( m_slot );
                        m_bezig.erase( id );
                        if( !gelukt )
                        {
                            m_wachtrij.push_back( id );
                            m_rustSeconden = RUST_NA_FOUT_SECONDEN;
                        }
                    }

                    if( !gelukt )
                    {
                        Logboek::Schrijf( "vtc", "lookup failed for player "
                                                     + std::to_string( id )
                                                     + " -- pausing briefly" );
                        break;
                    }
                    if( m_stoppen ) break;
                }
                SlaSpelerCacheOp();
            }

            if( m_rustSeconden > 0 ) --m_rustSeconden;

            std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
            ++secondenSindsServers;
            ++secondenSindsEvenementen;
            ++secondenSindsVtc;
        }
    }

    bool WebApi::HaalOp( const std::string &pad, std::string &antwoordUit )
    {
        HINTERNET sessie = WinHttpOpen( L"CabNavi/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
        if( !sessie )
        {
            std::lock_guard<std::mutex> slot( m_slot );
            m_status = "kon geen verbinding openen";
            return false;
        }

        HINTERNET verbinding = WinHttpConnect( sessie, L"api.truckersmp.com", 443, 0 );
        if( !verbinding )
        {
            WinHttpCloseHandle( sessie );
            std::lock_guard<std::mutex> slot( m_slot );
            m_status = "kon api.truckersmp.com niet bereiken";
            return false;
        }

        const std::wstring wpad = NaarWide( pad );
        HINTERNET request = WinHttpOpenRequest( verbinding, L"GET", wpad.c_str(), nullptr,
                                                 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                 WINHTTP_FLAG_SECURE );
        bool gelukt = false;
        if( request &&
            WinHttpSendRequest( request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0 ) &&
            WinHttpReceiveResponse( request, nullptr ) )
        {
            DWORD status = 0, maat = sizeof( status );
            WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                  WINHTTP_HEADER_NAME_BY_INDEX, &status, &maat, WINHTTP_NO_HEADER_INDEX );

            if( status == 429 )
            {
                m_rustSeconden = RUST_NA_429_SECONDEN;
                Logboek::Schrijf( "vtc", "status code 429 -- asking nothing for a minute" );
            }

            if( status == 200 )
            {
                std::string body;
                DWORD beschikbaar = 0;
                do
                {
                    beschikbaar = 0;
                    if( !WinHttpQueryDataAvailable( request, &beschikbaar ) ) break;
                    if( beschikbaar == 0 ) break;

                    std::string blok( beschikbaar, '\0' );
                    DWORD gelezen = 0;
                    if( !WinHttpReadData( request, blok.data(), beschikbaar, &gelezen ) ) break;
                    body.append( blok.data(), gelezen );
                } while( beschikbaar > 0 );

                Logboek::Schrijf( "vtc", pad + " -> " + std::to_string( body.size() ) + " bytes" );
                antwoordUit = std::move( body );
                gelukt = true;
            }
            else
            {
                std::lock_guard<std::mutex> slot( m_slot );
                m_status = "server gaf statuscode " + std::to_string( status );
            }
        }
        else
        {
            std::lock_guard<std::mutex> slot( m_slot );
            m_status = "verzoek mislukt (foutcode " + std::to_string( GetLastError() ) + ")";
        }

        if( request ) WinHttpCloseHandle( request );
        WinHttpCloseHandle( verbinding );
        WinHttpCloseHandle( sessie );
        return gelukt;
    }

    void WebApi::VerwerkServers( const std::string &tekst )
    {
        if( tekst.empty() )
        {
            Logboek::Schrijf( "ERROR", "server list: empty answer received" );
            std::lock_guard<std::mutex> slot( m_slot );
            m_status = "leeg antwoord";
            return;
        }

        try
        {
            const json j = json::parse( tekst );
            if( LeesJaNee( j, "error", true ) ) return;
            if( !j.contains( "response" ) || !j[ "response" ].is_array() ) return;

            std::vector<ServerInfo> nieuw;
            for( const auto &item : j[ "response" ] )
            {
                ServerInfo s;
                s.naam = LeesTekst( item, "name" );
                s.spel = LeesTekst( item, "game" );
                s.spelers = LeesGetal( item, "players", 0 );
                s.maxSpelers = LeesGetal( item, "maxplayers", 0 );
                s.wachtrij = LeesGetal( item, "queue", 0 );
                s.online = LeesJaNee( item, "online", false );
                s.collisions = LeesJaNee( item, "collisions", false );
                s.snelheidsbegrenzer = LeesGetal( item, "speedlimiter", 0 ) != 0;
                nieuw.push_back( std::move( s ) );
            }

            std::lock_guard<std::mutex> slot( m_slot );
            m_servers = std::move( nieuw );
            m_status = "bijgewerkt";
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "ERROR", std::string( "server list not readable: " ) + Logboek::KorteFout( ex.what() )
                                          + " | start of answer: " + tekst.substr( 0, 200 ) );
            std::lock_guard<std::mutex> slot( m_slot );
            m_status = std::string( "antwoord niet te lezen (" ) + Logboek::KorteFout( ex.what() ) + ")";
        }
    }

    void WebApi::VerwerkEvenementen( const std::string &tekst )
    {
        try
        {
            const json j = json::parse( tekst );
            if( LeesJaNee( j, "error", true ) ) return;
            if( !j.contains( "response" ) ) return;

            std::vector<EvenementInfo> nieuw;
            const auto &respons = j[ "response" ];

            for( const char *groep : { "featured", "today", "upcoming" } )
            {
                if( !respons.contains( groep ) ) continue;
                for( const auto &item : respons[ groep ] )
                {
                    EvenementInfo e;
                    e.id = LeesGetal( item, "id", 0 );
            e.naam = LeesTekst( item, "name" );
                    e.startTijd = LeesTekst( item, "start_at" );
                    e.spel = LeesTekst( item, "game" );
                    if( item.contains( "departure" ) && item[ "departure" ].is_object() )
                    {
                        e.vertrek = LeesTekst( item[ "departure" ], "city" );
                    }
                    if( item.contains( "arrive" ) && item[ "arrive" ].is_object() )
                    {
                        e.aankomst = LeesTekst( item[ "arrive" ], "city" );
                    }
                    if( item.contains( "server" ) && item[ "server" ].is_object() )
                    {
                        e.server = LeesTekst( item[ "server" ], "name" );
                    }
                    nieuw.push_back( std::move( e ) );

                    if( nieuw.size() >= 8 ) break;
                }
                if( nieuw.size() >= 8 ) break;
            }

            std::lock_guard<std::mutex> slot( m_slot );
            m_evenementen = std::move( nieuw );
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "ERROR", std::string( "events not readable: " ) + Logboek::KorteFout( ex.what() )
                                          + " | start of answer: " + tekst.substr( 0, 200 ) );
        }
    }

    void WebApi::VerwerkVtc( const std::string &tekst )
    {
        try
        {
            const json j = json::parse( tekst );
            if( LeesJaNee( j, "error", true ) )
            {
                std::lock_guard<std::mutex> slot( m_slot );
                m_vtc = VtcInfo{};
                m_vtcStatus = "VTC niet gevonden -- klopt het nummer?";
                return;
            }
            if( !j.contains( "response" ) || !j[ "response" ].is_object() ) return;
            const auto &r = j[ "response" ];

            VtcInfo v;
            v.geldig = true;
            v.naam = LeesTekst( r, "name" );
            v.tag = LeesTekst( r, "tag" );
            v.slogan = LeesTekst( r, "slogan" );
            v.taal = LeesTekst( r, "language" );
            v.werving = LeesTekst( r, "recruitment" );
            v.leden = static_cast<int>( LeesGetal( r, "members_count", 0 ) );
            v.geverifieerd = LeesJaNee( r, "verified", false );

            std::lock_guard<std::mutex> slot( m_slot );
            m_vtc = std::move( v );
            m_vtcStatus = "opgehaald";
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "ERROR", std::string( "VTC not readable: " ) + Logboek::KorteFout( ex.what() )
                                          + " | start of answer: " + tekst.substr( 0, 200 ) );
        }
    }

    namespace
    {
        std::string NuUtcTekst()
        {
            const std::time_t nu = std::time( nullptr );
            std::tm tmBuf{};
        #if defined( _WIN32 )
            gmtime_s( &tmBuf, &nu );
        #else
            gmtime_r( &nu, &tmBuf );
        #endif
            char buf[ 32 ];
            std::snprintf( buf, sizeof( buf ), "%04d-%02d-%02d %02d:%02d:%02d",
                            tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
                            tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec );
            return buf;
        }

        EvenementInfo LeesEvenement( const json &item )
        {
            EvenementInfo e;
            e.id = LeesGetal( item, "id", 0 );
            e.naam = LeesTekst( item, "name" );
            e.startTijd = LeesTekst( item, "start_at" );
            e.spel = LeesTekst( item, "game" );
            if( item.contains( "departure" ) && item[ "departure" ].is_object() )
                e.vertrek = LeesTekst( item[ "departure" ], "city" );
            if( item.contains( "arrive" ) && item[ "arrive" ].is_object() )
                e.aankomst = LeesTekst( item[ "arrive" ], "city" );
            if( item.contains( "server" ) && item[ "server" ].is_object() )
                e.server = LeesTekst( item[ "server" ], "name" );
            return e;
        }
    }

    void WebApi::ZetEigenAccount( std::uint64_t accountId )
    {
        m_eigenAccount = accountId;
    }

    std::vector<EvenementInfo> WebApi::AangemeldeEvenementen() const
    {
        std::lock_guard<std::mutex> slot( m_slot );
        return m_aangemeldVtc;
    }

    std::vector<EvenementInfo> WebApi::VtcAangemeld() const
    {
        std::lock_guard<std::mutex> slot( m_slot );
        return m_aangemeldVtc;
    }

    void WebApi::VerwerkAangemeld( const std::string &tekst, bool viaVtc )
    {
        try
        {
            const json j = json::parse( tekst );
            if( LeesJaNee( j, "error", true ) ) return;
            if( !j.contains( "response" ) || !j[ "response" ].is_array() ) return;

            const std::string nuUtc = NuUtcTekst();
            std::vector<EvenementInfo> nieuw;
            for( const auto &item : j[ "response" ] )
            {
                EvenementInfo e = LeesEvenement( item );
                if( !e.startTijd.empty() && e.startTijd < nuUtc ) continue;
                nieuw.push_back( std::move( e ) );
                if( nieuw.size() >= 20 ) break;
            }

            Logboek::Schrijf( "vtc", std::string( viaVtc ? "VTC signed up for " : "you signed up for " )
                                          + std::to_string( nieuw.size() ) + " convoy(s)" );

            std::lock_guard<std::mutex> slot( m_slot );
            m_aangemeldVtc = std::move( nieuw );
            (void)viaVtc;
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "ERROR", std::string( "sign-ups not readable: " ) + Logboek::KorteFout( ex.what() ) );
        }
    }

    void WebApi::VerwerkVtcEvenementen( const std::string &tekst )
    {
        try
        {
            const json j = json::parse( tekst );
            if( LeesJaNee( j, "error", true ) ) return;
            if( !j.contains( "response" ) ) return;

            const auto &respons = j[ "response" ];
            if( !respons.is_array() ) return;

            std::string nuUtc;
            {
                const std::time_t nu = std::time( nullptr );
                std::tm tmBuf{};
            #if defined( _WIN32 )
                gmtime_s( &tmBuf, &nu );
            #else
                gmtime_r( &nu, &tmBuf );
            #endif
                char buf[ 32 ];
                std::snprintf( buf, sizeof( buf ), "%04d-%02d-%02d %02d:%02d:%02d",
                                tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
                                tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec );
                nuUtc = buf;
            }

            std::vector<EvenementInfo> nieuw;
            for( const auto &item : respons )
            {
                EvenementInfo e;
                e.id = LeesGetal( item, "id", 0 );
            e.naam = LeesTekst( item, "name" );
                e.startTijd = LeesTekst( item, "start_at" );

                if( !e.startTijd.empty() && e.startTijd < nuUtc ) continue;

                e.spel = LeesTekst( item, "game" );
                if( item.contains( "departure" ) && item[ "departure" ].is_object() )
                {
                    e.vertrek = LeesTekst( item[ "departure" ], "city" );
                }
                if( item.contains( "arrive" ) && item[ "arrive" ].is_object() )
                {
                    e.aankomst = LeesTekst( item[ "arrive" ], "city" );
                }
                if( item.contains( "server" ) && item[ "server" ].is_object() )
                {
                    e.server = LeesTekst( item[ "server" ], "name" );
                }
                nieuw.push_back( std::move( e ) );
                if( nieuw.size() >= 60 ) break;
            }

            std::sort( nieuw.begin(), nieuw.end(),
                        []( const EvenementInfo &a, const EvenementInfo &b )
                        { return a.startTijd < b.startTijd; } );
            if( nieuw.size() > 8 ) nieuw.resize( 8 );

            std::lock_guard<std::mutex> slot( m_slot );
            m_vtcEvenementen = std::move( nieuw );
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "ERROR", std::string( "VTC events not readable: " ) + Logboek::KorteFout( ex.what() )
                                          + " | start of answer: " + tekst.substr( 0, 200 ) );
        }
    }

    void WebApi::VerwerkVtcNieuws( const std::string &tekst )
    {
        try
        {
            const json j = json::parse( tekst );
            if( LeesJaNee( j, "error", true ) ) return;
            if( !j.contains( "response" ) || !j[ "response" ].is_object() ) return;

            const auto &respons = j[ "response" ];
            if( !respons.contains( "news" ) || !respons[ "news" ].is_array() ) return;

            std::vector<VtcNieuwsInfo> nieuw;
            for( const auto &item : respons[ "news" ] )
            {
                VtcNieuwsInfo n;
                n.titel = LeesTekst( item, "title" );
                n.datum = LeesTekst( item, "published_at" );
                n.auteur = LeesTekst( item, "author" );
                n.vastgezet = LeesJaNee( item, "pinned", false );
                nieuw.push_back( std::move( n ) );
                if( nieuw.size() >= 6 ) break;
            }

            std::lock_guard<std::mutex> slot( m_slot );
            m_vtcNieuws = std::move( nieuw );
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "ERROR", std::string( "VTC news not readable: " ) + Logboek::KorteFout( ex.what() )
                                          + " | start of answer: " + tekst.substr( 0, 200 ) );
        }
    }

    void WebApi::MeldSpelerAan( std::uint64_t accountId, bool voorrang )
    {
        if( accountId == 0 ) return;
        if( !m_vtcAan ) return;

        std::lock_guard<std::mutex> slot( m_slot );
        if( m_spelerVtc.find( accountId ) != m_spelerVtc.end() ) return;
        if( m_bezig.find( accountId ) != m_bezig.end() ) return;
        for( std::uint64_t id : m_wachtrij ) if( id == accountId ) return;

        if( m_wachtrij.size() >= 200 ) return;
        if( voorrang ) m_wachtrij.push_front( accountId );
        else           m_wachtrij.push_back( accountId );
    }

    int WebApi::SpelerVtcId( std::uint64_t accountId ) const
    {
        std::lock_guard<std::mutex> slot( m_slot );
        const auto it = m_spelerVtc.find( accountId );
        return ( it == m_spelerVtc.end() ) ? -1 : it->second.vtcId;
    }

    int WebApi::SpelerPatron( std::uint64_t accountId ) const
    {
        std::lock_guard<std::mutex> slot( m_slot );
        const auto it = m_spelerVtc.find( accountId );
        return ( it == m_spelerVtc.end() ) ? -1 : ( it->second.patron ? 1 : 0 );
    }

    int WebApi::SpelerIsPatron( std::uint64_t accountId ) const
    {
        std::lock_guard<std::mutex> slot( m_slot );
        const auto it = m_spelerVtc.find( accountId );
        return ( it == m_spelerVtc.end() ) ? -1 : ( it->second.patron ? 1 : 0 );
    }

    void WebApi::OpzoekStand( int &opgezochtUit, int &inRijUit ) const
    {
        std::lock_guard<std::mutex> slot( m_slot );
        opgezochtUit = static_cast<int>( m_spelerVtc.size() );
        inRijUit = static_cast<int>( m_wachtrij.size() + m_bezig.size() );
    }

    void WebApi::VerwerkSpelerVtc( std::uint64_t accountId, const std::string &tekst )
    {
        int vtcId = 0;
        bool patron = false;
        try
        {
            const json j = json::parse( tekst );
            if( !LeesJaNee( j, "error", true ) && j.contains( "response" ) &&
                j[ "response" ].is_object() )
            {
                const auto &r = j[ "response" ];
                if( r.contains( "vtc" ) && r[ "vtc" ].is_object() )
                {
                    const auto &v = r[ "vtc" ];
                    if( LeesJaNee( v, "inVTC", false ) ) vtcId = LeesGetal( v, "id", 0 );
                }
                if( r.contains( "patreon" ) && r[ "patreon" ].is_object() )
                {
                    const auto &p = r[ "patreon" ];
                    patron = LeesJaNee( p, "isPatron", false ) && LeesJaNee( p, "active", false );
                }
            }
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "ERROR", std::string( "player VTC not readable: " ) + Logboek::KorteFout( ex.what() ) );
            return;
        }

        Logboek::Schrijf( "vtc", "player " + std::to_string( accountId )
                                     + " -> vtc " + std::to_string( vtcId )
                                     + " patron " + ( patron ? "yes" : "no" ) );

        std::lock_guard<std::mutex> slot( m_slot );
        SpelerGegevens g;
        g.vtcId = vtcId;
        g.patron = patron;
        m_spelerVtc[ accountId ] = g;
        m_cacheGewijzigd = true;
    }

    std::filesystem::path WebApi::CacheBestand()
    {
        std::filesystem::path pad;
        if( const char *appdata = std::getenv( "APPDATA" ) ) pad = appdata;
        else pad = std::filesystem::current_path();
        pad /= "CabNavi";
        std::error_code ec;
        std::filesystem::create_directories( pad, ec );
        return pad / "spelers_vtc.json";
    }

    void WebApi::LaadSpelerCache()
    {
        try
        {
            std::ifstream in( CacheBestand() );
            if( !in ) return;
            json j;
            in >> j;
            if( !j.is_object() ) return;

            std::lock_guard<std::mutex> slot( m_slot );
            for( auto it = j.begin(); it != j.end(); ++it )
            {
                const std::uint64_t id = std::strtoull( it.key().c_str(), nullptr, 10 );
                SpelerGegevens g;
                if( it.value().is_number() )
                {
                    g.vtcId = it.value().get<int>();
                }
                else if( it.value().is_object() )
                {
                    g.vtcId = it.value().value( "v", 0 );
                    g.patron = it.value().value( "p", false );
                }
                else continue;
                m_spelerVtc[ id ] = g;
            }
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "event", std::string( "spelers_vtc.json not readable: " ) + Logboek::KorteFout( ex.what() ) );
        }
    }

    void WebApi::SlaSpelerCacheOp()
    {
        try
        {
            json j;
            {
                std::lock_guard<std::mutex> slot( m_slot );
                if( !m_cacheGewijzigd ) return;
                m_cacheGewijzigd = false;
                for( const auto &paar : m_spelerVtc )
                {
                    json e;
                    e[ "v" ] = paar.second.vtcId;
                    e[ "p" ] = paar.second.patron;
                    j[ std::to_string( paar.first ) ] = e;
                }
            }
            std::ofstream uit( CacheBestand() );
            if( uit ) uit << j.dump();
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "event", std::string( "spelers_vtc.json not writable: " ) + Logboek::KorteFout( ex.what() ) );
        }
    }
}

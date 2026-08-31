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
        // Hoe vaak we opnieuw vragen. Serverstatus verandert langzaam,
        // evenementen nog langzamer -- dus geen reden om te haasten.
        constexpr int SERVERS_ELKE_SECONDEN = 60;
        constexpr int EVENEMENTEN_ELKE_SECONDEN = 15 * 60;
        // VTC-gegevens veranderen nog minder vaak dan evenementen: een naam
        // of ledenaantal blijft dagen hetzelfde. Elk kwartier is ruim zat.
        constexpr int VTC_ELKE_SECONDEN = 15 * 60;
        // Hoeveel spelers we per seconde opzoeken. Er staat geen limiet
        // gedocumenteerd, dus dit blijft behoudend -- maar bij 74 spelers in
        // bereik duurde twee per seconde bijna veertig seconden voordat de
        // laatste aan de beurt was, en dan lijkt het alsof het niet werkt.
        constexpr int OPZOEKINGEN_PER_SECONDE = 1;
        // Hoe lang we niets vragen na een mislukking.
        constexpr int RUST_NA_FOUT_SECONDEN = 10;
        // Na een echte 429 een stuk langer wachten. Doorduwen maakt het
        // alleen erger en blokkeert ook de gewone VTC-gegevens.
        constexpr int RUST_NA_429_SECONDEN = 60;

        // Ja/nee-waarde uitlezen die ook TEKST of een GETAL mag zijn.
        //
        // Nodig omdat de API zich niet aan zijn eigen documentatie houdt: het
        // schema zegt dat "error" een boolean is, maar de server stuurt de
        // TEKST "false". Dat gooide een type-fout en daardoor kwam de hele
        // serverlijst niet door.
        //
        // Zo'n verschil kan bij elk veld opduiken, dus lezen we alle ja/nee-
        // velden voortaan op deze manier: liever soepel dan stuk.
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

        // Zelfde gedachte voor getallen: accepteer ook een getal in tekstvorm.
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

        // En voor tekst: een getal of null mag ook, dan maken we er tekst van.
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
        // Wat we vorige keer al opgezocht hebben meteen terughalen, zodat je
        // na een herstart niet opnieuw begint.
        LaadSpelerCache();
    }

    WebApi::~WebApi()
    {
        m_stoppen = true;
        if( m_thread.joinable() ) m_thread.join();
    }

    // De werkthread moet draaien zodra ER IETS aan staat -- serverstatus OF
    // de VTC-kant. Stond dit alleen in ZetIngeschakeld, dan bleef de VTC
    // eeuwig op "wordt opgehaald" staan als je de serverstatus uit had.
    void WebApi::StartDraadIndienNodig()
    {
        if( m_thread.joinable() ) return;
        m_stoppen = false;
        m_thread = std::thread( [ this ] { WerkLus(); } );
        Logboek::Schrijf( "gebeurt", "Web API-thread gestart" );
    }

    void WebApi::ZetIngeschakeld( bool aan )
    {
        m_aan = aan;
        if( aan )
        {
            StartDraadIndienNodig();
            Logboek::Schrijf( "gebeurt", "Web API ingeschakeld" );
        }
        else
        {
            Logboek::Schrijf( "gebeurt", "Web API uitgeschakeld" );
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
            // Meteen opnieuw ophalen in plaats van tot het volgende kwartier
            // wachten -- anders lijkt het alsof je instelling niets doet.
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
        // Meteen bij het aanzetten een keer ophalen, daarna op het ritme
        // hierboven. De tellers staan expres op nul zodat de eerste ronde
        // direct gebeurt.
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

            // VTC staat los van de rest: je kunt de serverstatus uit hebben
            // en toch je eigen bedrijf volgen, of andersom. Zelfde rustige
            // ritme als de evenementen -- dit is geen telemetrie.
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
                        // Anders blijft er "wordt opgehaald" staan zonder dat
                        // je weet waarom. HaalOp heeft m_status al gevuld met
                        // de reden; die laten we hier ook zien.
                        std::lock_guard<std::mutex> slot( m_slot );
                        m_vtcStatus = m_status;
                    }
                    if( HaalOp( basis + "/events", antwoord ) ) VerwerkVtcEvenementen( antwoord );
                    if( HaalOp( basis + "/news", antwoord ) ) VerwerkVtcNieuws( antwoord );

                    // Waar je VTC zich voor heeft aangemeld.
                    if( HaalOp( basis + "/events/attending", antwoord ) )
                    {
                        VerwerkAangemeld( antwoord, true );
                    }
                    secondenSindsVtc = 0;
                }
            }

            // Wachtrij met speler-opzoekingen afwerken: hooguit een paar per
            // seconde. Bij vijftig spelers in beeld is iedereen dan binnen
            // een halve minuut bekend, zonder de API te bestoken. Wat nog
            // niet opgezocht is valt zolang terug op de tag, dus je ziet
            // meteen iets en het wordt vanzelf preciezer.
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
                        m_bezig.insert( id ); // zolang niet opnieuw aanmelden
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
                            // Achteraan terug in de rij, en even helemaal
                            // stoppen. Meteen opnieuw proberen maakt het
                            // alleen erger als er een limiet in het spel is.
                            m_wachtrij.push_back( id );
                            m_rustSeconden = RUST_NA_FOUT_SECONDEN;
                        }
                    }

                    if( !gelukt )
                    {
                        Logboek::Schrijf( "vtc", "opzoeken mislukt voor speler "
                                                     + std::to_string( id )
                                                     + " -- even pauze" );
                        break;
                    }
                    if( m_stoppen ) break;
                }
                SlaSpelerCacheOp(); // schrijft alleen als er iets veranderd is
            }

            // Rustteller loopt altijd af, ongeacht wat er verder gebeurt.
            if( m_rustSeconden > 0 ) --m_rustSeconden;

            // In stapjes van een seconde wachten, zodat afsluiten niet een
            // minuut hoeft te duren.
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
                // "Te veel verzoeken". GEMETEN 30-08: dit gebeurde echt, en
                // niet alleen bij het opzoeken van spelers -- ook het gewone
                // VTC-ophalen kreeg een 429 omdat het budget al op was.
                // Daarom een LANGE pauze voor alles wat deze klasse doet,
                // niet alleen voor de opzoekingen.
                m_rustSeconden = RUST_NA_429_SECONDEN;
                Logboek::Schrijf( "vtc", "statuscode 429 -- een minuut niets vragen" );
            }

            if( status == 200 )
            {
                // In brokken lezen: het antwoord kan tientallen kilobytes zijn.
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

                Logboek::Schrijf( "gebeurt", pad + " -> " + std::to_string( body.size() ) + " bytes" );
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
            Logboek::Schrijf( "FOUT", "serverlijst: leeg antwoord ontvangen" );
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
            // Ook de EERSTE 200 TEKENS meeloggen. Zonder dat weet je alleen
            // dat het misging, niet wat er binnenkwam -- en dan blijft het
            // gissen. Vaak is het een foutpagina in plaats van JSON.
            Logboek::Schrijf( "FOUT", std::string( "serverlijst niet te lezen: " ) + ex.what()
                                          + " | begin antwoord: " + tekst.substr( 0, 200 ) );
            std::lock_guard<std::mutex> slot( m_slot );
            m_status = std::string( "antwoord niet te lezen (" ) + ex.what() + ")";
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

            // De API levert drie groepen; alleen wat nog komt is interessant.
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

                    // Meer dan een handvol heeft geen zin op een HUD.
                    if( nieuw.size() >= 8 ) break;
                }
                if( nieuw.size() >= 8 ) break;
            }

            std::lock_guard<std::mutex> slot( m_slot );
            m_evenementen = std::move( nieuw );
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "FOUT", std::string( "evenementen niet te lezen: " ) + ex.what()
                                          + " | begin antwoord: " + tekst.substr( 0, 200 ) );
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
            Logboek::Schrijf( "FOUT", std::string( "VTC niet te lezen: " ) + ex.what()
                                          + " | begin antwoord: " + tekst.substr( 0, 200 ) );
        }
    }

    namespace
    {
        // Huidige tijd in dezelfde vorm als de API hem geeft. In die vorm is
        // een tekstvergelijking ook een datumvergelijking.
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

        // Eén evenement uit de JSON halen; de vorm is overal dezelfde.
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

    // Alleen nog de VTC-kant. Jouw eigen convooien vink je zelf aan en
    // worden lokaal bewaard, want de API geeft ze niet: /events/user/{id}
    // levert wat je zelf hebt AANGEMAAKT, niet waar je je voor opgaf
    // (gemeten 30-08: leeg antwoord terwijl er wel aanmeldingen waren).
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
                if( !e.startTijd.empty() && e.startTijd < nuUtc ) continue; // al geweest
                nieuw.push_back( std::move( e ) );
                if( nieuw.size() >= 20 ) break;
            }

            Logboek::Schrijf( "vtc", std::string( viaVtc ? "VTC meldt zich aan voor " : "jij aangemeld voor " )
                                          + std::to_string( nieuw.size() ) + " convooi(en)" );

            std::lock_guard<std::mutex> slot( m_slot );
            m_aangemeldVtc = std::move( nieuw );
            (void)viaVtc;
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "FOUT", std::string( "aanmeldingen niet te lezen: " ) + ex.what() );
        }
    }

    void WebApi::VerwerkVtcEvenementen( const std::string &tekst )
    {
        try
        {
            const json j = json::parse( tekst );
            if( LeesJaNee( j, "error", true ) ) return;
            if( !j.contains( "response" ) ) return;

            // Hier is "response" rechtstreeks een lijst, anders dan bij de
            // algemene evenementen (die drie groepen heeft).
            const auto &respons = j[ "response" ];
            if( !respons.is_array() ) return;

            // Alleen wat NOG KOMT. Dit endpoint geeft alles wat een VTC ooit
            // georganiseerd heeft; de eerste acht daarvan zijn dus de OUDSTE
            // (gezien: convooien uit 2019 en 2021 in beeld). De API kent geen
            // filter, dus we vergelijken zelf met de klok van nu.
            //
            // De tijd komt als "2026-09-05 17:00:00" binnen, en in die vorm
            // is een gewone tekstvergelijking ook een datumvergelijking --
            // jaar staat vooraan, dan maand, dan dag. Geen datum-ontleding
            // nodig, en dus ook niets dat stuk kan op een rare notatie.
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

                // Al geweest? Overslaan. Een lege tijd laten we staan -- dan
                // weten we het niet, en is weglaten erger dan tonen.
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
                if( nieuw.size() >= 60 ) break; // bovengrens tegen eindeloze lijsten
            }

            // Eerstvolgende bovenaan.
            std::sort( nieuw.begin(), nieuw.end(),
                        []( const EvenementInfo &a, const EvenementInfo &b )
                        { return a.startTijd < b.startTijd; } );
            if( nieuw.size() > 8 ) nieuw.resize( 8 );

            std::lock_guard<std::mutex> slot( m_slot );
            m_vtcEvenementen = std::move( nieuw );
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "FOUT", std::string( "VTC-evenementen niet te lezen: " ) + ex.what()
                                          + " | begin antwoord: " + tekst.substr( 0, 200 ) );
        }
    }

    void WebApi::VerwerkVtcNieuws( const std::string &tekst )
    {
        try
        {
            const json j = json::parse( tekst );
            if( LeesJaNee( j, "error", true ) ) return;
            if( !j.contains( "response" ) || !j[ "response" ].is_object() ) return;

            // Hier zit de lijst nog een laagje dieper, in "news".
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
            Logboek::Schrijf( "FOUT", std::string( "VTC-nieuws niet te lezen: " ) + ex.what()
                                          + " | begin antwoord: " + tekst.substr( 0, 200 ) );
        }
    }

    // --- VTC per speler --------------------------------------------------
    void WebApi::MeldSpelerAan( std::uint64_t accountId, bool voorrang )
    {
        if( accountId == 0 ) return;
        if( !m_vtcAan ) return;

        std::lock_guard<std::mutex> slot( m_slot );
        // Al bekend? Dan niets doen. Al in de rij? Ook niets doen.
        if( m_spelerVtc.find( accountId ) != m_spelerVtc.end() ) return;
        if( m_bezig.find( accountId ) != m_bezig.end() ) return;
        for( std::uint64_t id : m_wachtrij ) if( id == accountId ) return;

        // Bovengrens op de rij: bij een druk evenement wil je niet dat er
        // honderden opzoekingen blijven staan die je toch niet meer nodig
        // hebt tegen de tijd dat ze aan de beurt zijn.
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
        int vtcId = 0;      // 0 = zit niet bij een VTC; dat onthouden we ook
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
                // Patron uit dezelfde opvraging. "active" telt: iemand die
                // ooit gedoneerd heeft maar nu niet meer, is geen patron.
                if( r.contains( "patreon" ) && r[ "patreon" ].is_object() )
                {
                    const auto &p = r[ "patreon" ];
                    patron = LeesJaNee( p, "isPatron", false ) && LeesJaNee( p, "active", false );
                }
            }
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "FOUT", std::string( "speler-VTC niet te lezen: " ) + ex.what() );
            return; // niet onthouden, dan proberen we het later nog eens
        }

        // In het logboek, zodat in een ritje terug te zien is WELKE spelers
        // zijn opgezocht en welk VTC-nummer eruit kwam. Anders blijft het
        // gissen of het opzoeken werkt of dat de vergelijking niet klopt.
        Logboek::Schrijf( "vtc", "speler " + std::to_string( accountId )
                                     + " -> vtc " + std::to_string( vtcId )
                                     + " patron " + ( patron ? "ja" : "nee" ) );

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
                    g.vtcId = it.value().get<int>(); // oud formaat: alleen het nummer
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
            Logboek::Schrijf( "gebeurt", std::string( "spelers_vtc.json niet te lezen: " ) + ex.what() );
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
            Logboek::Schrijf( "gebeurt", std::string( "spelers_vtc.json niet te schrijven: " ) + ex.what() );
        }
    }
}

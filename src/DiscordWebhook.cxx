#include "DiscordWebhook.hxx"

#include "BoeteTekst.hxx"

#include <nlohmann/json.hpp>

#include <windows.h>
#include <winhttp.h>
#pragma comment( lib, "winhttp.lib" )

#include <cstdio>
#include <cstdlib>
#include <fstream>

using json = nlohmann::json;

namespace Ritten
{
    static std::wstring NaarWide( const std::string &tekst )
    {
        if( tekst.empty() ) return std::wstring();
        int nodig = MultiByteToWideChar( CP_UTF8, 0, tekst.c_str(), -1, nullptr, 0 );
        std::wstring resultaat( nodig > 0 ? nodig - 1 : 0, L'\0' );
        if( nodig > 0 )
        {
            MultiByteToWideChar( CP_UTF8, 0, tekst.c_str(), -1, &resultaat[ 0 ], nodig );
        }
        return resultaat;
    }

    std::filesystem::path DiscordWebhook::InstellingenPad()
    {
        std::filesystem::path basis;
        if( const char *appdata = std::getenv( "APPDATA" ) )
        {
            basis = appdata;
        }
        else
        {
            basis = std::filesystem::current_path();
        }
        basis /= "CabNavi";
        std::error_code ec;
        std::filesystem::create_directories( basis, ec );
        return basis / "discord.json";
    }

    DiscordWebhook::DiscordWebhook()
    {
        LaadInstellingen();
        m_worker = std::thread( &DiscordWebhook::WorkerLoop, this );
    }

    DiscordWebhook::~DiscordWebhook()
    {
        m_stoppen = true;
        m_queueCv.notify_all();
        if( m_worker.joinable() )
        {
            m_worker.join();
        }
    }

    void DiscordWebhook::LaadInstellingen()
    {
        std::ifstream in( InstellingenPad() );
        if( !in ) return;
        try
        {
            json j; in >> j;
            m_instellingen.webhookUrl = j.value( "webhook_url", std::string() );
            m_instellingen.ingeschakeld = j.value( "ingeschakeld", false );
        }
        catch( ... ) { /* corrupt bestand: default (uit) gebruiken */ }
    }

    void DiscordWebhook::SlaInstellingenOp() const
    {
        std::ofstream uit( InstellingenPad() );
        if( !uit ) return;
        json j;
        j[ "webhook_url" ] = m_instellingen.webhookUrl;
        j[ "ingeschakeld" ] = m_instellingen.ingeschakeld;
        uit << j.dump( 2 );
    }

    void DiscordWebhook::ZetWebhookUrl( const std::string &url )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_instellingen.webhookUrl = url;
        SlaInstellingenOp();
    }

    void DiscordWebhook::ZetIngeschakeld( bool ingeschakeld )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_instellingen.ingeschakeld = ingeschakeld;
        SlaInstellingenOp();
    }

    std::string DiscordWebhook::WebhookUrl() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_instellingen.webhookUrl;
    }

    bool DiscordWebhook::IsIngeschakeld() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_instellingen.ingeschakeld;
    }

    std::string DiscordWebhook::BouwEmbedJson( const Trip &trip ) const
    {
        const bool isBus = trip.type == TripType::Bus;
        const bool geannuleerd = trip.status == TripStatus::Geannuleerd;

        std::string titel = isBus ? "Buslijn " : "Vrachtrit ";
        titel += geannuleerd ? "geannuleerd" : "afgerond";

        std::string beschrijving;
        if( isBus )
        {
            beschrijving = std::to_string( trip.haltes.size() ) + " haltes";
        }
        else
        {
            beschrijving = ( trip.bronStad.empty() ? "?" : trip.bronStad ) + " -> "
                          + ( trip.bestemmingStad.empty() ? "?" : trip.bestemmingStad );
        }

        const std::int64_t opbrengst = trip.inkomen != 0 ? trip.inkomen : trip.geschatUitbetaling;
        const int kleur = geannuleerd ? 0xE2554A : ( isBus ? 0x3FB08A : 0xF2A33D ); // rood / groen / amber

        json embed;
        embed[ "title" ] = titel;
        embed[ "description" ] = beschrijving;
        embed[ "color" ] = kleur;

        // Hulpjes om het opbouwen leesbaar te houden.
        auto veld = [ &]( json &lijst, const std::string &naam, const std::string &waarde, bool inLijn = true )
        {
            if( waarde.empty() ) return;
            lijst.push_back( { { "name", naam }, { "value", waarde }, { "inline", inLijn } } );
        };
        auto geld = []( std::int64_t bedrag ) -> std::string
        {
            // Duizendtalscheiding met punten, zoals in het spel zelf.
            std::string cijfers = std::to_string( bedrag < 0 ? -bedrag : bedrag );
            std::string uit;
            int teller = 0;
            for( auto it = cijfers.rbegin(); it != cijfers.rend(); ++it )
            {
                if( teller > 0 && teller % 3 == 0 ) uit.insert( uit.begin(), '.' );
                uit.insert( uit.begin(), *it );
                ++teller;
            }
            return ( bedrag < 0 ? "-EUR " : "EUR " ) + uit;
        };
        auto komma = []( double waarde, int decimalen, const char *eenheid ) -> std::string
        {
            char buf[ 48 ];
            snprintf( buf, sizeof( buf ), "%.*f%s", decimalen, waarde, eenheid );
            return buf;
        };

        json velden = json::array();

        // --- Route en lading ---
        if( !isBus )
        {
            std::string van = trip.bronStad.empty() ? "?" : trip.bronStad;
            if( !trip.bronBedrijf.empty() ) van += " (" + trip.bronBedrijf + ")";
            std::string naar = trip.bestemmingStad.empty() ? "?" : trip.bestemmingStad;
            if( !trip.bestemmingBedrijf.empty() ) naar += " (" + trip.bestemmingBedrijf + ")";
            veld( velden, "Van", van );
            veld( velden, "Naar", naar );

            std::string ladingTekst = trip.lading;
            if( trip.ladingGewichtKg > 0.0 )
            {
                ladingTekst += " -- " + komma( trip.ladingGewichtKg / 1000.0, 1, " t" );
            }
            veld( velden, "Lading", ladingTekst );
        }

        // --- Afstand en tijd ---
        std::string afstand = std::to_string( (int)trip.afgelegdeAfstandKm ) + " km";
        if( trip.geplandeAfstandKm > 0.0 )
        {
            afstand += " (gepland " + std::to_string( (int)trip.geplandeAfstandKm ) + ")";
        }
        veld( velden, "Afstand", afstand );

        if( trip.economyEindTijd > trip.economyStartTijd )
        {
            const std::uint32_t minuten = trip.economyEindTijd - trip.economyStartTijd;
            veld( velden, "Duur (speltijd)",
                  std::to_string( minuten / 60 ) + "u " + std::to_string( minuten % 60 ) + "m" );
        }

        // --- Voertuig ---
        std::string truck = trip.voertuigMerk;
        if( !trip.voertuigModel.empty() )
        {
            if( !truck.empty() ) truck += " ";
            truck += trip.voertuigModel;
        }
        veld( velden, "Truck", truck );

        // --- Brandstof ---
        if( trip.brandstofVerbruikLiters > 0.0 )
        {
            std::string brandstof = komma( trip.brandstofVerbruikLiters, 1, " l" );
            if( trip.afgelegdeAfstandKm > 1.0 )
            {
                brandstof += " (" +
                    komma( trip.brandstofVerbruikLiters / trip.afgelegdeAfstandKm * 100.0, 1, " l/100km" ) + ")";
            }
            veld( velden, "Verbruik", brandstof );
        }
        if( trip.brandstofKostenEuro > 0.0 )
        {
            veld( velden, "Brandstofkosten", komma( trip.brandstofKostenEuro, 2, "" ).insert( 0, "EUR " ) );
        }

        // --- Schade ---
        if( trip.ladingSchadePercentage > 0.0 || trip.aanhangerSchadePercentage > 0.0 )
        {
            veld( velden, "Schade",
                  "lading " + komma( trip.ladingSchadePercentage, 0, "%" ) +
                  " | trailer " + komma( trip.aanhangerSchadePercentage, 0, "%" ) );
        }

        // --- Onkosten: het stuk dat TrucksBook-achtige rapportjes missen ---
        if( trip.tolKosten > 0 )      veld( velden, "Tol", geld( trip.tolKosten ) );
        if( trip.veerbootKosten > 0 ) veld( velden, "Veerboot", geld( trip.veerbootKosten ) );
        if( trip.treinKosten > 0 )    veld( velden, "Trein", geld( trip.treinKosten ) );

        if( !trip.boetes.empty() )
        {
            // Elke boete apart benoemen: "waarvoor" is nuttiger dan alleen
            // een totaalbedrag. Discord kapt een veld af boven 1024 tekens,
            // dus we stoppen netjes als het te lang wordt.
            std::string regels;
            int getoond = 0;
            for( const Boete &b : trip.boetes )
            {
                std::string regel = "- " + VertaalOffence( b.reden ) + ": " + geld( b.bedrag ) + "\n";
                if( regels.size() + regel.size() > 900 )
                {
                    regels += "- (+" + std::to_string( (int)trip.boetes.size() - getoond ) + " meer)";
                    break;
                }
                regels += regel;
                ++getoond;
            }
            veld( velden, "Boetes (" + geld( trip.boeteKosten ) + ")", regels, false );
        }

        // --- Financieel overzicht ---
        const std::int64_t onkosten = trip.tolKosten + trip.veerbootKosten
                                       + trip.treinKosten + trip.boeteKosten;
        const std::int64_t netto = opbrengst - onkosten
                                    - static_cast<std::int64_t>( trip.brandstofKostenEuro );

        veld( velden, "Opbrengst", geld( opbrengst ) );
        if( onkosten > 0 || trip.brandstofKostenEuro > 0.0 )
        {
            veld( velden, "Netto", geld( netto ) );
        }

        // --- Bus: haltes ---
        if( isBus && !trip.haltes.empty() )
        {
            std::string route;
            for( std::size_t i = 0; i < trip.haltes.size(); ++i )
            {
                if( route.size() > 900 ) { route += " ..."; break; }
                if( i > 0 ) route += " -> ";
                route += trip.haltes[ i ].naam;
            }
            veld( velden, "Route", route, false );
        }

        if( geannuleerd && !trip.annuleringsReden.empty() )
        {
            veld( velden, "Reden annulering", trip.annuleringsReden, false );
        }
        if( !trip.serverNaam.empty() )
        {
            veld( velden, "Server", trip.serverNaam );
        }

        embed[ "fields" ] = velden;
        embed[ "footer" ] = { { "text", "CabNavi" } };

        json payload;
        payload[ "embeds" ] = json::array( { embed } );
        return payload.dump();
    }

    void DiscordWebhook::StuurRitVoltooid( const Trip &trip )
    {
        std::string url;
        bool aan;
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            url = m_instellingen.webhookUrl;
            aan = m_instellingen.ingeschakeld;
        }
        if( !aan || url.empty() )
        {
            return;
        }

        std::string body = BouwEmbedJson( trip );
        {
            std::lock_guard<std::mutex> lock( m_queueMutex );
            m_wachtrij.push_back( WerkItem{ url, std::move( body ) } );
        }
        m_queueCv.notify_one();
    }

    void DiscordWebhook::StuurTestbericht()
    {
        std::string url;
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            url = m_instellingen.webhookUrl;
        }
        if( url.empty() )
        {
            return;
        }

        json payload;
        payload[ "content" ] = "CabNavi is verbonden -- als je dit ziet, werkt de webhook!";
        {
            std::lock_guard<std::mutex> lock( m_queueMutex );
            m_wachtrij.push_back( WerkItem{ url, payload.dump() } );
        }
        m_queueCv.notify_one();
    }

    void DiscordWebhook::WorkerLoop()
    {
        while( !m_stoppen )
        {
            std::deque<WerkItem> batch;
            {
                std::unique_lock<std::mutex> lock( m_queueMutex );
                m_queueCv.wait( lock, [ this ] { return m_stoppen || !m_wachtrij.empty(); } );
                batch.swap( m_wachtrij );
            }
            for( const WerkItem &item : batch )
            {
                VerstuurBericht( item.url, item.jsonBody );
            }
        }
    }

    // TIJDELIJK, zelfde patroon als bij TruckTracking: schrijft naar
    // debug.log als het versturen mislukt, zodat je niet blind hoeft te
    // gokken als de Discord-melding een keer niet aankomt.
    static void SchrijfDebugRegel( const std::string &regel )
    {
        std::filesystem::path pad;
        if( const char *appdata = std::getenv( "APPDATA" ) )
        {
            pad = appdata;
        }
        pad /= "CabNavi";
        std::error_code ec;
        std::filesystem::create_directories( pad, ec );
        pad /= "debug.log";

        std::ofstream uit( pad, std::ios::app );
        if( !uit ) return;
        uit << "[Discord] " << regel << "\n";
    }

    void DiscordWebhook::VerstuurBericht( const std::string &url, const std::string &jsonBody ) const
    {
        std::wstring wideUrl = NaarWide( url );

        wchar_t hostNaam[ 256 ] = {};
        wchar_t pad[ 2048 ] = {};

        URL_COMPONENTS comp{};
        comp.dwStructSize = sizeof( comp );
        comp.lpszHostName = hostNaam;
        comp.dwHostNameLength = 256;
        comp.lpszUrlPath = pad;
        comp.dwUrlPathLength = 2048;
        comp.dwSchemeLength = static_cast<DWORD>( -1 );

        if( !WinHttpCrackUrl( wideUrl.c_str(), static_cast<DWORD>( wideUrl.size() ), 0, &comp ) )
        {
            SchrijfDebugRegel( "Ongeldige webhook-URL, kon 'm niet ontleden: " + url );
            return; // ongeldige URL, niets aan te doen -- gebruiker moet 'm checken
        }

        const bool https = comp.nScheme == INTERNET_SCHEME_HTTPS;

        HINTERNET sessie = WinHttpOpen( L"CabNavi/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
        if( !sessie )
        {
            SchrijfDebugRegel( "WinHttpOpen mislukt (foutcode " + std::to_string( GetLastError() ) + ")" );
            return;
        }

        HINTERNET verbinding = WinHttpConnect( sessie, hostNaam, comp.nPort, 0 );
        if( !verbinding )
        {
            SchrijfDebugRegel( "WinHttpConnect mislukt (foutcode " + std::to_string( GetLastError() ) + ")" );
            WinHttpCloseHandle( sessie );
            return;
        }

        HINTERNET request = WinHttpOpenRequest(
            verbinding, L"POST", pad, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            https ? WINHTTP_FLAG_SECURE : 0 );
        if( !request )
        {
            SchrijfDebugRegel( "WinHttpOpenRequest mislukt (foutcode " + std::to_string( GetLastError() ) + ")" );
            WinHttpCloseHandle( verbinding );
            WinHttpCloseHandle( sessie );
            return;
        }

        const wchar_t *headers = L"Content-Type: application/json\r\n";
        BOOL verzonden = WinHttpSendRequest(
            request, headers, static_cast<DWORD>( -1 ),
            const_cast<char *>( jsonBody.data() ), static_cast<DWORD>( jsonBody.size() ),
            static_cast<DWORD>( jsonBody.size() ), 0 );

        if( !verzonden )
        {
            SchrijfDebugRegel( "WinHttpSendRequest mislukt (foutcode " + std::to_string( GetLastError() ) + ")" );
        }
        else if( !WinHttpReceiveResponse( request, nullptr ) )
        {
            SchrijfDebugRegel( "WinHttpReceiveResponse mislukt (foutcode " + std::to_string( GetLastError() ) + ")" );
        }
        else
        {
            DWORD statusCode = 0, statusSize = sizeof( statusCode );
            WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                  WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX );
            if( statusCode < 200 || statusCode >= 300 )
            {
                SchrijfDebugRegel( "Discord antwoordde met statuscode " + std::to_string( statusCode )
                                    + " -- check of de webhook-URL klopt en nog bestaat." );
            }
        }

        WinHttpCloseHandle( request );
        WinHttpCloseHandle( verbinding );
        WinHttpCloseHandle( sessie );
    }
}

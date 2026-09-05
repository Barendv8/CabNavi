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
            // For a trip cancelled right away OnJobDataReady never fired; then
            // the stop list is empty and "0 stops" says nothing. Better nothing
            // than a zero.
            beschrijving = trip.haltes.empty()
                ? std::string()
                : std::to_string( trip.haltes.size() ) + " haltes";
        }
        else
        {
            beschrijving = ( trip.bronStad.empty() ? "?" : trip.bronStad ) + " -> "
                          + ( trip.bestemmingStad.empty() ? "?" : trip.bestemmingStad );
        }

        // For a CANCELLED trip never report the estimated payout: that is
        // money you did not receive, and then an amount appears under
        // "cancelled" as if you were paid. Only for a completed trip may the
        // estimate step in when the game did not pass the actual amount.
        const std::int64_t opbrengst = geannuleerd
            ? trip.inkomen
            : ( trip.inkomen != 0 ? trip.inkomen : trip.geschatUitbetaling );
        const int kleur = geannuleerd ? 0xE2554A : ( isBus ? 0x3FB08A : 0xF2A33D );  // red / green / amber

        json embed;
        embed[ "title" ] = titel;
        embed[ "description" ] = beschrijving;
        embed[ "color" ] = kleur;

        // Little helpers to keep the assembly readable.
        auto veld = [ &]( json &lijst, const std::string &naam, const std::string &waarde, bool inLijn = true )
        {
            if( waarde.empty() ) return;
            lijst.push_back( { { "name", naam }, { "value", waarde }, { "inline", inLijn } } );
        };
        auto geld = []( std::int64_t bedrag ) -> std::string
        {
            // Thousands separated with dots, like in the game itself.
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

        // --- Route and cargo ---
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

        // --- Distance and time ---
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

        // --- Vehicle ---
        std::string truck = trip.voertuigMerk;
        if( !trip.voertuigModel.empty() )
        {
            if( !truck.empty() ) truck += " ";
            truck += trip.voertuigModel;
        }
        veld( velden, isBus ? "Bus" : "Truck", truck );

        // --- Fuel ---
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

        // --- Damage ---
        // Chassis belongs here too: that is the figure that counts in a
        // collision, and it was missing.
        if( trip.schadeChassisPercentage > 0.0 || trip.ladingSchadePercentage > 0.0
            || trip.aanhangerSchadePercentage > 0.0 )
        {
            veld( velden, "Schade",
                  "chassis " + komma( trip.schadeChassisPercentage, 0, "%" ) +
                  " | lading " + komma( trip.ladingSchadePercentage, 0, "%" ) +
                  " | trailer " + komma( trip.aanhangerSchadePercentage, 0, "%" ) );
        }

        // --- Expenses: the part TrucksBook-style reports miss ---
        if( trip.tolKosten > 0 )      veld( velden, "Tol", geld( trip.tolKosten ) );
        if( trip.veerbootKosten > 0 ) veld( velden, "Veerboot", geld( trip.veerbootKosten ) );
        if( trip.treinKosten > 0 )    veld( velden, "Trein", geld( trip.treinKosten ) );

        if( !trip.boetes.empty() )
        {
            // Name each fine separately: "for what" is more useful than just a
            // total. Discord truncates a field above 1024 characters, so we stop
            // neatly when it gets too long.
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

        // --- Financial summary ---
        const std::int64_t onkosten = trip.tolKosten + trip.veerbootKosten
                                       + trip.treinKosten + trip.boeteKosten;
        const std::int64_t netto = opbrengst - onkosten
                                    - static_cast<std::int64_t>( trip.brandstofKostenEuro );

        veld( velden, "Opbrengst", geld( opbrengst ) );
        if( onkosten > 0 || trip.brandstofKostenEuro > 0.0 )
        {
            veld( velden, "Netto", geld( netto ) );
        }

        // --- Bus: stops and passengers ---
        if( isBus && !trip.haltes.empty() )
        {
            std::string route;
            int totaalIn = 0;
            for( std::size_t i = 0; i < trip.haltes.size(); ++i )
            {
                totaalIn += trip.haltes[ i ].instappers;

                if( route.size() > 900 ) { route += " ..."; break; }
                if( i > 0 ) route += " -> ";
                route += trip.haltes[ i ].naam;

                // Boarding and alighting behind it, only when something happens.
                // At the final stop nobody boards, so there is only a minus -- that
                // reads naturally.
                const int in = trip.haltes[ i ].instappers;
                const int uit = trip.haltes[ i ].uitstappers;
                if( in > 0 && uit > 0 )
                    route += " (+" + std::to_string( in ) + " -" + std::to_string( uit ) + ")";
                else if( in > 0 )
                    route += " (+" + std::to_string( in ) + ")";
                else if( uit > 0 )
                    route += " (-" + std::to_string( uit ) + ")";
            }
            veld( velden, "Route", route, false );

            // How many people you carried this trip: the sum of all boarders.
            // trip.passagiers is the number ON BOARD, and at the end of the trip
            // that is zero -- not what you want to report here.
            if( totaalIn > 0 )
            {
                veld( velden, "Passagiers", std::to_string( totaalIn ) );
            }
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

    // TEMPORARY, same pattern as in TruckTracking: writes to debug.log
    // when sending fails, so you do not have to guess blindly when a
    // Discord message does not arrive.
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
            // NEVER log the URL itself: it contains the secret token, and
            // whoever has it can post in the Discord channel. Someone who makes a
            // typo and then pastes his debug.log on the forum would otherwise
            // give away his webhook. Length and whether it starts correctly is
            // enough to see what is wrong.
            const bool juistBegin = url.rfind( "https://discord.com/api/webhooks/", 0 ) == 0
                                 || url.rfind( "https://discordapp.com/api/webhooks/", 0 ) == 0;
            SchrijfDebugRegel( "Invalid webhook URL (length " + std::to_string( url.size() )
                               + ", starts " + ( juistBegin ? "correctly" : "incorrectly" )
                               + " with https://discord.com/api/webhooks/)" );
            return;  // invalid URL, nothing to do -- user must check it
        }

        const bool https = comp.nScheme == INTERNET_SCHEME_HTTPS;

        HINTERNET sessie = WinHttpOpen( L"CabNavi/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
        if( !sessie )
        {
            SchrijfDebugRegel( "WinHttpOpen failed (error code " + std::to_string( GetLastError() ) + ")" );
            return;
        }

        HINTERNET verbinding = WinHttpConnect( sessie, hostNaam, comp.nPort, 0 );
        if( !verbinding )
        {
            SchrijfDebugRegel( "WinHttpConnect failed (error code " + std::to_string( GetLastError() ) + ")" );
            WinHttpCloseHandle( sessie );
            return;
        }

        HINTERNET request = WinHttpOpenRequest(
            verbinding, L"POST", pad, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            https ? WINHTTP_FLAG_SECURE : 0 );
        if( !request )
        {
            SchrijfDebugRegel( "WinHttpOpenRequest failed (error code " + std::to_string( GetLastError() ) + ")" );
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
            SchrijfDebugRegel( "WinHttpSendRequest failed (error code " + std::to_string( GetLastError() ) + ")" );
        }
        else if( !WinHttpReceiveResponse( request, nullptr ) )
        {
            SchrijfDebugRegel( "WinHttpReceiveResponse failed (error code " + std::to_string( GetLastError() ) + ")" );
        }
        else
        {
            DWORD statusCode = 0, statusSize = sizeof( statusCode );
            WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                  WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX );
            if( statusCode < 200 || statusCode >= 300 )
            {
                SchrijfDebugRegel( "Discord answered with status code " + std::to_string( statusCode )
                                    + " -- check that the webhook URL is correct and still exists." );
            }
        }

        WinHttpCloseHandle( request );
        WinHttpCloseHandle( verbinding );
        WinHttpCloseHandle( sessie );
    }
}

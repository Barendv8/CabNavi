#include "VtcWebhook.hxx"
#include "Spel.hxx"
#include "Geheim.hxx"

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>

namespace Ritten
{
    using json = nlohmann::json;

    namespace
    {
        std::wstring NaarWide( const std::string &tekst )
        {
#ifdef _WIN32
            if( tekst.empty() ) return {};
            const int nodig = MultiByteToWideChar( CP_UTF8, 0, tekst.c_str(), -1, nullptr, 0 );
            std::wstring resultaat( nodig > 0 ? nodig - 1 : 0, L'\0' );
            if( nodig > 1 )
            {
                MultiByteToWideChar( CP_UTF8, 0, tekst.c_str(), -1, &resultaat[ 0 ], nodig );
            }
            return resultaat;
#else
            return std::wstring( tekst.begin(), tekst.end() );
#endif
        }

        // Same rule as DiscordWebhook's log: the endpoint may carry a
        // secret in its path and the key IS a secret, so neither is ever
        // written anywhere. Length and shape are enough to debug with.
        void SchrijfDebugRegel( const std::string &regel )
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
            uit << "[VTC-webhook] " << regel << "\n";
        }
    }

    std::filesystem::path VtcWebhook::InstellingenPad()
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
        return basis / "vtc_webhook.json";
    }

    VtcWebhook::VtcWebhook()
    {
        LaadInstellingen();
        m_worker = std::thread( &VtcWebhook::WorkerLoop, this );
    }

    VtcWebhook::~VtcWebhook()
    {
        m_stoppen = true;
        m_queueCv.notify_all();
        if( m_worker.joinable() )
        {
            m_worker.join();
        }
    }

    void VtcWebhook::LaadInstellingen()
    {
        std::ifstream in( InstellingenPad() );
        if( !in ) return;
        try
        {
            json j; in >> j;
            m_instellingen.endpointUrl = j.value( "endpoint_url", std::string() );
            m_instellingen.sleutel = Geheim::Ontsleutel( j.value( "sleutel", std::string() ) );
            m_instellingen.formaat = std::clamp( j.value( "formaat", 0 ), 0, 1 );
            m_instellingen.hubGeheim = Geheim::Ontsleutel( j.value( "hub_geheim", std::string() ) );
            m_instellingen.livePositie = j.value( "live_positie", false );
            m_instellingen.ingeschakeld = j.value( "ingeschakeld", false );
        }
        catch( ... ) { /* corrupt bestand: default (uit) gebruiken */ }
    }

    void VtcWebhook::SlaInstellingenOp() const
    {
        std::ofstream uit( InstellingenPad() );
        if( !uit ) return;
        json j;
        j[ "endpoint_url" ] = m_instellingen.endpointUrl;
        j[ "sleutel" ] = Geheim::Versleutel( m_instellingen.sleutel );
        j[ "formaat" ] = m_instellingen.formaat;
        j[ "hub_geheim" ] = Geheim::Versleutel( m_instellingen.hubGeheim );
        j[ "live_positie" ] = m_instellingen.livePositie;
        j[ "ingeschakeld" ] = m_instellingen.ingeschakeld;
        uit << j.dump( 2 );
    }

    void VtcWebhook::ZetEndpointUrl( const std::string &url )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_instellingen.endpointUrl = url;
        SlaInstellingenOp();
    }

    void VtcWebhook::ZetSleutel( const std::string &sleutel )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_instellingen.sleutel = sleutel;
        SlaInstellingenOp();
    }

    void VtcWebhook::ZetIngeschakeld( const bool ingeschakeld )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_instellingen.ingeschakeld = ingeschakeld;
        SlaInstellingenOp();
    }

    std::string VtcWebhook::EndpointUrl() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_instellingen.endpointUrl;
    }

    void VtcWebhook::ZetFormaat( int formaat )
    {
        { std::lock_guard<std::mutex> lock( m_mutex ); m_instellingen.formaat = std::clamp( formaat, 0, 1 ); }
        SlaInstellingenOp();
    }

    void VtcWebhook::ZetHubGeheim( const std::string &geheim )
    {
        { std::lock_guard<std::mutex> lock( m_mutex ); m_instellingen.hubGeheim = geheim; }
        SlaInstellingenOp();
    }

    void VtcWebhook::ZetLivePositie( bool aan )
    {
        { std::lock_guard<std::mutex> lock( m_mutex ); m_instellingen.livePositie = aan; }
        SlaInstellingenOp();
    }

    bool VtcWebhook::LivePositie() const { std::lock_guard<std::mutex> lock( m_mutex ); return m_instellingen.livePositie; }

    void VtcWebhook::ZetLive( const LiveStand &stand )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_live = stand;
    }

    int VtcWebhook::Formaat() const { std::lock_guard<std::mutex> lock( m_mutex ); return m_instellingen.formaat; }
    std::string VtcWebhook::HubGeheim() const { std::lock_guard<std::mutex> lock( m_mutex ); return m_instellingen.hubGeheim; }

    std::string VtcWebhook::Sleutel() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_instellingen.sleutel;
    }

    bool VtcWebhook::IsIngeschakeld() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_instellingen.ingeschakeld;
    }

    // -----------------------------------------------------------------
    // The open trip format -- keep in step with docs/TRIP_FORMAT.md.
    // Data stays in game units and game currency: the receiver gets facts,
    // display taste stays on this side of the wire.
    // -----------------------------------------------------------------
    std::string VtcWebhook::BouwRitJson( const Trip &trip, const TachoMoment &tacho, const RitContext &ctx ) const
    {
        const bool isBus = trip.type == TripType::Bus;
        const bool geannuleerd = trip.status == TripStatus::Geannuleerd;

        json j;
        j[ "format" ] = "cabnavi-trip";
        j[ "format_version" ] = 1;
        j[ "game" ] = SpelInfo::IsAts() ? "ats" : "ets2";
        j[ "currency" ] = SpelInfo::Munt();

        j[ "trip_id" ] = trip.id;
        j[ "type" ] = isBus ? "bus" : "freight";
        j[ "status" ] = geannuleerd ? "cancelled" : "completed";
        if( !trip.startTijdIso.empty() ) j[ "started_at" ] = trip.startTijdIso;
        if( !trip.eindTijdIso.empty() )  j[ "ended_at" ] = trip.eindTijdIso;
        if( !trip.serverNaam.empty() )   j[ "server" ] = trip.serverNaam;

        json voertuig;
        if( !trip.voertuigMerk.empty() )  voertuig[ "brand" ] = trip.voertuigMerk;
        if( !trip.voertuigModel.empty() ) voertuig[ "model" ] = trip.voertuigModel;
        if( !voertuig.empty() ) j[ "vehicle" ] = voertuig;

        json route;
        if( !trip.bronStad.empty() )           route[ "from_city" ] = trip.bronStad;
        if( !trip.bronBedrijf.empty() )        route[ "from_company" ] = trip.bronBedrijf;
        if( !trip.bestemmingStad.empty() )     route[ "to_city" ] = trip.bestemmingStad;
        if( !trip.bestemmingBedrijf.empty() )  route[ "to_company" ] = trip.bestemmingBedrijf;
        if( !route.empty() ) j[ "route" ] = route;

        if( !isBus )
        {
            json lading;
            if( !trip.lading.empty() )       lading[ "name" ] = trip.lading;
            if( trip.ladingGewichtKg > 0.0 ) lading[ "weight_kg" ] = trip.ladingGewichtKg;
            if( !lading.empty() ) j[ "cargo" ] = lading;
        }

        json afstand;
        afstand[ "driven_km" ] = trip.afgelegdeAfstandKm;
        if( trip.geplandeAfstandKm > 0.0 ) afstand[ "planned_km" ] = trip.geplandeAfstandKm;
        j[ "distance" ] = afstand;

        if( trip.economyEindTijd > trip.economyStartTijd )
            j[ "duration_game_minutes" ] = trip.economyEindTijd - trip.economyStartTijd;

        if( trip.brandstofVerbruikLiters > 0.0 )
        {
            json brandstof;
            brandstof[ "used_litres" ] = trip.brandstofVerbruikLiters;
            // Estimate, not a game fact: measured consumption times the
            // self-set litre price -- exactly as the overlay labels it.
            if( trip.brandstofKostenEuro > 0.0 )
                brandstof[ "estimated_cost" ] = trip.brandstofKostenEuro;
            // Which price the litres were charged at, and how trustworthy:
            // last_refuel = what this truck actually paid at the pump.
            if( ctx.prijsPerLiter > 0.0 )
            {
                brandstof[ "price_per_litre" ] = ctx.prijsPerLiter;
                brandstof[ "price_source" ] = ctx.prijsBron == 0 ? "last_refuel" : ctx.prijsBron == 1 ? "location" : "manual";
            }
            j[ "fuel" ] = brandstof;
        }

        // What the trip's memory saw: the highest speed, speeding stretches,
        // teleports, the route as points. All optional; absent when unknown.
        if( ctx.hub.topKmh > 0.0 ) j[ "top_speed_kmh" ] = std::round( ctx.hub.topKmh * 10.0 ) / 10.0;
        if( !ctx.hub.overtredingen.empty() )
        {
            json lijst = json::array();
            for( const auto &o : ctx.hub.overtredingen )
            {
                json e;
                e[ "max_kmh" ] = std::round( o.maxKmh * 10.0 ) / 10.0;
                e[ "limit_kmh" ] = o.limietKmh;
                e[ "seconds" ] = o.eind - o.start;
                lijst.push_back( e );
            }
            j[ "speeding" ] = lijst;
        }
        {
            // Ferry and train crossings are position jumps, not teleports.
            int overtochten = 0;
            for( const auto &d : trip.doorgangen )
                if( d.type == DoorgangType::Veerboot || d.type == DoorgangType::Trein ) ++overtochten;
            const int teleports = std::max( 0, ctx.hub.teleports - overtochten );
            if( teleports > 0 ) j[ "teleports" ] = teleports;
        }
        if( !ctx.hub.route.empty() )
        {
            json route = json::array();
            for( const auto &p : ctx.hub.route )
                route.push_back( { { "t", p.tijd }, { "x", std::round( p.x * 10.0 ) / 10.0 }, { "z", std::round( p.z * 10.0 ) / 10.0 } } );
            j[ "route" ] = route;
        }
        if( !ctx.hub.kenteken.empty() )
        {
            json kt; kt[ "plate" ] = ctx.hub.kenteken;
            if( !ctx.hub.kentekenLand.empty() ) kt[ "country" ] = ctx.hub.kentekenLand;
            j[ "license_plate" ] = kt;
        }
        if( !ctx.hub.realistischeInstellingen.empty() )
        {
            json gs;
            for( const auto &[ k, v ] : ctx.hub.realistischeInstellingen ) gs[ k ] = v;
            j[ "game_settings" ] = gs;
        }

        // The empty run before this job: km driven without cargo since the
        // previous job ended. Left out when there was none.
        if( ctx.leegKm > 0.05 || ctx.leegLiters > 0.05 )
        {
            json leeg;
            leeg[ "km" ] = std::round( ctx.leegKm * 10.0 ) / 10.0;
            leeg[ "fuel_litres" ] = std::round( ctx.leegLiters * 10.0 ) / 10.0;
            leeg[ "estimated_cost" ] = std::round( ctx.leegKosten * 100.0 ) / 100.0;
            j[ "empty_run" ] = leeg;
        }

        json geld;
        geld[ "income" ] = trip.inkomen;
        if( geannuleerd == false && trip.inkomen == 0 && trip.geschatUitbetaling != 0 )
            geld[ "income_estimated" ] = trip.geschatUitbetaling;
        if( trip.tolKosten > 0 )      geld[ "toll" ] = trip.tolKosten;
        if( trip.veerbootKosten > 0 ) geld[ "ferry" ] = trip.veerbootKosten;
        if( trip.treinKosten > 0 )    geld[ "train" ] = trip.treinKosten;
        if( trip.boeteKosten > 0 )    geld[ "fines" ] = trip.boeteKosten;
        j[ "money" ] = geld;

        json schade;
        schade[ "chassis_pct" ] = trip.schadeChassisPercentage;
        schade[ "cargo_pct" ] = trip.ladingSchadePercentage;
        schade[ "trailer_pct" ] = trip.aanhangerSchadePercentage;
        j[ "damage" ] = schade;

        j[ "on_time" ] = trip.opTijd;
        if( geannuleerd && !trip.annuleringsReden.empty() )
            j[ "cancel_reason" ] = trip.annuleringsReden;

        if( isBus )
        {
            json haltes = json::array();
            int totaalIn = 0;
            for( const auto &h : trip.haltes )
            {
                totaalIn += h.instappers;
                json halte;
                halte[ "name" ] = h.naam;
                halte[ "boarded" ] = h.instappers;
                halte[ "alighted" ] = h.uitstappers;
                haltes.push_back( halte );
            }
            j[ "bus" ] = { { "stops", haltes }, { "passengers_total", totaalIn } };
        }

        // The tachograph block: driving time on CabNavi's own tachograph at
        // the moment the trip closed. This is the field a company that wants
        // driving-time realism cannot get anywhere else.
        if( tacho.geldig )
        {
            j[ "tachograph" ] = {
                { "driving_minutes_since_rest", tacho.rijMinutenSindsRust },
                { "resting", tacho.inRust },
            };
        }

        return j.dump();
    }

    void VtcWebhook::StuurRit( const Trip &trip, const TachoMoment &tacho, const RitContext &ctx )
    {
        std::string url, sleutel, hubGeheim; int formaat = 0;
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            if( !m_instellingen.ingeschakeld ) return;
            url = m_instellingen.endpointUrl;
            sleutel = m_instellingen.sleutel;
            formaat = m_instellingen.formaat;
            hubGeheim = m_instellingen.hubGeheim;
        }
        if( url.empty() ) return;

        // Envelope format: a different body, signed with the hub's secret in
        // the header those hubs verify. The Bearer key still goes along.
        if( formaat == 1 )
        {
            WerkItem item;
            item.url = url; item.sleutel = sleutel;
            item.jsonBody = HubFormaat::BouwBody( trip, ctx.hub );
            if( !hubGeheim.empty() )
            {
                item.extraHeaderNaam = HubFormaat::HANDTEKENING_HEADER;
                item.extraHeaderWaarde = HubFormaat::Handtekening( hubGeheim, item.jsonBody );
            }
            {
                std::lock_guard<std::mutex> lock( m_queueMutex );
                m_wachtrij.push_back( std::move( item ) );
            }
            m_queueCv.notify_one();
            return;
        }

        const std::string body = BouwRitJson( trip, tacho, ctx );
        {
            std::lock_guard<std::mutex> lock( m_queueMutex );
            m_wachtrij.push_back( WerkItem{ url, sleutel, body } );
        }
        m_queueCv.notify_one();
    }

    void VtcWebhook::StuurTestbericht()
    {
        std::string url, sleutel;
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            url = m_instellingen.endpointUrl;
            sleutel = m_instellingen.sleutel;
        }
        if( url.empty() ) return;

        json j;
        j[ "format" ] = "cabnavi-trip";
        j[ "format_version" ] = 1;
        j[ "game" ] = SpelInfo::IsAts() ? "ats" : "ets2";
        j[ "type" ] = "test";

        {
            std::lock_guard<std::mutex> lock( m_queueMutex );
            m_wachtrij.push_back( WerkItem{ url, sleutel, j.dump() } );
        }
        m_queueCv.notify_one();
    }

    void VtcWebhook::WorkerLoop()
    {
        while( !m_stoppen )
        {
            std::deque<WerkItem> batch;
            {
                std::unique_lock<std::mutex> lock( m_queueMutex );
                m_queueCv.wait_for( lock, std::chrono::seconds( 10 ), [ this ] { return m_stoppen || !m_wachtrij.empty(); } );
                batch.swap( m_wachtrij );
            }
            for( const WerkItem &item : batch )
            {
                VerstuurBericht( item.url, item.sleutel, item.jsonBody, item.extraHeaderNaam, item.extraHeaderWaarde );
            }

            // Live position: one small POST every 10 s to <address>/live,
            // only while the switch is on, the webhook is enabled, and we
            // actually know where we are.
            {
                std::string url, sleutel; LiveStand stand; bool aan = false;
                {
                    std::lock_guard<std::mutex> lock( m_mutex );
                    aan = m_instellingen.livePositie && m_instellingen.ingeschakeld;
                    url = m_instellingen.endpointUrl; sleutel = m_instellingen.sleutel; stand = m_live;
                }
                const auto nu = std::chrono::steady_clock::now();
                if( aan && stand.bekend && !url.empty()
                    && std::chrono::duration<double>( nu - m_laatsteLive ).count() >= 10.0 )
                {
                    m_laatsteLive = nu;
                    const std::string staart = "/trip";
                    if( url.size() >= staart.size() && url.compare( url.size() - staart.size(), staart.size(), staart ) == 0 )
                        url = url.substr( 0, url.size() - staart.size() );
                    while( !url.empty() && url.back() == '/' ) url.pop_back();
                    json j;
                    j[ "format" ] = "cabnavi-live"; j[ "format_version" ] = 1;
                    j[ "game" ] = SpelInfo::IsAts() ? "ats" : "ets2";
                    j[ "x" ] = std::round( stand.x * 10.0 ) / 10.0; j[ "z" ] = std::round( stand.z * 10.0 ) / 10.0;
                    j[ "speed_kmh" ] = std::round( stand.snelheidKmh );
                    j[ "on_job" ] = stand.opRit;
                    if( stand.opRit ) { j[ "from_city" ] = stand.van; j[ "to_city" ] = stand.naar; }
                    if( !stand.server.empty() ) j[ "server" ] = stand.server;
                    VerstuurBericht( url + "/live", sleutel, j.dump() );
                }
            }
        }
    }

    void VtcWebhook::VerstuurBericht( const std::string &url, const std::string &sleutel,
                                      const std::string &jsonBody,
                                      const std::string &extraHeaderNaam,
                                      const std::string &extraHeaderWaarde ) const
    {
#ifdef _WIN32
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
            SchrijfDebugRegel( "Invalid endpoint URL (length " + std::to_string( url.size() )
                               + "); it must start with https:// or http://" );
            return;
        }

        const bool https = comp.nScheme == INTERNET_SCHEME_HTTPS;
        // Plain http is allowed to the machine itself only: a loopback
        // connection never leaves the network card, so nobody can read it.
        // Every other host must be https. Documented in PRIVACY.md.
        if( !https )
        {
            const std::wstring h( hostNaam );
            const bool eigenPc = ( h == L"localhost" || h == L"127.0.0.1" || h == L"::1" );
            if( !eigenPc )
            {
                SchrijfDebugRegel( "Endpoint refused: http:// is only allowed to localhost; use https://" );
                return;
            }
        }

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

        // The key travels in two headers: Bearer for frameworks that expect
        // it (Laravel and friends), X-CabNavi-Key for a bare PHP file that
        // just reads one header. Same value, receiver picks.
        std::wstring headers = L"Content-Type: application/json\r\n";
        if( !sleutel.empty() )
        {
            const std::wstring ws = NaarWide( sleutel );
            headers += L"Authorization: Bearer " + ws + L"\r\n";
            headers += L"X-CabNavi-Key: " + ws + L"\r\n";
        }
        if( !extraHeaderNaam.empty() )
        {
            headers += NaarWide( extraHeaderNaam ) + L": " + NaarWide( extraHeaderWaarde ) + L"\r\n";
            headers += NaarWide( HubFormaat::HANDTEKENING_HEADER_GENERIEK ) + L": " + NaarWide( extraHeaderWaarde ) + L"\r\n";
        }

        BOOL verzonden = WinHttpSendRequest(
            request, headers.c_str(), static_cast<DWORD>( -1 ),
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
                SchrijfDebugRegel( "Endpoint answered HTTP " + std::to_string( statusCode ) );
            }
        }

        WinHttpCloseHandle( request );
        WinHttpCloseHandle( verbinding );
        WinHttpCloseHandle( sessie );
#else
        (void)url; (void)sleutel; (void)jsonBody;
#endif
    }
}

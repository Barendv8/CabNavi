#include "VtcBron.hxx"
#include "VtcWebhook.hxx"
#include "HttpHulp.hxx"
#include "Logboek.hxx"
#include "Geheim.hxx"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace Ritten
{
    using json = nlohmann::json;

    // -----------------------------------------------------------------
    // Small readers: APIs promise one type and send another (TruckersMP
    // sends "false" where it documents a boolean; others send numbers as
    // strings). Never trust, always read defensively.
    // -----------------------------------------------------------------
    namespace
    {
        std::string Tekst( const json &j, const char *veld, const std::string &standaard = "" )
        {
            if( !j.is_object() || !j.contains( veld ) || j[ veld ].is_null() ) return standaard;
            const json &v = j[ veld ];
            if( v.is_string() ) return v.get<std::string>();
            if( v.is_number_integer() ) return std::to_string( v.get<long long>() );
            if( v.is_number() ) return std::to_string( v.get<double>() );
            if( v.is_boolean() ) return v.get<bool>() ? "true" : "false";
            return standaard;
        }

        double Getal( const json &j, const char *veld, double standaard = 0.0 )
        {
            if( !j.is_object() || !j.contains( veld ) || j[ veld ].is_null() ) return standaard;
            const json &v = j[ veld ];
            if( v.is_number() ) return v.get<double>();
            if( v.is_string() ) { try { return std::stod( v.get<std::string>() ); } catch( ... ) {} }
            return standaard;
        }

        int Heel( const json &j, const char *veld, int standaard = 0 )
        {
            return static_cast<int>( Getal( j, veld, standaard ) );
        }

        const json &Sub( const json &j, const char *veld )
        {
            static const json leeg = json::object();
            if( j.is_object() && j.contains( veld ) && j[ veld ].is_object() ) return j[ veld ];
            return leeg;
        }

        std::wstring Breed( const std::string &s )
        {
            return std::wstring( s.begin(), s.end() );
        }

        // "https://host[:port]/path" -> host, port, https, path. Plain http is
        // accepted for the machine itself only (localhost / 127.0.0.1): a
        // loopback connection never leaves the PC. Anything else -> false.
        bool SplitsUrl( const std::string &url, std::wstring &host, std::wstring &pad, int &poort, bool &https )
        {
            std::string rest;
            if( url.rfind( "https://", 0 ) == 0 ) { https = true; rest = url.substr( 8 ); }
            else if( url.rfind( "http://", 0 ) == 0 ) { https = false; rest = url.substr( 7 ); }
            else return false;
            const auto slash = rest.find( '/' );
            std::string hostPoort = slash == std::string::npos ? rest : rest.substr( 0, slash );
            pad = Breed( slash == std::string::npos ? "/" : rest.substr( slash ) );
            poort = https ? 443 : 80;
            const auto dp = hostPoort.rfind( ':' );
            if( dp != std::string::npos && hostPoort.find( ']' ) == std::string::npos )
            {
                try { poort = std::stoi( hostPoort.substr( dp + 1 ) ); } catch( ... ) { return false; }
                hostPoort = hostPoort.substr( 0, dp );
            }
            if( !https && hostPoort != "localhost" && hostPoort != "127.0.0.1" ) return false;
            host = Breed( hostPoort );
            return !host.empty();
        }

        std::filesystem::path InstellingenPad()
        {
            std::filesystem::path basis;
            if( const char *appdata = std::getenv( "APPDATA" ) ) basis = appdata;
            else basis = std::filesystem::current_path();
            basis /= "CabNavi";
            std::error_code ec;
            std::filesystem::create_directories( basis, ec );
            return basis / "vtc_bron.json";
        }

        // One GET with headers, parsed. Returns false and fills `fout`.
        bool Ophalen( const std::wstring &host, const std::wstring &pad, const std::wstring &headers,
                      json &uit, std::string &fout, int poort = 443, bool https = true )
        {
            std::string body; int status = 0;
            // Company APIs behind a CDN hiccup now and then (seen 15-09 on
            // Trucky: 12152 every other minute). One retry hides that; a real
            // outage still shows after the second miss.
            bool ok = HttpVerzoek( L"GET", host.c_str(), pad.c_str(), headers, "", body, status, fout, poort, https );
            if( !ok && ( status == 0 || status >= 500 ) )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
                ok = HttpVerzoek( L"GET", host.c_str(), pad.c_str(), headers, "", body, status, fout, poort, https );
            }
            if( !ok ) return false;
            try { uit = json::parse( body ); }
            catch( ... ) { fout = "unreadable reply"; return false; }
            return true;
        }
    }

    const char *VtcBron::Naam( VtcBronType t )
    {
        switch( t )
        {
            case VtcBronType::Bot:         return "CabNavi bot";
            case VtcBronType::Trucky:      return "Trucky";
            case VtcBronType::Horizon:     return "Horizon Dispatch";
            case VtcBronType::TruckersHub: return "TruckersHub";
            case VtcBronType::Vtlog:       return "VTLog";
            default:                       return "";
        }
    }

    // -----------------------------------------------------------------
    VtcBron::VtcBron( VtcWebhook &webhook ) : m_webhook( webhook )
    {
        Laad();
        m_thread = std::thread( [ this ] { WorkerLoop(); } );
    }

    VtcBron::~VtcBron()
    {
        m_stop = true;
        m_wekker.notify_all();
        if( m_thread.joinable() ) m_thread.join();
    }

    void VtcBron::Laad()
    {
        try
        {
            std::ifstream in( InstellingenPad() );
            if( !in ) return;
            json j; in >> j;
            const int t = Heel( j, "bron", 0 );
            m_type = ( t >= 0 && t <= 5 ) ? static_cast<VtcBronType>( t ) : VtcBronType::Geen;
            m_sleutel = Geheim::Ontsleutel( Tekst( j, "sleutel" ) );
            m_truckyUserId = Heel( j, "trucky_user_id", 0 );
            m_truckyCompanyId = Heel( j, "trucky_company_id", 0 );
            m_vtlogVtcId = Heel( j, "vtlog_vtc_id", 0 );
        }
        catch( ... ) { /* a broken file is the same as no file */ }
    }

    void VtcBron::Bewaar() const
    {
        try
        {
            json j;
            {
                std::lock_guard<std::mutex> lock( m_mutex );
                j[ "bron" ] = static_cast<int>( m_type );
                j[ "sleutel" ] = Geheim::Versleutel( m_sleutel );
                j[ "trucky_user_id" ] = m_truckyUserId;
                j[ "trucky_company_id" ] = m_truckyCompanyId;
                j[ "vtlog_vtc_id" ] = m_vtlogVtcId;
            }
            std::ofstream uit( InstellingenPad() );
            if( uit ) uit << j.dump( 2 );
        }
        catch( ... ) { /* saving must never disturb the game */ }
    }

    VtcOverzicht VtcBron::Overzicht() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_overzicht;
    }

    VtcBronType VtcBron::Type() const { std::lock_guard<std::mutex> lock( m_mutex ); return m_type; }
    std::string VtcBron::Sleutel() const { std::lock_guard<std::mutex> lock( m_mutex ); return m_sleutel; }

    void VtcBron::ZetType( VtcBronType t )
    {
        { std::lock_guard<std::mutex> lock( m_mutex ); m_type = t; m_overzicht = VtcOverzicht{}; }
        Bewaar();
        VerversNu();
    }

    void VtcBron::ZetSleutel( const std::string &sleutel )
    {
        { std::lock_guard<std::mutex> lock( m_mutex ); m_sleutel = sleutel; m_truckyUserId = 0; m_truckyCompanyId = 0; m_vtlogVtcId = 0; }
        Bewaar();
        VerversNu();
    }

    void VtcBron::ZetEigenSteamId( std::uint64_t steamId )
    {
        bool nieuw = false;
        { std::lock_guard<std::mutex> lock( m_mutex ); nieuw = ( steamId != 0 && steamId != m_steamId ); if( nieuw ) m_steamId = steamId; }
        if( nieuw ) VerversNu();
    }

    void VtcBron::AccepteerOpdracht( const std::string &id )
    {
        { std::lock_guard<std::mutex> lock( m_mutex ); m_teAccepteren.push_back( id ); }
        VerversNu();
    }

    void VtcBron::VerversNu()
    {
        m_nu = true;
        m_wekker.notify_all();
    }

    void VtcBron::WorkerLoop()
    {
        std::mutex slaapSlot;
        while( !m_stop )
        {
            Ververs();
            std::unique_lock<std::mutex> lock( slaapSlot );
            m_wekker.wait_for( lock, std::chrono::seconds( 60 ), [ this ] { return m_stop.load() || m_nu.load(); } );
            m_nu = false;
        }
    }

    void VtcBron::Ververs()
    {
        VtcBronType t;
        { std::lock_guard<std::mutex> lock( m_mutex ); t = m_type; }

        VtcOverzicht o;
        switch( t )
        {
            case VtcBronType::Bot:         o = HaalBot(); break;
            case VtcBronType::Trucky:      o = HaalTrucky(); break;
            case VtcBronType::Horizon:     o = HaalHorizon(); break;
            case VtcBronType::TruckersHub: o = HaalTruckersHub(); break;
            case VtcBronType::Vtlog:       o = HaalVtlog(); break;
            default: return;
        }
        o.bron = t;
        o.bronNaam = Naam( t );

        std::lock_guard<std::mutex> lock( m_mutex );
        if( m_type != t ) return;   // settings changed meanwhile; this poll is stale

        // A miss after a hit keeps the hit: the tab shows the last good data
        // with a note, instead of flipping to an error every other minute.
        if( !o.geldig && m_overzicht.geldig && m_overzicht.bron == t )
        {
            const std::string fout = o.status;
            o = m_overzicht;
            o.status = fout + " (previous data kept)";
        }
        // One log line per change of state, so a broken source is visible in
        // debug.log without flooding it every minute.
        if( o.status != m_overzicht.status || o.geldig != m_overzicht.geldig )
            Logboek::Schrijf( "bron", std::string( Naam( t ) ) + ": " + ( o.status.empty() ? "ok" : o.status ) );
        m_overzicht = o;
    }

    // -----------------------------------------------------------------
    // Bot: the trip-webhook address with /trip swapped for /dispatch.
    // -----------------------------------------------------------------
    VtcOverzicht VtcBron::HaalBot()
    {
        VtcOverzicht o;
        std::string url = m_webhook.EndpointUrl();
        const std::string sleutel = m_webhook.Sleutel();
        if( url.empty() || sleutel.empty() ) { o.status = "VTC webhook not set up"; return o; }

        const std::string staart = "/trip";
        if( url.size() >= staart.size() && url.compare( url.size() - staart.size(), staart.size(), staart ) == 0 )
            url = url.substr( 0, url.size() - staart.size() );
        while( !url.empty() && url.back() == '/' ) url.pop_back();

        std::wstring host, basis; int poort = 443; bool https = true;
        if( !SplitsUrl( url, host, basis, poort, https ) ) { o.status = "address must be https:// (http only to localhost)"; return o; }
        if( basis == L"/" ) basis.clear();
        const std::wstring headers = L"Authorization: Bearer " + Breed( sleutel ) + L"\r\n";

        // First anything the driver accepted since last time.
        std::vector<std::string> accepteren;
        { std::lock_guard<std::mutex> lock( m_mutex ); accepteren.swap( m_teAccepteren ); }
        for( const std::string &id : accepteren )
        {
            std::string body, fout; int status = 0;
            // Numeric ids go as numbers, anything else as a JSON string built
            // by the library, so a stray quote in an id cannot break the body.
            json lichaam;
            if( !id.empty() && id.find_first_not_of( "0123456789" ) == std::string::npos ) lichaam[ "id" ] = std::stoll( id );
            else lichaam[ "id" ] = id;
            const std::string data = lichaam.dump();
            HttpVerzoek( L"POST", host.c_str(), ( basis + L"/dispatch/accept" ).c_str(),
                         headers + L"Content-Type: application/json\r\n", data, body, status, fout, poort, https );
            if( status != 200 ) Logboek::Schrijf( "vtc", "dispatch accept " + id + ": " + fout );
        }

        json j; std::string fout;
        if( !Ophalen( host, basis + L"/dispatch", headers, j, fout, poort, https ) ) { o.status = fout; return o; }

        o.geldig = true;
        o.bedrijf = m_webhook.EndpointUrl();
        if( j.contains( "jobs" ) && j[ "jobs" ].is_array() )
        {
            for( const json &job : j[ "jobs" ] )
            {
                Opdracht op;
                op.id = Tekst( job, "id" );
                op.vertrek = Tekst( job, "from_city" );
                op.aankomst = Tekst( job, "to_city" );
                op.lading = Tekst( job, "cargo" );
                op.deadline = Tekst( job, "deadline" );
                op.notitie = Tekst( job, "note" );
                op.status = Tekst( job, "status", "open" );
                op.accepteerbaar = ( op.status == "open" );
                o.opdrachten.push_back( op );
            }
        }
        return o;
    }

    // -----------------------------------------------------------------
    // Trucky: Steam ID -> user -> company. Open endpoints; a company token
    // (optional, in the key field) unlocks the company's own events.
    // -----------------------------------------------------------------
    VtcOverzicht VtcBron::HaalTrucky()
    {
        VtcOverzicht o;
        std::uint64_t steam; std::string token; int userId, companyId;
        { std::lock_guard<std::mutex> lock( m_mutex ); steam = m_steamId; token = m_sleutel; userId = m_truckyUserId; companyId = m_truckyCompanyId; }
        if( steam == 0 ) { o.status = "waiting for TruckersMP (Steam ID)"; return o; }

        const std::wstring host = L"e.truckyapp.com";
        // Trucky refuses default user agents; the WinHTTP session name above
        // is "CabNavi/1.1", plus the explicit Accept it suggests.
        std::wstring headers = L"Accept: application/json\r\n";
        if( !token.empty() ) headers += L"X-ACCESS-TOKEN: " + Breed( token ) + L"\r\n";

        json u; std::string fout;
        if( !Ophalen( host, L"/api/v1/user/steam/" + std::to_wstring( steam ), headers, u, fout ) )
        {
            o.status = ( fout == "status 404" ) ? "not found on Trucky" : fout;
            return o;
        }
        // Some Trucky replies wrap in { "response": ... }.
        if( u.contains( "response" ) ) u = u[ "response" ];

        o.geldig = true;
        o.mijnNaam = Tekst( u, "name" );
        o.mijnKm = Getal( u, "total_driven_distance" );
        o.mijnRol = Tekst( Sub( u, "role" ), "name" );
        userId = Heel( u, "id", userId );
        companyId = Heel( u, "company_id", companyId );

        const json &c = Sub( u, "company" );
        o.bedrijf = Tekst( c, "name" );
        o.tag = Tekst( c, "tag" );
        o.leden = Heel( c, "members_count" );

        if( companyId > 0 )
        {
            json d;
            if( Ophalen( host, L"/api/v1/company/" + std::to_wstring( companyId ) + L"/dispatches", headers, d, fout ) )
            {
                if( d.contains( "response" ) ) d = d[ "response" ];
                if( d.is_array() )
                {
                    for( const json &disp : d )
                    {
                        const int voor = Heel( disp, "dispatched_to_user_id", 0 );
                        if( voor != 0 && voor != userId ) continue;   // someone else's
                        const json &data = Sub( disp, "data" );
                        Opdracht op;
                        op.id = Tekst( disp, "id" );
                        op.vertrek = Tekst( data, "source_city_name" );
                        op.aankomst = Tekst( data, "destination_city_name" );
                        op.lading = Tekst( data, "cargo_name" );
                        op.notitie = Tekst( disp, "description" );
                        op.status = Heel( disp, "claimed_by_user_id", 0 ) == userId ? "accepted" : "open";
                        op.accepteerbaar = false;   // claiming is done in the Trucky app
                        o.opdrachten.push_back( op );
                    }
                }
            }
            if( !token.empty() )
            {
                json ev;
                if( Ophalen( host, L"/api/v1/company/" + std::to_wstring( companyId ) + L"/events", headers, ev, fout ) )
                {
                    if( ev.contains( "response" ) ) ev = ev[ "response" ];
                    if( ev.contains( "data" ) ) ev = ev[ "data" ];
                    if( ev.is_array() )
                        for( const json &e : ev )
                        {
                            BronConvooi k;
                            k.titel = Tekst( e, "information" ).substr( 0, 60 );
                            if( k.titel.empty() ) k.titel = Tekst( e, "event_type" );
                            k.start = Tekst( e, "start_date" );
                            k.vertrek = Tekst( e, "start_city" );
                            k.aankomst = Tekst( e, "end_city" );
                            k.server = Tekst( e, "server" );
                            o.convooien.push_back( k );
                        }
                }
            }
        }

        bool bewaren = false;
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            if( userId != m_truckyUserId || companyId != m_truckyCompanyId ) { m_truckyUserId = userId; m_truckyCompanyId = companyId; bewaren = true; }
        }
        if( bewaren ) Bewaar();
        return o;
    }

    // -----------------------------------------------------------------
    // Horizon Dispatch: pk_ key, read only, 120 requests/minute/key.
    // -----------------------------------------------------------------
    VtcOverzicht VtcBron::HaalHorizon()
    {
        VtcOverzicht o;
        std::string sleutel;
        { std::lock_guard<std::mutex> lock( m_mutex ); sleutel = m_sleutel; }
        if( sleutel.empty() ) { o.status = "no pk_ key set"; return o; }

        const std::wstring host = L"hdispatch.eu";
        const std::wstring headers = L"x-api-key: " + Breed( sleutel ) + L"\r\n";
        json v; std::string fout;
        if( !Ophalen( host, L"/api/v1/vtc", headers, v, fout ) )
        {
            o.status = ( fout == "status 403" ) ? "key refused (5-boost tier needed)" : fout;
            return o;
        }
        o.geldig = true;
        o.bedrijf = Tekst( v, "name" );
        o.tag = Tekst( v, "tag" );
        o.leden = Heel( v, "memberCount" );
        o.totaalKm = Getal( Sub( v, "totals" ), "distanceKm" );

        json live;
        if( Ophalen( host, L"/api/v1/live", headers, live, fout ) && live.is_array() )
            for( const json &d : live ) { const std::string n = Tekst( d, "name", Tekst( d, "username" ) ); if( !n.empty() ) o.online.push_back( n ); }

        json ev;
        if( Ophalen( host, L"/api/v1/events", headers, ev, fout ) && ev.is_array() )
            for( const json &e : ev )
            {
                BronConvooi k;
                k.titel = Tekst( e, "title", Tekst( e, "name" ) );
                k.start = Tekst( e, "startAt", Tekst( e, "start" ) );
                k.vertrek = Tekst( e, "from", Tekst( e, "departure" ) );
                k.aankomst = Tekst( e, "to", Tekst( e, "arrival" ) );
                k.server = Tekst( e, "server" );
                o.convooien.push_back( k );
            }

        json news;
        if( Ophalen( host, L"/api/v1/news?limit=5", headers, news, fout ) && news.is_array() )
            for( const json &n : news ) o.nieuws.push_back( { Tekst( n, "title" ), Tekst( n, "publishedAt", Tekst( n, "createdAt" ) ) } );

        return o;
    }

    // -----------------------------------------------------------------
    // VTLog: Steam ID -> user (open) -> company (open). A VTC key unlocks
    // members and live; 10 requests per minute per key, we use at most 3.
    // -----------------------------------------------------------------
    VtcOverzicht VtcBron::HaalVtlog()
    {
        VtcOverzicht o;
        std::uint64_t steam; std::string sleutel; int vtcId;
        { std::lock_guard<std::mutex> lock( m_mutex ); steam = m_steamId; sleutel = m_sleutel; vtcId = m_vtlogVtcId; }
        if( steam == 0 ) { o.status = "waiting for TruckersMP (Steam ID)"; return o; }

        const std::wstring host = L"api.vtlog.net";
        const std::wstring open = L"Accept: application/json\r\n";
        json u; std::string fout;
        if( !Ophalen( host, L"/v2/user/" + std::to_wstring( steam ), open, u, fout ) )
        {
            o.status = ( fout == "status 404" ) ? "not found on VTLog" : fout;
            return o;
        }
        o.geldig = true;
        o.mijnNaam = Tekst( u, "username" );
        const int level = Heel( u, "level", 0 );
        if( level > 0 ) o.mijnRol = "level " + std::to_string( level );
        vtcId = Heel( u, "vtc_id", vtcId );

        if( vtcId > 0 )
        {
            json v;
            if( Ophalen( host, L"/v2/vtc/" + std::to_wstring( vtcId ), open, v, fout ) )
            {
                if( v.contains( "data" ) ) v = v[ "data" ];
                o.bedrijf = Tekst( v, "name" );
            }
            if( !sleutel.empty() )
            {
                const std::wstring keyed = open + L"Authorization: Bearer " + Breed( sleutel ) + L"\r\n";
                json leden;
                if( Ophalen( host, L"/v2/vtc/" + std::to_wstring( vtcId ) + L"/members", keyed, leden, fout ) )
                {
                    if( leden.contains( "data" ) ) leden = leden[ "data" ];
                    if( leden.is_array() ) o.leden = static_cast<int>( leden.size() );
                }
                json live;
                if( Ophalen( host, L"/v2/vtc/" + std::to_wstring( vtcId ) + L"/live", keyed, live, fout ) )
                {
                    if( live.contains( "data" ) ) live = live[ "data" ];
                    if( live.contains( "drivers" ) && live[ "drivers" ].is_array() )
                        for( const json &d : live[ "drivers" ] )
                        {
                            std::string n = Tekst( d, "username" );
                            const json &job = ( d.contains( "job" ) && d[ "job" ].is_object() ) ? d[ "job" ] : json::object();
                            if( job.contains( "destination" ) ) n += " -> " + Tekst( Sub( job, "destination" ), "city_id" );
                            if( !n.empty() ) o.online.push_back( n );
                        }
                }
                else if( fout == "status 403" || fout == "status 401" )
                    o.status = "VTC key refused";
            }
        }
        else o.status = "not in a VTC on VTLog";

        bool bewaren = false;
        { std::lock_guard<std::mutex> lock( m_mutex ); if( vtcId != m_vtlogVtcId ) { m_vtlogVtcId = vtcId; bewaren = true; } }
        if( bewaren ) Bewaar();
        return o;
    }

    // -----------------------------------------------------------------
    // TruckersHub: company token in Authorization, as is (no "Bearer").
    // -----------------------------------------------------------------
    VtcOverzicht VtcBron::HaalTruckersHub()
    {
        VtcOverzicht o;
        std::string token; std::uint64_t steam;
        { std::lock_guard<std::mutex> lock( m_mutex ); token = m_sleutel; steam = m_steamId; }
        if( token.empty() ) { o.status = "no company token set"; return o; }

        const std::wstring host = L"api.truckershub.in";
        const std::wstring headers = L"Authorization: " + Breed( token ) + L"\r\nContent-Type: application/json\r\n";
        json me; std::string fout;
        if( !Ophalen( host, L"/v1/me", headers, me, fout ) )
        {
            o.status = ( fout == "status 401" || fout == "status 403" ) ? "token refused" : fout;
            return o;
        }
        o.geldig = true;
        o.bedrijf = Tekst( me, "name" );
        o.tag = Tekst( me, "tag" );
        o.leden = Heel( me, "members", Heel( me, "memberCount" ) );

        if( steam != 0 )
        {
            json d;
            if( Ophalen( host, L"/v1/drivers/" + std::to_wstring( steam ), headers, d, fout ) )
            {
                o.mijnNaam = Tekst( d, "username", Tekst( d, "name" ) );
                o.mijnKm = Getal( d, "distance", Getal( d, "totalDistance" ) );
                o.mijnRitten = Heel( d, "jobs", Heel( d, "totalJobs" ) );
            }
        }

        json live;
        if( Ophalen( host, L"/v1/live/drivers", headers, live, fout ) && live.is_array() )
            for( const json &x : live ) { const std::string n = Tekst( x, "username", Tekst( x, "name" ) ); if( !n.empty() ) o.online.push_back( n ); }

        json ev;
        if( Ophalen( host, L"/v1/events/vtc", headers, ev, fout ) || Ophalen( host, L"/v1/events", headers, ev, fout ) )
        {
            if( ev.contains( "events" ) ) ev = ev[ "events" ];
            if( ev.is_array() )
                for( const json &e : ev )
                {
                    BronConvooi k;
                    k.titel = Tekst( e, "title" );
                    k.start = Tekst( e, "start", Tekst( e, "meetup" ) );
                    k.vertrek = Tekst( e, "sourceCity" );
                    k.aankomst = Tekst( e, "destCity" );
                    k.server = Tekst( e, "server" );
                    o.convooien.push_back( k );
                }
        }
        return o;
    }
}

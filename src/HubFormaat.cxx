#include "HubFormaat.hxx"
#include "Spel.hxx"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace Ritten::HubFormaat
{
    // =====================================================================
    // SHA-256 (FIPS 180-4), small and dependency-free.
    // =====================================================================
    namespace
    {
        inline std::uint32_t Rotr( std::uint32_t x, int n ) { return ( x >> n ) | ( x << ( 32 - n ) ); }

        const std::uint32_t K[ 64 ] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };

        std::vector<std::uint8_t> Sha256( const std::uint8_t *data, std::size_t len )
        {
            std::uint32_t h[ 8 ] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
            std::vector<std::uint8_t> msg( data, data + len );
            msg.push_back( 0x80 );
            while( msg.size() % 64 != 56 ) msg.push_back( 0 );
            const std::uint64_t bits = static_cast<std::uint64_t>( len ) * 8;
            for( int i = 7; i >= 0; --i ) msg.push_back( static_cast<std::uint8_t>( bits >> ( i * 8 ) ) );

            for( std::size_t blok = 0; blok < msg.size(); blok += 64 )
            {
                std::uint32_t w[ 64 ];
                for( int i = 0; i < 16; ++i )
                    w[ i ] = ( std::uint32_t( msg[ blok + i * 4 ] ) << 24 ) | ( std::uint32_t( msg[ blok + i * 4 + 1 ] ) << 16 )
                           | ( std::uint32_t( msg[ blok + i * 4 + 2 ] ) << 8 ) | std::uint32_t( msg[ blok + i * 4 + 3 ] );
                for( int i = 16; i < 64; ++i )
                {
                    const std::uint32_t s0 = Rotr( w[ i - 15 ], 7 ) ^ Rotr( w[ i - 15 ], 18 ) ^ ( w[ i - 15 ] >> 3 );
                    const std::uint32_t s1 = Rotr( w[ i - 2 ], 17 ) ^ Rotr( w[ i - 2 ], 19 ) ^ ( w[ i - 2 ] >> 10 );
                    w[ i ] = w[ i - 16 ] + s0 + w[ i - 7 ] + s1;
                }
                std::uint32_t a = h[ 0 ], b = h[ 1 ], c = h[ 2 ], d = h[ 3 ], e = h[ 4 ], f = h[ 5 ], g = h[ 6 ], hh = h[ 7 ];
                for( int i = 0; i < 64; ++i )
                {
                    const std::uint32_t S1 = Rotr( e, 6 ) ^ Rotr( e, 11 ) ^ Rotr( e, 25 );
                    const std::uint32_t ch = ( e & f ) ^ ( ~e & g );
                    const std::uint32_t t1 = hh + S1 + ch + K[ i ] + w[ i ];
                    const std::uint32_t S0 = Rotr( a, 2 ) ^ Rotr( a, 13 ) ^ Rotr( a, 22 );
                    const std::uint32_t maj = ( a & b ) ^ ( a & c ) ^ ( b & c );
                    const std::uint32_t t2 = S0 + maj;
                    hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
                }
                h[ 0 ] += a; h[ 1 ] += b; h[ 2 ] += c; h[ 3 ] += d; h[ 4 ] += e; h[ 5 ] += f; h[ 6 ] += g; h[ 7 ] += hh;
            }
            std::vector<std::uint8_t> uit( 32 );
            for( int i = 0; i < 8; ++i )
                for( int j = 0; j < 4; ++j ) uit[ i * 4 + j ] = static_cast<std::uint8_t>( h[ i ] >> ( 24 - j * 8 ) );
            return uit;
        }

        std::string Hex( const std::vector<std::uint8_t> &b )
        {
            static const char *H = "0123456789abcdef";
            std::string s; s.reserve( b.size() * 2 );
            for( std::uint8_t x : b ) { s += H[ x >> 4 ]; s += H[ x & 15 ]; }
            return s;
        }

        std::vector<std::uint8_t> Hmac( const std::string &sleutel, const std::string &boodschap )
        {
            std::vector<std::uint8_t> k( sleutel.begin(), sleutel.end() );
            if( k.size() > 64 ) k = Sha256( k.data(), k.size() );
            k.resize( 64, 0 );
            std::vector<std::uint8_t> ipad( 64 ), opad( 64 );
            for( int i = 0; i < 64; ++i ) { ipad[ i ] = k[ i ] ^ 0x36; opad[ i ] = k[ i ] ^ 0x5c; }
            std::vector<std::uint8_t> binnen( ipad );
            binnen.insert( binnen.end(), boodschap.begin(), boodschap.end() );
            const std::vector<std::uint8_t> h1 = Sha256( binnen.data(), binnen.size() );
            std::vector<std::uint8_t> buiten( opad );
            buiten.insert( buiten.end(), h1.begin(), h1.end() );
            return Sha256( buiten.data(), buiten.size() );
        }
    }
}

namespace Ritten
{
    using namespace HubFormaat;

    std::string HubFormaat::Sha256Hex( const std::string &data )
    {
        return Hex( Sha256( reinterpret_cast<const std::uint8_t *>( data.data() ), data.size() ) );
    }

    std::string HubFormaat::Handtekening( const std::string &geheim, const std::string &body )
    {
        return Hex( Hmac( geheim, body ) );
    }

    // =====================================================================
    // Python-compatible JSON writer. Objects keep insertion order.
    // =====================================================================
    std::string HubFormaat::PyFloat( double v )
    {
        if( std::isnan( v ) || std::isinf( v ) ) return "null";
        char buf[ 32 ];
        const auto r = std::to_chars( buf, buf + sizeof( buf ), v );   // shortest round-trip, like Python repr
        std::string s( buf, r.ptr );
        if( s.find_first_of( ".eE" ) == std::string::npos ) s += ".0";
        return s;
    }

    std::string HubFormaat::PyString( const std::string &s )
    {
        // ensure_ascii=True: anything above 0x7F becomes \uXXXX (surrogate
        // pairs beyond the BMP), the usual control escapes otherwise.
        std::string uit = "\"";
        std::size_t i = 0;
        while( i < s.size() )
        {
            const unsigned char c = static_cast<unsigned char>( s[ i ] );
            std::uint32_t cp = 0; int extra = 0;
            if( c < 0x80 ) { cp = c; extra = 0; }
            else if( ( c & 0xE0 ) == 0xC0 ) { cp = c & 0x1F; extra = 1; }
            else if( ( c & 0xF0 ) == 0xE0 ) { cp = c & 0x0F; extra = 2; }
            else if( ( c & 0xF8 ) == 0xF0 ) { cp = c & 0x07; extra = 3; }
            else { ++i; continue; }   // stray byte: drop it
            if( i + extra >= s.size() + ( extra == 0 ? 1 : 0 ) && extra > 0 && i + extra > s.size() - 1 + 1 ) { break; }
            for( int k = 1; k <= extra; ++k )
            {
                if( i + k >= s.size() ) { cp = 0xFFFD; break; }
                cp = ( cp << 6 ) | ( static_cast<unsigned char>( s[ i + k ] ) & 0x3F );
            }
            i += extra + 1;

            auto hex4 = [ & ]( std::uint32_t u )
            {
                char b[ 8 ]; std::snprintf( b, sizeof( b ), "\\u%04x", u & 0xFFFF ); uit += b;
            };
            switch( cp )
            {
                case '"':  uit += "\\\""; break;
                case '\\': uit += "\\\\"; break;
                case '\n': uit += "\\n"; break;
                case '\r': uit += "\\r"; break;
                case '\t': uit += "\\t"; break;
                case '\b': uit += "\\b"; break;
                case '\f': uit += "\\f"; break;
                default:
                    if( cp < 0x20 ) hex4( cp );
                    else if( cp < 0x80 ) uit += static_cast<char>( cp );
                    else if( cp < 0x10000 ) hex4( cp );
                    else { cp -= 0x10000; hex4( 0xD800 | ( cp >> 10 ) ); hex4( 0xDC00 | ( cp & 0x3FF ) ); }
            }
        }
        uit += "\"";
        return uit;
    }

    namespace
    {
        // A tiny ordered JSON builder: enough for this one document.
        struct Node;
        using Obj = std::vector<std::pair<std::string, Node>>;
        struct Node
        {
            enum Soort { Null, Bool, Int, Float, Str, Object, Array } soort = Null;
            bool b = false; long long i = 0; double f = 0.0; std::string s;
            std::vector<std::pair<std::string, Node>> obj;
            std::vector<Node> arr;

            static Node Nul() { return Node{}; }
            static Node B( bool v ) { Node n; n.soort = Bool; n.b = v; return n; }
            static Node I( long long v ) { Node n; n.soort = Int; n.i = v; return n; }
            static Node F( double v ) { Node n; n.soort = Float; n.f = v; return n; }
            static Node S( const std::string &v ) { Node n; n.soort = Str; n.s = v; return n; }
            static Node O() { Node n; n.soort = Object; return n; }
            static Node A() { Node n; n.soort = Array; return n; }
            Node &Zet( const std::string &k, Node v ) { obj.emplace_back( k, std::move( v ) ); return *this; }
            Node &Voeg( Node v ) { arr.push_back( std::move( v ) ); return *this; }
        };

        void Schrijf( const Node &n, std::string &uit )
        {
            switch( n.soort )
            {
                case Node::Null:   uit += "null"; break;
                case Node::Bool:   uit += n.b ? "true" : "false"; break;
                case Node::Int:    uit += std::to_string( n.i ); break;
                case Node::Float:  uit += PyFloat( n.f ); break;
                case Node::Str:    uit += PyString( n.s ); break;
                case Node::Object:
                    uit += "{";
                    for( std::size_t k = 0; k < n.obj.size(); ++k )
                    {
                        if( k ) uit += ", ";
                        uit += PyString( n.obj[ k ].first ); uit += ": ";
                        Schrijf( n.obj[ k ].second, uit );
                    }
                    uit += "}"; break;
                case Node::Array:
                    uit += "[";
                    for( std::size_t k = 0; k < n.arr.size(); ++k )
                    {
                        if( k ) uit += ", ";
                        Schrijf( n.arr[ k ], uit );
                    }
                    uit += "]"; break;
            }
        }

        // "Dans le Jardin" -> "dans_le_jardin": the closest we get to the
        // game's own token without the def files. Documented as such.
        std::string Token( const std::string &naam )
        {
            std::string t;
            for( unsigned char c : naam )
            {
                if( std::isalnum( c ) ) t += static_cast<char>( std::tolower( c ) );
                else if( c == ' ' || c == '-' || c == '_' ) { if( !t.empty() && t.back() != '_' ) t += '_'; }
            }
            while( !t.empty() && t.back() == '_' ) t.pop_back();
            return t;
        }

        Node Plaats( const std::string &naam )
        {
            Node p = Node::O();
            p.Zet( "unique_id", Node::S( Token( naam ) ) );
            p.Zet( "name", naam.empty() ? Node::Nul() : Node::S( naam ) );
            return p;
        }

        Node Schade( double cabine, double chassis, double motor, double bak, double wielen )
        {
            Node d = Node::O();
            d.Zet( "cabin", Node::F( std::max( 0.0, cabine ) / 100.0 ) );
            d.Zet( "chassis", Node::F( std::max( 0.0, chassis ) / 100.0 ) );
            d.Zet( "engine", Node::F( std::max( 0.0, motor ) / 100.0 ) );
            d.Zet( "transmission", Node::F( std::max( 0.0, bak ) / 100.0 ) );
            d.Zet( "wheels", Node::F( std::max( 0.0, wielen ) / 100.0 ) );
            return d;
        }

        Node Event( const char *type, const std::string &tijd, Node meta, double x = 0.0, double z = 0.0 )
        {
            Node e = Node::O();
            Node loc = Node::O();
            loc.Zet( "x", Node::F( x ) ).Zet( "y", Node::F( 0.0 ) ).Zet( "z", Node::F( z ) );
            e.Zet( "location", loc );
            e.Zet( "real_time", Node::S( tijd ) );
            e.Zet( "time", Node::I( 0 ) );
            e.Zet( "type", Node::S( type ) );
            e.Zet( "meta", meta );
            return e;
        }

        std::string IsoVanUnix( long long t )
        {
            if( t <= 0 ) return "";
            // civil-from-days, UTC
            long long z = t / 86400; const long long rest = t - z * 86400;
            z += 719468;
            const long long era = ( z >= 0 ? z : z - 146096 ) / 146097;
            const long long doe = z - era * 146097;
            const long long yoe = ( doe - doe / 1460 + doe / 36524 - doe / 146096 ) / 365;
            long long y = yoe + era * 400;
            const long long doy = doe - ( 365 * yoe + yoe / 4 - yoe / 100 );
            const long long mp = ( 5 * doy + 2 ) / 153;
            const long long d = doy - ( 153 * mp + 2 ) / 5 + 1;
            const long long m = mp + ( mp < 10 ? 3 : -9 );
            if( m <= 2 ) ++y;
            char buf[ 32 ];
            std::snprintf( buf, sizeof( buf ), "%04lld-%02lld-%02lldT%02lld:%02lld:%02lldZ",
                           y, m, d, rest / 3600, ( rest % 3600 ) / 60, rest % 60 );
            return buf;
        }

        long long Seconden( const std::string &isoStart, const std::string &isoEind )
        {
            // "YYYY-MM-DDTHH:MM:SSZ" -> seconds, enough for a duration.
            auto naarTijd = []( const std::string &iso ) -> long long
            {
                if( iso.size() < 19 ) return 0;
                std::tm t{};
                t.tm_year = std::stoi( iso.substr( 0, 4 ) ) - 1900; t.tm_mon = std::stoi( iso.substr( 5, 2 ) ) - 1;
                t.tm_mday = std::stoi( iso.substr( 8, 2 ) );  t.tm_hour = std::stoi( iso.substr( 11, 2 ) );
                t.tm_min = std::stoi( iso.substr( 14, 2 ) );  t.tm_sec = std::stoi( iso.substr( 17, 2 ) );
                // days-from-civil, UTC, no timezone games
                long long y = t.tm_year + 1900, m = t.tm_mon + 1, d = t.tm_mday;
                y -= m <= 2; const long long era = ( y >= 0 ? y : y - 399 ) / 400;
                const long long yoe = y - era * 400;
                const long long doy = ( 153 * ( m + ( m > 2 ? -3 : 9 ) ) + 2 ) / 5 + d - 1;
                const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
                const long long dagen = era * 146097 + doe - 719468;
                return dagen * 86400 + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
            };
            const long long a = naarTijd( isoStart ), b = naarTijd( isoEind );
            return ( a >= 0 && b > a ) ? b - a : 0;
        }
    }

    std::string HubFormaat::BouwBody( const Trip &trip, const HubFormaat::Context &ctx )
    {
        const bool geannuleerd = trip.status == TripStatus::Geannuleerd;
        const long long tijd = Seconden( trip.startTijdIso, trip.eindTijdIso );

        // --- driver ---
        Node driver = Node::O();
        driver.Zet( "id", Node::I( 0 ) );
        driver.Zet( "steam_id", Node::S( std::to_string( ctx.steamId ) ) );
        driver.Zet( "username", Node::S( ctx.spelerNaam ) );
        driver.Zet( "profile_photo_url", Node::Nul() );
        {
            Node versie = Node::O();
            versie.Zet( "name", Node::S( ctx.clientVersie ) ).Zet( "branch_name", Node::S( "stable" ) ).Zet( "platform", Node::S( "windows" ) );
            Node client = Node::O(); client.Zet( "version", versie );
            driver.Zet( "client", client );
        }
        driver.Zet( "last_active", Node::S( trip.eindTijdIso ) );

        // --- cargo ---
        Node cargo = Node::O();
        cargo.Zet( "unique_id", Node::S( Token( trip.lading ) ) );
        cargo.Zet( "name", trip.lading.empty() ? Node::Nul() : Node::S( trip.lading ) );
        cargo.Zet( "mass", Node::F( trip.ladingGewichtKg ) );
        cargo.Zet( "damage", Node::F( std::max( 0.0, trip.ladingSchadePercentage ) / 100.0 ) );

        // --- game / multiplayer ---
        Node game = Node::O();
        game.Zet( "short_name", Node::S( ctx.game ) ).Zet( "language", Node::Nul() ).Zet( "had_police_enabled", Node::B( false ) );
        if( !ctx.realistischeInstellingen.empty() )
        {
            Node rs = Node::O();
            for( const auto &[ k, v ] : ctx.realistischeInstellingen ) rs.Zet( k, Node::B( v ) );
            game.Zet( "realistic_settings", rs );
        }
        Node mp = Node::O();
        {
            Node meta = Node::O();
            meta.Zet( "player_id", ctx.tmpSpelerId.empty() ? Node::Nul() : Node::S( ctx.tmpSpelerId ) );
            meta.Zet( "server", trip.serverNaam.empty() ? Node::Nul() : Node::S( trip.serverNaam ) );
            mp.Zet( "mode", Node::S( "truckersmp" ) ).Zet( "meta", meta );
        }

        // --- truck ---
        Node truck = Node::O();
        truck.Zet( "unique_id", Node::S( "vehicle." + Token( trip.voertuigMerk ) + "." + Token( trip.voertuigModel ) ) );
        truck.Zet( "name", trip.voertuigModel.empty() ? Node::Nul() : Node::S( trip.voertuigModel ) );
        {
            Node merk = Node::O();
            merk.Zet( "unique_id", Node::S( Token( trip.voertuigMerk ) ) ).Zet( "name", trip.voertuigMerk.empty() ? Node::Nul() : Node::S( trip.voertuigMerk ) );
            truck.Zet( "brand", merk );
        }
        const long long odoEind = static_cast<long long>( std::lround( ctx.kmStandEind ) );
        const long long odoStart = static_cast<long long>( std::lround( std::max( 0.0, ctx.kmStandEind - trip.afgelegdeAfstandKm ) ) );
        truck.Zet( "odometer", Node::I( odoEind ) );
        truck.Zet( "initial_odometer", Node::I( odoStart ) );
        truck.Zet( "wheel_count", Node::I( 0 ) );
        truck.Zet( "license_plate", Node::S( ctx.kenteken ) );
        {
            Node land = Node::O();
            land.Zet( "unique_id", Node::S( ctx.kentekenLand ) );
            land.Zet( "name", ctx.kentekenLand.empty() ? Node::Nul() : Node::S( ctx.kentekenLand ) );
            truck.Zet( "license_plate_country", land );
        }
        truck.Zet( "current_damage", Schade( ctx.schadeCabine, ctx.schadeChassis, ctx.schadeMotor, ctx.schadeBak, ctx.schadeWielen ) );
        truck.Zet( "total_damage", Schade( ctx.schadeCabine, ctx.schadeChassis, ctx.schadeMotor, ctx.schadeBak, ctx.schadeWielen ) );
        // Speeds in METRES PER SECOND: receiving hubs multiply by 3.6 to get
        // km/h (verified in an open-source hub's parser). Average from distance
        // and time; top speed is not tracked yet, so the same figure goes in
        // rather than a made-up one.
        const double gemKmh = ( tijd > 0 ) ? trip.afgelegdeAfstandKm / ( tijd / 3600.0 ) : 0.0;
        const double gemMs = std::round( gemKmh / 3.6 * 100.0 ) / 100.0;
        const double topMs = ctx.topKmh > 0.0 ? std::round( ctx.topKmh / 3.6 * 100.0 ) / 100.0 : gemMs;
        truck.Zet( "top_speed", Node::F( topMs ) );
        truck.Zet( "average_speed", Node::F( gemMs ) );

        // --- trailers ---
        Node trailers = Node::A();
        {
            Node tr = Node::O();
            tr.Zet( "name", Node::Nul() ).Zet( "body_type", Node::Nul() ).Zet( "chain_type", Node::S( "single" ) )
              .Zet( "wheel_count", Node::I( 0 ) ).Zet( "brand", Node::Nul() ).Zet( "license_plate", Node::S( "" ) );
            Node land = Node::O(); land.Zet( "unique_id", Node::S( "" ) ).Zet( "name", Node::Nul() );
            tr.Zet( "license_plate_country", land );
            Node cd = Node::O();
            cd.Zet( "cargo", Node::F( std::max( 0.0, trip.ladingSchadePercentage ) / 100.0 ) )
              .Zet( "chassis", Node::F( std::max( 0.0, trip.aanhangerSchadePercentage ) / 100.0 ) ).Zet( "wheels", Node::F( 0.0 ) );
            tr.Zet( "current_damage", cd ).Zet( "total_damage", cd );
            trailers.Voeg( tr );
        }

        // --- events: what CabNavi already knows about this trip ---
        Node events = Node::A();
        {
            Node m = Node::O(); m.Zet( "autoLoaded", Node::B( false ) );
            events.Voeg( Event( "job.started", trip.startTijdIso, m ) );
        }
        for( const auto &t : ctx.tankbeurten )
        {
            Node m = Node::O(); m.Zet( "amount", Node::I( static_cast<long long>( std::lround( t.kosten ) ) ) );
            events.Voeg( Event( "refuel", trip.eindTijdIso, m ) );
        }
        for( const auto &b : trip.boetes )
        {
            Node m = Node::O(); m.Zet( "offence", Node::S( b.reden ) ).Zet( "amount", Node::I( b.bedrag ) );
            events.Voeg( Event( "fine", trip.eindTijdIso, m ) );
        }
        for( const auto &d : trip.doorgangen )
        {
            Node m = Node::O(); m.Zet( "cost", Node::I( d.bedrag ) );
            const char *type = "tollgate";
            if( d.type == DoorgangType::Veerboot ) { type = "ferry"; m.Zet( "source_id", Node::S( Token( d.vanaf ) ) ).Zet( "source_name", Node::S( d.vanaf ) ).Zet( "target_id", Node::S( Token( d.naar ) ) ).Zet( "target_name", Node::S( d.naar ) ); }
            else if( d.type == DoorgangType::Trein ) { type = "train"; m.Zet( "source_id", Node::S( Token( d.vanaf ) ) ).Zet( "source_name", Node::S( d.vanaf ) ).Zet( "target_id", Node::S( Token( d.naar ) ) ).Zet( "target_name", Node::S( d.naar ) ); }
            events.Voeg( Event( type, trip.eindTijdIso, m ) );
        }
        for( const auto &ov : ctx.overtredingen )
        {
            // Both spellings: the docs say `speed`, an open-source hub reads
            // `max_speed`. km/h, like `speed_limit` -- the hub only compares them.
            Node m = Node::O();
            m.Zet( "speed", Node::F( std::round( ov.maxKmh * 10.0 ) / 10.0 ) )
             .Zet( "max_speed", Node::F( std::round( ov.maxKmh * 10.0 ) / 10.0 ) )
             .Zet( "speed_limit", Node::F( ov.limietKmh ) )
             .Zet( "duration", Node::I( ov.eind - ov.start ) );
            events.Voeg( Event( "speeding", IsoVanUnix( ov.start ), m, ov.x, ov.z ) );
        }
        if( geannuleerd )
        {
            Node m = Node::O(); m.Zet( "penalty", Node::I( 0 ) );
            events.Voeg( Event( "job.cancelled", trip.eindTijdIso, m ) );
        }
        else
        {
            Node m = Node::O();
            m.Zet( "revenue", Node::I( trip.inkomen ) ).Zet( "earnedXP", Node::I( 0 ) )
             .Zet( "cargoDamage", Node::F( std::max( 0.0, trip.ladingSchadePercentage ) / 100.0 ) )
             .Zet( "distance", Node::I( static_cast<long long>( std::lround( trip.afgelegdeAfstandKm ) ) ) )
             .Zet( "timeTaken", Node::I( tijd ) ).Zet( "autoPark", Node::B( false ) );
            events.Voeg( Event( "job.delivered", trip.eindTijdIso, m ) );
        }

        // --- the job ---
        Node job = Node::O();
        // The job id hubs deduplicate on: the trip's start time in unix
        // seconds. Unique per driver, increasing, and no hash collisions.
        // Falls back to a hash of CabNavi's own id only when the start time
        // is unreadable.
        long long ritId = Seconden( "1970-01-01T00:00:00Z", trip.startTijdIso );
        if( ritId <= 0 ) ritId = static_cast<long long>( std::hash<std::string>{}( trip.id ) & 0x7fffffff );
        job.Zet( "id", Node::I( ritId ) );
        job.Zet( "object", Node::S( "job" ) );
        job.Zet( "driver", driver );
        job.Zet( "start_time", Node::S( trip.startTijdIso ) );
        job.Zet( "stop_time", Node::S( trip.eindTijdIso ) );
        job.Zet( "time_spent", Node::I( tijd ) );
        job.Zet( "planned_distance", Node::I( static_cast<long long>( std::lround( trip.geplandeAfstandKm ) ) ) );
        job.Zet( "driven_distance", Node::I( static_cast<long long>( std::lround( trip.afgelegdeAfstandKm ) ) ) );
        job.Zet( "adblue_used", Node::I( 0 ) );
        // Consumption is stored as a negative delta in this format.
        job.Zet( "fuel_used", Node::F( -std::round( trip.brandstofVerbruikLiters * 100.0 ) / 100.0 ) );
        job.Zet( "is_special", Node::B( false ) );
        job.Zet( "is_late", Node::B( !trip.opTijd ) );
        job.Zet( "market", Node::S( "cargo_market" ) );
        job.Zet( "cargo", cargo );
        job.Zet( "game", game );
        job.Zet( "multiplayer", mp );
        job.Zet( "source_city", Plaats( trip.bronStad ) );
        job.Zet( "source_company", Plaats( trip.bronBedrijf ) );
        job.Zet( "destination_city", Plaats( trip.bestemmingStad ) );
        job.Zet( "destination_company", Plaats( trip.bestemmingBedrijf ) );
        job.Zet( "truck", truck );
        job.Zet( "trailers", trailers );
        job.Zet( "events", events );
        // A ferry or train crossing is a position jump too, but a legitimate
        // one: subtract those passages from what the trip's memory counted.
        int overtochten = 0;
        for( const auto &d : trip.doorgangen )
            if( d.type == DoorgangType::Veerboot || d.type == DoorgangType::Trein ) ++overtochten;
        job.Zet( "warp", Node::I( std::max( 0, ctx.teleports - overtochten ) ) );
        if( !ctx.route.empty() )
        {
            Node route = Node::A();
            for( const auto &p : ctx.route )
            {
                Node pt = Node::O();
                pt.Zet( "time", Node::I( p.tijd ) ).Zet( "x", Node::F( std::round( p.x * 10.0 ) / 10.0 ) ).Zet( "z", Node::F( std::round( p.z * 10.0 ) / 10.0 ) );
                route.Voeg( pt );
            }
            job.Zet( "route", route );
        }

        // --- the envelope ---
        Node data = Node::O();
        data.Zet( "object", job );
        Node env = Node::O();
        env.Zet( "object", Node::S( "event" ) );
        env.Zet( "type", Node::S( geannuleerd ? "job.cancelled" : "job.delivered" ) );
        env.Zet( "data", data );

        std::string uit;
        Schrijf( env, uit );
        return uit;
    }
}

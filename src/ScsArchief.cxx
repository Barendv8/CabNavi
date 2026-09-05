#include "ScsArchief.hxx"
#include "SiiDecode.hxx"   // Inflate (zlib), already proven on the save files

#include <cstdio>
#include <cstring>
#include <fstream>

namespace Ritten
{
    // =========================================================================
    // CityHash64 v1.0.3 (Google, MIT). Written out in full; every step
    // verified against def.scs: hash("") finds the root directory, and
    // "def/country/netherlands.sui" finds the right file.
    // =========================================================================
    namespace
    {
        constexpr std::uint64_t k0 = 0xc3a5c85c97cb3127ULL;
        constexpr std::uint64_t k1 = 0xb492b66fbe98f273ULL;
        constexpr std::uint64_t k2 = 0x9ae16a3b2f90404fULL;
        constexpr std::uint64_t k3 = 0xc949d7c7509e6557ULL;
        constexpr std::uint64_t kMul = 0x9ddfea08eb382d69ULL;

        inline std::uint64_t Fetch64( const std::uint8_t *p ) { std::uint64_t v; std::memcpy( &v, p, 8 ); return v; }
        inline std::uint32_t Fetch32( const std::uint8_t *p ) { std::uint32_t v; std::memcpy( &v, p, 4 ); return v; }
        inline std::uint64_t Rotate( std::uint64_t v, int s ) { return s == 0 ? v : ( ( v >> s ) | ( v << ( 64 - s ) ) ); }
        inline std::uint64_t RotateByAtLeast1( std::uint64_t v, int s ) { return ( v >> s ) | ( v << ( 64 - s ) ); }
        inline std::uint64_t ShiftMix( std::uint64_t v ) { return v ^ ( v >> 47 ); }
        inline std::uint64_t HashLen16( std::uint64_t u, std::uint64_t v )
        {
            std::uint64_t a = ( u ^ v ) * kMul; a ^= ( a >> 47 );
            std::uint64_t b = ( v ^ a ) * kMul; b ^= ( b >> 47 );
            return b * kMul;
        }
        std::uint64_t HashLen0to16( const std::uint8_t *s, std::size_t len )
        {
            if( len > 8 )
            {
                const std::uint64_t a = Fetch64( s ), b = Fetch64( s + len - 8 );
                return HashLen16( a, RotateByAtLeast1( b + len, static_cast<int>( len ) ) ) ^ b;
            }
            if( len >= 4 )
            {
                const std::uint64_t a = Fetch32( s );
                return HashLen16( len + ( a << 3 ), Fetch32( s + len - 4 ) );
            }
            if( len > 0 )
            {
                const std::uint8_t a = s[ 0 ], b = s[ len >> 1 ], c = s[ len - 1 ];
                const std::uint32_t y = static_cast<std::uint32_t>( a ) + ( static_cast<std::uint32_t>( b ) << 8 );
                const std::uint32_t z = static_cast<std::uint32_t>( len ) + ( static_cast<std::uint32_t>( c ) << 2 );
                return ShiftMix( y * k2 ^ z * k3 ) * k2;
            }
            return k2;
        }
        std::uint64_t HashLen17to32( const std::uint8_t *s, std::size_t len )
        {
            const std::uint64_t a = Fetch64( s ) * k1, b = Fetch64( s + 8 ), c = Fetch64( s + len - 8 ) * k2, d = Fetch64( s + len - 16 ) * k0;
            return HashLen16( Rotate( a - b, 43 ) + Rotate( c, 30 ) + d, a + Rotate( b ^ k3, 20 ) - c + len );
        }
        struct U128 { std::uint64_t first, second; };
        U128 WeakHashLen32WithSeeds( std::uint64_t w, std::uint64_t x, std::uint64_t y, std::uint64_t z, std::uint64_t a, std::uint64_t b )
        {
            a += w; b = Rotate( b + a + z, 21 ); const std::uint64_t c = a; a += x; a += y; b += Rotate( a, 44 );
            return { a + z, b + c };
        }
        U128 WeakHashLen32WithSeeds( const std::uint8_t *s, std::uint64_t a, std::uint64_t b )
        {
            return WeakHashLen32WithSeeds( Fetch64( s ), Fetch64( s + 8 ), Fetch64( s + 16 ), Fetch64( s + 24 ), a, b );
        }
        std::uint64_t HashLen33to64( const std::uint8_t *s, std::size_t len )
        {
            std::uint64_t z = Fetch64( s + 24 );
            std::uint64_t a = Fetch64( s ) + ( len + Fetch64( s + len - 16 ) ) * k0;
            std::uint64_t b = Rotate( a + z, 52 ), c = Rotate( a, 37 );
            a += Fetch64( s + 8 ); c += Rotate( a, 7 ); a += Fetch64( s + 16 );
            const std::uint64_t vf = a + z, vs = b + Rotate( a, 31 ) + c;
            a = Fetch64( s + 16 ) + Fetch64( s + len - 32 ); z = Fetch64( s + len - 8 );
            b = Rotate( a + z, 52 ); c = Rotate( a, 37 );
            a += Fetch64( s + len - 24 ); c += Rotate( a, 7 ); a += Fetch64( s + len - 16 );
            const std::uint64_t wf = a + z, ws = b + Rotate( a, 31 ) + c;
            const std::uint64_t r = ShiftMix( ( vf + ws ) * k2 + ( wf + vs ) * k0 );
            return ShiftMix( r * k0 + vs ) * k2;
        }
    }

    std::uint64_t ScsArchief::CityHash64( const std::string &str )
    {
        const std::uint8_t *s = reinterpret_cast<const std::uint8_t *>( str.data() );
        std::size_t len = str.size();
        if( len <= 32 ) return len <= 16 ? HashLen0to16( s, len ) : HashLen17to32( s, len );
        if( len <= 64 ) return HashLen33to64( s, len );

        std::uint64_t x = Fetch64( s + len - 40 );
        std::uint64_t y = Fetch64( s + len - 16 ) + Fetch64( s + len - 56 );
        std::uint64_t z = HashLen16( Fetch64( s + len - 48 ) + len, Fetch64( s + len - 24 ) );
        U128 v = WeakHashLen32WithSeeds( s + len - 64, len, z );
        U128 w = WeakHashLen32WithSeeds( s + len - 32, y + k1, x );
        x = x * k1 + Fetch64( s );
        len = ( len - 1 ) & ~static_cast<std::size_t>( 63 );
        do
        {
            x = Rotate( x + y + v.first + Fetch64( s + 8 ), 37 ) * k1;
            y = Rotate( y + v.second + Fetch64( s + 48 ), 42 ) * k1;
            x ^= w.second;
            y += v.first + Fetch64( s + 40 );
            z = Rotate( z + w.first, 33 ) * k1;
            v = WeakHashLen32WithSeeds( s, v.second * k1, x + w.first );
            w = WeakHashLen32WithSeeds( s + 32, z + w.second, y + Fetch64( s + 16 ) );
            std::swap( z, x );
            s += 64; len -= 64;
        } while( len != 0 );
        return HashLen16( HashLen16( v.first, w.first ) + ShiftMix( y ) * k1 + z, HashLen16( v.second, w.second ) + x );
    }

    // =========================================================================
    // HashFS version 2
    // =========================================================================
    namespace
    {
        bool LeesBlok( const std::string &pad, std::uint64_t offset, std::size_t lengte, std::vector<std::uint8_t> &uit )
        {
            std::ifstream in( pad, std::ios::binary );
            if( !in ) return false;
            in.seekg( static_cast<std::streamoff>( offset ) );
            uit.resize( lengte );
            in.read( reinterpret_cast<char *>( uit.data() ), static_cast<std::streamsize>( lengte ) );
            return static_cast<std::size_t>( in.gcount() ) == lengte;
        }
        inline std::uint16_t U16( const std::uint8_t *p ) { std::uint16_t v; std::memcpy( &v, p, 2 ); return v; }
        inline std::uint32_t U32( const std::uint8_t *p ) { std::uint32_t v; std::memcpy( &v, p, 4 ); return v; }
        inline std::uint64_t U64( const std::uint8_t *p ) { std::uint64_t v; std::memcpy( &v, p, 8 ); return v; }
    }

    bool ScsArchief::Open( const std::string &pad, std::string &fout )
    {
        m_pad = pad;
        m_entries.clear();
        std::vector<std::uint8_t> kop;
        if( !LeesBlok( pad, 0, 0x34, kop ) ) { fout = "cannot read header"; return false; }
        if( std::memcmp( kop.data(), "SCS#", 4 ) != 0 ) { fout = "not a HashFS archive"; return false; }
        const std::uint16_t versie = U16( kop.data() + 4 );
        const std::uint16_t salt = U16( kop.data() + 6 );
        if( versie != 2 ) { fout = "HashFS version " + std::to_string( versie ) + " unsupported"; return false; }
        if( salt != 0 ) { fout = "HashFS salt unsupported"; return false; }
        if( std::memcmp( kop.data() + 8, "CITY", 4 ) != 0 ) { fout = "hash method unsupported"; return false; }
        const std::uint32_t aantal = U32( kop.data() + 12 );
        const std::uint32_t size1 = U32( kop.data() + 16 );
        const std::uint32_t words2 = U32( kop.data() + 20 );
        const std::uint32_t size2 = U32( kop.data() + 24 );
        const std::uint64_t start1 = U64( kop.data() + 28 );
        const std::uint64_t start2 = U64( kop.data() + 36 );

        std::vector<std::uint8_t> raw1, raw2, idx1, idx2;
        if( !LeesBlok( pad, start1, size1, raw1 ) || !LeesBlok( pad, start2, size2, raw2 ) ) { fout = "cannot read index"; return false; }
        if( !SiiDecode::Inflate( raw1.data(), raw1.size(), idx1, static_cast<std::size_t>( aantal ) * 16 ) || idx1.size() != static_cast<std::size_t>( aantal ) * 16 )
        { fout = "index 1 inflate failed"; return false; }
        if( !SiiDecode::Inflate( raw2.data(), raw2.size(), idx2, static_cast<std::size_t>( words2 ) * 4 ) || idx2.size() != static_cast<std::size_t>( words2 ) * 4 )
        { fout = "index 2 inflate failed"; return false; }

        for( std::uint32_t j = 0; j < aantal; ++j )
        {
            const std::uint8_t *e1 = idx1.data() + static_cast<std::size_t>( j ) * 16;
            const std::uint64_t hash = U64( e1 );
            const std::uint32_t partOffset = U32( e1 + 8 );
            const std::uint16_t parts = U16( e1 + 12 );
            const std::uint16_t flags1 = U16( e1 + 14 );

            const std::uint8_t *dataPart = nullptr;
            for( std::uint16_t k = 0; k < parts; ++k )
            {
                const std::size_t pos = ( static_cast<std::size_t>( partOffset ) + k ) * 4;
                if( pos + 4 > idx2.size() ) break;
                const std::uint32_t offset = U16( idx2.data() + pos ) | ( static_cast<std::uint32_t>( idx2[ pos + 2 ] ) << 16 );
                const std::uint8_t kind = idx2[ pos + 3 ];
                if( kind & 0x80 )
                {
                    const std::size_t hp = static_cast<std::size_t>( offset ) * 4;
                    if( hp + 16 <= idx2.size() ) dataPart = idx2.data() + hp;
                }
            }
            if( !dataPart ) continue; // tobj-only or unknown; not needed here

            Entry e;
            e.zsize = U16( dataPart ) | ( static_cast<std::uint32_t>( dataPart[ 2 ] ) << 16 );
            e.compressie = static_cast<std::uint8_t>( dataPart[ 3 ] & 0xf0 );
            e.size = U16( dataPart + 4 ) | ( static_cast<std::uint32_t>( dataPart[ 6 ] ) << 16 );
            e.offset = static_cast<std::uint64_t>( U32( dataPart + 12 ) ) * 16;
            e.isMap = ( flags1 & 1 ) != 0;
            m_entries[ hash ] = e;
        }
        return true;
    }

    bool ScsArchief::LeesEntry( const Entry &e, std::vector<std::uint8_t> &uit ) const
    {
        std::vector<std::uint8_t> raw;
        if( !LeesBlok( m_pad, e.offset, e.zsize, raw ) ) return false;
        if( e.compressie == 0x10 )
        {
            if( !SiiDecode::Inflate( raw.data(), raw.size(), uit, e.size ) ) return false;
            return uit.size() == e.size;
        }
        uit = std::move( raw );
        return true;
    }

    bool ScsArchief::Bevat( const std::string &pad ) const { return m_entries.count( CityHash64( pad ) ) != 0; }

    bool ScsArchief::Lees( const std::string &pad, std::vector<std::uint8_t> &uit ) const
    {
        const auto it = m_entries.find( CityHash64( pad ) );
        if( it == m_entries.end() || it->second.isMap ) return false;
        return LeesEntry( it->second, uit );
    }

    bool ScsArchief::LeesMap( const std::string &pad, std::vector<std::string> &mappen, std::vector<std::string> &bestanden ) const
    {
        mappen.clear(); bestanden.clear();
        const auto it = m_entries.find( CityHash64( pad ) );
        if( it == m_entries.end() || !it->second.isMap ) return false;
        std::vector<std::uint8_t> d;
        if( !LeesEntry( it->second, d ) || d.size() < 4 ) return false;
        const std::uint32_t aantal = U32( d.data() );
        if( d.size() < 4u + aantal ) return false;
        std::size_t pos = 4u + aantal;
        for( std::uint32_t i = 0; i < aantal; ++i )
        {
            const std::size_t len = d[ 4 + i ];
            if( pos + len > d.size() ) return false;
            std::string item( reinterpret_cast<const char *>( d.data() + pos ), len );
            pos += len;
            if( !item.empty() && item[ 0 ] == '/' ) mappen.push_back( item.substr( 1 ) );
            else bestanden.push_back( item );
        }
        return true;
    }

    double ScsArchief::FuelPriceUit( const std::vector<std::uint8_t> &sui )
    {
        const std::string t( sui.begin(), sui.end() );
        const std::size_t p = t.find( "fuel_price:" );
        if( p == std::string::npos ) return -1.0;
        return std::atof( t.c_str() + p + 11 );
    }

    std::map<std::string, double> ScsArchief::Brandstofprijzen() const
    {
        std::map<std::string, double> uit;
        std::vector<std::string> mappen, bestanden;
        if( !LeesMap( "def/country", mappen, bestanden ) ) return uit;
        for( const std::string &b : bestanden )
        {
            std::vector<std::uint8_t> d;
            if( !Lees( "def/country/" + b, d ) ) continue;
            const double prijs = FuelPriceUit( d );
            if( prijs <= 0.0 ) continue;
            // token = "country.data.<token>"
            const std::string t( d.begin(), d.end() );
            const std::size_t k = t.find( "country.data." );
            if( k == std::string::npos ) continue;
            std::size_t e = k + 13;
            while( e < t.size() && ( std::isalnum( static_cast<unsigned char>( t[ e ] ) ) || t[ e ] == '_' ) ) ++e;
            uit[ t.substr( k + 13, e - ( k + 13 ) ) ] = prijs;
        }
        return uit;
    }

    std::map<std::string, ScsArchief::XZ> ScsArchief::Landposities() const
    {
        std::map<std::string, XZ> uit;
        std::vector<std::string> mappen, bestanden;
        if( !LeesMap( "def/country", mappen, bestanden ) ) return uit;
        for( const std::string &b : bestanden )
        {
            std::vector<std::uint8_t> d;
            if( !Lees( "def/country/" + b, d ) ) continue;
            const std::string t( d.begin(), d.end() );
            const std::size_t k = t.find( "country.data." );
            const std::size_t p = t.find( "pos:" );
            if( k == std::string::npos || p == std::string::npos ) continue;
            std::size_t e = k + 13;
            while( e < t.size() && ( std::isalnum( static_cast<unsigned char>( t[ e ] ) ) || t[ e ] == '_' ) ) ++e;
            const std::string token = t.substr( k + 13, e - ( k + 13 ) );
            // "pos: (-15575, 0, -9660)" -- X, height, Z
            const std::size_t open = t.find( '(', p );
            if( open == std::string::npos ) continue;
            double x = 0, y = 0, z = 0;
            if( std::sscanf( t.c_str() + open, "(%lf, %lf, %lf)", &x, &y, &z ) == 3 ) uit[ token ] = XZ{ x, z };
        }
        return uit;
    }

    double ScsArchief::GarageKorting() const
    {
        std::vector<std::uint8_t> d;
        if( !Lees( "def/economy_data.sii", d ) ) return -1.0;
        const std::string t( d.begin(), d.end() );
        const std::size_t p = t.find( "fuel_discount_in_garage:" );
        if( p == std::string::npos ) return -1.0;
        return std::atof( t.c_str() + p + 24 );
    }
}

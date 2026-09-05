#include "SiiDecode.hxx"

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string>

namespace Ritten
{
    // =========================================================================
    // AES-256, decrypt only, CBC. Straight from FIPS-197. No Windows API so
    // it is portable and testable here.
    // =========================================================================
    namespace
    {
        // Public key of SCS Software, identical to SII_Decrypt and all
        // save editors. Public for more than ten years.
        const std::array<std::uint8_t, 32> SCS_SLEUTEL = {
            0x2a, 0x5f, 0xcb, 0x17, 0x91, 0xd2, 0x2f, 0xb6, 0x02, 0x45, 0xb3, 0xd8,
            0x36, 0x9e, 0xd0, 0xb2, 0xc2, 0x73, 0x71, 0x56, 0x3f, 0xbf, 0x1f, 0x3c,
            0x9e, 0xdf, 0x6b, 0x11, 0x82, 0x5a, 0x5d, 0x0a };

        const std::uint8_t SBOX[ 256 ] = {
            0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
            0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
            0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
            0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
            0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
            0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
            0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
            0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
            0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
            0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
            0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
            0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
            0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
            0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
            0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
            0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16 };

        std::uint8_t INV_SBOX[ 256 ];
        bool invSboxKlaar = false;

        void MaakInvSbox()
        {
            if( invSboxKlaar ) return;
            for( int i = 0; i < 256; ++i ) INV_SBOX[ SBOX[ i ] ] = static_cast<std::uint8_t>( i );
            invSboxKlaar = true;
        }

        inline std::uint8_t Xtime( std::uint8_t a )
        {
            return static_cast<std::uint8_t>( ( a << 1 ) ^ ( ( a & 0x80 ) ? 0x1b : 0 ) );
        }

        std::uint8_t GfMul( std::uint8_t a, std::uint8_t b )
        {
            std::uint8_t r = 0;
            while( b )
            {
                if( b & 1 ) r ^= a;
                a = Xtime( a );
                b >>= 1;
            }
            return r;
        }

        // 15 round keys of 16 bytes for AES-256.
        void ExpandeerSleutel( const std::array<std::uint8_t, 32> &sleutel, std::uint8_t rk[ 15 ][ 16 ] )
        {
            std::uint8_t w[ 60 ][ 4 ];
            for( int i = 0; i < 8; ++i )
                for( int j = 0; j < 4; ++j ) w[ i ][ j ] = sleutel[ 4 * i + j ];
            std::uint8_t rcon = 1;
            for( int i = 8; i < 60; ++i )
            {
                std::uint8_t t[ 4 ] = { w[ i - 1 ][ 0 ], w[ i - 1 ][ 1 ], w[ i - 1 ][ 2 ], w[ i - 1 ][ 3 ] };
                if( i % 8 == 0 )
                {
                    const std::uint8_t t0 = t[ 0 ];
                    t[ 0 ] = SBOX[ t[ 1 ] ] ^ rcon; t[ 1 ] = SBOX[ t[ 2 ] ]; t[ 2 ] = SBOX[ t[ 3 ] ]; t[ 3 ] = SBOX[ t0 ];
                    rcon = Xtime( rcon );
                }
                else if( i % 8 == 4 )
                {
                    for( auto &b : t ) b = SBOX[ b ];
                }
                for( int j = 0; j < 4; ++j ) w[ i ][ j ] = w[ i - 8 ][ j ] ^ t[ j ];
            }
            for( int r = 0; r < 15; ++r )
                for( int c = 0; c < 4; ++c )
                    for( int j = 0; j < 4; ++j ) rk[ r ][ 4 * c + j ] = w[ 4 * r + c ][ j ];
        }

        void OntsleutelBlok( const std::uint8_t rk[ 15 ][ 16 ], const std::uint8_t in[ 16 ], std::uint8_t uit[ 16 ] )
        {
            std::uint8_t s[ 16 ];
            for( int i = 0; i < 16; ++i ) s[ i ] = in[ i ] ^ rk[ 14 ][ i ];
            for( int ronde = 13; ronde >= 0; --ronde )
            {
                // InvShiftRows: row r shifts r positions to the right
                std::uint8_t t[ 16 ];
                for( int c = 0; c < 4; ++c )
                    for( int r = 0; r < 4; ++r ) t[ ( ( c + r ) % 4 ) * 4 + r ] = s[ c * 4 + r ];
                // InvSubBytes + AddRoundKey
                for( int i = 0; i < 16; ++i ) s[ i ] = INV_SBOX[ t[ i ] ] ^ rk[ ronde ][ i ];
                // InvMixColumns (not in the last round)
                if( ronde > 0 )
                {
                    for( int c = 0; c < 4; ++c )
                    {
                        const std::uint8_t a0 = s[ 4 * c ], a1 = s[ 4 * c + 1 ], a2 = s[ 4 * c + 2 ], a3 = s[ 4 * c + 3 ];
                        s[ 4 * c ]     = GfMul( a0, 14 ) ^ GfMul( a1, 11 ) ^ GfMul( a2, 13 ) ^ GfMul( a3, 9 );
                        s[ 4 * c + 1 ] = GfMul( a0, 9 )  ^ GfMul( a1, 14 ) ^ GfMul( a2, 11 ) ^ GfMul( a3, 13 );
                        s[ 4 * c + 2 ] = GfMul( a0, 13 ) ^ GfMul( a1, 9 )  ^ GfMul( a2, 14 ) ^ GfMul( a3, 11 );
                        s[ 4 * c + 3 ] = GfMul( a0, 11 ) ^ GfMul( a1, 13 ) ^ GfMul( a2, 9 )  ^ GfMul( a3, 14 );
                    }
                }
            }
            std::memcpy( uit, s, 16 );
        }
    }

    bool SiiDecode::OntsleutelScsC( const std::vector<std::uint8_t> &in, std::vector<std::uint8_t> &uit, std::string &fout )
    {
        if( in.size() < 56 || std::memcmp( in.data(), "ScsC", 4 ) != 0 )
        {
            fout = "no ScsC header";
            return false;
        }
        MaakInvSbox();
        std::uint8_t rk[ 15 ][ 16 ];
        ExpandeerSleutel( SCS_SLEUTEL, rk );

        const std::uint8_t *iv = in.data() + 36;
        std::uint32_t verwacht = 0;
        std::memcpy( &verwacht, in.data() + 52, 4 );
        std::size_t lengte = in.size() - 56;
        lengte -= lengte % 16;
        const std::uint8_t *ct = in.data() + 56;

        std::vector<std::uint8_t> plat( lengte );
        std::uint8_t vorig[ 16 ];
        std::memcpy( vorig, iv, 16 );
        for( std::size_t i = 0; i < lengte; i += 16 )
        {
            std::uint8_t d[ 16 ];
            OntsleutelBlok( rk, ct + i, d );
            for( int j = 0; j < 16; ++j ) plat[ i + j ] = d[ j ] ^ vorig[ j ];
            std::memcpy( vorig, ct + i, 16 );
        }

        if( !Inflate( plat.data(), plat.size(), uit, verwacht ) )
        {
            fout = "inflate failed (key or format)";
            return false;
        }
        if( verwacht != 0 && uit.size() != verwacht )
        {
            fout = "size mismatch after inflate";
            return false;
        }
        return true;
    }

    // =========================================================================
    // Inflate: RFC 1950 (zlib header) + RFC 1951 (deflate). Stored, fixed and
    // dynamic Huffman. Fails loudly on bad input.
    // =========================================================================
    namespace
    {
        struct BitLezer
        {
            const std::uint8_t *d; std::size_t n; std::size_t pos = 0; std::uint32_t buf = 0; int bits = 0;
            bool ok = true;
            std::uint32_t Bits( int k )
            {
                while( bits < k )
                {
                    if( pos >= n ) { ok = false; return 0; }
                    buf |= static_cast<std::uint32_t>( d[ pos++ ] ) << bits;
                    bits += 8;
                }
                const std::uint32_t v = buf & ( ( 1u << k ) - 1 );
                buf >>= k; bits -= k;
                return v;
            }
            void NaarByte() { buf = 0; bits = 0; }
        };

        struct Huffman
        {
            std::uint16_t telling[ 16 ] = {};
            std::uint16_t symbool[ 320 ] = {};
            void Bouw( const std::uint8_t *lengtes, int n )
            {
                std::memset( telling, 0, sizeof( telling ) );
                for( int i = 0; i < n; ++i ) telling[ lengtes[ i ] ]++;
                telling[ 0 ] = 0;
                std::uint16_t offset[ 16 ] = {};
                for( int i = 1; i < 16; ++i ) offset[ i ] = offset[ i - 1 ] + telling[ i - 1 ];
                for( int i = 0; i < n; ++i ) if( lengtes[ i ] ) symbool[ offset[ lengtes[ i ] ]++ ] = static_cast<std::uint16_t>( i );
            }
            int Decodeer( BitLezer &b ) const
            {
                int code = 0, first = 0, index = 0;
                for( int len = 1; len < 16; ++len )
                {
                    code |= static_cast<int>( b.Bits( 1 ) );
                    if( !b.ok ) return -1;
                    const int count = telling[ len ];
                    if( code - count < first ) return symbool[ index + ( code - first ) ];
                    index += count; first += count; first <<= 1; code <<= 1;
                }
                return -1;
            }
        };

        const std::uint16_t LEN_BASIS[ 29 ] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
        const std::uint8_t  LEN_EXTRA[ 29 ] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
        const std::uint16_t DIST_BASIS[ 30 ] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
        const std::uint8_t  DIST_EXTRA[ 30 ] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

        bool InflateBlok( BitLezer &b, std::vector<std::uint8_t> &uit, const Huffman &lit, const Huffman &dist )
        {
            for( ;; )
            {
                const int sym = lit.Decodeer( b );
                if( sym < 0 ) return false;
                if( sym < 256 ) { uit.push_back( static_cast<std::uint8_t>( sym ) ); continue; }
                if( sym == 256 ) return true;
                const int li = sym - 257;
                if( li >= 29 ) return false;
                const int len = LEN_BASIS[ li ] + static_cast<int>( b.Bits( LEN_EXTRA[ li ] ) );
                const int ds = dist.Decodeer( b );
                if( ds < 0 || ds >= 30 ) return false;
                const std::size_t d = DIST_BASIS[ ds ] + b.Bits( DIST_EXTRA[ ds ] );
                if( !b.ok || d > uit.size() ) return false;
                const std::size_t start = uit.size() - d;
                for( int i = 0; i < len; ++i ) uit.push_back( uit[ start + i ] );
            }
        }
    }

    bool SiiDecode::Inflate( const std::uint8_t *data, std::size_t lengte, std::vector<std::uint8_t> &uit, std::size_t verwachteGrootte )
    {
        if( lengte < 2 ) return false;
        // zlib header: CMF/FLG, method 8, no dictionary
        if( ( data[ 0 ] & 0x0f ) != 8 || ( ( data[ 0 ] << 8 ) | data[ 1 ] ) % 31 != 0 || ( data[ 1 ] & 0x20 ) ) return false;
        BitLezer b{ data + 2, lengte - 2 };
        uit.clear();
        if( verwachteGrootte ) uit.reserve( verwachteGrootte );

        for( ;; )
        {
            const int laatste = static_cast<int>( b.Bits( 1 ) );
            const int type = static_cast<int>( b.Bits( 2 ) );
            if( !b.ok ) return false;
            if( type == 0 )
            {
                b.NaarByte();
                if( b.pos + 4 > b.n ) return false;
                const std::uint16_t len = static_cast<std::uint16_t>( b.d[ b.pos ] | ( b.d[ b.pos + 1 ] << 8 ) );
                const std::uint16_t nlen = static_cast<std::uint16_t>( b.d[ b.pos + 2 ] | ( b.d[ b.pos + 3 ] << 8 ) );
                b.pos += 4;
                if( static_cast<std::uint16_t>( ~len ) != nlen || b.pos + len > b.n ) return false;
                uit.insert( uit.end(), b.d + b.pos, b.d + b.pos + len );
                b.pos += len;
            }
            else if( type == 1 )
            {
                std::uint8_t l[ 288 ];
                int i = 0;
                for( ; i < 144; ++i ) l[ i ] = 8;
                for( ; i < 256; ++i ) l[ i ] = 9;
                for( ; i < 280; ++i ) l[ i ] = 7;
                for( ; i < 288; ++i ) l[ i ] = 8;
                std::uint8_t dl[ 30 ];
                for( i = 0; i < 30; ++i ) dl[ i ] = 5;
                Huffman lit, dist;
                lit.Bouw( l, 288 ); dist.Bouw( dl, 30 );
                if( !InflateBlok( b, uit, lit, dist ) ) return false;
            }
            else if( type == 2 )
            {
                const int hlit = static_cast<int>( b.Bits( 5 ) ) + 257;
                const int hdist = static_cast<int>( b.Bits( 5 ) ) + 1;
                const int hclen = static_cast<int>( b.Bits( 4 ) ) + 4;
                static const int volgorde[ 19 ] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
                std::uint8_t cl[ 19 ] = {};
                for( int i = 0; i < hclen; ++i ) cl[ volgorde[ i ] ] = static_cast<std::uint8_t>( b.Bits( 3 ) );
                if( !b.ok ) return false;
                Huffman code; code.Bouw( cl, 19 );
                std::uint8_t lengtes[ 320 ] = {};
                int n = 0;
                while( n < hlit + hdist )
                {
                    int sym = code.Decodeer( b );
                    if( sym < 0 ) return false;
                    if( sym < 16 ) lengtes[ n++ ] = static_cast<std::uint8_t>( sym );
                    else
                    {
                        int herhaal = 0; std::uint8_t waarde = 0;
                        if( sym == 16 ) { if( n == 0 ) return false; waarde = lengtes[ n - 1 ]; herhaal = 3 + static_cast<int>( b.Bits( 2 ) ); }
                        else if( sym == 17 ) herhaal = 3 + static_cast<int>( b.Bits( 3 ) );
                        else herhaal = 11 + static_cast<int>( b.Bits( 7 ) );
                        if( n + herhaal > hlit + hdist ) return false;
                        while( herhaal-- ) lengtes[ n++ ] = waarde;
                    }
                }
                Huffman lit, dist;
                lit.Bouw( lengtes, hlit ); dist.Bouw( lengtes + hlit, hdist );
                if( !InflateBlok( b, uit, lit, dist ) ) return false;
            }
            else return false;
            if( laatste ) break;
        }
        return true;
    }

    // =========================================================================
    // BSII: structure definitions + data blocks. We read EVERYTHING to follow
    // the stream, but keep only the fields of 'vehicle' blocks that we need.
    // Type codes from Trucky/sii-decrypt-ts.
    // =========================================================================
    namespace
    {
        struct Lezer
        {
            const std::uint8_t *d; std::size_t n; std::size_t pos = 0; bool ok = true;
            bool Heeft( std::size_t k ) { if( pos + k > n ) { ok = false; return false; } return true; }
            std::uint8_t U8() { if( !Heeft( 1 ) ) return 0; return d[ pos++ ]; }
            std::uint16_t U16() { if( !Heeft( 2 ) ) return 0; std::uint16_t v; std::memcpy( &v, d + pos, 2 ); pos += 2; return v; }
            std::uint32_t U32() { if( !Heeft( 4 ) ) return 0; std::uint32_t v; std::memcpy( &v, d + pos, 4 ); pos += 4; return v; }
            std::uint64_t U64() { if( !Heeft( 8 ) ) return 0; std::uint64_t v; std::memcpy( &v, d + pos, 8 ); pos += 8; return v; }
            float F32() { if( !Heeft( 4 ) ) return 0; float v; std::memcpy( &v, d + pos, 4 ); pos += 4; return v; }
            std::string Str() { const std::uint32_t l = U32(); if( !Heeft( l ) ) return {}; std::string s( reinterpret_cast<const char *>( d + pos ), l ); pos += l; return s; }
            void SlaStr() { const std::uint32_t l = U32(); if( Heeft( l ) ) pos += l; }
            void SlaIdent()
            {
                const std::uint8_t delen = U8();
                if( delen == 0xFF ) { U64(); return; }
                for( int i = 0; i < delen; ++i ) U64();
            }
            void Sla( std::size_t k ) { if( Heeft( k ) ) pos += k; }
        };

        enum : std::uint32_t
        {
            T_Str = 0x01, T_StrArr = 0x02, T_Tok = 0x03, T_TokArr = 0x04, T_F = 0x05, T_FArr = 0x06,
            T_V2 = 0x07, T_V2Arr = 0x08, T_V3 = 0x09, T_V3Arr = 0x0A, T_V3I = 0x11, T_V3IArr = 0x12,
            T_V4 = 0x17, T_V4Arr = 0x18, T_V8 = 0x19, T_V8Arr = 0x1A, T_I32 = 0x25, T_I32Arr = 0x26,
            T_U32 = 0x27, T_U32Arr = 0x28, T_I16 = 0x29, T_I16Arr = 0x2A, T_U16 = 0x2B, T_U16Arr = 0x2C,
            T_U32b = 0x2F, T_I64 = 0x31, T_I64Arr = 0x32, T_U64 = 0x33, T_U64Arr = 0x34, T_Bool = 0x35,
            T_BoolArr = 0x36, T_Ord = 0x37, T_Id = 0x39, T_IdArrA = 0x3A, T_Id2 = 0x3B, T_IdArrC = 0x3C,
            T_Id3 = 0x3D, T_IdArrE = 0x3E
        };

        // Reads a value of this type and returns it as double if it is a
        // single number (for our fields); otherwise just skips.
        bool LeesWaarde( Lezer &r, std::uint32_t type, std::uint32_t versie, double &getal )
        {
            getal = 0.0;
            const std::size_t v8 = ( versie == 1 ) ? 7 : 8;
            switch( type )
            {
                case T_Str: r.SlaStr(); break;
                case T_StrArr: { const std::uint32_t k = r.U32(); for( std::uint32_t i = 0; i < k && r.ok; ++i ) r.SlaStr(); break; }
                case T_Tok: r.U64(); break;
                case T_TokArr: { const std::uint32_t k = r.U32(); r.Sla( 8ull * k ); break; }
                case T_F: getal = r.F32(); break;
                case T_FArr: { const std::uint32_t k = r.U32(); r.Sla( 4ull * k ); break; }
                case T_V2: r.Sla( 8 ); break;
                case T_V2Arr: { const std::uint32_t k = r.U32(); r.Sla( 8ull * k ); break; }
                case T_V3: r.Sla( 12 ); break;
                case T_V3Arr: { const std::uint32_t k = r.U32(); r.Sla( 12ull * k ); break; }
                case T_V3I: r.Sla( 12 ); break;
                case T_V3IArr: { const std::uint32_t k = r.U32(); r.Sla( 12ull * k ); break; }
                case T_V4: r.Sla( 16 ); break;
                case T_V4Arr: { const std::uint32_t k = r.U32(); r.Sla( 16ull * k ); break; }
                case T_V8: r.Sla( 4 * v8 ); break;
                case T_V8Arr: { const std::uint32_t k = r.U32(); r.Sla( 4ull * v8 * k ); break; }
                case T_I32: getal = static_cast<std::int32_t>( r.U32() ); break;
                case T_I32Arr: { const std::uint32_t k = r.U32(); r.Sla( 4ull * k ); break; }
                case T_U32: case T_U32b: getal = r.U32(); break;
                case T_U32Arr: { const std::uint32_t k = r.U32(); r.Sla( 4ull * k ); break; }
                case T_I16: getal = static_cast<std::int16_t>( r.U16() ); break;
                case T_I16Arr: { const std::uint32_t k = r.U32(); r.Sla( 2ull * k ); break; }
                case T_U16: getal = r.U16(); break;
                case T_U16Arr: { const std::uint32_t k = r.U32(); r.Sla( 2ull * k ); break; }
                case T_I64: getal = static_cast<double>( static_cast<std::int64_t>( r.U64() ) ); break;
                case T_I64Arr: { const std::uint32_t k = r.U32(); r.Sla( 8ull * k ); break; }
                case T_U64: getal = static_cast<double>( r.U64() ); break;
                case T_U64Arr: { const std::uint32_t k = r.U32(); r.Sla( 8ull * k ); break; }
                case T_Bool: getal = r.U8() ? 1.0 : 0.0; break;
                case T_BoolArr: { const std::uint32_t k = r.U32(); r.Sla( k ); break; }
                case T_Ord: r.U32(); break;
                case T_Id: case T_Id2: case T_Id3: r.SlaIdent(); break;
                case T_IdArrA: case T_IdArrC: case T_IdArrE: { const std::uint32_t k = r.U32(); for( std::uint32_t i = 0; i < k && r.ok; ++i ) r.SlaIdent(); break; }
                default: return false;
            }
            return r.ok;
        }

        struct Structuur
        {
            std::string naam;
            std::vector<std::pair<std::string, std::uint32_t>> velden;
        };
    }

    std::vector<SiiDecode::SaveTruck> SiiDecode::TrucksUitBsii( const std::vector<std::uint8_t> &data, std::string &fout )
    {
        std::vector<SiiDecode::SaveTruck> uit;
        Lezer r{ data.data(), data.size() };
        if( data.size() < 8 || std::memcmp( data.data(), "BSII", 4 ) != 0 ) { fout = "no BSII header"; return uit; }
        r.pos = 4;
        const std::uint32_t versie = r.U32();
        if( versie < 1 || versie > 3 ) { fout = "unknown BSII version"; return uit; }

        std::map<std::uint32_t, Structuur> structuren;
        while( r.ok && r.pos < r.n )
        {
            const std::uint32_t bloktype = r.U32();
            if( !r.ok ) break;
            if( bloktype == 0 )
            {
                const bool geldig = r.U8() != 0;
                if( !geldig ) continue;  // end of definitions
                const std::uint32_t id = r.U32();
                Structuur s;
                s.naam = r.Str();
                for( ;; )
                {
                    const std::uint32_t t = r.U32();
                    if( !r.ok || t == 0 ) break;
                    std::string veldnaam = r.Str();
                    if( t == T_Ord )
                    {
                        // ordinal list: count, then (ordinal, string) pairs
                        const std::uint32_t k = r.U32();
                        for( std::uint32_t i = 0; i < k && r.ok; ++i ) { r.U32(); r.SlaStr(); }
                    }
                    s.velden.emplace_back( std::move( veldnaam ), t );
                }
                if( !structuren.count( id ) ) structuren[ id ] = std::move( s );
            }
            else
            {
                auto it = structuren.find( bloktype );
                if( it == structuren.end() ) { fout = "unknown structure in data block"; return {}; }
                const Structuur &s = it->second;
                r.SlaIdent();  // block id
                const bool isTruck = ( s.naam == "vehicle" );
                SiiDecode::SaveTruck t; double odoF = 0, integF = 0, tripFuelF = 0, tripDistM = 0;
                for( const auto &[ naam, type ] : s.velden )
                {
                    double g = 0.0;
                    if( !LeesWaarde( r, type, versie, g ) ) { fout = "unknown field type"; return {}; }
                    if( !isTruck ) continue;
                    if( naam == "odometer" ) t.kmStand = g;
                    else if( naam == "odometer_float_part" ) odoF = g;
                    else if( naam == "integrity_odometer" ) t.integriteit = g;
                    else if( naam == "integrity_odometer_float_part" ) integF = g;
                    else if( naam == "trip_fuel_l" ) t.tripLiters = g;
                    else if( naam == "trip_fuel" ) tripFuelF = g;
                    else if( naam == "trip_distance_km" ) t.tripKm = g;
                    else if( naam == "trip_distance" ) tripDistM = g;
                }
                if( isTruck && r.ok )
                {
                    t.heeftBreukdeel = odoF != 0.0;
                    t.kmStand += odoF;
                    t.integriteit += integF;
                    t.tripLiters += tripFuelF;
                    t.tripKm += tripDistM / 1000.0;
                    uit.push_back( t );
                }
            }
        }
        if( !r.ok ) { fout = "BSII ended unexpectedly"; return {}; }
        return uit;
    }

    // =========================================================================
    // SiiN text: line by line, only inside 'vehicle : ... {' blocks.
    // Numbers like '&hex' are floats in hex.
    // =========================================================================
    namespace
    {
        double LeesGetal( const std::string &s )
        {
            if( s.empty() ) return 0.0;
            if( s[ 0 ] == '&' )
            {
                std::uint32_t bits = 0;
                for( std::size_t i = 1; i < s.size(); ++i )
                {
                    const char c = s[ i ];
                    bits <<= 4;
                    if( c >= '0' && c <= '9' ) bits |= static_cast<std::uint32_t>( c - '0' );
                    else if( c >= 'a' && c <= 'f' ) bits |= static_cast<std::uint32_t>( c - 'a' + 10 );
                    else if( c >= 'A' && c <= 'F' ) bits |= static_cast<std::uint32_t>( c - 'A' + 10 );
                    else break;
                }
                float f; std::memcpy( &f, &bits, 4 );
                return f;
            }
            return std::atof( s.c_str() );
        }
    }

    std::vector<SiiDecode::SaveTruck> SiiDecode::TrucksUitTekst( const std::vector<std::uint8_t> &data )
    {
        std::vector<SiiDecode::SaveTruck> uit;
        const char *p = reinterpret_cast<const char *>( data.data() );
        const std::size_t n = data.size();
        std::size_t i = 0;
        bool inTruck = false;
        SiiDecode::SaveTruck t; double odoF = 0, integF = 0, tripFuelF = 0, tripDistM = 0;
        while( i < n )
        {
            std::size_t e = i;
            while( e < n && p[ e ] != '\n' ) ++e;
            std::string regel( p + i, e - i );
            i = e + 1;
            if( !regel.empty() && regel.back() == '\r' ) regel.pop_back();

            if( !inTruck )
            {
                if( regel.rfind( "vehicle : ", 0 ) == 0 ) { inTruck = true; t = SiiDecode::SaveTruck{}; odoF = integF = tripFuelF = tripDistM = 0; }
                continue;
            }
            if( regel == "}" )
            {
                t.heeftBreukdeel = odoF != 0.0;
                t.kmStand += odoF; t.integriteit += integF; t.tripLiters += tripFuelF; t.tripKm += tripDistM / 1000.0;
                uit.push_back( t );
                inTruck = false;
                continue;
            }
            const std::size_t dp = regel.find(": ");
            if( dp == std::string::npos || regel.empty() || regel[ 0 ] != ' ' ) continue;
            const std::string naam = regel.substr( 1, dp - 1 );
            const std::string waarde = regel.substr( dp + 2 );
            if( naam == "odometer" ) t.kmStand = LeesGetal( waarde );
            else if( naam == "odometer_float_part" ) odoF = LeesGetal( waarde );
            else if( naam == "integrity_odometer" ) t.integriteit = LeesGetal( waarde );
            else if( naam == "integrity_odometer_float_part" ) integF = LeesGetal( waarde );
            else if( naam == "trip_fuel_l" ) t.tripLiters = LeesGetal( waarde );
            else if( naam == "trip_fuel" ) tripFuelF = LeesGetal( waarde );
            else if( naam == "trip_distance_km" ) t.tripKm = LeesGetal( waarde );
            else if( naam == "trip_distance" ) tripDistM = LeesGetal( waarde );
        }
        return uit;
    }

    // =========================================================================
    std::vector<SiiDecode::SaveTruck> SiiDecode::LeesTrucks( const std::vector<std::uint8_t> &bestand, std::string &fout )
    {
        fout.clear();
        if( bestand.size() < 4 ) { fout = "file too small"; return {}; }
        if( std::memcmp( bestand.data(), "ScsC", 4 ) == 0 )
        {
            std::vector<std::uint8_t> binnen;
            if( !OntsleutelScsC( bestand, binnen, fout ) ) return {};
            return LeesTrucks( binnen, fout );
        }
        if( std::memcmp( bestand.data(), "BSII", 4 ) == 0 ) return TrucksUitBsii( bestand, fout );
        if( std::memcmp( bestand.data(), "SiiN", 4 ) == 0 ) return TrucksUitTekst( bestand );
        fout = "unknown format";
        return {};
    }
}

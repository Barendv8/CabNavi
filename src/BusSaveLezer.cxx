#include "BusSaveLezer.hxx"
#include "SiiDecode.hxx"
#include "Spel.hxx"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shlobj.h>
#endif

namespace Ritten
{
    const BusSaveLezer::Veld *BusSaveLezer::Eenheid::Zoek( const char *naam ) const
    {
        for( const Veld &v : velden )
            if( v.naam == naam ) return &v;
        return nullptr;
    }

    namespace
    {
        bool Gewenst( const std::vector<std::string> &typen, const char *p, std::size_t n )
        {
            for( const std::string &t : typen )
                if( t.size() == n && std::memcmp( t.data(), p, n ) == 0 ) return true;
            return false;
        }

        void EenhedenUitTekst( const std::vector<std::uint8_t> &data, const std::vector<std::string> &typen,
                               std::vector<BusSaveLezer::Eenheid> &uit )
        {
            const char *p = reinterpret_cast<const char *>( data.data() );
            const std::size_t n = data.size();
            std::size_t i = 0;
            bool inBlok = false, bewaar = false;
            BusSaveLezer::Eenheid huidig;
            while( i < n )
            {
                const void *nl = std::memchr( p + i, '\n', n - i );
                std::size_t e = nl ? static_cast<std::size_t>( static_cast<const char *>( nl ) - p ) : n;
                const std::size_t volgende = e + 1;
                if( e > i && p[ e - 1 ] == '\r' ) --e;
                const char *r = p + i;
                const std::size_t len = e - i;
                i = volgende;
                if( len == 0 ) continue;

                if( !inBlok )
                {
                    if( r[ 0 ] == ' ' || r[ 0 ] == '{' || r[ 0 ] == '}' || r[ len - 1 ] != '{' ) continue;
                    std::size_t s = 0;
                    while( s + 3 <= len && std::memcmp( r + s, " : ", 3 ) != 0 ) ++s;
                    if( s + 3 > len ) continue;
                    inBlok = true;
                    bewaar = Gewenst( typen, r, s );
                    if( bewaar )
                    {
                        huidig = BusSaveLezer::Eenheid{};
                        huidig.type.assign( r, s );
                        std::size_t eind = len - 1;
                        while( eind > s + 3 && r[ eind - 1 ] == ' ' ) --eind;
                        huidig.id.assign( r + s + 3, eind - ( s + 3 ) );
                    }
                    continue;
                }
                if( len == 1 && r[ 0 ] == '}' )
                {
                    if( bewaar ) uit.push_back( std::move( huidig ) );
                    inBlok = false;
                    bewaar = false;
                    continue;
                }
                if( !bewaar || r[ 0 ] != ' ' ) continue;

                const char *dp = static_cast<const char *>( std::memchr( r + 1, ':', len - 1 ) );
                if( dp == nullptr || dp + 1 >= r + len || dp[ 1 ] != ' ' ) continue;
                const char *naamEind = dp;
                const char *haak = static_cast<const char *>( std::memchr( r + 1, '[', static_cast<std::size_t>( dp - ( r + 1 ) ) ) );
                if( haak != nullptr ) naamEind = haak;
                const std::size_t naamLen = static_cast<std::size_t>( naamEind - ( r + 1 ) );

                const char *w = dp + 2;
                std::size_t wLen = static_cast<std::size_t>( ( r + len ) - w );
                if( wLen >= 2 && w[ 0 ] == '"' && w[ wLen - 1 ] == '"' ) { ++w; wLen -= 2; }

                if( haak == nullptr )
                {
                    BusSaveLezer::Veld v;
                    v.naam.assign( r + 1, naamLen );
                    v.waarden.emplace_back( w, wLen );
                    huidig.velden.push_back( std::move( v ) );
                    continue;
                }
                BusSaveLezer::Veld *lijst = nullptr;
                for( std::size_t k = huidig.velden.size(); k-- > 0; )
                {
                    BusSaveLezer::Veld &v = huidig.velden[ k ];
                    if( v.naam.size() == naamLen && std::memcmp( v.naam.data(), r + 1, naamLen ) == 0 ) { lijst = &v; break; }
                }
                if( lijst == nullptr )
                {
                    BusSaveLezer::Veld v;
                    v.naam.assign( r + 1, naamLen );
                    huidig.velden.push_back( std::move( v ) );
                    lijst = &huidig.velden.back();
                }
                if( !lijst->lijst )
                {
                    lijst->lijst = true;
                    lijst->waarden.clear();
                }
                lijst->waarden.emplace_back( w, wLen );
            }
        }

        bool TekstIsCompleet( const std::vector<std::uint8_t> &data )
        {
            std::size_t n = data.size();
            while( n > 0 && ( data[ n - 1 ] == '\n' || data[ n - 1 ] == '\r' || data[ n - 1 ] == ' ' || data[ n - 1 ] == '\t' ) ) --n;
            if( n < 2 || data[ n - 1 ] != '}' ) return false;
            --n;
            while( n > 0 && ( data[ n - 1 ] == '\r' || data[ n - 1 ] == ' ' || data[ n - 1 ] == '\t' ) ) --n;
            if( n == 0 || data[ n - 1 ] != '\n' ) return false;
            --n;
            while( n > 0 && ( data[ n - 1 ] == '\n' || data[ n - 1 ] == '\r' || data[ n - 1 ] == ' ' || data[ n - 1 ] == '\t' ) ) --n;
            return n > 0 && data[ n - 1 ] == '}';
        }

        std::string Token( std::uint64_t v )
        {
            static const char tabel[] = "0123456789abcdefghijklmnopqrstuvwxyz_";
            std::string s;
            while( v != 0 )
            {
                const int idx = static_cast<int>( v % 38 ) - 1;
                v /= 38;
                if( idx >= 0 && idx < 37 ) s += tabel[ idx ];
            }
            return s;
        }

        struct Lezer
        {
            const std::uint8_t *d; std::size_t n; std::size_t pos = 0; bool ok = true;
            bool Heeft( std::size_t k ) { if( k > n || pos > n - k ) { ok = false; return false; } return true; }
            std::uint8_t U8() { if( !Heeft( 1 ) ) return 0; return d[ pos++ ]; }
            std::uint16_t U16() { if( !Heeft( 2 ) ) return 0; std::uint16_t v; std::memcpy( &v, d + pos, 2 ); pos += 2; return v; }
            std::uint32_t U32() { if( !Heeft( 4 ) ) return 0; std::uint32_t v; std::memcpy( &v, d + pos, 4 ); pos += 4; return v; }
            std::uint64_t U64() { if( !Heeft( 8 ) ) return 0; std::uint64_t v; std::memcpy( &v, d + pos, 8 ); pos += 8; return v; }
            std::string Str() { const std::uint32_t l = U32(); if( !Heeft( l ) ) return {}; std::string s( reinterpret_cast<const char *>( d + pos ), l ); pos += l; return s; }
            void SlaStr() { const std::uint32_t l = U32(); if( Heeft( l ) ) pos += l; }
            void Sla( std::size_t k ) { if( Heeft( k ) ) pos += k; }
            void SlaIdent()
            {
                const std::uint8_t delen = U8();
                if( delen == 0xFF ) { U64(); return; }
                for( int i = 0; i < delen; ++i ) U64();
            }
            std::string Ident()
            {
                const std::uint8_t delen = U8();
                if( delen == 0xFF )
                {
                    static const char hex[] = "0123456789abcdef";
                    std::uint64_t v = U64();
                    std::string s;
                    do { s.insert( s.begin(), hex[ v & 0xF ] ); v >>= 4; } while( v != 0 );
                    return "_nameless." + s;
                }
                if( delen == 0 ) return "null";
                std::string s;
                for( int i = 0; i < delen && ok; ++i )
                {
                    if( i > 0 ) s += '.';
                    s += Token( U64() );
                }
                return s;
            }
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

        std::string Komma( std::uint32_t bits )
        {
            float f;
            std::memcpy( &f, &bits, 4 );
            if( f == std::trunc( f ) && std::fabs( f ) < 1e7f ) return std::to_string( static_cast<long long>( f ) );
            static const char hex[] = "0123456789abcdef";
            std::string s = "&00000000";
            for( int i = 0; i < 8; ++i ) s[ 8 - i ] = hex[ ( bits >> ( 4 * i ) ) & 0xF ];
            return s;
        }

        bool LeesWaarde( Lezer &r, std::uint32_t type, std::uint32_t versie, BusSaveLezer::Veld *doel )
        {
            const std::size_t v8 = ( versie == 1 ) ? 7 : 8;
            const auto getal = [ doel ]( long long v ) { if( doel ) doel->waarden.push_back( std::to_string( v ) ); };
            const auto groot = [ doel ]( unsigned long long v ) { if( doel ) doel->waarden.push_back( std::to_string( v ) ); };
            const auto ofNil = [ doel ]( unsigned long long v, unsigned long long nil )
            {
                if( doel ) doel->waarden.push_back( v == nil ? std::string( "nil" ) : std::to_string( v ) );
            };
            switch( type )
            {
                case T_Str:
                    if( doel ) doel->waarden.push_back( r.Str() ); else r.SlaStr();
                    break;
                case T_StrArr:
                {
                    const std::uint32_t k = r.U32();
                    if( doel ) doel->lijst = true;
                    for( std::uint32_t i = 0; i < k && r.ok; ++i )
                    {
                        if( doel ) doel->waarden.push_back( r.Str() ); else r.SlaStr();
                    }
                    break;
                }
                case T_Tok:
                {
                    const std::uint64_t v = r.U64();
                    if( doel ) doel->waarden.push_back( Token( v ) );
                    break;
                }
                case T_TokArr:
                {
                    const std::uint32_t k = r.U32();
                    if( !doel ) { r.Sla( 8ull * k ); break; }
                    doel->lijst = true;
                    for( std::uint32_t i = 0; i < k && r.ok; ++i ) doel->waarden.push_back( Token( r.U64() ) );
                    break;
                }
                case T_F:
                {
                    const std::uint32_t bits = r.U32();
                    if( doel ) doel->waarden.push_back( Komma( bits ) );
                    break;
                }
                case T_FArr:
                {
                    const std::uint32_t k = r.U32();
                    if( !doel ) { r.Sla( 4ull * k ); break; }
                    doel->lijst = true;
                    for( std::uint32_t i = 0; i < k && r.ok; ++i ) doel->waarden.push_back( Komma( r.U32() ) );
                    break;
                }
                case T_V2: r.Sla( 8 ); break;
                case T_V2Arr: { const std::uint32_t k = r.U32(); r.Sla( 8ull * k ); if( doel ) doel->lijst = true; break; }
                case T_V3: r.Sla( 12 ); break;
                case T_V3Arr: { const std::uint32_t k = r.U32(); r.Sla( 12ull * k ); if( doel ) doel->lijst = true; break; }
                case T_V3I: r.Sla( 12 ); break;
                case T_V3IArr: { const std::uint32_t k = r.U32(); r.Sla( 12ull * k ); if( doel ) doel->lijst = true; break; }
                case T_V4: r.Sla( 16 ); break;
                case T_V4Arr: { const std::uint32_t k = r.U32(); r.Sla( 16ull * k ); if( doel ) doel->lijst = true; break; }
                case T_V8: r.Sla( 4 * v8 ); break;
                case T_V8Arr: { const std::uint32_t k = r.U32(); r.Sla( 4ull * v8 * k ); if( doel ) doel->lijst = true; break; }
                case T_I32: getal( static_cast<std::int32_t>( r.U32() ) ); break;
                case T_I32Arr:
                {
                    const std::uint32_t k = r.U32();
                    if( !doel ) { r.Sla( 4ull * k ); break; }
                    doel->lijst = true;
                    for( std::uint32_t i = 0; i < k && r.ok; ++i ) getal( static_cast<std::int32_t>( r.U32() ) );
                    break;
                }
                case T_U32: case T_U32b: ofNil( r.U32(), 0xFFFFFFFFull ); break;
                case T_U32Arr:
                {
                    const std::uint32_t k = r.U32();
                    if( !doel ) { r.Sla( 4ull * k ); break; }
                    doel->lijst = true;
                    for( std::uint32_t i = 0; i < k && r.ok; ++i ) groot( r.U32() );
                    break;
                }
                case T_I16:
                {
                    const std::int16_t v = static_cast<std::int16_t>( r.U16() );
                    if( doel ) doel->waarden.push_back( v == 32767 ? std::string( "nil" ) : std::to_string( v ) );
                    break;
                }
                case T_I16Arr:
                {
                    const std::uint32_t k = r.U32();
                    if( !doel ) { r.Sla( 2ull * k ); break; }
                    doel->lijst = true;
                    for( std::uint32_t i = 0; i < k && r.ok; ++i ) getal( static_cast<std::int16_t>( r.U16() ) );
                    break;
                }
                case T_U16: ofNil( r.U16(), 0xFFFFull ); break;
                case T_U16Arr:
                {
                    const std::uint32_t k = r.U32();
                    if( !doel ) { r.Sla( 2ull * k ); break; }
                    doel->lijst = true;
                    for( std::uint32_t i = 0; i < k && r.ok; ++i ) groot( r.U16() );
                    break;
                }
                case T_I64: getal( static_cast<std::int64_t>( r.U64() ) ); break;
                case T_I64Arr:
                {
                    const std::uint32_t k = r.U32();
                    if( !doel ) { r.Sla( 8ull * k ); break; }
                    doel->lijst = true;
                    for( std::uint32_t i = 0; i < k && r.ok; ++i ) getal( static_cast<std::int64_t>( r.U64() ) );
                    break;
                }
                case T_U64: groot( r.U64() ); break;
                case T_U64Arr:
                {
                    const std::uint32_t k = r.U32();
                    if( !doel ) { r.Sla( 8ull * k ); break; }
                    doel->lijst = true;
                    for( std::uint32_t i = 0; i < k && r.ok; ++i ) groot( r.U64() );
                    break;
                }
                case T_Bool:
                {
                    const bool b = r.U8() != 0;
                    if( doel ) doel->waarden.emplace_back( b ? "true" : "false" );
                    break;
                }
                case T_BoolArr:
                {
                    const std::uint32_t k = r.U32();
                    if( !doel ) { r.Sla( k ); break; }
                    doel->lijst = true;
                    for( std::uint32_t i = 0; i < k && r.ok; ++i ) doel->waarden.emplace_back( r.U8() != 0 ? "true" : "false" );
                    break;
                }
                case T_Ord: groot( r.U32() ); break;
                case T_Id: case T_Id2: case T_Id3:
                    if( doel ) doel->waarden.push_back( r.Ident() ); else r.SlaIdent();
                    break;
                case T_IdArrA: case T_IdArrC: case T_IdArrE:
                {
                    const std::uint32_t k = r.U32();
                    if( doel ) doel->lijst = true;
                    for( std::uint32_t i = 0; i < k && r.ok; ++i )
                    {
                        if( doel ) doel->waarden.push_back( r.Ident() ); else r.SlaIdent();
                    }
                    break;
                }
                default: return false;
            }
            return r.ok;
        }

        struct Structuur
        {
            std::string naam;
            bool gewenst = false;
            std::vector<std::pair<std::string, std::uint32_t>> velden;
        };

        bool EenhedenUitBsii( const std::vector<std::uint8_t> &data, const std::vector<std::string> &typen,
                              std::vector<BusSaveLezer::Eenheid> &uit, std::string &fout )
        {
            Lezer r{ data.data(), data.size() };
            if( data.size() < 8 || std::memcmp( data.data(), "BSII", 4 ) != 0 ) { fout = "no BSII header"; return false; }
            r.pos = 4;
            const std::uint32_t versie = r.U32();
            if( versie < 1 || versie > 3 ) { fout = "unknown BSII version"; return false; }

            std::map<std::uint32_t, Structuur> structuren;
            while( r.ok && r.pos < r.n )
            {
                const std::uint32_t bloktype = r.U32();
                if( !r.ok ) break;
                if( bloktype == 0 )
                {
                    const bool geldig = r.U8() != 0;
                    if( !geldig ) continue;
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
                            const std::uint32_t k = r.U32();
                            for( std::uint32_t i = 0; i < k && r.ok; ++i ) { r.U32(); r.SlaStr(); }
                        }
                        s.velden.emplace_back( std::move( veldnaam ), t );
                    }
                    s.gewenst = Gewenst( typen, s.naam.data(), s.naam.size() );
                    if( !structuren.count( id ) ) structuren[ id ] = std::move( s );
                    continue;
                }

                const auto it = structuren.find( bloktype );
                if( it == structuren.end() ) { fout = "unknown structure in data block"; return false; }
                const Structuur &s = it->second;
                if( !s.gewenst )
                {
                    r.SlaIdent();
                    for( const auto &veld : s.velden )
                        if( !LeesWaarde( r, veld.second, versie, nullptr ) ) { fout = "unknown field type"; return false; }
                    continue;
                }
                BusSaveLezer::Eenheid e;
                e.type = s.naam;
                e.id = r.Ident();
                e.velden.reserve( s.velden.size() );
                for( const auto &veld : s.velden )
                {
                    BusSaveLezer::Veld v;
                    v.naam = veld.first;
                    if( !LeesWaarde( r, veld.second, versie, &v ) ) { fout = "unknown field type"; return false; }
                    e.velden.push_back( std::move( v ) );
                }
                if( r.ok ) uit.push_back( std::move( e ) );
            }
            if( !r.ok ) { fout = "BSII ended unexpectedly"; return false; }
            return true;
        }

        std::string Staart( const std::string &s )
        {
            const std::size_t p = s.rfind( '.' );
            return p == std::string::npos ? s : s.substr( p + 1 );
        }

        const std::string *Enkel( const BusSaveLezer::Eenheid &e, const char *naam )
        {
            const BusSaveLezer::Veld *v = e.Zoek( naam );
            if( v == nullptr || v->lijst || v->waarden.size() != 1 ) return nullptr;
            return &v->waarden[ 0 ];
        }

        const std::vector<std::string> &Lijst( const BusSaveLezer::Eenheid &e, const char *naam )
        {
            static const std::vector<std::string> leeg;
            const BusSaveLezer::Veld *v = e.Zoek( naam );
            return ( v != nullptr && v->lijst ) ? v->waarden : leeg;
        }

        bool Kilometers( const std::string &s, std::uint32_t &uit )
        {
            if( s.empty() || s.size() > 6 ) return false;
            std::uint32_t v = 0;
            for( const char c : s )
            {
                if( c < '0' || c > '9' ) return false;
                v = v * 10 + static_cast<std::uint32_t>( c - '0' );
            }
            uit = v;
            return true;
        }

        bool SomKlopt( const std::vector<std::uint32_t> &km, std::uint32_t totaalKm, std::string &afwijking )
        {
            std::uint64_t som = 0;
            for( const std::uint32_t k : km ) som += k;
            if( totaalKm != 0 && som == totaalKm ) return true;
            afwijking.clear();
            for( std::size_t i = 0; i < km.size(); ++i )
                afwijking += ( i == 0 ? "" : "+" ) + std::to_string( km[ i ] );
            afwijking += "=" + std::to_string( som ) + " against total " + std::to_string( totaalKm );
            return false;
        }
    }

    bool BusSaveLezer::LeesEenheden( const std::vector<std::uint8_t> &bestand, const std::vector<std::string> &typen,
                                      std::vector<Eenheid> &uit, std::string &fout )
    {
        fout.clear();
        uit.clear();
        if( bestand.size() < 4 ) { fout = "file too small"; return false; }
        if( std::memcmp( bestand.data(), "ScsC", 4 ) == 0 )
        {
            std::vector<std::uint8_t> binnen;
            if( !SiiDecode::OntsleutelScsC( bestand, binnen, fout ) ) return false;
            if( binnen.size() >= 4 && std::memcmp( binnen.data(), "BSII", 4 ) == 0 ) return EenhedenUitBsii( binnen, typen, uit, fout );
            if( binnen.size() >= 4 && std::memcmp( binnen.data(), "SiiN", 4 ) == 0 ) { EenhedenUitTekst( binnen, typen, uit ); return true; }
            fout = "unknown format";
            return false;
        }
        if( std::memcmp( bestand.data(), "BSII", 4 ) == 0 ) return EenhedenUitBsii( bestand, typen, uit, fout );
        if( std::memcmp( bestand.data(), "SiiN", 4 ) == 0 )
        {
            if( !TekstIsCompleet( bestand ) ) { fout = "text save not complete"; return false; }
            EenhedenUitTekst( bestand, typen, uit );
            return true;
        }
        fout = "unknown format";
        return false;
    }

    bool BusSaveLezer::ZoekRit( const std::vector<Eenheid> &eenheden, const std::vector<std::string> &keten,
                                 const std::uint32_t totaalKm, std::vector<std::uint32_t> &etappeKm, std::string &bron,
                                 std::string &afwijking )
    {
        etappeKm.clear();
        bron.clear();
        afwijking.clear();
        if( keten.size() < 2 ) return false;
        const std::size_t n = keten.size() - 1;
        std::vector<std::string> haltes;
        haltes.reserve( keten.size() );
        for( const std::string &halte : keten ) haltes.push_back( Staart( halte ) );

        std::map<std::string, const Eenheid *> etappes;
        for( const Eenheid &e : eenheden )
            if( e.type == "player_bus_job_stage" ) etappes[ e.id ] = &e;
        for( const Eenheid &e : eenheden )
        {
            if( e.type != "player_bus_job" ) continue;
            const std::vector<std::string> &ids = Lijst( e, "stages" );
            if( ids.size() != n ) continue;
            std::vector<std::uint32_t> km;
            bool goed = true;
            for( std::size_t i = 0; i < n && goed; ++i )
            {
                const auto it = etappes.find( ids[ i ] );
                if( it == etappes.end() ) { goed = false; break; }
                const std::string *van = Enkel( *it->second, "source_bus_stop" );
                const std::string *naar = Enkel( *it->second, "target_bus_stop" );
                const std::string *afstand = Enkel( *it->second, "planned_distance" );
                std::uint32_t k = 0;
                if( van == nullptr || naar == nullptr || afstand == nullptr || !Kilometers( *afstand, k )
                    || Staart( *van ) != haltes[ i ] || Staart( *naar ) != haltes[ i + 1 ] )
                {
                    goed = false;
                    break;
                }
                km.push_back( k );
            }
            if( goed && SomKlopt( km, totaalKm, afwijking ) )
            {
                etappeKm = std::move( km );
                bron = "ride";
                afwijking.clear();
                return true;
            }
        }

        for( const Eenheid &e : eenheden )
        {
            if( e.type != "line_offer_data" ) continue;
            const std::vector<std::string> &doelen = Lijst( e, "saved_targets" );
            const std::vector<std::string> &afstanden = Lijst( e, "targets_distances" );
            if( doelen.size() != n || afstanden.size() != n ) continue;
            const std::string *van = Enkel( e, "source" );
            if( van == nullptr || Staart( *van ) != haltes[ 0 ] ) continue;
            std::vector<std::uint32_t> km;
            bool goed = true;
            for( std::size_t i = 0; i < n; ++i )
            {
                std::uint32_t k = 0;
                if( Staart( doelen[ i ] ) != haltes[ i + 1 ] || !Kilometers( afstanden[ i ], k ) ) { goed = false; break; }
                km.push_back( k );
            }
            if( goed && SomKlopt( km, totaalKm, afwijking ) )
            {
                etappeKm = std::move( km );
                bron = "line";
                afwijking.clear();
                return true;
            }
        }
        return false;
    }

    std::vector<std::pair<std::filesystem::path, std::int64_t>> BusSaveLezer::ZoekSaveBestandenIn( const std::filesystem::path &spelMap )
    {
        std::vector<std::pair<std::filesystem::path, std::int64_t>> uit;
        if( spelMap.empty() ) return uit;
        std::error_code ec;
        for( const char *soort : { "profiles", "steam_profiles" } )
        {
            const std::filesystem::path basis = spelMap / soort;
            if( !std::filesystem::is_directory( basis, ec ) ) continue;
            for( std::filesystem::directory_iterator profiel( basis, ec ), eind; !ec && profiel != eind; profiel.increment( ec ) )
            {
                const std::filesystem::path saveMap = profiel->path() / "save";
                std::error_code ec2;
                if( !std::filesystem::is_directory( saveMap, ec2 ) ) continue;
                for( std::filesystem::directory_iterator slot( saveMap, ec2 ); !ec2 && slot != eind; slot.increment( ec2 ) )
                {
                    const std::filesystem::path g = slot->path() / "game.sii";
                    std::error_code ec3;
                    if( !std::filesystem::is_regular_file( g, ec3 ) ) continue;
                    const auto tijd = std::filesystem::last_write_time( g, ec3 );
                    if( ec3 ) continue;
                    uit.emplace_back( g, static_cast<std::int64_t>( tijd.time_since_epoch().count() ) );
                }
            }
            ec.clear();
        }
        std::sort( uit.begin(), uit.end(), []( const auto &a, const auto &b ) { return a.second > b.second; } );
        return uit;
    }

#ifdef _WIN32
    std::filesystem::path BusSaveLezer::SpelMap()
    {
        PWSTR doc = nullptr;
        if( SHGetKnownFolderPath( FOLDERID_Documents, 0, nullptr, &doc ) != S_OK || !doc ) return {};
        std::filesystem::path map = std::filesystem::path( doc )
            / ( SpelInfo::IsAts() ? L"American Truck Simulator" : L"Euro Truck Simulator 2" );
        CoTaskMemFree( doc );
        return map;
    }

    bool BusSaveLezer::LeesBestand( const std::filesystem::path &pad, std::vector<std::uint8_t> &uit )
    {
        HANDLE h = CreateFileW( pad.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
        if( h == INVALID_HANDLE_VALUE ) return false;
        LARGE_INTEGER groot{};
        if( !GetFileSizeEx( h, &groot ) || groot.QuadPart <= 0 || groot.QuadPart > ( 512LL << 20 ) ) { CloseHandle( h ); return false; }
        uit.resize( static_cast<std::size_t>( groot.QuadPart ) );
        std::size_t gelezen = 0;
        while( gelezen < uit.size() )
        {
            DWORD n = 0;
            const DWORD wil = static_cast<DWORD>( std::min<std::size_t>( uit.size() - gelezen, 1u << 20 ) );
            if( !ReadFile( h, uit.data() + gelezen, wil, &n, nullptr ) || n == 0 ) { CloseHandle( h ); return false; }
            gelezen += n;
        }
        CloseHandle( h );
        return true;
    }
#else
    std::filesystem::path BusSaveLezer::SpelMap()
    {
        const char *map = std::getenv( "CABNAVI_TEST_SPELMAP" );
        return map != nullptr ? std::filesystem::path( map ) : std::filesystem::path();
    }

    bool BusSaveLezer::LeesBestand( const std::filesystem::path &pad, std::vector<std::uint8_t> &uit )
    {
        std::ifstream in( pad, std::ios::binary );
        if( !in ) return false;
        uit.assign( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
        return true;
    }
#endif

    BusSaveLezer::Resultaat BusSaveLezer::Zoek( const std::filesystem::path &spelMap, const std::vector<std::string> &keten,
                                                 const std::uint32_t totaalKm, std::set<Sleutel> &alBekeken, const std::size_t maxSaves,
                                                 const std::int64_t vanaf )
    {
        Resultaat r;
        if( keten.size() < 2 || totaalKm == 0 ) return r;
        const std::vector<std::string> typen = { "line_offer_data", "player_bus_job", "player_bus_job_stage" };

        std::size_t gedaan = 0;
        for( const auto &bestand : ZoekSaveBestandenIn( spelMap ) )
        {
            if( bestand.second > r.nieuwste ) r.nieuwste = bestand.second;
            if( gedaan >= maxSaves || bestand.second < vanaf ) break;
            const Sleutel sleutel( bestand.first.native(), bestand.second );
            if( alBekeken.count( sleutel ) ) continue;
            ++gedaan;

            std::vector<std::uint8_t> ruw;
            if( !LeesBestand( bestand.first, ruw ) ) { r.fout = "not readable"; continue; }
            std::vector<Eenheid> eenheden;
            std::string fout;
            if( !LeesEenheden( ruw, typen, eenheden, fout ) ) { r.fout = fout; continue; }
            alBekeken.insert( sleutel );
            ++r.gelezen;

            std::string afwijking;
            if( ZoekRit( eenheden, keten, totaalKm, r.etappeKm, r.bron, afwijking ) )
            {
                r.gevonden = true;
                r.save = bestand.first.parent_path().filename().string();
                return r;
            }
            if( !afwijking.empty() ) r.afwijking = afwijking;
        }
        r.etappeKm.clear();
        return r;
    }
}

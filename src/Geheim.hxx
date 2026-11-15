#pragma once

#include <string>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace Ritten::Geheim
{
    namespace detail
    {
        inline const char *B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        inline std::string Base64( const std::vector<std::uint8_t> &in )
        {
            std::string uit;
            std::size_t i = 0;
            while( i + 2 < in.size() )
            {
                const std::uint32_t v = ( in[ i ] << 16 ) | ( in[ i + 1 ] << 8 ) | in[ i + 2 ];
                uit += B64[ ( v >> 18 ) & 63 ]; uit += B64[ ( v >> 12 ) & 63 ];
                uit += B64[ ( v >> 6 ) & 63 ];  uit += B64[ v & 63 ];
                i += 3;
            }
            if( i + 1 == in.size() )
            {
                const std::uint32_t v = in[ i ] << 16;
                uit += B64[ ( v >> 18 ) & 63 ]; uit += B64[ ( v >> 12 ) & 63 ]; uit += "==";
            }
            else if( i + 2 == in.size() )
            {
                const std::uint32_t v = ( in[ i ] << 16 ) | ( in[ i + 1 ] << 8 );
                uit += B64[ ( v >> 18 ) & 63 ]; uit += B64[ ( v >> 12 ) & 63 ]; uit += B64[ ( v >> 6 ) & 63 ]; uit += '=';
            }
            return uit;
        }

        inline std::vector<std::uint8_t> VanBase64( const std::string &in )
        {
            std::vector<std::uint8_t> uit;
            std::uint32_t buf = 0; int bits = 0;
            for( char c : in )
            {
                if( c == '=' ) break;
                const char *p = nullptr;
                for( const char *q = B64; *q; ++q ) if( *q == c ) { p = q; break; }
                if( !p ) continue;
                buf = ( buf << 6 ) | static_cast<std::uint32_t>( p - B64 );
                bits += 6;
                if( bits >= 8 ) { bits -= 8; uit.push_back( static_cast<std::uint8_t>( ( buf >> bits ) & 0xFF ) ); }
            }
            return uit;
        }

        inline const char *PREFIX = "dpapi:";
    }

    inline std::string Versleutel( const std::string &klare )
    {
        if( klare.empty() ) return {};
#ifdef _WIN32
        DATA_BLOB in{ static_cast<DWORD>( klare.size() ), reinterpret_cast<BYTE *>( const_cast<char *>( klare.data() ) ) };
        DATA_BLOB uit{ 0, nullptr };
        if( !CryptProtectData( &in, L"CabNavi", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &uit ) )
            return klare;
        std::vector<std::uint8_t> bytes( uit.pbData, uit.pbData + uit.cbData );
        LocalFree( uit.pbData );
        return std::string( detail::PREFIX ) + detail::Base64( bytes );
#else
        return klare;
#endif
    }

    inline std::string Ontsleutel( const std::string &opgeslagen )
    {
        const std::string prefix = detail::PREFIX;
        if( opgeslagen.rfind( prefix, 0 ) != 0 ) return opgeslagen;
#ifdef _WIN32
        std::vector<std::uint8_t> bytes = detail::VanBase64( opgeslagen.substr( prefix.size() ) );
        if( bytes.empty() ) return {};
        DATA_BLOB in{ static_cast<DWORD>( bytes.size() ), bytes.data() };
        DATA_BLOB uit{ 0, nullptr };
        if( !CryptUnprotectData( &in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &uit ) )
            return {};
        std::string klare( reinterpret_cast<char *>( uit.pbData ), uit.cbData );
        LocalFree( uit.pbData );
        return klare;
#else
        return opgeslagen.substr( prefix.size() );
#endif
    }
}

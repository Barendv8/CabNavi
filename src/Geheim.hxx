#pragma once
// Geheim.hxx
//
// Keys at rest. A webhook key or a company token used to sit in plain text
// in %APPDATA%\CabNavi\*.json; copy the file and you hold the key. Now the
// value is wrapped with Windows DPAPI (CryptProtectData), which ties it to
// the Windows account that wrote it: the same file on another machine, or
// under another user, decrypts to nothing.
//
// Stored form: "dpapi:" + base64(ciphertext). A value without that prefix
// is treated as plain text -- that is how files from older versions keep
// working; they are re-saved encrypted the next time settings are written.
// Off Windows (the type check) everything passes through unchanged.

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

    // Plain text -> stored form. Empty stays empty.
    inline std::string Versleutel( const std::string &klare )
    {
        if( klare.empty() ) return {};
#ifdef _WIN32
        DATA_BLOB in{ static_cast<DWORD>( klare.size() ), reinterpret_cast<BYTE *>( const_cast<char *>( klare.data() ) ) };
        DATA_BLOB uit{ 0, nullptr };
        if( !CryptProtectData( &in, L"CabNavi", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &uit ) )
            return klare;   // could not protect: keep working, store as before
        std::vector<std::uint8_t> bytes( uit.pbData, uit.pbData + uit.cbData );
        LocalFree( uit.pbData );
        return std::string( detail::PREFIX ) + detail::Base64( bytes );
#else
        return klare;
#endif
    }

    // Stored form -> plain text. Handles both old plain values and new
    // wrapped ones, so nothing breaks on update.
    inline std::string Ontsleutel( const std::string &opgeslagen )
    {
        const std::string prefix = detail::PREFIX;
        if( opgeslagen.rfind( prefix, 0 ) != 0 ) return opgeslagen;   // old plain value
#ifdef _WIN32
        std::vector<std::uint8_t> bytes = detail::VanBase64( opgeslagen.substr( prefix.size() ) );
        if( bytes.empty() ) return {};
        DATA_BLOB in{ static_cast<DWORD>( bytes.size() ), bytes.data() };
        DATA_BLOB uit{ 0, nullptr };
        if( !CryptUnprotectData( &in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &uit ) )
            return {};   // another account or machine: the key is simply gone
        std::string klare( reinterpret_cast<char *>( uit.pbData ), uit.cbData );
        LocalFree( uit.pbData );
        return klare;
#else
        return opgeslagen.substr( prefix.size() );
#endif
    }
}

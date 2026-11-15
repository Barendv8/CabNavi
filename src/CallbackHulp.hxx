#pragma once

#include "Logboek.hxx"

#include <exception>
#include <string>
#include <utility>

namespace Ritten
{
    void LogPluginFout( const std::string &bericht );

    template <typename Functie>
    auto Beschermd( const char *naam, Functie fn )
    {
        return [ naam, fn = std::move( fn ) ]( auto &&...args )
        {
            try
            {
                fn( std::forward<decltype( args )>( args )... );
            }
            catch( const std::exception &ex )
            {
                const std::string melding =
                    std::string( "Exception in " ) + naam + ": " + Logboek::KorteFout( ex.what() )
                    + " | last location: " + Logboek::LaatstBekend();
                Logboek::Schrijf( "ERROR", melding );
                LogPluginFout( melding );
            }
            catch( ... )
            {
                const std::string melding =
                    std::string( "Onbekende exceptie in " ) + naam
                    + " | last location: " + Logboek::LaatstBekend();
                Logboek::Schrijf( "ERROR", melding );
                LogPluginFout( melding );
            }
        };
    }
}

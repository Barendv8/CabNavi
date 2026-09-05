#pragma once
// CallbackHulp.hxx
//
// Small helper to protect SDK callbacks against escaping exceptions.
//
// The SDK docs are explicit about this (Events -> Delivery guarantees):
//
//     "Exceptions must not escape your callback. The SDK contains guard
//      rails, but an exception that leaves your handler is a bug in your
//      plugin. Catch your own exceptions and log them."
//
// Our callbacks do things that CAN throw: JSON serialisation
// (nlohmann::json throws on odd values), writing to disk, and
// std::string/std::vector allocations. Without a safety net such an
// exception lands in the middle of the client's frame handling.
//
// Usage:
//
//     module.OnIets.Register( Beschermd( "OnIets", [ this ]( SomeEvent &e )
//     {
//         ...
//     } ) );
//
// The name is purely for the log line, so debug.log shows which
// callback it was.

#include "Logboek.hxx"

#include <exception>
#include <string>
#include <utility>

namespace Ritten
{
    // Implemented in Plugin.cxx: writes to the TruckersMP client log via
    // Core().LogMessage, so the message ends up where users and the TMP
    // team already look (recommendation from the docs).
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

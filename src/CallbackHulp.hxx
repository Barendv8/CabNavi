#pragma once
// CallbackHulp.hxx
//
// Kleine helper om SDK-callbacks te beschermen tegen ontsnappende excepties.
//
// De SDK-docs zijn hier expliciet over (Events -> Delivery guarantees):
//
//     "Exceptions must not escape your callback. The SDK contains guard
//      rails, but an exception that leaves your handler is a bug in your
//      plugin. Catch your own exceptions and log them."
//
// Onze callbacks doen dingen die WEL kunnen gooien: JSON serialiseren
// (nlohmann::json gooit bij rare waarden), naar schijf schrijven, en
// std::string/std::vector-allocaties. Zonder vangnet belandt zo'n exceptie
// midden in de frame-afhandeling van de client.
//
// Gebruik:
//
//     module.OnIets.Register( Beschermd( "OnIets", [ this ]( SomeEvent &e )
//     {
//         ...
//     } ) );
//
// De naam is puur voor de logregel, zodat je in debug.log ziet welke
// callback het was.

#include "Logboek.hxx"

#include <exception>
#include <string>
#include <utility>

namespace Ritten
{
    // Wordt geimplementeerd in Plugin.cxx: schrijft naar de TruckersMP-
    // clientlog via Core().LogMessage, zodat de melding terechtkomt waar
    // gebruikers en het TMP-team toch al kijken (aanbeveling uit de docs).
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
                    std::string( "Exceptie in " ) + naam + ": " + ex.what()
                    + " | laatste plek: " + Logboek::LaatstBekend();
                Logboek::Schrijf( "FOUT", melding );
                LogPluginFout( melding );
            }
            catch( ... )
            {
                const std::string melding =
                    std::string( "Onbekende exceptie in " ) + naam
                    + " | laatste plek: " + Logboek::LaatstBekend();
                Logboek::Schrijf( "FOUT", melding );
                LogPluginFout( melding );
            }
        };
    }
}

#pragma once
// ---------------------------------------------------------------------------
// SiiDecode -- READ game.sii and extract the trucks.
//
// Purpose: the trip counter of the truck dashboard (litres and kilometres
// since the last reset) is not in the SDK but it is in the save. This module
// extracts it so "gem" in the HUD can run exactly in step with the
// dashboard, without anyone having to use a reset button.
//
// READ ONLY. There is not a single write call in this module, and it must
// stay that way. The user's save is opened read-only, processed in memory,
// and nothing more.
//
// Formats (MEASURED 04-09 on two real saves):
//   ScsC  = AES-256-CBC (known key, IV at byte 36, data from 56),
//           then zlib. Inside is SiiN or BSII.
//   BSII  = binary. Structure definitions followed by data blocks.
//   SiiN  = text.
//
// Fully portable (no Windows API), so it can be tested on Linux.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

namespace Ritten
{
    struct SiiDecode
    {
    // What we want to know about a truck. All values are summed:
    // kmStand = odometer + odometer_float_part, and so on.
        struct SaveTruck
    {
        double kmStand = 0.0;  // odometer
        double integriteit = 0.0;  // integrity_odometer (km since last major service)
        double tripLiters = 0.0;  // trip_fuel_l + trip_fuel
        double tripKm = 0.0;  // trip_distance_km + trip_distance / 1000
        bool heeftBreukdeel = false;  // false for dealer stock (round reading, never driven)
    };

    // Raw bytes of a game.sii -> list of trucks. Empty on error.
    // Error message (short, without paths) in 'fout' if it is not empty.
        static std::vector<SaveTruck> LeesTrucks( const std::vector<std::uint8_t> &bestand, std::string &fout );

    // Separate steps, public for the test:
        static bool OntsleutelScsC( const std::vector<std::uint8_t> &in, std::vector<std::uint8_t> &uit, std::string &fout );
        static bool Inflate( const std::uint8_t *data, std::size_t lengte, std::vector<std::uint8_t> &uit, std::size_t verwachteGrootte );
        static std::vector<SaveTruck> TrucksUitBsii( const std::vector<std::uint8_t> &data, std::string &fout );
        static std::vector<SaveTruck> TrucksUitTekst( const std::vector<std::uint8_t> &data );
    };
}

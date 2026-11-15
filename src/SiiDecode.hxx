#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Ritten
{
    struct SiiDecode
    {
        struct SaveTruck
    {
        double kmStand = 0.0;
        double integriteit = 0.0;
        double tripLiters = 0.0;
        double tripKm = 0.0;
        bool heeftBreukdeel = false;
    };

        static std::vector<SaveTruck> LeesTrucks( const std::vector<std::uint8_t> &bestand, std::string &fout );

        static bool OntsleutelScsC( const std::vector<std::uint8_t> &in, std::vector<std::uint8_t> &uit, std::string &fout );
        static bool Inflate( const std::uint8_t *data, std::size_t lengte, std::vector<std::uint8_t> &uit, std::size_t verwachteGrootte );
        static std::vector<SaveTruck> TrucksUitBsii( const std::vector<std::uint8_t> &data, std::string &fout );
        static std::vector<SaveTruck> TrucksUitTekst( const std::vector<std::uint8_t> &data );
    };
}

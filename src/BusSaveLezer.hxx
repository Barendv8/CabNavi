#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Ritten
{
    class BusSaveLezer
    {
    public:
        struct Veld
        {
            std::string naam;
            bool lijst = false;
            std::vector<std::string> waarden;
        };
        struct Eenheid
        {
            std::string type;
            std::string id;
            std::vector<Veld> velden;
            const Veld *Zoek( const char *naam ) const;
        };

        using Sleutel = std::pair<std::filesystem::path::string_type, std::int64_t>;

        struct Resultaat
        {
            bool gevonden = false;
            std::vector<std::uint32_t> etappeKm;
            std::string bron;
            std::string save;
            std::string fout;
            std::string afwijking;
            int gelezen = 0;
            std::int64_t nieuwste = ( std::numeric_limits<std::int64_t>::min )();
        };

        static bool LeesEenheden( const std::vector<std::uint8_t> &bestand, const std::vector<std::string> &typen,
                                  std::vector<Eenheid> &uit, std::string &fout );

        static bool ZoekRit( const std::vector<Eenheid> &eenheden, const std::vector<std::string> &keten,
                             std::uint32_t totaalKm, std::vector<std::uint32_t> &etappeKm, std::string &bron,
                             std::string &afwijking );

        static std::vector<std::pair<std::filesystem::path, std::int64_t>> ZoekSaveBestandenIn( const std::filesystem::path &spelMap );

        static std::filesystem::path SpelMap();

        static Resultaat Zoek( const std::filesystem::path &spelMap, const std::vector<std::string> &keten,
                               std::uint32_t totaalKm, std::set<Sleutel> &alBekeken, std::size_t maxSaves,
                               std::int64_t vanaf );

        static bool LeesBestand( const std::filesystem::path &pad, std::vector<std::uint8_t> &uit );
    };
}

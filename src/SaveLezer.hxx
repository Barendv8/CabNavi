#pragma once

#include "SiiDecode.hxx"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Ritten
{
    struct SaveTripteller
    {
        double kilometerstandKm = 0.0;
        double onderhoudKm = 0.0;
        double tripLiters = 0.0;
        double tripKm = 0.0;
        std::string bron;
    };

    class SaveLezer
    {
    public:
        static std::vector<std::filesystem::path> ZoekSaveBestanden();

        static std::optional<SaveTripteller> ZoekInSave( const std::filesystem::path &pad, double kilometerstandKm, std::string &fout );

        static std::optional<SaveTripteller> ZoekInAlleSaves( double kilometerstandKm, std::string &fout );

        static bool LeesBestand( const std::filesystem::path &pad, std::vector<std::uint8_t> &uit );
    };
}

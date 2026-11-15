#pragma once

#include "FuelCosts.hxx"
#include "TripTypes.hxx"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Ritten
{
    struct RitMoment
    {
        double kmStand = 0.0;
        double liters = 0.0;
        double kosten = 0.0;
        double km = 0.0;
        std::uint32_t boetes = 0;
        std::uint32_t doorgangen = 0;
        std::uint32_t tankbeurten = 0;
    };

    struct LopendeRit
    {
        std::string sleutel;

        std::string id;
        std::string startTijdIso;
        std::uint32_t economyStartTijd = 0;

        std::string voertuigMerk, voertuigModel;
        double kmStand = 0.0;

        double liters = 0.0;
        double kosten = 0.0;
        double km = 0.0;
        FuelCosts::LeegRijden leegVoor;
        std::vector<FuelCosts::Tankbeurt> tankbeurten;
        std::vector<Boete> boetes;
        std::vector<Doorgang> doorgangen;
        std::int64_t boeteKosten = 0;
        std::int64_t tolKosten = 0;
        std::int64_t veerbootKosten = 0;
        std::int64_t treinKosten = 0;

        std::vector<RitMoment> momenten;

        std::string bewaardIso;
    };

    class RitBoek
    {
    public:
        enum class Soort { Vracht, Bus };

        static bool Lees( Soort soort, LopendeRit &uit );

        static bool Bewaar( Soort soort, const LopendeRit &rit );

        static void Wis( Soort soort );

        static std::filesystem::path Pad( Soort soort );

        static constexpr double MOMENT_KM = 2.0;
        static void VoegMomentToe( std::vector<RitMoment> &momenten, const RitMoment &moment );

        static constexpr double SPELING_KM = 5.0;
        static constexpr double GELIJK_KM = 0.1;

        static bool KnipTerug( const LopendeRit &rit, double kmNu, LopendeRit &uit );
    };
}

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Ritten
{
    enum class TripType
    {
        Vracht,
        Bus
    };

    enum class TripStatus
    {
        Bezig,
        Voltooid,
        Geannuleerd
    };

    struct StopInfo
    {
        std::string naam;
        std::string cityIdentifier;
        bool voltooid = false;
        double afgelegdeAfstandKm = 0.0;
        double geplandeAfstandKm = 0.0;

        int instappers = 0;
        int uitstappers = 0;

        double geplandeTijdMin = 0.0;

        std::uint32_t geplandVertrek = 0;
        std::uint32_t werkelijkVertrek = 0;
        std::uint32_t geplandAankomst = 0;
        std::uint32_t werkelijkAankomst = 0;
    };

    struct Boete
    {
        std::string reden;
        std::int64_t bedrag = 0;
    };

    enum class DoorgangType
    {
        Tol,
        Veerboot,
        Trein
    };

    struct Doorgang
    {
        DoorgangType type = DoorgangType::Tol;
        std::int64_t bedrag = 0;
        std::string vanaf;
        std::string naar;
    };

    struct Trip
    {
        TripType type = TripType::Vracht;
        TripStatus status = TripStatus::Bezig;

        std::string id;

        std::string startTijdIso;
        std::string eindTijdIso;
        std::uint32_t economyStartTijd = 0;
        std::uint32_t economyEindTijd = 0;

        std::string serverNaam;
        std::string voertuigMerk;
        std::string voertuigModel;

        std::string lading;
        std::string bronStad;
        std::string bestemmingStad;
        std::string bronBedrijf;
        std::string bestemmingBedrijf;
        double geplandeAfstandKm = 0.0;
        double afgelegdeAfstandKm = 0.0;
        std::int64_t inkomen = 0;
        bool opTijd = true;

        double brandstofVerbruikLiters = 0.0;
        double brandstofKostenEuro = 0.0;

        std::vector<Boete> boetes;
        std::vector<Doorgang> doorgangen;
        std::int64_t tolKosten = 0;
        std::int64_t veerbootKosten = 0;
        std::int64_t treinKosten = 0;
        std::int64_t boeteKosten = 0;

        double aanhangerSchadePercentage = 0.0;
        double ladingSchadePercentage = 0.0;
        double ladingGewichtKg = 0.0;

        std::vector<StopInfo> haltes;
        std::int64_t geschatUitbetaling = 0;

        std::uint32_t passagiers = 0;
        std::string annuleringsReden;

        std::string busMarkt;
        double tevredenheid = -1.0;
        std::uint32_t passagiersVervoerd = 0;
        std::int32_t verdiendeXp = 0;
        std::int64_t annuleerBoete = 0;

        std::string markt;
        bool speciaal = false;
        std::int32_t minutenOver = 0;
        bool minutenOverBekend = false;

        double huidigeSnelheidKmh = 0.0;
        double brandstofPercentage = 0.0;
        double schadeChassisPercentage = 0.0;
    };
}

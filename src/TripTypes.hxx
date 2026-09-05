#pragma once
// TripTypes.hxx
//
// A "Trip" is our own uniform representation of a job -- whether it comes
// from the TruckersMP BusModule (bus lines) or from the SCS telemetry job
// channels (regular cargo). Everything the overlay shows and everything
// TripLogger writes to trips.jsonl is based on this struct.

#include <cstdint>
#include <string>
#include <vector>

namespace Ritten
{
    enum class TripType
    {
        Vracht,  // regular ETS2/ATS cargo job (SCS telemetry)
        Bus  // TruckersMP bus line job (BusModule)
    };

    enum class TripStatus
    {
        Bezig,
        Voltooid,
        Geannuleerd
    };

    struct StopInfo
    {
        std::string naam;  // display name (e.g. "Parijs") -- for showing in the overlay
        std::string cityIdentifier;  // internal city code (e.g. "paris") -- for matching OnStopCompleted
        bool voltooid = false;
        double afgelegdeAfstandKm = 0.0;
        double geplandeAfstandKm = 0.0;  // from trip start to this stop, for the remaining-time estimate

        // Who boards and alights at THIS stop. Comes straight from the SDK
        // at OnJobDataReady; nothing to compute ourselves.
        int instappers = 0;
        int uitstappers = 0;

        // Planned driving time in ECONOMY minutes (game time), cumulative
        // from trip start to this stop. The SDK gives per stop the time from
        // the PREVIOUS stop (like the distance), so we add it up ourselves.
        // Stays 0 until OnJobDataReady has fired -- the game only computes
        // the navigation data after the job starts.
        double geplandeTijdMin = 0.0;
    };

    // One fine as the game reports it via the "player.fined" gameplay
    // event. `reden` is the game's raw offence code (e.g. "speeding",
    // "red_signal"); Overlay translates it to Dutch.
    struct Boete
    {
        std::string reden;
        std::int64_t bedrag = 0;
    };

    // One paid passage: toll gate, ferry or train. The game reports all
    // three via separate gameplay events, but in the same shape.
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
        std::string vanaf;  // only filled for ferry/train
        std::string naar;  // same
    };

    struct Trip
    {
        TripType type = TripType::Vracht;
        TripStatus status = TripStatus::Bezig;

        std::string id;  // unique id (timestamp + type), used in trips.jsonl

        // General
        std::string startTijdIso;
        std::string eindTijdIso;
        std::uint32_t economyStartTijd = 0;  // in-game economy minutes
        std::uint32_t economyEindTijd = 0;

        std::string serverNaam;
        std::string voertuigMerk;
        std::string voertuigModel;

        // Cargo-specific
        std::string lading;
        std::string bronStad;
        std::string bestemmingStad;
        std::string bronBedrijf;
        std::string bestemmingBedrijf;
        double geplandeAfstandKm = 0.0;
        double afgelegdeAfstandKm = 0.0;
        std::int64_t inkomen = 0;
        bool opTijd = true;

        // Estimated fuel cost of this trip (see FuelCosts.hxx: based on
        // measured consumption x self-set price per litre, not on an exact
        // in-game price the telemetry does not expose).
        double brandstofVerbruikLiters = 0.0;
        double brandstofKostenEuro = 0.0;

        // Expenses the game itself reports as gameplay events during the
        // trip (these are REAL in-game amounts, not an estimate like fuel --
        // for fuel we must fill in a price per litre ourselves because the
        // telemetry does not expose it).
        std::vector<Boete> boetes;
        std::vector<Doorgang> doorgangen;
        std::int64_t tolKosten = 0;  // sum of all toll gates this trip
        std::int64_t veerbootKosten = 0;  // sum of all ferries this trip
        std::int64_t treinKosten = 0;  // sum of all trains this trip
        std::int64_t boeteKosten = 0;  // sum of all fines this trip

        // Trailer: damage to the chassis and to the cargo itself. These two
        // are not the same -- you can have a dented trailer with perfect
        // cargo, and the other way round. Only cargo damage counts towards
        // your payout.
        double aanhangerSchadePercentage = 0.0;
        double ladingSchadePercentage = 0.0;
        double ladingGewichtKg = 0.0;

        // Bus-specific
        std::vector<StopInfo> haltes;
        std::int64_t geschatUitbetaling = 0;

        // Number of passengers for this bus line. Comes from the SDK at
        // trip start.
        std::uint32_t passagiers = 0;
        std::string annuleringsReden;

        // Live/current reading (only relevant while status == Bezig)
        double huidigeSnelheidKmh = 0.0;
        double brandstofPercentage = 0.0;
        double schadeChassisPercentage = 0.0;
    };
}

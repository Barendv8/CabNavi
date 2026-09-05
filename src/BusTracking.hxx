#pragma once
// BusTracking.hxx
//
// Follows bus line jobs via TruckersMP::BusModule and turns every
// completed/cancelled trip into a Ritten::Trip that goes to the TripLogger.
// Also keeps the "live" trip so the overlay can show progress during the
// trip.

#include "TripLogger.hxx"
#include "TripTypes.hxx"

#include <TruckersMP/TruckersMP.hxx>
#include <TruckersMP/Bus.hxx>

#include <chrono>
#include <deque>
#include <memory>
#include <utility>

namespace Ritten
{
    class BusTracking
    {
    public:
        BusTracking( TruckersMP::Session &session, TripLogger &logger );

        // To be called by the telemetry callback as soon as the "game.time"
        // channel changes (see Plugin.cxx / TruckTracking.cxx).
        void ZetEconomyTijd( std::uint32_t minuten );

        // Called by TruckTracking on every live speed reading (the
        // "truck.speed" channel is not job-type specific -- same vehicle, so
        // same speed, whether you drive cargo or a bus line). That lets the
        // bus line tracking do the same reliable IRL time estimate as cargo,
        // without registering telemetry itself.
        void OpLiveSnelheid( double snelheidKmh, bool gepauzeerd );

        // Navigation data from the game, passed on from TruckTracking (which
        // registers the SCS channels; the bus has none of its own).
        // `navTijdRuw` is the value of truck.navigation.time as the game gives
        // it, `navAfstandKm` the remaining route distance.
        void ZetNavigatie( double navTijdRuw, double navAfstandKm );

        // For the overlay: is there an active bus line trip right now, and if
        // so, which. NOTE: this deliberately looks at its own 'm_actief' flag,
        // not at m_huidigeRit.status -- a default-constructed Trip has status
        // == Bezig, so that comparison would wrongly show a non-existent trip
        // as active.
        bool HeeftActieveRit() const { return m_actief; }
        const Trip &HuidigeRit() const { return m_huidigeRit; }

        // Real (clock) time since trip start, pause-aware (does not run
        // during a pause -- see the GepauzeerdCallback pattern in
        // TruckTracking; here we receive that status via OpLiveSnelheid).
        double VerstrekenMinutenEcht() const;

        // Estimated remaining IRL time to the next uncompleted stop, based on
        // your moving-average speed.
        // -1.0 = not yet estimable (too little data, or no active trip).
        double GeschatteResterendeMinutenEcht() const;

        // Estimated REAL minutes to stop `index`, computed the same way as
        // the time to the next stop: that estimate is the starting point, and
        // the extra distance after it is converted with the same travel speed.
        //
        // -1.0 = not determinable (stop already completed, or no distance data).
        double GeschatteMinutenTotHalte( std::size_t index ) const;

        // --- Being late (TMP 0.7.5.0 introduced a penalty) ---------------
        //
        // The penalty works like this: only the LAST stop counts, the first 60
        // minutes of delay are free, and above that every minute costs 0.333%
        // of the payout -- after a good 6 hours you keep nothing.
        //
        // To know whether you are late we have to compare two clocks: the
        // deadline is in ECONOMY minutes (game time), our arrival estimate in
        // REAL minutes. The ratio between them is derived from how fast game
        // time runs (see TijdSchaal()), instead of assuming a fixed factor --
        // TruckersMP changed that scale again in 0.7.5.0, so a constant would
        // be outdated right away.
        //
        // Returns the number of economy minutes you expect to be LATE at the
        // last stop. Negative means you are ahead of schedule.
        // -1e9 = not determinable (no active trip, or navigation data not
        // ready yet).
        double GeschatteVertragingMinuten() const;

        // What percentage of the payout you lose at the current delay
        // (0..100). 0 as long as you stay within the hour of slack.
        double GeschatteBoetePercentage() const;

        // Economy minutes per real minute, derived from observations.
        // 0 while too little has been measured.
        double TijdSchaal() const;

    private:
        void StartRecord( const TruckersMP::BusJob &job );

        TripLogger &m_logger;
        std::unique_ptr<TruckersMP::BusModule> m_busModule;
        Trip m_huidigeRit;
        bool m_actief = false;
        std::uint32_t m_economyTijd = 0;

        // --- Measuring the time scale -------------------------------------
        // game.time only refreshes once per economy minute (per the docs), so
        // we measure over a longer window: we remember the first moment we
        // saw a change and compare it with the last. That averages out the
        // jerkiness.
        std::uint32_t m_schaalEersteEconomy = 0;
        std::chrono::steady_clock::time_point m_schaalEersteEcht{};
        bool m_schaalGestart = false;
        // See TruckTracking: once reliably measured we lock the scale, so it
        // no longer moves while driving.
        mutable double m_vastgezetteSchaal = 0.0;
        std::chrono::steady_clock::time_point m_ritStartMoment;

        // Pause tracking (same principle as TruckTracking, but here received
        // via OpLiveSnelheid instead of our own SCS registration).
        bool m_gepauzeerd = false;
        std::chrono::steady_clock::time_point m_pauzeStartMoment;
        double m_totaalGepauzeerdSeconden = 0.0;

        // Moving average of the last ~3 minutes of speed.
        std::deque<std::pair<std::chrono::steady_clock::time_point, double>> m_snelheidVenster;

        // --- Own copy of the arrival-time setup ------------------------------
        // Deliberately separate from TruckTracking: same construction, but its
        // own instance, so tinkering with the bus does not touch the cargo trip.
        double m_navTijdRuw = -1.0;
        double m_navAfstandKm = -1.0;
        mutable double m_gladdeSchattingMin = -1.0;
        double Gladstrijken( double ruweMinuten ) const;

        // Effective travel speed in km per REAL hour. Separate function so the
        // per-stop estimate uses the same speed as the estimate to the next
        // stop -- without me having to touch that existing function.
        double EffectieveSnelheidEcht() const;
        static constexpr double VENSTER_SECONDEN = 180.0;

        // Live distance since the last COMPLETED stop (via speed x elapsed
        // time, like cargo) -- needed because m_huidigeRit.afgelegdeAfstandKm
        // only updates ONCE a stop is officially completed (via the game
        // event). Without this the remaining-time estimate thought you still
        // had to drive the WHOLE last leg, even when you were almost there --
        // the counter only caught up at arrival itself, not on the way.
        double m_liveKmSindsLaatsteHalte = 0.0;
        std::chrono::steady_clock::time_point m_laatsteSnelheidMeting;

        // When did we last write the per-stop prediction?
        std::chrono::steady_clock::time_point m_laatsteHalteLog{};

        // How often that line may appear. Ten seconds is enough to follow a
        // bus trip; shorter only makes the log unreadable.
        static constexpr double LOG_INTERVAL_SECONDEN = 10.0;
    };
}

#pragma once
// TruckTracking.hxx
//
// Regular cargo jobs (so not TruckersMP bus lines) are base-game data and
// run via the SCS Telemetry SDK, not via the TruckersMP Client SDK itself.
// This class processes the telemetry callbacks (job started/delivered/
// cancelled + live channels like speed, fuel and damage) and turns them
// into Ritten::Trip records.
//
// NOTE: this file uses channel/config/event names as PLAIN TEXT (e.g.
// "truck.speed") instead of the *_CHANNEL_*/*_CONFIG_* macros from the SCS
// examples. That is just as correct -- the macros are ultimately these same
// text strings -- but it avoids a dependency on the game-specific headers
// (eurotrucks2/scssdk_telemetry_eut2.h etc.) that are not in the same place
// in every SDK download. See readme.txt in your scssdk folder if you want
// to verify the channels exactly.

#include "FuelCosts.hxx"
#include "SaveLezer.hxx"
#include "TripLogger.hxx"
#include "TripTypes.hxx"

#include <scssdk_telemetry.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace Ritten
{
    class BusTracking;  // forward declaration, see m_busTracking below
    class PlayersNearby;
    class IncidentRecorder;

    class TruckTracking
    {
    public:
        TruckTracking( TripLogger &logger, FuelCosts &brandstof );

        // Only to write out the per-vehicle counter. It is saved at most
        // once a minute while driving; without this, up to a minute of litres
        // and kilometres was lost at shutdown.
        ~TruckTracking();

        // Called from scs_telemetry_init with the registration function
        // provided by SCS, to subscribe to all channels/events.
        void RegistreerBijTelemetrie( const scs_telemetry_init_params_v101_t *params );

        std::uint32_t EconomyTijd() const { return m_economyTijd; }

        // --- Time scale ---------------------------------------------------
        // How many GAME minutes pass per REAL minute. In ETS2 the clock runs
        // faster than in reality, and TruckersMP puts its own scale on it
        // (which they changed again in 0.7.5.0). So we measure it from
        // game.time instead of assuming a fixed number.
        //
        // Why this matters: the speedometer gives km per GAME hour. To know
        // how long something takes in REAL life you must multiply that speed
        // by the scale -- otherwise you compute game minutes and call them
        // real minutes. Exactly that confusion produced estimates of 22 hours
        // for a fifteen-minute trip.
        //
        // 0 = not measured enough yet.
        double TijdSchaal() const;

        // Raw values behind the ETA estimate. Purely to be able to see WHY a
        // number is shown, instead of having to guess.
        // navigatieTijd < 0 means: the channel has not sent anything yet.
        double RuweNavigatieTijd() const { return m_navigatieTijd; }
        bool SchaalStaatVast() const { return m_vastgezetteSchaal > 0.0; }
        double RuweNavigatieAfstandKm() const
        {
            return m_navigatieAfstandMeter >= 0.0 ? m_navigatieAfstandMeter / 1000.0 : -1.0;
        }

    private:
        // Keeps the estimate from bouncing back and forth at every traffic light.
        double Gladstrijken( double ruweMinuten ) const;

    public:
        // NOTE: deliberately looks at its own 'm_actief' flag, not at
        // m_huidigeRit.status -- see the same remark in BusTracking.hxx.
        bool HeeftActieveRit() const { return m_actief; }
        const Trip &HuidigeRit() const { return m_huidigeRit; }

        // Real (clock) time since trip start, and an estimate of how long it
        // still takes in real life based on your average speed so far (or
        // your current speed if there is too little data yet). So this is
        // REAL minutes as you experience them, not in-game economy time --
        // those two do not run in step, depending on your economy time
        // setting in the game.
        double VerstrekenMinutenEcht() const;

        // Live speed, always updated while you drive -- even without an
        // active job. For small "feels alive" details in the overlay (like
        // letting the minimap move subtly), not tied to a specific trip like
        // HuidigeRit().huidigeSnelheidKmh.
        double LiveSnelheidKmh() const { return m_liveSnelheidKmh; }
        double GeschatteResterendeMinutenEcht() const;  // -1.0 = not estimable yet

        // All "board computer" values together, so the overlay can fetch
        // them in one go instead of ten separate getters. Values the game has
        // not (yet) passed on stay at -1.0, so the overlay can show "--"
        // instead of a misleading 0.
        struct VoertuigStatus
        {
            double bereikKm = -1.0;  // truck.fuel.range
            double verbruikLiterPer100Km = -1.0;  // derived from truck.fuel.consumption.average (l/km)
            // The two above come from the game itself and are really an SCS trip
            // computer figure (does not necessarily reset per our trip, depends
            // heavily on truck model/trailer) -- see the research of 30-08. The
            // two below are OUR own, more reliable calculation based on the fuel
            // level, which is spot on:
            double verbruikGemiddeldLiterPer100Km = -1.0;  // own average: only DRIVING consumption / driving km
            double verbruikNuLiterPer100Km = -1.0;  // short window (~8 sec), falls back on the average
            double verbruikLiterPerUur = -1.0;  // idle/slow consumption; l/100km is meaningless then
            double verbruikRitLiterPer100Km = -1.0;  // average of THIS trip, for the economy line
            double ritLiters = 0.0, ritKm = 0.0;  // raw trip counter
            double voertuigLiters = 0.0, voertuigKm = 0.0;  // raw counter of the current vehicle (incl. this trip)
            bool staatStil = false;  // if so: show l/h instead of l/100km
            bool echtStil = false;  // truly standing still (for the word "stationair")
            double kilometerstandKm = -1.0;  // truck.odometer
            double snelheidslimietKmh = -1.0;  // truck.navigation.speed.limit
            double cruiseControlKmh = 0.0;  // 0 = off
            double schadeMotor = -1.0;  // percentages 0-100
            double schadeBak = -1.0;
            double schadeCabine = -1.0;
            double schadeWielen = -1.0;
            double schadeChassis = -1.0;
            double aanhangerSchade = -1.0;
            double ladingSchade = -1.0;
            double ladingGewichtKg = -1.0;
            bool heeftAanhanger = false;
        };

        VoertuigStatus HuidigeVoertuigStatus() const;

        // Speeding? Only true if a limit is known AND you are above it by
        // more than the margin (small margin so a tiny overshoot while
        // overtaking does not flicker).
        bool RijdtTeHard() const;

    private:
        // Static trampolines (SCS callbacks are C function pointers without
        // context, except one where we pass `this` as user_data).
        // Measurement: which country channels does the game offer? Only to
        // find out whether automatic prices per country are feasible.
        static SCSAPI_VOID LandCallback( const scs_string_t name, scs_u32_t index,
                                          const scs_value_t *value, scs_context_t context );

        static SCSAPI_VOID NavigatieTijdCallback( const scs_string_t name, scs_u32_t index,
                                                   const scs_value_t *value, scs_context_t context );

        static SCSAPI_VOID RustStopCallback( const scs_string_t name, scs_u32_t index,
                                              const scs_value_t *value, scs_context_t context );

        static SCSAPI_VOID GameTimeCallback( const scs_string_t name, scs_u32_t index,
                                              const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID SnelheidCallback( const scs_string_t name, scs_u32_t index,
                                              const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID BrandstofLitersCallback( const scs_string_t name, scs_u32_t index,
                                                      const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID SchadeCallback( const scs_string_t name, scs_u32_t index,
                                            const scs_value_t *value, scs_context_t context );
        // The game's own remaining navigation distance (metres, real road).
        static SCSAPI_VOID NavigatieAfstandCallback( const scs_string_t name, scs_u32_t index,
                                                       const scs_value_t *value, scs_context_t context );
        // Purely for logging (see note at registration in .cxx).
        static SCSAPI_VOID LocalScaleCallback( const scs_string_t name, scs_u32_t index,
                                                 const scs_value_t *value, scs_context_t context );

        // --- New channels (idea list #1,2,6,7,8,9,10) --------------------
        // All names below are verified against the official SCS headers
        // scssdk_telemetry_truck_common_channels.h and the trailer
        // registration in RenCloud/scs-sdk-plugin -- not guessed.
        static SCSAPI_VOID BereikCallback( const scs_string_t name, scs_u32_t index,
                                            const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID VerbruikCallback( const scs_string_t name, scs_u32_t index,
                                              const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID KilometerstandCallback( const scs_string_t name, scs_u32_t index,
                                                     const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID SnelheidslimietCallback( const scs_string_t name, scs_u32_t index,
                                                      const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID CruiseControlCallback( const scs_string_t name, scs_u32_t index,
                                                    const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID GaspedaalCallback( const scs_string_t name, scs_u32_t index,
                                               const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID SchadeMotorCallback( const scs_string_t name, scs_u32_t index,
                                                  const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID SchadeBakCallback( const scs_string_t name, scs_u32_t index,
                                               const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID SchadeCabineCallback( const scs_string_t name, scs_u32_t index,
                                                   const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID SchadeWielenCallback( const scs_string_t name, scs_u32_t index,
                                                   const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID AanhangerSchadeCallback( const scs_string_t name, scs_u32_t index,
                                                      const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID LadingSchadeCallback( const scs_string_t name, scs_u32_t index,
                                                   const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID ConfigCallback( const scs_event_t event, const void *event_info,
                                            scs_context_t context );
        static SCSAPI_VOID GameplayEventCallback( const scs_event_t event, const void *event_info,
                                                    scs_context_t context );
        // SCS_TELEMETRY_EVENT_paused/started -- tell us exactly when the
        // simulation stands still (pause menu, loading screen etc.); this uses
        // the same pattern as the official SCS example (telemetry.cpp
        // registers these two events too). Needed to NOT let the IRL clock
        // tick during a pause.
        static SCSAPI_VOID GepauzeerdCallback( const scs_event_t event, const void *event_info,
                                                scs_context_t context );
        static SCSAPI_VOID HervatCallback( const scs_event_t event, const void *event_info,
                                            scs_context_t context );

        void OpVoertuigConfig( const scs_telemetry_configuration_t *cfg );
        void OpGameplayEvent( const scs_telemetry_gameplay_event_t *info );

        TripLogger &m_logger;
        FuelCosts &m_brandstof;
        Trip m_huidigeRit;
        bool m_actief = false;
        std::string m_huidigeLadingId;  // internal cargo.id of the active trip, to recognise repeated config updates
        std::uint32_t m_economyTijd = 0;

        // Measurement points for the time scale. game.time only refreshes
        // once per game minute, so we measure over a longer window and
        // average out the jerkiness.
        std::uint32_t m_schaalEersteEconomy = 0;
        std::chrono::steady_clock::time_point m_schaalEersteEcht{};
        std::chrono::steady_clock::time_point m_schaalLaatsteEcht{};
        bool m_schaalGestart = false;

        // Smoothed estimate, so the number does not bounce around at every
        // traffic light. -1 = no value yet.
        mutable double m_gladdeSchattingMin = -1.0;

        // The very first usable estimate of this trip, in real minutes. Only
        // for the ETA check in debug.log; at completion it is placed next to
        // the actual duration and then set back to -1.
        mutable double m_eersteSchattingMinuten = -1.0;

        // Once the scale is reliably measured we LOCK it for the rest of the
        // session. After that no lag spike or time jump can make the arrival
        // time jump. At the next startup it is measured again, so a
        // TruckersMP change is picked up by itself -- just not mid-trip.
        mutable double m_vastgezetteSchaal = 0.0;

    public:
        // Manual override of the time scale, for when the measurement ever
        // does something odd or you know the right value yourself.
        // 0 = off (then it measures itself). Stored in uiterlijk.json.
        //
        // Known values: TruckersMP = 6, singleplayer = about 19.
        void ZetHandmatigeSchaal( double schaal ) { m_handmatigeSchaal = schaal; }
        double HandmatigeSchaal() const { return m_handmatigeSchaal; }

    private:
        double m_handmatigeSchaal = 0.0;
        double m_tankInhoudLiters = 0.0;

        // For the real-time estimate: when the trip started, and when we
        // last measured speed (to keep km "live" via speed x elapsed time --
        // an estimate, because the telemetry gives no live distance-driven
        // channel; only at completion do we correct this with the real
        // "distance.km" from the gameplay event).
        std::chrono::steady_clock::time_point m_ritStartMoment;
        std::chrono::steady_clock::time_point m_laatsteSnelheidMeting;

        // To also supply the bus line tracking with live speed + pause state
        // (the "truck.speed" channel is not job-type specific -- no need to
        // register it twice, just forward it). See ZetBusTracking() and
        // OpLiveSnelheid() in BusTracking.
        BusTracking *m_busTracking = nullptr;
    public:
        void ZetBusTracking( BusTracking *bus ) { m_busTracking = bus; }

        // For the incident recorder: on a sudden damage jump we ask
        // PlayersNearby who was nearest, and report that to the recorder so
        // it freezes the buffer.
        void ZetIncidentKoppeling( PlayersNearby *spelers, IncidentRecorder *recorder )
        {
            m_spelersVoorIncident = spelers;
            m_incidentRecorder = recorder;
        }
    private:
        PlayersNearby *m_spelersVoorIncident = nullptr;

        // When did we last report damage to the recorder? Without this
        // lockout a long scrape would make dozens of recordings in a row, and
        // each new one overwrites the previous.
        std::chrono::steady_clock::time_point m_laatsteSchademelding{};

        // Has damage been measured once already? The first reading is the
        // state at load, not a collision.
        bool m_schadeGemeten = false;
        IncidentRecorder *m_incidentRecorder = nullptr;
        double m_vorigeSchadePercentage = 0.0;
        double m_minutenTotRust = -1.0;  // -1 = channel not received (yet)
        double m_rustPeriodeMax = 0.0;  // highest seen, = length of a full period

        double m_liveSnelheidKmh = 0.0;
        double m_navigatieTijd = -1.0;  // raw value from truck.navigation.time
        double m_navigatieAfstandMeter = -1.0;  // -1 = not received yet / not available

        // New channel values. -1.0 everywhere means "never received", so the
        // overlay sees the difference between "0" and "unknown".
        double m_bereikKm = -1.0;
        double m_verbruikLiterPerKm = -1.0;  // raw channel is l/km, not l/100km
        double m_kilometerstandKm = -1.0;
        double m_snelheidslimietMs = -1.0;  // raw channel is m/s
        double m_cruiseControlMs = 0.0;  // 0 = disabled
        double m_schadeMotor = -1.0;  // stored as percentage 0-100
        double m_schadeBak = -1.0;
        double m_schadeCabine = -1.0;
        double m_schadeWielen = -1.0;
        double m_aanhangerSchade = -1.0;
        double m_ladingSchade = -1.0;
        double m_ladingGewichtKg = -1.0;
        bool m_heeftAanhanger = false;

    public:
        // Tachograph: driving time since the last rest, and whether you
        // currently count as "resting" (standing still longer than a short
        // threshold, so traffic lights do not count as rest). EU rule: max
        // 4.5 hours driving, then a mandatory 45 min rest -- purely
        // informative, no enforcement.
        double TachograafRijtijdMinuten() const;

        // --- Rest time according to the GAME itself ----------------------
        // The SCS channel "game.next.rest.stop" gives how many GAME MINUTES
        // you may still drive before rest is mandatory. That is the real
        // source; our own summed driving time is no more than an
        // approximation.
        //
        // Note: on many TruckersMP servers fatigue is off. Then this channel
        // stays at a fixed value or never arrives -- hence -1.0 as "not
        // available", so the overlay can fall back on our own counter instead
        // of showing something meaningless.
        double MinutenTotRustSpel() const { return m_minutenTotRust; }

        // Diagnostics: what DOES the channel do on your server? Since you
        // cannot rest in multiplayer (sleeping skips no time there), we do
        // not know whether this counter ever resets. These values make that
        // visible instead of us continuing to speculate.
        double LaagsteRustWaarde() const { return m_rustLaagst; }
        int RustResetsGezien() const { return m_rustResets; }

        // 0 = registration of the rest channel FAILED (channel does not exist
        // or has a different type), 1 = s32, 2 = u32, 3 = float. That way the
        // HUD shows right away whether it is us or the game.
        int RustKanaalType() const { return m_rustKanaalType; }
        double EigenRijSpelMin() const { return m_pauzeRijSpelMin; }

        // Highest value seen since the last rest. Serves as the scale for the
        // bar: it tells us how long a full period is, without hard-coding 11
        // hours (that differs per game version and per mod).
        double VolledigeRustperiodeMinuten() const { return m_rustPeriodeMax; }
        bool TachograafInRust() const { return m_tachoInRust; }

        // --- Mandatory break (ETS2 1.60 "Mandatory Break") ---------------
        //
        // IMPORTANT: the telemetry does NOT give this value. The channel
        // game.next.rest.stop belongs to the other counter -- the "Rest
        // State", the long nine-hour rest. For the mandatory break there is
        // (as yet) no channel; that was reported by other dashboard projects
        // too, with the answer that there is no workaround for now.
        //
        // So we count it ourselves, by the rule ETS2 ACTUALLY applies. SCS
        // writes it in the 1.60 announcement: you may drive up to 10 hours
        // before a mandatory break, and that break requires 9 consecutive
        // hours of rest. Both in GAME time.
        //
        // This used to say 4h30 driving and 45 minutes break. That is the
        // real European driving-time law, but NOT what the game does -- the
        // counter filled more than twice as fast as the P counter in your
        // Route Advisor. Now they run in step.
        //
        // Returns the remaining GAME minutes until you must take a break.
        // Negative means you are already over.
        double MinutenTotVerplichtePauze() const;

        // Length of a full driving period in game minutes -- derived from
        // what the game passes, with 10 hours as fallback. Needed to scale the
        // bar without assuming a number.
        double RijPeriodeSpelMinuten() const;

        // How long you have been standing still consecutively, in game minutes.
        double PauzeMinutenGemaakt() const { return m_pauzeStilstandSpelMin; }

        // 11 hours, not 10. SCS says 10 in the 1.60 announcement, but on
        // TruckersMP the P counter visibly shows 11 hours after a rest --
        // checked in the game itself. This is only the FALLBACK: as soon as
        // the rest channel reports a higher value, the plugin uses that.
        // --- Adjustable tachograph ---------------------------------------
        //
        // Three modes. Mode 1 stays exactly what it was; that code touches
        // nothing below.
        enum class TachoStand
        {
            SpelVolgen = 0,  // 11 hours, matching the game's P counter
            EigenRegels = 1,  // self-set times
            ATW = 2  // preset with the real driving-time law
        };

        struct TachoInstelling
        {
            TachoStand stand = TachoStand::SpelVolgen;

            // Everything in GAME minutes, because the rest reckons in those too.
            double maxAaneengeslotenRijden = 4.5 * 60.0;  // 4h30
            double pauzeDuur = 45.0;  // 45 min
            double maxDagRijden = 9.0 * 60.0;  // 9 hours
            double dagRust = 11.0 * 60.0;  // 11 hours
        };

        void ZetTachoInstelling( const TachoInstelling &nieuw );
        TachoInstelling HuidigeTachoInstelling() const { return m_tacho; }

        // Remaining GAME minutes to the next BREAK (modes 2 and 3).
        // -1 = not applicable in this mode.
        double MinutenTotPauzeEigen() const;

        // Remaining GAME minutes of your DAILY driving time (modes 2 and 3).
        double MinutenDagrijtijdOver() const;

        static constexpr double MAX_RIJ_SPELMINUTEN = 11.0 * 60.0;
        static constexpr double PAUZE_SPELMINUTEN = 9.0 * 60.0;  // 9 hours rest
        // SCS warns two hours ahead; so do we.
        static constexpr double WAARSCHUW_SPELMINUTEN = 2.0 * 60.0;

    private:
        void TachograafUpdate( double snelheidKmh );

        double m_tachoRijSecondenSindsRust = 0.0;
        double m_tachoStilstandSeconden = 0.0;

        // Mandatory-break counter, in GAME minutes (not in real seconds --
        // that was the bug in the old counter: 1 real minute standing still
        // is only 6 game minutes, and that already zeroed the driving time).
        // Game clock reading at the moment of the last rest. The remaining
        // time is simply: period - (now - that moment). No running sum that
        // can drift, and automatically in proportion to the server clock.
        std::uint32_t m_economyTijdLaatsteRust = 0;
        bool m_rustMomentBekend = false;

        // Ferries and trains also make the game clock jump ahead. We already
        // catch those events for the expenses, so here we set a flag so the
        // NEXT time jump does not count as rest.
        bool m_negeerVolgendeSprong = false;

        // Last shown value. A countdown should only go down; if the server
        // clock jumps back a minute (sync, ping wobble) the number would
        // otherwise visibly bob up. We hold it until it really comes out
        // lower again.
        mutable double m_getoondeRestMin = -1.0;

        // Same brake for the driving-time counter. That counts UP instead of
        // down, so there the number may only go up -- otherwise the same
        // logic.
        mutable double m_getoondeRijtijdMin = -1.0;

        // --- Remembering between sessions ---------------------------------
        // ETS2 stores your driving time in your profile: quit the game and at
        // startup you are still at the same P time. Without this our counter
        // started at eleven hours every time, and so ran wrong from the first
        // moment.
        //
        // We store the game minute of your last rest in
        // %APPDATA%\\CabNavi\\tachograaf.json. Nothing more is needed: the
        // rest is a subtraction from the current clock.
        void LaadTachoStand();
        void BewaarTachoStand() const;

        // We store the REMAINING TIME, not the moment of your last rest.
        //
        // That moment seemed logical, but does not work on TruckersMP: the
        // server clock keeps running while you are offline. Quit with 9 hours
        // left and come back a week later, and that clock is thousands of
        // game minutes further and the sum would say you should have paused
        // long ago. While your in-game P counter still simply says 9 hours.
        //
        // By storing the remaining time and re-anchoring it to the clock at
        // startup, you just pick up the thread where you left it.
        mutable double m_laatstBewaardeRest = -1.0;

        // Read value that still needs anchoring once the clock arrives.
        double m_teHerstellenRest = -1.0;

        double m_pauzeRijSpelMin = 0.0;
        double m_pauzeStilstandSpelMin = 0.0;
        double m_laatsteRustSpelMinuten = 0.0;  // how long the last rest lasted

        // Own tachograph (modes 2 and 3). Two separate counters: since the
        // last BREAK and since the last DAILY REST. Both in game minutes, both
        // derived from the game clock -- like mode 1.
        TachoInstelling m_tacho;
        std::uint32_t m_eigenLaatstePauze = 0;
        std::uint32_t m_eigenLaatsteDagrust = 0;
        bool m_eigenGestart = false;
        double m_rustLaagst = -1.0;  // lowest value ever seen
        int m_rustResets = 0;  // how often the counter jumped up
        int m_rustKanaalType = 0;  // which type registered; 0 = failed
        bool m_tachoInRust = false;
        std::chrono::steady_clock::time_point m_tachoLaatsteMeting;
        bool m_tachoGeinitialiseerd = false;

    public:
        // Reset the counter of the CURRENT vehicle to zero. Meant to be
        // pressed together with the reset on the truck dashboard: then the
        // two count from the same moment and run in step.
        void ResetVoertuigTeller();

        // Name of the current vehicle for display ("Scania Streamline"),
        // empty if not recognised yet.
        std::string HuidigVoertuigNaam() const;

        // Is the simulation paused right now? The game sends this itself via
        // SCS_TELEMETRY_EVENT_paused, and that happens as soon as you are in
        // a menu or a garage screen. There is no menu query in the TruckersMP
        // SDK, so this is the closest signal we have to keep the overlay and
        // the mouse out of the way then.
        bool IsGepauzeerd() const { return m_gepauzeerd; }

    private:
        // ---- Per-vehicle counter -------------------------------------------
        // The truck's dashboard counts litres and kilometres since the last
        // reset and then simply keeps going -- never per trip. The trip
        // counter above does, and that is exactly why "gem" and the dashboard
        // did not run in step. This counter does what the dashboard does: per
        // vehicle, cumulative, only zeroed by a reset the user gives himself.
        // Stored in voertuigen.txt.
        // ---- Driving style -------------------------------------------------
        // Four COUNTS, no formula: seconds off the throttle while driving,
        // seconds full throttle, seconds standing still with the engine
        // running, and the number of hard braking events. What a real truck
        // measures too (Scania Driver Support, Volvo Dynafleet), but without
        // turning it into a weighted score -- that weight would be invented.
        struct RijstijlTelling
        {
            double rijdendSec = 0.0;  // above 10 km/h
            double uitrolSec = 0.0;  // driving and throttle below 10%
            double volgasSec = 0.0;  // throttle above 90%
            double stationairSec = 0.0;  // still, engine running (fuel is going down)
            double totaalSec = 0.0;
            double km = 0.0;
            double geladenSec = 0.0;  // how much of totaalSec with trailer
            int remmingen = 0;  // more than 8 km/h lost in one second
        };

        struct VoertuigTeller
        {
            std::string merk;  // "Scania", from the truck configuration
            std::string model;  // "Streamline"
            double kmStand = 0.0;  // last known odometer; distinguishes
                                  // two identical trucks, because those never cross
            double liters = 0.0;
            double km = 0.0;

            // Reference for the driving style, PER SITUATION: you drive empty
            // differently than with a trailer, so the two are kept separately.
            RijstijlTelling leeg;
            RijstijlTelling geladen;
        };

        // The window: the last ten kilometres, or at most half an hour when
        // standing still. Every reading is a row; rows drop off the front as
        // soon as the sum exceeds it.
        // One row per SECOND, not per reading. Readings arrive sixty times a
        // second; storing per reading gave tens of thousands of rows in the
        // window at low speed, and "8 km/h loss per reading" was never
        // reached -- so braking events stayed at zero.
        struct RijstijlMeting
        {
            double dKm = 0.0, dSec = 0.0;
            double rijdendSec = 0.0, uitrolSec = 0.0, volgasSec = 0.0, stationairSec = 0.0, geladenSec = 0.0;
            int remming = 0;
        };
        std::deque<RijstijlMeting> m_rijstijlVenster;
        RijstijlTelling m_rijstijlVensterSom;  // running sum of the window
        RijstijlMeting m_rijstijlPending;  // what this second already holds
        double m_vorigeSnelheidVoorRem = -1.0;  // speed at the START of the previous second
        // The window is the last THREE MINUTES OF DRIVING, like the running
        // meter of Scania Driver Support and Volvo Dynafleet. Not kilometres:
        // MEASURED 04-09, the odometer counts map kilometres and on
        // TruckersMP "10 km" is then 36 seconds. And not clock time: then
        // standing at a stop fills the window and the driving is cut off the
        // front. Standing still does stay in (for "Stationair"), up to half
        // an hour.
        static constexpr double RIJSTIJL_VENSTER_RIJDEND_SEC = 180.0;
        static constexpr double RIJSTIJL_VENSTER_SEC = 1800.0;
        static constexpr double RIJSTIJL_REFERENTIE_SEC = 600.0;  // ten minutes of driving before comparing
        void RijstijlTellen( double snelheidKmh, double gas, double dKm, double dSec );
        static void TelOp( RijstijlTelling &t, const RijstijlMeting &m, int teken );

    public:
        // What will appear at the bottom: a word and a short reason.
        struct RijstijlStatus
        {
            // Layer 2: the assessment of the last three minutes against your
            // normal in this truck. Unknown as long as there is no reference --
            // then only the direct meter is shown.
            enum Stand { Onbekend, Zuinig, Gewoon, Sportief, Stationair } stand = Onbekend;
            bool geladen = false;  // which reference was used

            // Layer 1: the direct meter, what your foot does NOW. No reference
            // needed, shown from the first second. The vacuum gauge of old:
            // throttle closed is green, throttle open is orange.
            enum Nu { Niets, Uitrollen, ZuinigNu, Normaal, Trekken, StilMotorAan } nu = Niets;
        };
        RijstijlStatus HuidigeRijstijl() const;
    private:
        std::vector<VoertuigTeller> m_voertuigen;
        int m_huidigVoertuig = -1;  // index in m_voertuigen, -1 = not recognised yet
        std::string m_configMerk, m_configModel;  // from the latest truck configuration
        bool m_voertuigenGeladen = false;
        bool m_kmStandVersNaConfig = false;  // has a DIFFERENT odometer been seen since the last truck config?
        double m_kmStandBijConfig = -1.0;  // the reading at the moment of that config (still the previous truck's)
        std::chrono::steady_clock::time_point m_configMoment{};
        bool m_voertuigenGewijzigd = false;
        std::chrono::steady_clock::time_point m_voertuigenLaatstBewaard{};
        void LaadVoertuigen();
        void BewaarVoertuigen( bool forceer );
        void IdentificeerVoertuig();

        // ---- Trip counter from the save ------------------------------------
        // At load (and when reloading an autosave) the live odometer is
        // exactly equal to the one in the save. At that moment we let a
        // background thread read the save and fetch the dashboard trip
        // counter. When it arrives, the vehicle's counter is set to it, plus
        // what has been counted since the read started. Then "gem" runs
        // exactly in step with the dashboard, without a reset button.
        void StartSaveLezen( double kmStandBijLaden );
        void VerwerkSaveResultaat();  // on the game thread, every reading
        std::thread m_saveThread;
        std::mutex m_saveMutex;
        bool m_saveBezig = false;
        bool m_saveKlaar = false;
        std::optional<SaveTripteller> m_saveResultaat;
        std::string m_saveFout;
        double m_saveLitersBijStart = 0.0;  // vehicle counter at the moment of starting
        double m_saveKmBijStart = 0.0;
        int m_saveVoertuig = -1;  // which vehicle this read was for
        double m_saveKmStandGevraagd = 0.0;  // the reading searched for (for the retry)
        int m_saveHerkansingen = 0;  // once more if the save was half written
        std::chrono::steady_clock::time_point m_saveHerkansingMoment{};
        // Odometer jumping back: only read after two seconds. If a truck
        // configuration arrives in that time it was a SWITCH and not a
        // reload, and then the normal recognition reads the save for the new
        // truck. That way truck B's trip can never end up in truck A's counter.
        double m_herlaadKmStand = -1.0;
        std::chrono::steady_clock::time_point m_herlaadMoment{};

        // Pause tracking: counts how much time has been paused during this
        // trip, so we can subtract it from the "elapsed time".
        bool m_gepauzeerd = false;
        std::chrono::steady_clock::time_point m_pauzeStartMoment;
        double m_totaalGepauzeerdSeconden = 0.0;

        // Moving average of the last ~3 minutes (timestamp, cumulative km
        // driven at that moment) -- reacts faster to "motorway now, city now"
        // than the average over the whole trip, just as Trucky recomputes
        // every second instead of using one fixed average.
        std::deque<std::pair<std::chrono::steady_clock::time_point, double>> m_kmVenster;

        // Same principle, now for consumption. The distance does NOT come
        // from the odometer: that has too little precision for a window of a
        // few seconds, and a coarse denominator makes l/100km both too high
        // and jumpy. Instead we sum speed x time -- the same way the trip
        // distance is already tracked.
        struct VerbruikMeting
        {
            std::chrono::steady_clock::time_point moment;
            double verbruiktLiters = 0.0;
            double gemetenKm = 0.0;  // summed from speed x time
        };
        std::deque<VerbruikMeting> m_brandstofVenster;

        // Only what you consume and drive WHILE DRIVING counts in the
        // average. Idling burns litres but yields no kilometres; counting it
        // makes the average climb endlessly as soon as you stand still for a
        // bit. Idle consumption is shown separately, in l/h -- like the board
        // computer in the game.
        double m_rijdendLiters = 0.0;
        double m_rijdendKm = 0.0;
        double m_meetKmTotaal = 0.0;  // running counter for the window
        double m_vorigMeetLiters = -1.0;  // -1 = no reading done yet
        double m_vorigMeetOdometerKm = -1.0;  // odometer at the previous reading

        // How many kilometres the ODOMETER advances per "speedometer
        // kilometre". MEASURED 30-08: about 18.6. The ETS2 world is scaled
        // down, and the odometer counts real kilometres while the speedometer
        // belongs to that scaled world. We measure this while driving instead
        // of typing a number, because it differs per game mode.
        //
        // Needed for the l/h figure: that was a factor 2 too low because it
        // was divided by the TIME scale instead of by this.
        double m_afstandsFactor = 18.6;
        // What was last written, so we do not go to disk on every reading.
        // See MetingPad() in TruckTracking.cxx.
        double m_bewaardeFactor = 18.6;
        void LaadMeting();
        static constexpr double AFSTANDSFACTOR_MIN = 1.0;
        static constexpr double AFSTANDSFACTOR_MAX = 40.0;

        // Throttle 0..1, and whether it was pressed at the previous reading.
        // As soon as that flips (throttle on or off) everything in the
        // measurement window is stale: your consumption changes at that very
        // moment, but the measurement looks back in time. We then empty the
        // window, so the figure shows the NEW driving behaviour within a
        // second instead of the old.
        double m_gaspedaal = 0.0;
        bool m_gasIngedrukt = false;
        bool m_gasOmslag = false;  // set by the callback, processed by the measurement
        static constexpr double GAS_DREMPEL = 0.05;
        std::chrono::steady_clock::time_point m_vorigMeetMoment;
        bool m_meetGestart = false;
        static constexpr double RIJDT_DREMPEL_KMH = 3.0;

        // Below this speed we show l/h instead of km/l. Practically: only
        // when you REALLY stand still. In km/l crawling is just a low,
        // readable figure (0.1 km/l) -- unlike l/100km, where it showed 1000
        // and the threshold therefore had to be high. The dashboard in the
        // truck also only switches at standstill.
        static constexpr double PER100_MIN_KMH = 0.1;


        // Damped (EMA) results. The window below is short enough to react
        // immediately, but therefore also restless; this damping makes it a
        // readable figure. On a BIG jump -- throttle off, braking, standstill
        // -- it switches over directly instead of slowly damping out, exactly
        // as Gladstrijken() does for the arrival time. Without that the
        // figure would stay at your driving consumption for seconds after
        // braking.
        double m_gladLiterPerUur = -1.0;  // per REAL hour; conversion happens at display
        double m_gladVerbruikNu = -1.0;  // l/100km
        static double Demp( double huidig, double nieuw, double dtSec, double tauSec );

        // Roughly how long a change takes to work through. Together with the
        // window above this determines how fast the figure catches up. Four
        // seconds of window plus four seconds of damping is about eight
        // counts -- enough to average out hills, short enough that your
        // acceleration value does not stick.
        static constexpr double DEMP_TAU_SECONDEN = 0.5;

        // Separate, SHORT window for the l/h figure. That needs no distance,
        // so it does not have to follow the long window of l/100km. With the
        // long window, 15 seconds of driving data kept counting after
        // stopping; it showed "94.1 l/h" while the engine was OFF (measured
        // 30-08).
        static constexpr double LUUR_VENSTER_SECONDEN = 0.8;

        // And faster damping with it: switch the engine off and the figure
        // must be at zero within a few counts, not creep towards it slowly.
        static constexpr double LUUR_TAU_SECONDEN = 0.3;

        // Conversion from litres per REAL hour to the figure the truck's
        // dashboard shows. MEASURED 01-09-2026 in three situations (see the
        // explanation in HuidigeVoertuigStatus):
        //   Scania V8, idle                 6.0 -> 2.0
        //   DAF MX-11 370, idle             4.0 -> 1.3
        //   DAF, full throttle in neutral  25.3 -> 8.4
        // All three give divisor 3. This is a game constant and applies to
        // every truck; the CONSUMPTION differs per engine, the conversion
        // does not.
        static constexpr double LUUR_DELER = 3.0;

        // Was it driving at the previous reading? At the transition driving
        // <-> standstill the damping is skipped, so the figure flips right
        // away instead of taking seconds.
        bool m_vorigRijdt = false;

        // Throttle for the consumption line in debug.log.
        std::chrono::steady_clock::time_point m_laatsteVerbruikLog;

    public:
        // How often the consumption line may appear in debug.log, in
        // seconds. Default three; set it to 0.5 temporarily if you want to
        // lay a screen recording next to the log, because a gear change
        // takes less than three seconds and otherwise falls between two
        // lines.
        static double &VerbruikLogInterval()
        {
            static double seconden = 3.0;
            return seconden;
        }

    private:

        // Own window time for consumption. SHORT is fine: since version 128
        // the distance comes from speed x time and so is perfectly smooth --
        // the long window was only there for the coarse odometer, which we no
        // longer use. And the fuel channel itself runs neatly: at idle the
        // tank drops exactly 0.0051 litres every 3 seconds (measured 30-08).
        // A long window mainly kept your acceleration value in view for
        // seconds. The damping does the smoothing.
        // DELIBERATELY its own constant: VENSTER_SECONDEN belongs to
        // m_kmVenster and thus to the IRL arrival time -- we stay away from
        // that.
        static constexpr double VERBRUIK_VENSTER_SECONDEN = 1.0;

        // Minimum time span for a usable reading. Below this the litre
        // difference is so small you mainly measure the game's rounding
        // steps. 0.3 seconds is about the floor: at idle you still burn
        // 0.0005 litres then, and that is just enough above the precision of
        // the fuel channel.
        static constexpr double MIN_SPAN_SECONDEN = 0.3;

        static constexpr double VENSTER_SECONDEN = 180.0;
    };
}

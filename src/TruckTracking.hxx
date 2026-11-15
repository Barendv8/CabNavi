#pragma once

#include "SpelTeller.hxx"
#include "RitBoek.hxx"
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
    class BusTracking;
    class PlayersNearby;
    class IncidentRecorder;

    class TruckTracking
    {
    public:
        TruckTracking( TripLogger &logger, FuelCosts &brandstof );

        ~TruckTracking();

        void RegistreerBijTelemetrie( const scs_telemetry_init_params_v101_t *params );

        std::uint32_t EconomyTijd() const { return m_economyTijd; }

        double TijdSchaal() const;

        double RuweNavigatieTijd() const { return m_navigatieTijd; }
        bool SchaalStaatVast() const { return m_vastgezetteSchaal > 0.0; }
        double RuweNavigatieAfstandKm() const
        {
            return m_navigatieAfstandMeter >= 0.0 ? m_navigatieAfstandMeter / 1000.0 : -1.0;
        }

    private:
        double Gladstrijken( double ruweMinuten ) const;

    public:
        bool HeeftActieveRit() const { return m_actief; }
        std::string Kenteken() const { return m_kenteken; }
        std::string KentekenLand() const { return m_kentekenLand; }
        const Trip &HuidigeRit() const { return m_huidigeRit; }

        double VerstrekenMinutenEcht() const;

        double LiveSnelheidKmh() const { return m_liveSnelheidKmh; }
        double GeschatteResterendeMinutenEcht() const;

        struct VoertuigStatus
        {
            double bereikKm = -1.0;
            double verbruikLiterPer100Km = -1.0;
            double verbruikGemiddeldLiterPer100Km = -1.0;
            double verbruikNuLiterPer100Km = -1.0;
            double verbruikLiterPerUur = -1.0;
            double verbruikRitLiterPer100Km = -1.0;
            double ritLiters = 0.0, ritKm = 0.0;
            double voertuigLiters = 0.0, voertuigKm = 0.0;
            bool staatStil = false;
            bool echtStil = false;
            double kilometerstandKm = -1.0;
            double snelheidslimietKmh = -1.0;
            double cruiseControlKmh = 0.0;
            double schadeMotor = -1.0;
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

        bool RijdtTeHard() const;

    private:
        static SCSAPI_VOID LandCallback( const scs_string_t name, scs_u32_t index,
                                          const scs_value_t *value, scs_context_t context );

        static SCSAPI_VOID NavigatieTijdCallback( const scs_string_t name, scs_u32_t index,
                                                   const scs_value_t *value, scs_context_t context );

        static SCSAPI_VOID RustStopCallback( const scs_string_t name, scs_u32_t index,
                                              const scs_value_t *value, scs_context_t context );

        static SCSAPI_VOID GameTimeCallback( const scs_string_t name, scs_u32_t index,
                                              const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID BusTevredenheidCallback( const scs_string_t name, scs_u32_t index,
                                                     const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID MeekijkRustCallback( const scs_string_t name, scs_u32_t index,
                                                 const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID MeekijkPauzeCallback( const scs_string_t name, scs_u32_t index,
                                                  const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID MeekijkTijdVerschilCallback( const scs_string_t name, scs_u32_t index,
                                                         const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID SnelheidCallback( const scs_string_t name, scs_u32_t index,
                                              const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID BrandstofLitersCallback( const scs_string_t name, scs_u32_t index,
                                                      const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID SchadeCallback( const scs_string_t name, scs_u32_t index,
                                            const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID NavigatieAfstandCallback( const scs_string_t name, scs_u32_t index,
                                                       const scs_value_t *value, scs_context_t context );
        static SCSAPI_VOID LocalScaleCallback( const scs_string_t name, scs_u32_t index,
                                                 const scs_value_t *value, scs_context_t context );

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
        std::string m_huidigeLadingId;
        std::uint32_t m_economyTijd = 0;

        std::uint32_t m_schaalEersteEconomy = 0;
        std::chrono::steady_clock::time_point m_schaalEersteEcht{};
        std::chrono::steady_clock::time_point m_schaalLaatsteEcht{};
        bool m_schaalGestart = false;

        mutable double m_gladdeSchattingMin = -1.0;

        mutable double m_eersteSchattingMinuten = -1.0;

        mutable double m_vastgezetteSchaal = 0.0;

    public:
        void ZetHandmatigeSchaal( double schaal ) { m_handmatigeSchaal = schaal; }
        double HandmatigeSchaal() const { return m_handmatigeSchaal; }

    private:
        double m_handmatigeSchaal = 0.0;
        double m_tankInhoudLiters = 0.0;

        std::chrono::steady_clock::time_point m_ritStartMoment;
        std::chrono::steady_clock::time_point m_laatsteSnelheidMeting;

        BusTracking *m_busTracking = nullptr;

        std::uint32_t m_ritUitersteTijd = 0;

        std::string m_ritSleutel;
        bool m_hervatWacht = false;
        LopendeRit m_hervatKandidaat;
        double m_hervatWachtSec = 0.0;
        std::chrono::steady_clock::time_point m_boekLaatsteTik{};
        std::chrono::steady_clock::time_point m_boekLaatstBewaard{};
        std::vector<RitMoment> m_boekMomenten;
        int m_boekMomentVoertuig = -1;
        void BoekBewaar( bool forceer );
        void BoekHervatBeslis( bool nuBeslissen );
        void BoekTik();
        void BoekMoment( bool nu );
        RitMoment BoekStandNu() const;
        static constexpr double HERVAT_WACHT_SECONDEN = 20.0;
    public:
        void ZetBusTracking( BusTracking *bus );

        void ZetIncidentKoppeling( PlayersNearby *spelers, IncidentRecorder *recorder )
        {
            m_spelersVoorIncident = spelers;
            m_incidentRecorder = recorder;
        }
    private:
        PlayersNearby *m_spelersVoorIncident = nullptr;

        std::chrono::steady_clock::time_point m_laatsteSchademelding{};

        bool m_schadeGemeten = false;
        IncidentRecorder *m_incidentRecorder = nullptr;
        double m_vorigeSchadePercentage = 0.0;
        double m_minutenTotRust = -1.0;
        double m_rustPeriodeMax = 0.0;

        double m_liveSnelheidKmh = 0.0;
        double m_navigatieTijd = -1.0;
        double m_navigatieAfstandMeter = -1.0;

        double m_bereikKm = -1.0;
        double m_verbruikLiterPerKm = -1.0;
        double m_kilometerstandKm = -1.0;
        double m_snelheidslimietMs = -1.0;
        double m_cruiseControlMs = 0.0;
        double m_schadeMotor = -1.0;
        double m_schadeBak = -1.0;
        double m_schadeCabine = -1.0;
        double m_schadeWielen = -1.0;
        double m_aanhangerSchade = -1.0;
        double m_ladingSchade = -1.0;
        double m_ladingGewichtKg = -1.0;
        bool m_heeftAanhanger = false;

    public:
        double TachograafRijtijdMinuten() const;

        double MinutenTotRustSpel() const { return m_minutenTotRust; }

        double LaagsteRustWaarde() const { return m_rustLaagst; }
        int RustResetsGezien() const { return m_rustResets; }

        int RustKanaalType() const { return m_rustKanaalType; }
        double EigenRijSpelMin() const { return m_pauzeRijSpelMin; }

        double VolledigeRustperiodeMinuten() const { return m_rustPeriodeMax; }
        bool TachograafInRust() const { return m_tachoInRust; }

        double MinutenTotVerplichtePauze() const;

        double RijPeriodeSpelMinuten() const;

        double PauzeMinutenGemaakt() const { return m_pauzeStilstandSpelMin; }

        enum class TachoStand
        {
            SpelVolgen = 0,
            EigenRegels = 1,
            ATW = 2
        };

        struct TachoInstelling
        {
            TachoStand stand = TachoStand::SpelVolgen;

            double maxAaneengeslotenRijden = 4.5 * 60.0;
            double pauzeDuur = 45.0;
            double maxDagRijden = 9.0 * 60.0;
            double dagRust = 11.0 * 60.0;
        };

        void ZetTachoInstelling( const TachoInstelling &nieuw );
        TachoInstelling HuidigeTachoInstelling() const { return m_tacho; }

        double MinutenTotPauzeEigen() const;

        double MinutenDagrijtijdOver() const;

        struct SpelTeller
        {
            bool loopt = false;
            int minuten = 0;
        };
        SpelTeller SpelTellerPauze() const;
        SpelTeller SpelTellerRust() const;

        static constexpr double MAX_RIJ_SPELMINUTEN = 11.0 * 60.0;
        static constexpr double PAUZE_SPELMINUTEN = 9.0 * 60.0;
        static constexpr double WAARSCHUW_SPELMINUTEN = 2.0 * 60.0;

    private:
        void TachograafUpdate( double snelheidKmh );

        double m_tachoRijSecondenSindsRust = 0.0;
        double m_tachoStilstandSeconden = 0.0;

        std::uint32_t m_economyTijdLaatsteRust = 0;
        bool m_rustMomentBekend = false;

        bool m_negeerVolgendeSprong = false;

        mutable double m_getoondeRestMin = -1.0;

        mutable double m_getoondeRijtijdMin = -1.0;

        void LaadTachoStand();
        void BewaarTachoStand() const;

        mutable double m_laatstBewaardeRest = -1.0;

        double m_teHerstellenRest = -1.0;

        double m_pauzeRijSpelMin = 0.0;
        double m_pauzeStilstandSpelMin = 0.0;
        double m_laatsteRustSpelMinuten = 0.0;

        TachoInstelling m_tacho;
        std::uint32_t m_eigenLaatstePauze = 0;
        std::uint32_t m_eigenLaatsteDagrust = 0;
        bool m_eigenGestart = false;
        double m_rustLaagst = -1.0;
        int m_rustResets = 0;
        int m_rustKanaalType = 0;

        void MeekijkLog();
        int m_meekijkRust = 0, m_meekijkPauze = 0, m_meekijkTijdVerschil = 0;
        bool m_meekijkRustGezien = false, m_meekijkPauzeGezien = false, m_meekijkTijdVerschilGezien = false;
        std::chrono::steady_clock::time_point m_meekijkLaatsteLog{};
        std::uint32_t m_meekijkSpelTijdVorige = 0;
        void MeekijkSprong( const char *naam, int oud, int nieuw );
        SpelTellerVolger m_volgRust, m_volgPauze;
        bool m_volgRustGemeld = false, m_volgPauzeGemeld = false;
        void SpelTellerMeld();
        bool m_tachoInRust = false;
        std::chrono::steady_clock::time_point m_tachoLaatsteMeting;
        bool m_tachoGeinitialiseerd = false;

    public:
        void ResetVoertuigTeller();

        std::string HuidigVoertuigNaam() const;

        bool IsGepauzeerd() const { return m_gepauzeerd; }

    private:
        struct RijstijlTelling
        {
            double rijdendSec = 0.0;
            double uitrolSec = 0.0;
            double volgasSec = 0.0;
            double stationairSec = 0.0;
            double totaalSec = 0.0;
            double km = 0.0;
            double geladenSec = 0.0;
            int remmingen = 0;
        };

        struct VoertuigTeller
        {
            std::string merk;
            std::string model;
            double kmStand = 0.0;
            double liters = 0.0;
            double km = 0.0;

            RijstijlTelling leeg;
            RijstijlTelling geladen;
        };

        struct RijstijlMeting
        {
            double dKm = 0.0, dSec = 0.0;
            double rijdendSec = 0.0, uitrolSec = 0.0, volgasSec = 0.0, stationairSec = 0.0, geladenSec = 0.0;
            int remming = 0;
        };
        std::deque<RijstijlMeting> m_rijstijlVenster;
        RijstijlTelling m_rijstijlVensterSom;
        RijstijlMeting m_rijstijlPending;
        double m_vorigeSnelheidVoorRem = -1.0;
        static constexpr double RIJSTIJL_VENSTER_RIJDEND_SEC = 180.0;
        static constexpr double RIJSTIJL_VENSTER_SEC = 1800.0;
        static constexpr double RIJSTIJL_REFERENTIE_SEC = 600.0;
        void RijstijlTellen( double snelheidKmh, double gas, double dKm, double dSec );
        static void TelOp( RijstijlTelling &t, const RijstijlMeting &m, int teken );

    public:
        struct RijstijlStatus
        {
            enum Stand { Onbekend, Zuinig, Gewoon, Sportief, Stationair } stand = Onbekend;
            bool geladen = false;

            enum Nu { Niets, Uitrollen, ZuinigNu, Normaal, Trekken, StilMotorAan } nu = Niets;
        };
        RijstijlStatus HuidigeRijstijl() const;
    private:
        std::vector<VoertuigTeller> m_voertuigen;
        int m_huidigVoertuig = -1;
        std::string m_configMerk, m_configModel;
        std::string m_kenteken, m_kentekenLand;
        bool m_voertuigenGeladen = false;
        bool m_kmStandVersNaConfig = false;
        double m_kmStandBijConfig = -1.0;
        std::chrono::steady_clock::time_point m_configMoment{};
        bool m_voertuigenGewijzigd = false;
        std::chrono::steady_clock::time_point m_voertuigenLaatstBewaard{};
        void LaadVoertuigen();
        void BewaarVoertuigen( bool forceer );
        void IdentificeerVoertuig();

        void StartSaveLezen( double kmStandBijLaden );
        void VerwerkSaveResultaat();
        std::thread m_saveThread;
        std::mutex m_saveMutex;
        bool m_saveBezig = false;
        bool m_saveKlaar = false;
        std::optional<SaveTripteller> m_saveResultaat;
        std::string m_saveFout;
        double m_saveLitersBijStart = 0.0;
        double m_saveKmBijStart = 0.0;
        int m_saveVoertuig = -1;
        double m_saveKmStandGevraagd = 0.0;
        int m_saveHerkansingen = 0;
        std::chrono::steady_clock::time_point m_saveHerkansingMoment{};
        double m_herlaadKmStand = -1.0;
        std::chrono::steady_clock::time_point m_herlaadMoment{};

        bool m_gepauzeerd = false;
        std::chrono::steady_clock::time_point m_pauzeStartMoment;
        double m_totaalGepauzeerdSeconden = 0.0;

        std::deque<std::pair<std::chrono::steady_clock::time_point, double>> m_kmVenster;

        struct VerbruikMeting
        {
            std::chrono::steady_clock::time_point moment;
            double verbruiktLiters = 0.0;
            double gemetenKm = 0.0;
        };
        std::deque<VerbruikMeting> m_brandstofVenster;

        double m_rijdendLiters = 0.0;
        double m_rijdendKm = 0.0;
        double m_meetKmTotaal = 0.0;
        double m_vorigMeetLiters = -1.0;
        double m_vorigMeetOdometerKm = -1.0;

        double m_afstandsFactor = 18.6;
        double m_bewaardeFactor = 18.6;
        void LaadMeting();
        static constexpr double AFSTANDSFACTOR_MIN = 1.0;
        static constexpr double AFSTANDSFACTOR_MAX = 40.0;

        double m_gaspedaal = 0.0;
        bool m_gasIngedrukt = false;
        bool m_gasOmslag = false;
        static constexpr double GAS_DREMPEL = 0.05;
        std::chrono::steady_clock::time_point m_vorigMeetMoment;
        bool m_meetGestart = false;
        static constexpr double RIJDT_DREMPEL_KMH = 3.0;

        static constexpr double PER100_MIN_KMH = 0.1;

        double m_gladLiterPerUur = -1.0;
        double m_gladVerbruikNu = -1.0;
        static double Demp( double huidig, double nieuw, double dtSec, double tauSec );

        static constexpr double DEMP_TAU_SECONDEN = 0.5;

        static constexpr double LUUR_VENSTER_SECONDEN = 0.8;

        static constexpr double LUUR_TAU_SECONDEN = 0.3;

        static constexpr double LUUR_DELER = 3.0;

        bool m_vorigRijdt = false;

        std::chrono::steady_clock::time_point m_laatsteVerbruikLog;

    public:
        static double &VerbruikLogInterval()
        {
            static double seconden = 3.0;
            return seconden;
        }

    private:

        static constexpr double VERBRUIK_VENSTER_SECONDEN = 1.0;

        static constexpr double MIN_SPAN_SECONDEN = 0.3;

        static constexpr double VENSTER_SECONDEN = 180.0;
    };
}

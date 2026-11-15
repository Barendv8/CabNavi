#pragma once

#include "BusSaveLezer.hxx"
#include "RitBoek.hxx"
#include "TripLogger.hxx"
#include "TripTypes.hxx"

#include <TruckersMP/TruckersMP.hxx>
#include <TruckersMP/Bus.hxx>

#include <chrono>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <scssdk_telemetry.h>

namespace Ritten
{
    class BusTracking
    {
    public:
        BusTracking( TruckersMP::Session &session, TripLogger &logger );
        ~BusTracking();

        void ZetEconomyTijd( std::uint32_t minuten );

        void OpLiveSnelheid( double snelheidKmh, bool gepauzeerd );

        void ZetNavigatie( double navTijdRuw, double navAfstandKm );

        bool HeeftActieveRit() const { return m_actief && !ScsRitVerlopen(); }
        const Trip &HuidigeRit() const { return m_huidigeRit; }

        void KoppelBrandstof( FuelCosts *brandstof );
        void ZetVoertuig( const std::string &merk, const std::string &model );
        void ZetKilometerstand( double km, bool vers );
        void BoekBoete( const Boete &boete );
        void BoekDoorgang( const Doorgang &doorgang );

        void OpScsBusConfig( const scs_telemetry_configuration_t *cfg );
        void OpScsBusEvent( const scs_telemetry_gameplay_event_t *info );
        void ZetScsTevredenheid( double waarde );
        void ZetScsSpelTijd( std::uint32_t minuten );

        bool IsScsRit() const { return m_actief && m_scsRit; }

        double ScsVertragingMinuten() const;

        bool ScsEtappesExact() const { return m_actief && m_scsRit && m_scsEtappesExact; }

        bool ScsVertrokken() const { return !m_scsEtappes.empty() && m_scsEtappes[ 0 ].vertrekWerkelijk != 0; }
        int ScsStartInstappers() const { return m_scsEtappes.empty() ? 0 : static_cast<int>( m_scsEtappes[ 0 ].instappers ); }

        double ScsResterendeKmTotHalte( std::size_t index ) const;

        double VerstrekenMinutenEcht() const;

        double GeschatteResterendeMinutenEcht() const;

        double GeschatteMinutenTotHalte( std::size_t index ) const;

        double GeschatteVertragingMinuten() const;

        double GeschatteBoetePercentage() const;

        double TijdSchaal() const;

    private:
        void StartRecord( const TruckersMP::BusJob &job );

        FuelCosts *m_brandstof = nullptr;
        std::string m_voertuigMerk, m_voertuigModel;
        double m_kmStand = 0.0;
        bool m_kmStandVers = false;
        std::string m_boekSleutel;
        bool m_hervatWacht = false;
        LopendeRit m_hervatKandidaat;
        double m_hervatWachtSec = 0.0;
        std::chrono::steady_clock::time_point m_boekLaatsteTik{};
        std::chrono::steady_clock::time_point m_boekLaatstBewaard{};
        std::vector<RitMoment> m_boekMomenten;
        RitMoment BoekStandNu() const;
        void BoekMoment( bool nu );
        void BoekStart( const std::string &sleutel );
        void BoekSluit();
        void BoekLaatLos();
        void BoekBewaar( bool forceer );
        void BoekHervatBeslis( bool nuBeslissen );
        void BoekTik( bool gepauzeerd );
        static constexpr double HERVAT_WACHT_SECONDEN = 20.0;

        TripLogger &m_logger;
        std::unique_ptr<TruckersMP::BusModule> m_busModule;
        Trip m_huidigeRit;
        bool m_actief = false;
        std::uint32_t m_economyTijd = 0;

        std::uint32_t m_schaalEersteEconomy = 0;
        std::chrono::steady_clock::time_point m_schaalEersteEcht{};
        bool m_schaalGestart = false;
        mutable double m_vastgezetteSchaal = 0.0;
        std::chrono::steady_clock::time_point m_ritStartMoment;

        bool m_gepauzeerd = false;
        std::chrono::steady_clock::time_point m_pauzeStartMoment;
        double m_totaalGepauzeerdSeconden = 0.0;

        std::deque<std::pair<std::chrono::steady_clock::time_point, double>> m_snelheidVenster;

        double m_navTijdRuw = -1.0;
        double m_navAfstandKm = -1.0;
        mutable double m_gladdeSchattingMin = -1.0;
        double Gladstrijken( double ruweMinuten ) const;

        double EffectieveSnelheidEcht() const;
        static constexpr double VENSTER_SECONDEN = 180.0;

        double m_liveKmSindsLaatsteHalte = 0.0;
        std::chrono::steady_clock::time_point m_laatsteSnelheidMeting;

        std::chrono::steady_clock::time_point m_laatsteHalteLog{};

        static constexpr double LOG_INTERVAL_SECONDEN = 10.0;

        struct ScsEtappe
        {
            std::string bronId, bronNaam, bronType;
            std::string doelId, doelNaam, doelType;
            std::uint32_t vertrekGepland = 0, vertrekWerkelijk = 0;
            std::uint32_t aankomstGepland = 0, aankomstWerkelijk = 0;
            std::uint32_t instappers = 0, uitstappers = 0;
        };
        void ScsStartRit( const std::vector<ScsEtappe> &etappes, const std::string &sleutel );
        void ScsWerkBij( const std::vector<ScsEtappe> &etappes );
        void ScsWisRit();
        bool ScsRitVerlopen() const;

        bool m_scsRit = false;
        std::string m_scsSleutel;
        std::vector<ScsEtappe> m_scsEtappes;
        std::uint32_t m_scsSpelTijd = 0;
        double m_scsTevredenheidKanaal = -1.0;
        std::uint32_t m_scsGeplandeKm = 0;
        bool m_scsLeegGezien = false;
        std::chrono::steady_clock::time_point m_scsLeegMoment{};
        static constexpr double SCS_LEEG_WACHT_SECONDEN = 15.0;

        void ScsEtappesVerwerk();
        void ScsZetExacteEtappes( const std::vector<std::uint32_t> &km );
        void ScsEtappesOpnieuw();
        std::thread m_etappeDraad;
        std::mutex m_etappeMutex;
        bool m_etappeBezig = false;
        bool m_etappeKlaar = false;
        BusSaveLezer::Resultaat m_etappeResultaat;
        std::set<BusSaveLezer::Sleutel> m_etappeBekekenUit;
        std::set<BusSaveLezer::Sleutel> m_etappeBekeken;
        std::string m_etappeSleutel;
        int m_etappePogingen = 0;
        bool m_etappeGestopt = false;
        std::chrono::steady_clock::time_point m_etappeVolgendePoging{};
        static constexpr int ETAPPE_BREDE_POGINGEN = 3;
        std::int64_t m_etappeVanaf = ( std::numeric_limits<std::int64_t>::min )();
        bool m_scsEtappesExact = false;
        double m_scsGemetenPositie = -1.0;
    };
}

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <chrono>
#include <thread>
#include <vector>

namespace Ritten
{
    struct BrandstofInstellingen
    {
        bool landAutomatisch = true;
        double prijsPerLiterEuro = 2.05;
    };

    struct BrandstofState
    {
        double huidigeLiters = 0.0;
        double tankInhoudLiters = 0.0;
        double verbruikSindsRitStartLiters = 0.0;

        double verbruiktSessieLiters = 0.0;
        double kostenDezeRitEuro = 0.0;
        double totaalVerbruikLiters = 0.0;
        double totaalKostenEuro = 0.0;
    };

    class FuelCosts
    {
    public:
        FuelCosts();

        void ZetLiters( double liters, double tankInhoud );

        struct Tankbeurt
        {
            double liters = 0.0;
            double kostenEuro = 0.0;
            double kmStand = 0.0;
            std::string land;
            std::string stad;
            bool garage = false;
            double prijsPerLiter = 0.0;
        };

        std::vector<Tankbeurt> TankbeurtenDezeRit() const;

        int AantalTankbeurten() const;
        double TotaalGetanktLiters() const;
        double TotaalGetanktKosten() const;

        void ZetKilometerstand( double km );

        void LaadPrijzenPerLand();

        double PrijsVoorLand( const std::string &landcode ) const;

        void ZetHuidigLand( const std::string &landcode );
        std::string HuidigLand() const;

        std::vector<std::string> BekendeLanden() const;
        void StartNieuweRit();
        double SluitRitAf();

        BrandstofState HuidigeState() const;

        void ZetPrijsPerLiter( double prijs );
        double PrijsPerLiter() const;

        void VoertuigGewisseld();

        void SpelGepauzeerd();

        void ZetVoertuigSleutel( const std::string &sleutel );

        struct PrijsInfo
        {
            double prijs = 0.0;
            int bron = 2;
            std::string land, stad;
            bool garage = false;
        };
        PrijsInfo HuidigePrijsInfo() const;

        struct LeegRijden
        {
            double km = 0.0;
            double liters = 0.0;
            double kosten = 0.0;
        };
        LeegRijden LeegTotaal() const;
        LeegRijden LeegVoorDezeRit() const;

        struct RitStand
        {
            double liters = 0.0;
            double kosten = 0.0;
            double km = 0.0;
        };
        void StartBusRit();
        RitStand BusRitStand() const;
        RitStand SluitBusRitAf();
        void VerwerpBusRit();
        bool BusRitActief() const;
        LeegRijden LeegVoorBusRit() const;
        std::vector<Tankbeurt> TankbeurtenBusRit() const;

        void HervatRit( double liters, double kosten, const LeegRijden &leegVoor,
                        const std::vector<Tankbeurt> &tankbeurten );
        void HervatBusRit( const RitStand &vooraf, const LeegRijden &leegVoor,
                           const std::vector<Tankbeurt> &tankbeurten );

        void ZetPositie( double x, double z, bool bekend );
        void ZetLandAutomatisch( bool aan );
        bool LandAutomatisch() const;

        struct Plaatsbepaling
        {
            bool bekend = false;
            std::string stad, land;
            bool bijGarage = false;
            double prijs = 0.0;
        };
        Plaatsbepaling HuidigePlaats() const;

        void StartSpelPrijzen( const std::filesystem::path &spelmap, const std::string &versieSleutel );
        std::string PrijzenBron() const;
        ~FuelCosts();

    private:
        void LaadInstellingen();
        void SlaInstellingenOp() const;
        static std::filesystem::path InstellingenPad();

        mutable std::mutex m_mutex;
        BrandstofInstellingen m_instellingen;
        double m_litersBijRitStart = -1.0;

        double m_vorigNiveau = -1.0;

        std::vector<Tankbeurt> m_tankbeurten;
        int m_tankbeurtenTotaal = 0;
        double m_getanktTotaalLiters = 0.0;
        double m_getanktTotaalKosten = 0.0;
        double m_kmStand = 0.0;
        std::string m_huidigLand;
        std::map<std::string, double> m_prijsPerLand;
        BrandstofState m_state;

        struct OpenTankbeurt
        {
            bool actief = false;
            double liters = 0.0;
            std::chrono::steady_clock::time_point laatsteStap{};
            Tankbeurt t;
        } m_open;
        void SluitOpenTankbeurt();

        std::chrono::steady_clock::time_point m_wisselMoment{};
        bool m_naWissel = false;
        bool m_wisselMetingGezien = true;

        double m_posX = 0.0, m_posZ = 0.0;
        bool m_posBekend = false;
        std::string m_laatstBekendLand, m_laatstBekendeStad;

        struct TruckPrijs { double prijs = 0.0; std::string land, stad; bool garage = false; };
        std::string m_voertuigSleutel;
        std::map<std::string, TruckPrijs> m_truckPrijzen;
        void LaadTruckPrijzen();
        void BewaarTruckPrijzen() const;
        static std::filesystem::path TruckPrijzenPad();
        PrijsInfo PrijsInfoOnderSlot() const;

        double m_kostenTeller = 0.0;
        double m_kostenBijRitStart = 0.0;
        bool m_ritActief = false;
        LeegRijden m_leeg;
        LeegRijden m_leegSindsVorigeRit;
        LeegRijden m_leegVoorDezeRit;
        double m_kmVorig = -1.0;
        double m_vorigNiveauVoorKosten = -1.0;

        bool m_busActief = false;
        double m_literTeller = 0.0;
        double m_literBijBusStart = 0.0;
        double m_kostenBijBusStart = 0.0;
        double m_busKm = 0.0;
        RitStand m_busVooraf;
        LeegRijden m_leegVoorBusRit;
        int m_tankbeurtenBijBusStart = 0;
        std::vector<Tankbeurt> m_busTankbeurtenVooraf;
        RitStand BusStandOnderSlot() const;
        void VerwerpBusRitOnderSlot();

        LeegRijden m_leegEerder;
        int m_tankEerderAantal = 0;
        double m_tankEerderLiters = 0.0, m_tankEerderKosten = 0.0;
        bool m_totalenGewijzigd = false;
        std::chrono::steady_clock::time_point m_totalenLaatstBewaard{};
        void LaadTotalen();
        void BewaarTotalen( bool forceer );
        static std::filesystem::path TotalenPad();

        std::string m_prijzenVersie;
        std::string m_prijzenBron;

        double m_garageKorting = 0.15;
        struct XZ { double x = 0.0, z = 0.0; };
        std::map<std::string, XZ> m_landPosities;

        void BepaalLand( std::string &land, std::string &stad, bool &bijGarage ) const;

        std::thread m_spelThread;
        void LeesSpelPrijzen( std::filesystem::path spelmap, std::string versieSleutel );
        void SchrijfPrijzenBestand( const std::map<std::string, double> &prijzen, const std::string &versie, const std::string &bron ) const;
        static std::filesystem::path PrijzenPad();
    };
}

#pragma once
// FuelCosts.hxx
//
// The SCS telemetry SDK gives the fuel level of your own truck (in litres
// and as a percentage) and the tank volume, but NO price -- fuel prices are
// screen-bound economy data that does not travel over telemetry. So this
// system measures how much you actually consume (via the difference in
// litre level, corrected for refuelling) and converts that to cost with a
// price-per-litre that YOU set (with a sensible default you can freely
// change in the overlay -- "Settings" tab).
//
// That gives an honest estimate ("based on EUR X.XX/litre") instead of a
// number pretending to be the exact in-game price.

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
        // Country from the truck's position (nearest city on the map) instead
        // of a manual choice. Default on: the SDK gives no country, but the
        // position is known and the map table knows where the cities are.
        bool landAutomatisch = true;
        double prijsPerLiterEuro = 2.05;  // sensible default for EU diesel, adjustable
    };

    struct BrandstofState
    {
        double huidigeLiters = 0.0;
        double tankInhoudLiters = 0.0;
        double verbruikSindsRitStartLiters = 0.0;

        // Consumed since the plugin STARTED. Always keeps running, never
        // resets, ignores refuelling. This is the counter the consumption
        // measurement runs on, so it is no longer tied to a trip: idle and
        // instantaneous belong to the truck, not to a trip, and used to be
        // wiped at every trip start and completion.
        double verbruiktSessieLiters = 0.0;
        double kostenDezeRitEuro = 0.0;
        double totaalVerbruikLiters = 0.0;
        double totaalKostenEuro = 0.0;
    };

    class FuelCosts
    {
    public:
        FuelCosts();

        // Called by TruckTracking when new telemetry values arrive (litres
        // and tank capacity), and at the start of a new trip to set the
        // zero reading.
        void ZetLiters( double liters, double tankInhoud );

        // --- Refuelling stops ----------------------------------------------
        // A refuelling stop is recognised by a JUMP up in the level: the game
        // gives no "refuelled" event. Cost is computed with the price YOU set
        // -- the SDK does not pass on the real pump price.
        struct Tankbeurt
        {
            double liters = 0.0;
            double kostenEuro = 0.0;
            double kmStand = 0.0;  // odometer at that moment, 0 = unknown
            std::string land;  // country code, empty = unknown
            std::string stad;  // nearest city token when the position was known
            bool garage = false;       // at an own large garage: 15% discount applied
            double prijsPerLiter = 0.0; // the price actually used
        };

        // Refuelling stops of the current trip, newest first.
        std::vector<Tankbeurt> TankbeurtenDezeRit() const;

        // Totals over all trips since startup.
        int AantalTankbeurten() const;
        double TotaalGetanktLiters() const;

        // Pass on the odometer, so a refuelling stop knows where it was.
        void ZetKilometerstand( double km );

        // --- Prices per country --------------------------------------------
        // Prices live in %APPDATA%\\CabNavi\\brandstofprijzen.json, NOT in
        // the code. If SCS changes prices in an update you adjust a number
        // there without rebuilding.
        //
        // The file is created on first start with indicative prices. They are
        // deliberately "approximate": the game does not pass on the exact pump
        // price, so we do not pretend it does.
        void LaadPrijzenPerLand();

        // Price for a country code ("germany", "netherlands"). Unknown
        // country or empty code -> the manually set price.
        double PrijsVoorLand( const std::string &landcode ) const;

        // Which country the game currently reports; empty if unknown.
        void ZetHuidigLand( const std::string &landcode );
        std::string HuidigLand() const;

        // All countries from the price file, sorted. For the dropdown in the
        // settings: the game does NOT report your current country (six
        // channels tried, all six refused), so you pick it yourself.
        std::vector<std::string> BekendeLanden() const;
        void StartNieuweRit();
        double SluitRitAf();  // returns the cost of the completed trip

        BrandstofState HuidigeState() const;

        // Settings: price per litre, stored next to trips.jsonl so it
        // survives between sessions.
        void ZetPrijsPerLiter( double prijs );
        double PrijsPerLiter() const;

        // Another truck: the fuel level jumps to that truck's tank, up or
        // down. Up must not count as refuelling, down not as consumption.
        // MEASURED 05-09 00:49: Volvo -> Scania logged a 5.6 L "refuel" at the
        // garage. Called by TruckTracking on a truck configuration change.
        void VoertuigGewisseld();

        // --- Position -> country, garage -------------------------------------
        // World X/Z of the truck, from the TruckersMP SDK (the radar already
        // reads it). At a refuelling event the nearest city gives the country
        // and thus the price; a large-garage site gives the owner discount.
        void ZetPositie( double x, double z, bool bekend );
        void ZetLandAutomatisch( bool aan );
        bool LandAutomatisch() const;

        struct Plaatsbepaling
        {
            bool bekend = false;
            std::string stad, land;
            bool bijGarage = false;
            double prijs = 0.0;    // price per litre that would be charged here
        };
        // What the position says right now (for the settings tab).
        Plaatsbepaling HuidigePlaats() const;

        // --- Prices straight from the game files -------------------------------
        // Reads def/country/*.sui out of the game's .scs archives on a
        // background thread and replaces the price table. Cached in
        // brandstofprijzen.json under "_versie": only re-read when the game
        // version changes. READ ONLY; the archives are never touched.
        void StartSpelPrijzen( const std::filesystem::path &spelmap, const std::string &versieSleutel );
        std::string PrijzenBron() const;   // for display/log: where the prices came from
        ~FuelCosts();

    private:
        void LaadInstellingen();
        void SlaInstellingenOp() const;
        static std::filesystem::path InstellingenPad();

        mutable std::mutex m_mutex;
        BrandstofInstellingen m_instellingen;
        double m_litersBijRitStart = -1.0;  // -1 = no reading yet

        // Previously measured level. Used to be a `static thread_local`
        // INSIDE the function: that is set once and never reset, not even
        // for a new trip. As a member it works correctly.
        double m_vorigNiveau = -1.0;

        std::vector<Tankbeurt> m_tankbeurten;  // this trip
        int m_tankbeurtenTotaal = 0;
        double m_getanktTotaalLiters = 0.0;
        double m_kmStand = 0.0;
        std::string m_huidigLand;
        std::map<std::string, double> m_prijsPerLand;
        BrandstofState m_state;

        // A refuelling stop in progress. MEASURED 04-09 23:36: the pump fills
        // the tank in steps of ~16 litres every ~300 ms, so one visit to the
        // pump arrives as several jumps. They are collected here and only
        // become a Tankbeurt once the level has stopped rising for a moment.
        struct OpenTankbeurt
        {
            bool actief = false;
            double liters = 0.0;
            std::chrono::steady_clock::time_point laatsteStap{};
            Tankbeurt t;   // country/garage/price fixed at the FIRST step
        } m_open;
        void SluitOpenTankbeurt();   // caller holds m_mutex

        // Moment of the last truck switch; level jumps within a few seconds
        // of it are a new baseline, not a refuel and not consumption.
        std::chrono::steady_clock::time_point m_wisselMoment{};
        bool m_naWissel = false;

        // Position (game thread writes, game thread reads at refuel time).
        double m_posX = 0.0, m_posZ = 0.0;
        bool m_posBekend = false;

        // Price file provenance.
        std::string m_prijzenVersie;   // "_versie" in the file, empty = indicative defaults
        std::string m_prijzenBron;

        // Also from the game files: the garage discount (economy_data.sii,
        // default the measured 0.15) and the country centres (def/country pos)
        // as fallback for map regions the embedded city table does not know.
        double m_garageKorting = 0.15;
        struct XZ { double x = 0.0, z = 0.0; };
        std::map<std::string, XZ> m_landPosities;

        // Nearest city, or -- more than 15 km (game metres) from any known
        // city, i.e. a region the table predates -- the nearest country
        // centre. Caller holds m_mutex. `stad` stays empty for the fallback.
        void BepaalLand( std::string &land, std::string &stad, bool &bijGarage ) const;

        // Background reader for the game archives.
        std::thread m_spelThread;
        void LeesSpelPrijzen( std::filesystem::path spelmap, std::string versieSleutel );
        void SchrijfPrijzenBestand( const std::map<std::string, double> &prijzen, const std::string &versie, const std::string &bron ) const;
        static std::filesystem::path PrijzenPad();
    };
}

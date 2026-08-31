#pragma once
// FuelCosts.hxx
//
// De SCS telemetry SDK geeft de brandstofstand van je eigen truck (in liters
// en als percentage) en het tank-volume, maar géén prijs -- brandstofprijzen
// zijn schermgebonden economiedata die niet over telemetrie loopt. Dit
// systeem meet daarom hoeveel je daadwerkelijk verbruikt (via het verschil
// in literstand, gecorrigeerd voor bijtanken) en rekent dat om naar kosten
// met een prijs-per-liter die JIJ instelt (met een redelijke standaardwaarde
// die je vrij kunt aanpassen in de overlay -- "Instellingen"-tab).
//
// Zo krijg je een eerlijke schatting ("op basis van €X,XX/liter") in plaats
// van een cijfer dat doet alsof het de exacte in-game prijs is.

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace Ritten
{
    struct BrandstofInstellingen
    {
        double prijsPerLiterEuro = 1.65; // redelijke default voor EU-diesel, zelf aan te passen
    };

    struct BrandstofState
    {
        double huidigeLiters = 0.0;
        double tankInhoudLiters = 0.0;
        double verbruikSindsRitStartLiters = 0.0;
        double kostenDezeRitEuro = 0.0;
        double totaalVerbruikLiters = 0.0;
        double totaalKostenEuro = 0.0;
    };

    class FuelCosts
    {
    public:
        FuelCosts();

        // Aangeroepen door TruckTracking wanneer nieuwe telemetriewaarden
        // binnenkomen (liters en tankinhoud), en bij het starten van een
        // nieuwe rit om de nulmeting te zetten.
        void ZetLiters( double liters, double tankInhoud );

        // --- Tankbeurten ---------------------------------------------------
        // Een tankbeurt herkennen we aan een SPRONG omhoog in het niveau: het
        // spel geeft geen "getankt"-gebeurtenis. Kosten rekenen we met de
        // prijs die JIJ instelt -- de echte pompprijs geeft de SDK niet door.
        struct Tankbeurt
        {
            double liters = 0.0;
            double kostenEuro = 0.0;
            double kmStand = 0.0;   // kilometerstand op dat moment, 0 = onbekend
            std::string land;       // landcode, leeg = onbekend
        };

        // Tankbeurten van de huidige rit, nieuwste eerst.
        std::vector<Tankbeurt> TankbeurtenDezeRit() const;

        // Totalen over alle ritten sinds het opstarten.
        int AantalTankbeurten() const;
        double TotaalGetanktLiters() const;

        // De kilometerstand doorgeven, zodat een tankbeurt weet waar hij was.
        void ZetKilometerstand( double km );

        // --- Prijzen per land ----------------------------------------------
        // De prijzen staan in %APPDATA%\\CabNavi\\brandstofprijzen.json,
        // NIET in de code. Verandert SCS de prijzen bij een update, dan pas je
        // daar een getal aan zonder opnieuw te bouwen.
        //
        // Het bestand wordt bij de eerste start aangemaakt met richtprijzen.
        // Die zijn met opzet "ongeveer": de exacte pompprijs geeft het spel
        // niet door, dus doen we niet alsof.
        void LaadPrijzenPerLand();

        // Prijs voor een landcode ("germany", "netherlands"). Onbekend land of
        // lege code -> de handmatig ingestelde prijs.
        double PrijsVoorLand( const std::string &landcode ) const;

        // Welk land het spel op dit moment doorgeeft; leeg als onbekend.
        void ZetHuidigLand( const std::string &landcode );
        std::string HuidigLand() const;

        // Alle landen uit het prijzenbestand, gesorteerd. Voor het keuzemenu
        // in de instellingen: het spel geeft je huidige land NIET door (zes
        // kanalen geprobeerd, alle zes geweigerd), dus kies je het zelf.
        std::vector<std::string> BekendeLanden() const;
        void StartNieuweRit();
        double SluitRitAf(); // geeft de kosten van de afgesloten rit terug

        BrandstofState HuidigeState() const;

        // Instellingen: prijs per liter, opgeslagen naast trips.jsonl zodat
        // hij bewaard blijft tussen sessies.
        void ZetPrijsPerLiter( double prijs );
        double PrijsPerLiter() const;

    private:
        void LaadInstellingen();
        void SlaInstellingenOp() const;
        static std::filesystem::path InstellingenPad();

        mutable std::mutex m_mutex;
        BrandstofInstellingen m_instellingen;
        double m_litersBijRitStart = -1.0; // -1 = nog geen meting gehad

        // Vorig gemeten niveau. Stond als `static thread_local` IN de functie:
        // die wordt eenmalig gezet en nooit meer gereset, ook niet bij een
        // nieuwe rit. Als lid klopt het wel.
        double m_vorigNiveau = -1.0;

        std::vector<Tankbeurt> m_tankbeurten;   // deze rit
        int m_tankbeurtenTotaal = 0;
        double m_getanktTotaalLiters = 0.0;
        double m_kmStand = 0.0;
        std::string m_huidigLand;
        std::map<std::string, double> m_prijsPerLand;
        BrandstofState m_state;
    };
}

#pragma once
// TripTypes.hxx
//
// Een "Trip" is onze eigen, uniforme voorstelling van een rit -- of hij nu
// van de TruckersMP BusModule komt (buslijnen) of uit de SCS telemetry job-
// kanalen (regulier vrachtvervoer). Alles wat de overlay laat zien en wat
// TripLogger wegschrijft naar trips.jsonl is gebaseerd op deze struct.

#include <cstdint>
#include <string>
#include <vector>

namespace Ritten
{
    enum class TripType
    {
        Vracht, // reguliere ETS2/ATS vrachtjob (SCS telemetry)
        Bus     // TruckersMP buslijn-job (BusModule)
    };

    enum class TripStatus
    {
        Bezig,
        Voltooid,
        Geannuleerd
    };

    struct StopInfo
    {
        std::string naam;             // weergavenaam (bv. "Parijs") -- voor tonen in de overlay
        std::string cityIdentifier;   // interne stad-code (bv. "paris") -- voor het matchen van OnStopCompleted
        bool voltooid = false;
        double afgelegdeAfstandKm = 0.0;
        double geplandeAfstandKm = 0.0; // vanaf ritstart tot deze halte, voor de resterende-tijd-schatting

        // Geplande rijtijd in ECONOMY-minuten (speltijd), cumulatief vanaf
        // ritstart tot deze halte. De SDK geeft per halte de tijd vanaf de
        // VORIGE halte (net als de afstand), dus we tellen die zelf op.
        // Blijft 0 tot OnJobDataReady is gevuurd -- het spel rekent de
        // navigatiegegevens pas na de jobstart uit.
        double geplandeTijdMin = 0.0;
    };

    // Eén boete zoals het spel hem meldt via het "player.fined"-
    // gameplay-event. `reden` is de ruwe offence-code van het spel
    // (bv. "speeding", "red_signal"); Overlay vertaalt die naar Nederlands.
    struct Boete
    {
        std::string reden;
        std::int64_t bedrag = 0;
    };

    // Eén betaalde doorgang: tolpoort, veerboot of trein. Het spel meldt
    // deze alle drie via aparte gameplay-events, maar met dezelfde vorm.
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
        std::string vanaf;  // alleen gevuld bij veerboot/trein
        std::string naar;   // idem
    };

    struct Trip
    {
        TripType type = TripType::Vracht;
        TripStatus status = TripStatus::Bezig;

        std::string id; // uniek id (tijdstempel + type), gebruikt in trips.jsonl

        // Algemeen
        std::string startTijdIso;
        std::string eindTijdIso;
        std::uint32_t economyStartTijd = 0; // in-game economy-minuten
        std::uint32_t economyEindTijd = 0;

        std::string serverNaam;
        std::string voertuigMerk;
        std::string voertuigModel;

        // Vracht-specifiek
        std::string lading;
        std::string bronStad;
        std::string bestemmingStad;
        std::string bronBedrijf;
        std::string bestemmingBedrijf;
        double geplandeAfstandKm = 0.0;
        double afgelegdeAfstandKm = 0.0;
        std::int64_t inkomen = 0;
        bool opTijd = true;

        // Geschatte brandstofkosten van deze rit (zie FuelCosts.hxx: gebaseerd
        // op gemeten verbruik x zelf-ingestelde prijs per liter, niet op een
        // exacte in-game prijs die de telemetrie niet blootlegt).
        double brandstofVerbruikLiters = 0.0;
        double brandstofKostenEuro = 0.0;

        // Onkosten die het spel zelf meldt als gameplay-event tijdens de rit
        // (dit zijn ECHTE in-game bedragen, geen schatting zoals bij
        // brandstof -- daar moeten we zelf een prijs per liter voor
        // invullen omdat de telemetrie die niet blootlegt).
        std::vector<Boete> boetes;
        std::vector<Doorgang> doorgangen;
        std::int64_t tolKosten = 0;       // som van alle tolpoorten deze rit
        std::int64_t veerbootKosten = 0;  // som van alle veerboten deze rit
        std::int64_t treinKosten = 0;     // som van alle treinen deze rit
        std::int64_t boeteKosten = 0;     // som van alle boetes deze rit

        // Aanhanger: schade aan het chassis en aan de lading zelf. Deze
        // twee zijn niet hetzelfde -- je kan een gedeukte trailer hebben
        // met perfecte lading, en andersom. Alleen de ladingschade telt
        // mee voor je uitbetaling.
        double aanhangerSchadePercentage = 0.0;
        double ladingSchadePercentage = 0.0;
        double ladingGewichtKg = 0.0;

        // Bus-specifiek
        std::vector<StopInfo> haltes;
        std::int64_t geschatUitbetaling = 0;
        std::string annuleringsReden;

        // Live/actuele meting (alleen relevant terwijl status == Bezig)
        double huidigeSnelheidKmh = 0.0;
        double brandstofPercentage = 0.0;
        double schadeChassisPercentage = 0.0;
    };
}

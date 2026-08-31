#pragma once
// BusTracking.hxx
//
// Volgt buslijn-jobs via TruckersMP::BusModule en zet elke afgeronde/
// geannuleerde rit om naar een Ritten::Trip die naar de TripLogger gaat.
// Houdt ook de "live" rit bij zodat de overlay tijdens de rit voortgang kan
// tonen.

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

        // Door de telemetry-callback aan te roepen zodra het "game.time"
        // kanaal verandert (zie Plugin.cxx / TruckTracking.cxx).
        void ZetEconomyTijd( std::uint32_t minuten );

        // Door TruckTracking aangeroepen bij elke live snelheidsmeting (het
        // "truck.speed"-kanaal is niet job-type-specifiek -- hetzelfde
        // voertuig, dus dezelfde snelheid, of je nu vracht of een buslijn
        // rijdt). Zo kan de buslijn-tracking dezelfde betrouwbare IRL-
        // tijdschatting doen als vracht, zonder zelf telemetrie te hoeven
        // registreren.
        void OpLiveSnelheid( double snelheidKmh, bool gepauzeerd );

        // Navigatiegegevens van het spel, doorgegeven vanuit TruckTracking
        // (die registreert de SCS-kanalen; de bus heeft er geen eigen).
        // `navTijdRuw` is de waarde van truck.navigation.time zoals het spel
        // hem geeft, `navAfstandKm` de resterende route-afstand.
        void ZetNavigatie( double navTijdRuw, double navAfstandKm );

        // Voor de overlay: is er nu een actieve buslijn-rit, en zo ja, welke.
        // LET OP: dit kijkt bewust naar een eigen 'm_actief'-vlag, niet naar
        // m_huidigeRit.status -- een default-geconstrueerde Trip heeft
        // status == Bezig, dus die vergelijking zou een niet-bestaande rit
        // ten onrechte als actief laten zien.
        bool HeeftActieveRit() const { return m_actief; }
        const Trip &HuidigeRit() const { return m_huidigeRit; }

        // Echte (klok)tijd sinds ritstart, pauze-bewust (telt niet door
        // tijdens een pauze -- zie GepauzeerdCallback-patroon in
        // TruckTracking, hier ontvangen we die status via OpLiveSnelheid).
        double VerstrekenMinutenEcht() const;

        // Geschatte resterende IRL-tijd tot de eerstvolgende niet-voltooide
        // halte, op basis van je voortschrijdend-gemiddelde snelheid.
        // -1.0 = nog niet te schatten (te weinig data, of geen actieve rit).
        double GeschatteResterendeMinutenEcht() const;

        // Geschatte ECHTE minuten tot halte `index`, op dezelfde manier
        // gerekend als de tijd tot de eerstvolgende halte: die schatting is
        // het vertrekpunt, en de extra afstand daarna wordt met dezelfde
        // reissnelheid omgerekend.
        //
        // -1.0 = niet te bepalen (halte al voltooid, of geen afstandsgegevens).
        double GeschatteMinutenTotHalte( std::size_t index ) const;

        // --- Te laat komen (TMP 0.7.5.0 introduceerde een boete) ---------
        //
        // De boete werkt zo: alleen de LAATSTE halte telt, de eerste 60
        // minuten vertraging zijn gratis, en daarboven kost elke minuut
        // 0,333% van de uitbetaling -- na ruim 6 uur hou je niets over.
        //
        // Om te weten of je te laat komt moeten we twee klokken vergelijken:
        // de deadline staat in ECONOMY-minuten (speltijd), onze aankomst-
        // schatting in ECHTE minuten. De verhouding daartussen leiden we af
        // uit hoe snel de speltijd loopt (zie TijdSchaal()), in plaats van
        // een vaste factor aan te nemen -- TruckersMP heeft die schaal in
        // 0.7.5.0 nog aangepast, dus een constante zou meteen verouderen.
        //
        // Geeft het aantal economy-minuten dat je TE LAAT verwacht te zijn
        // bij de laatste halte. Negatief betekent dat je voorligt op schema.
        // -1e9 = niet te bepalen (geen actieve rit, of navigatiedata nog
        // niet klaar).
        double GeschatteVertragingMinuten() const;

        // Hoeveel procent van de uitbetaling je bij de huidige vertraging
        // kwijtraakt (0..100). 0 zolang je binnen het uur speling blijft.
        double GeschatteBoetePercentage() const;

        // Economy-minuten per echte minuut, afgeleid uit de waarnemingen.
        // 0 zolang er nog te weinig gemeten is.
        double TijdSchaal() const;

    private:
        void StartRecord( const TruckersMP::BusJob &job );

        TripLogger &m_logger;
        std::unique_ptr<TruckersMP::BusModule> m_busModule;
        Trip m_huidigeRit;
        bool m_actief = false;
        std::uint32_t m_economyTijd = 0;

        // --- Meting van de tijdschaal ------------------------------------
        // game.time ververst maar eens per economy-minuut (staat zo in de
        // docs), dus meten we over een langer venster: we onthouden het
        // eerste moment waarop we een verandering zagen, en vergelijken dat
        // met het laatste. Zo middelen we die schokkerigheid weg.
        std::uint32_t m_schaalEersteEconomy = 0;
        std::chrono::steady_clock::time_point m_schaalEersteEcht{};
        bool m_schaalGestart = false;
        // Zie TruckTracking: eenmaal betrouwbaar gemeten zetten we de
        // schaal vast, zodat hij tijdens het rijden niet meer beweegt.
        mutable double m_vastgezetteSchaal = 0.0;
        std::chrono::steady_clock::time_point m_ritStartMoment;

        // Pauze-tracking (zelfde principe als TruckTracking, maar hier
        // ontvangen via OpLiveSnelheid i.p.v. eigen SCS-registratie).
        bool m_gepauzeerd = false;
        std::chrono::steady_clock::time_point m_pauzeStartMoment;
        double m_totaalGepauzeerdSeconden = 0.0;

        // Voortschrijdend gemiddelde van de laatste ~3 minuten snelheid.
        std::deque<std::pair<std::chrono::steady_clock::time_point, double>> m_snelheidVenster;

        // --- Eigen kopie van de aankomsttijd-opzet --------------------------
        // Bewust apart van TruckTracking: dezelfde constructie, maar een eigen
        // exemplaar, zodat sleutelen aan de bus de vrachtrit niet raakt.
        double m_navTijdRuw = -1.0;
        double m_navAfstandKm = -1.0;
        mutable double m_gladdeSchattingMin = -1.0;
        double Gladstrijken( double ruweMinuten ) const;

        // Effectieve reissnelheid in km per ECHT uur. Losse functie zodat de
        // schatting per halte dezelfde snelheid gebruikt als de schatting tot
        // de eerstvolgende halte -- zonder dat ik aan die bestaande functie
        // hoef te komen.
        double EffectieveSnelheidEcht() const;
        static constexpr double VENSTER_SECONDEN = 180.0;

        // Live afstand sinds de laatste AFGERONDE halte (via snelheid x
        // verstreken tijd, net als bij vracht) -- nodig omdat
        // m_huidigeRit.afgelegdeAfstandKm alleen bijwerkt ZODRA een halte
        // officieel voltooid wordt (via het spel-event). Zonder dit dacht
        // de resterende-tijd-schatting dat je nog de HELE laatste etappe
        // moest rijden, ook al was je er bijna -- de teller sprong pas bij
        // aankomst zelf bij, niet er onderweg naartoe.
        double m_liveKmSindsLaatsteHalte = 0.0;
        std::chrono::steady_clock::time_point m_laatsteSnelheidMeting;
    };
}

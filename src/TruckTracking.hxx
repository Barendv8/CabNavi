#pragma once
// TruckTracking.hxx
//
// Reguliere vrachtjobs (dus geen TruckersMP-buslijnen) zijn basisspel-data
// en lopen via de SCS Telemetry SDK, niet via de TruckersMP Client SDK zelf.
// Deze klasse verwerkt de telemetrie-callbacks (job gestart/afgeleverd/
// geannuleerd + live kanalen zoals snelheid, brandstof en schade) en zet ze
// om naar Ritten::Trip records.
//
// LET OP: dit bestand gebruikt kanaal-/config-/event-namen als PLATTE TEKST
// (bv. "truck.speed") in plaats van de *_CHANNEL_*/*_CONFIG_* macro's uit de
// SCS-voorbeelden. Dat is net zo correct -- de macro's zijn uiteindelijk ook
// gewoon deze tekst-strings -- maar voorkomt een afhankelijkheid van de
// spel-specifieke headers (eurotrucks2/scssdk_telemetry_eut2.h e.d.) die niet
// in elke SDK-download op dezelfde plek staan. Zie readme.txt in je scssdk-
// map als je de kanalen exact wilt verifi\u00ebren.

#include "FuelCosts.hxx"
#include "TripLogger.hxx"
#include "TripTypes.hxx"

#include <scssdk_telemetry.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <utility>

namespace Ritten
{
    class BusTracking; // voorwaartse declaratie, zie m_busTracking hieronder
    class PlayersNearby;
    class IncidentRecorder;

    class TruckTracking
    {
    public:
        TruckTracking( TripLogger &logger, FuelCosts &brandstof );

        // Wordt aangeroepen vanuit scs_telemetry_init met de door SCS
        // gegeven registratiefunctie, om alle kanalen/events te abonneren.
        void RegistreerBijTelemetrie( const scs_telemetry_init_params_v101_t *params );

        std::uint32_t EconomyTijd() const { return m_economyTijd; }

        // --- Tijdschaal ---------------------------------------------------
        // Hoeveel SPELminuten er verstrijken per ECHTE minuut. In ETS2 loopt
        // de klok sneller dan in het echt, en TruckersMP zet daar zijn eigen
        // schaal op (die ze in 0.7.5.0 nog hebben aangepast). Daarom meten we
        // het af aan game.time in plaats van een vast getal aan te nemen.
        //
        // Waarom dit ertoe doet: de snelheidsmeter geeft km per SPELuur. Wil
        // je weten hoe lang iets in het ECHT duurt, dan moet je die snelheid
        // met de schaal vermenigvuldigen -- anders reken je spelminuten uit
        // en noem je ze echte minuten. Precies die verwarring zorgde voor
        // schattingen van 22 uur bij een ritje van een kwartier.
        //
        // 0 = nog niet genoeg gemeten.
        double TijdSchaal() const;

        // Ruwe waarden achter de ETA-schatting. Puur om te kunnen zien
        // WAAROM er een getal staat, in plaats van te moeten gokken.
        // navigatieTijd < 0 betekent: het kanaal heeft nog niets gestuurd.
        double RuweNavigatieTijd() const { return m_navigatieTijd; }
        bool SchaalStaatVast() const { return m_vastgezetteSchaal > 0.0; }
        double RuweNavigatieAfstandKm() const
        {
            return m_navigatieAfstandMeter >= 0.0 ? m_navigatieAfstandMeter / 1000.0 : -1.0;
        }

    private:
        // Voorkomt dat de schatting bij elk stoplicht heen en weer springt.
        double Gladstrijken( double ruweMinuten ) const;

    public:
        // LET OP: kijkt bewust naar een eigen 'm_actief'-vlag, niet naar
        // m_huidigeRit.status -- zie dezelfde opmerking in BusTracking.hxx.
        bool HeeftActieveRit() const { return m_actief; }
        const Trip &HuidigeRit() const { return m_huidigeRit; }

        // Echte (klok)tijd sinds ritstart, en een schatting van hoe lang het
        // in het echt nog duurt op basis van je gemiddelde snelheid tot nu
        // toe (of je huidige snelheid als er nog te weinig data is). Dit is
        // dus ECHTE minuten zoals jij ze beleeft, geen in-game economy-tijd
        // -- die twee lopen niet gelijk op, afhankelijk van je economy-
        // tijdsinstelling in het spel.
        double VerstrekenMinutenEcht() const;

        // Live snelheid, altijd bijgewerkt zolang je rijdt -- ook als er
        // geen job actief is. Voor kleine "voelt levend"-details in de
        // overlay (zoals de minimap subtiel laten meebewegen), niet aan
        // een specifieke rit gebonden zoals HuidigeRit().huidigeSnelheidKmh.
        double LiveSnelheidKmh() const { return m_liveSnelheidKmh; }
        double GeschatteResterendeMinutenEcht() const; // -1.0 = nog niet te schatten

        // Alle "boordcomputer"-waarden bij elkaar, zodat de overlay ze in
        // één keer kan ophalen in plaats van tien losse getters. Waarden
        // die het spel (nog) niet heeft doorgegeven blijven op -1.0 staan,
        // zodat de overlay "--" kan tonen in plaats van een misleidende 0.
        struct VoertuigStatus
        {
            double bereikKm = -1.0;             // truck.fuel.range
            double verbruikLiterPer100Km = -1.0; // afgeleid van truck.fuel.consumption.average (l/km)
            // De twee hierboven komen van het spel zelf en zijn eigenlijk een
            // tripcomputer-cijfer van SCS (reset niet per se bij onze rit,
            // hangt sterk af van truckmodel/aanhanger) -- zie het onderzoek
            // van 30-08. Onderstaande twee zijn ONZE eigen, betrouwbaardere
            // berekening op basis van het brandstofniveau, dat wel loepzuiver
            // is:
            double verbruikGemiddeldLiterPer100Km = -1.0; // eigen gemiddelde: alleen RIJDEND verbruik / rijdende km
            double verbruikNuLiterPer100Km = -1.0;         // kort-lopend venster (~8 sec), valt terug op gemiddelde
            double verbruikLiterPerUur = -1.0;             // stationair/langzaam verbruik; l/100km is dan zinloos
            bool staatStil = false;                        // zo ja: toon l/uur i.p.v. l/100km
            bool echtStil = false;                         // echt stilstaand (voor het woord "stationair")
            double kilometerstandKm = -1.0;      // truck.odometer
            double snelheidslimietKmh = -1.0;    // truck.navigation.speed.limit
            double cruiseControlKmh = 0.0;       // 0 = uit
            double schadeMotor = -1.0;           // percentages 0-100
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

        // Te hard rijden? Alleen waar als er een limiet bekend is EN je er
        // meer dan de marge overheen zit (kleine marge om piepklein
        // overschrijden bij het inhalen niet te laten knipperen).
        bool RijdtTeHard() const;

    private:
        // Statische trampolines (SCS callbacks zijn C function pointers zonder context,
        // op één na waar we `this` als user_data meegeven).
        // Meting: welke landkanalen biedt het spel aan? Alleen om uit te
        // zoeken of automatische prijzen per land haalbaar zijn.
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
        // Het spel's eigen resterende navigatie-afstand (meters, echte weg).
        static SCSAPI_VOID NavigatieAfstandCallback( const scs_string_t name, scs_u32_t index,
                                                       const scs_value_t *value, scs_context_t context );
        // Puur voor logging (zie opmerking bij registratie in .cxx).
        static SCSAPI_VOID LocalScaleCallback( const scs_string_t name, scs_u32_t index,
                                                 const scs_value_t *value, scs_context_t context );

        // --- Nieuwe kanalen (ideeenlijst #1,2,6,7,8,9,10) --------------
        // Alle namen hieronder zijn geverifieerd tegen de officiele SCS-
        // headers scssdk_telemetry_truck_common_channels.h en de
        // trailer-registratie in RenCloud/scs-sdk-plugin -- niet gegokt.
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
        // SCS_TELEMETRY_EVENT_paused/started -- vertellen ons precies
        // wanneer de simulatie stilstaat (pauze-menu, laadscherm e.d.), dit
        // gebruikt hetzelfde patroon als het officiele SCS-voorbeeld
        // (telemetry.cpp registreert deze twee events ook). Nodig om de
        // IRL-tijdklok NIET door te laten tikken tijdens een pauze.
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
        std::string m_huidigeLadingId; // interne cargo.id van de actieve rit, voor het herkennen van herhaalde config-updates
        std::uint32_t m_economyTijd = 0;

        // Meetpunten voor de tijdschaal. game.time ververst maar eens per
        // spelminuut, dus we meten over een langer venster en middelen die
        // schokkerigheid weg.
        std::uint32_t m_schaalEersteEconomy = 0;
        std::chrono::steady_clock::time_point m_schaalEersteEcht{};
        std::chrono::steady_clock::time_point m_schaalLaatsteEcht{};
        bool m_schaalGestart = false;

        // Gladgestreken schatting, zodat het getal niet heen en weer springt
        // bij elk stoplicht. -1 = nog geen waarde.
        mutable double m_gladdeSchattingMin = -1.0;

        // Zodra de schaal betrouwbaar gemeten is, zetten we hem VAST voor de
        // rest van de sessie. Daarna kan geen enkele lag-piek of tijdsprong
        // de aankomsttijd nog laten verspringen. Bij de volgende keer opstarten
        // wordt opnieuw gemeten, dus een wijziging van TruckersMP pikt hij
        // vanzelf op -- alleen niet middenin een rit.
        mutable double m_vastgezetteSchaal = 0.0;

    public:
        // Handmatige overschrijving van de tijdschaal, voor als de meting
        // ooit iets geks doet of je zelf weet wat de juiste waarde is.
        // 0 = uit (dan meet hij zelf). Wordt bewaard in uiterlijk.json.
        //
        // Bekende waarden: TruckersMP = 6, singleplayer = ongeveer 19.
        void ZetHandmatigeSchaal( double schaal ) { m_handmatigeSchaal = schaal; }
        double HandmatigeSchaal() const { return m_handmatigeSchaal; }

    private:
        double m_handmatigeSchaal = 0.0;
        double m_tankInhoudLiters = 0.0;

        // Voor de echte-tijd-schatting: wanneer de rit begon, en wanneer we
        // voor het laatst de snelheid gemeten hebben (om km "live" bij te
        // houden via snelheid x verstreken tijd -- een schatting, want de
        // telemetrie geeft geen live afgelegde-afstand-kanaal; pas bij
        // afronding corrigeren we dit met de echte "distance.km" van het
        // gameplay-event).
        std::chrono::steady_clock::time_point m_ritStartMoment;
        std::chrono::steady_clock::time_point m_laatsteSnelheidMeting;

        // Om ook de buslijn-tracking van live snelheid + pauze-status te
        // voorzien (het "truck.speed"-kanaal is niet job-type-specifiek --
        // we hoeven het niet twee keer te registreren, gewoon doorsturen).
        // Zie ZetBusTracking() en OpLiveSnelheid() in BusTracking.
        BusTracking *m_busTracking = nullptr;
    public:
        void ZetBusTracking( BusTracking *bus ) { m_busTracking = bus; }

        // Voor de incident-recorder: bij een plotselinge schadesprong
        // vragen we PlayersNearby wie het dichtstbij was, en melden dat aan
        // de recorder zodat die de buffer bevriest.
        void ZetIncidentKoppeling( PlayersNearby *spelers, IncidentRecorder *recorder )
        {
            m_spelersVoorIncident = spelers;
            m_incidentRecorder = recorder;
        }
    private:
        PlayersNearby *m_spelersVoorIncident = nullptr;
        IncidentRecorder *m_incidentRecorder = nullptr;
        double m_vorigeSchadePercentage = 0.0;
        double m_minutenTotRust = -1.0;   // -1 = kanaal (nog) niet ontvangen
        double m_rustPeriodeMax = 0.0;    // hoogste gezien, = lengte van een volle periode

        double m_liveSnelheidKmh = 0.0;
        double m_navigatieTijd = -1.0;    // ruwe waarde uit truck.navigation.time
        double m_navigatieAfstandMeter = -1.0; // -1 = nog niet ontvangen / niet beschikbaar

        // Nieuwe kanaalwaarden. -1.0 betekent overal "nog nooit ontvangen",
        // zodat de overlay het verschil ziet tussen "0" en "onbekend".
        double m_bereikKm = -1.0;
        double m_verbruikLiterPerKm = -1.0;  // ruw kanaal is l/km, niet l/100km
        double m_kilometerstandKm = -1.0;
        double m_snelheidslimietMs = -1.0;   // ruw kanaal is m/s
        double m_cruiseControlMs = 0.0;      // 0 = uitgeschakeld
        double m_schadeMotor = -1.0;         // opgeslagen als percentage 0-100
        double m_schadeBak = -1.0;
        double m_schadeCabine = -1.0;
        double m_schadeWielen = -1.0;
        double m_aanhangerSchade = -1.0;
        double m_ladingSchade = -1.0;
        double m_ladingGewichtKg = -1.0;
        bool m_heeftAanhanger = false;

    public:
        // Tachograaf: rijtijd sinds de laatste rust, en of je momenteel als
        // "rustend" geldt (stilstaand langer dan een korte drempel, om
        // stoplichten niet als rust te tellen). EU-richtlijn: max 4,5 uur
        // rijden, dan verplicht 45 min rust -- puur informatief, geen
        // handhaving.
        double TachograafRijtijdMinuten() const;

        // --- Rusttijd volgens het SPEL zelf --------------------------------
        // Het SCS-kanaal "game.next.rest.stop" geeft hoeveel SPELMINUTEN je
        // nog mag rijden voordat rust verplicht is. Dat is de echte bron;
        // onze eigen opgetelde rijtijd is niet meer dan een benadering.
        //
        // Let op: op veel TruckersMP-servers staat vermoeidheid uit. Dan
        // blijft dit kanaal op een vaste waarde staan of komt het nooit
        // binnen -- vandaar -1.0 als "niet beschikbaar", zodat de overlay
        // kan terugvallen op onze eigen teller in plaats van iets te tonen
        // wat nergens op slaat.
        double MinutenTotRustSpel() const { return m_minutenTotRust; }

        // Diagnose: wat DOET het kanaal eigenlijk op jouw server? Omdat je in
        // multiplayer niet kunt rusten (slapen slaat daar geen tijd over),
        // weten we niet of deze teller wel terugspringt. Deze waarden maken
        // dat zichtbaar in plaats van dat we erover blijven speculeren.
        double LaagsteRustWaarde() const { return m_rustLaagst; }
        int RustResetsGezien() const { return m_rustResets; }

        // 0 = registratie van het rustkanaal MISLUKT (kanaal bestaat niet of
        // heeft een ander type), 1 = s32, 2 = u32, 3 = float. Zo zie je op de
        // HUD meteen of het aan ons of aan het spel ligt.
        int RustKanaalType() const { return m_rustKanaalType; }
        double EigenRijSpelMin() const { return m_pauzeRijSpelMin; }

        // Hoogste waarde die we sinds de laatste rust gezien hebben. Dient
        // als schaal voor de balk: we weten daarmee hoe lang een volledige
        // periode is, zonder 11 uur hard in te bakken (dat verschilt per
        // spelversie en per mod).
        double VolledigeRustperiodeMinuten() const { return m_rustPeriodeMax; }
        bool TachograafInRust() const { return m_tachoInRust; }

        // --- Verplichte pauze (ETS2 1.60 "Mandatory Break") ---------------
        //
        // BELANGRIJK: de telemetrie geeft deze waarde NIET. Het kanaal
        // game.next.rest.stop hoort bij de andere teller -- de "Rest State",
        // de lange rust van negen uur. Voor de verplichte pauze bestaat (nog)
        // geen kanaal; dat is ook gemeld bij andere dashboard-projecten, met
        // als antwoord dat er voorlopig geen omweg is.
        //
        // Daarom tellen we die zelf, volgens de regel die ETS2 ECHT aanhoudt.
        // SCS schrijft dat zelf in de 1.60-aankondiging: je mag tot 10 uur
        // rijden voor een verplichte pauze, en die pauze vraagt 9
        // aaneengesloten uur rust. Beide in SPELtijd.
        //
        // Hier stond eerst 4u30 rijden en 45 minuten pauze. Dat is de echte
        // Europese rijtijdenwet, maar NIET wat het spel doet -- de teller liep
        // daardoor ruim twee keer zo snel vol als de P-teller in je Route
        // Advisor. Nu lopen ze gelijk.
        //
        // Geeft de resterende SPELminuten tot je moet pauzeren. Negatief
        // betekent dat je er al overheen bent.
        double MinutenTotVerplichtePauze() const;

        // Lengte van een volle rijperiode in spelminuten -- afgeleid uit wat
        // het spel doorgeeft, met 10 uur als terugval. Nodig om de balk te
        // schalen zonder een getal aan te nemen.
        double RijPeriodeSpelMinuten() const;

        // Hoe lang je aaneengesloten stilstaat, in spelminuten.
        double PauzeMinutenGemaakt() const { return m_pauzeStilstandSpelMin; }

        // 11 uur, niet 10. SCS noemt 10 in de 1.60-aankondiging, maar op
        // TruckersMP staat de P-teller na een rust zichtbaar op 11 uur -- in
        // het spel zelf nagekeken. Dit is alleen de TERUGVAL: zodra het
        // rustkanaal een hogere waarde doorgeeft, gebruikt de plugin die.
        // --- Instelbare tachograaf ---------------------------------------
        //
        // Drie standen. Stand 1 blijft precies wat hij was; die code raakt
        // niets van het onderstaande aan.
        enum class TachoStand
        {
            SpelVolgen = 0,  // 11 uur, gelijk met de P-teller van het spel
            EigenRegels = 1, // zelf ingestelde tijden
            ATW = 2          // voorinstelling met de echte rijtijdenwet
        };

        struct TachoInstelling
        {
            TachoStand stand = TachoStand::SpelVolgen;

            // Alles in SPELminuten, want daarin rekent de rest ook.
            double maxAaneengeslotenRijden = 4.5 * 60.0; // 4u30
            double pauzeDuur = 45.0;                     // 45 min
            double maxDagRijden = 9.0 * 60.0;            // 9 uur
            double dagRust = 11.0 * 60.0;                // 11 uur
        };

        void ZetTachoInstelling( const TachoInstelling &nieuw );
        TachoInstelling HuidigeTachoInstelling() const { return m_tacho; }

        // Resterende SPELminuten tot de volgende PAUZE (stand 2 en 3).
        // -1 = niet van toepassing in deze stand.
        double MinutenTotPauzeEigen() const;

        // Resterende SPELminuten van je DAGrijtijd (stand 2 en 3).
        double MinutenDagrijtijdOver() const;

        static constexpr double MAX_RIJ_SPELMINUTEN = 11.0 * 60.0;
        static constexpr double PAUZE_SPELMINUTEN = 9.0 * 60.0;    // 9 uur rust
        // SCS waarschuwt twee uur van tevoren; dat doen wij dus ook.
        static constexpr double WAARSCHUW_SPELMINUTEN = 2.0 * 60.0;

    private:
        void TachograafUpdate( double snelheidKmh );

        double m_tachoRijSecondenSindsRust = 0.0;
        double m_tachoStilstandSeconden = 0.0;

        // Verplichte-pauzeteller, in SPELminuten (niet in echte seconden --
        // dat was de fout in de oude teller: 1 echte minuut stilstaan is maar
        // 6 spelminuten, en dat zette de rijtijd al op nul).
        // Spelklok-stand op het moment van de laatste rust. De resterende
        // tijd is gewoon: periode - (nu - dat moment). Geen optelsom die kan
        // afdrijven, en automatisch in verhouding met de serverklok.
        std::uint32_t m_economyTijdLaatsteRust = 0;
        bool m_rustMomentBekend = false;

        // Veerboten en treinen laten de spelklok ook vooruitspringen. Die
        // events vangen we al op voor de onkosten, dus we zetten hier een
        // vlag zodat de EERSTVOLGENDE tijdsprong niet als rust telt.
        bool m_negeerVolgendeSprong = false;

        // Laatst getoonde waarde. Een aftelteller hoort alleen omlaag te
        // gaan; springt de serverklok een minuut terug (synchronisatie,
        // ping-schommeling), dan zou het getal anders zichtbaar omhoog
        // wippen. We houden 'm vast tot hij weer echt lager uitkomt.
        mutable double m_getoondeRestMin = -1.0;

        // Zelfde rem voor de rijtijd-teller. Die telt OP in plaats van af,
        // dus daar mag het getal alleen omhoog -- verder dezelfde logica.
        mutable double m_getoondeRijtijdMin = -1.0;

        // --- Onthouden tussen sessies -------------------------------------
        // ETS2 bewaart je rijtijd in je profiel: sluit je het spel af, dan
        // sta je bij het opstarten nog steeds op dezelfde P-tijd. Zonder dit
        // begon onze teller elke keer weer op elf uur, en liep hij dus vanaf
        // het eerste moment fout.
        //
        // We bewaren het spelminuut van je laatste rust in
        // %APPDATA%\\CabNavi\\tachograaf.json. Meer is niet nodig: de
        // rest is een aftreksom met de huidige klok.
        void LaadTachoStand();
        void BewaarTachoStand() const;

        // We bewaren de RESTERENDE TIJD, niet het moment van je laatste rust.
        //
        // Dat moment leek logisch, maar werkt niet op TruckersMP: de
        // serverklok loopt door terwijl jij offline bent. Sluit je af met 9
        // uur over en kom je een week later terug, dan is die klok duizenden
        // spelminuten verder en zou de som zeggen dat je allang moet pauzeren.
        // Terwijl je in-game P-teller gewoon nog op 9 uur staat.
        //
        // Door de resterende tijd te bewaren en bij het opstarten opnieuw te
        // verankeren aan de klok van dat moment, pak je gewoon de draad op
        // waar je hem liet liggen.
        mutable double m_laatstBewaardeRest = -1.0;

        // Ingelezen waarde die nog verankerd moet worden zodra de klok binnenkomt.
        double m_teHerstellenRest = -1.0;

        double m_pauzeRijSpelMin = 0.0;
        double m_pauzeStilstandSpelMin = 0.0;
        double m_laatsteRustSpelMinuten = 0.0; // hoe lang de laatste rust duurde

        // Eigen tachograaf (stand 2 en 3). Twee losse tellers: sinds de
        // laatste PAUZE en sinds de laatste DAGRUST. Beide in spelminuten,
        // beide afgeleid van de spelklok -- net als stand 1.
        TachoInstelling m_tacho;
        std::uint32_t m_eigenLaatstePauze = 0;
        std::uint32_t m_eigenLaatsteDagrust = 0;
        bool m_eigenGestart = false;
        double m_rustLaagst = -1.0;            // laagste waarde ooit gezien
        int m_rustResets = 0;                  // hoe vaak de teller omhoog sprong
        int m_rustKanaalType = 0;              // welk type registreerde; 0 = mislukt
        bool m_tachoInRust = false;
        std::chrono::steady_clock::time_point m_tachoLaatsteMeting;
        bool m_tachoGeinitialiseerd = false;

        // Pauze-tracking: telt hoeveel tijd er tijdens deze rit al gepauzeerd
        // is geweest, zodat we dat van de "verstreken tijd" kunnen aftrekken.
        bool m_gepauzeerd = false;
        std::chrono::steady_clock::time_point m_pauzeStartMoment;
        double m_totaalGepauzeerdSeconden = 0.0;

        // Voortschrijdend gemiddelde van de laatste ~3 minuten (tijdstip,
        // cumulatieve afgelegde km op dat moment) -- reageert sneller op
        // "nu snelweg, nu stad" dan het gemiddelde over de hele rit, net
        // zoals Trucky elke seconde herberekent i.p.v. één vast gemiddelde
        // te gebruiken.
        std::deque<std::pair<std::chrono::steady_clock::time_point, double>> m_kmVenster;

        // Zelfde principe, nu voor het verbruik. De afstand komt NIET uit de
        // kilometerstand: die heeft te weinig precisie voor een venster van
        // een paar seconden, en een grove noemer maakt l/100km zowel te hoog
        // als springerig. In plaats daarvan tellen we snelheid x tijd op --
        // dezelfde manier waarop de ritafstand al wordt bijgehouden.
        struct VerbruikMeting
        {
            std::chrono::steady_clock::time_point moment;
            double verbruiktLiters = 0.0;
            double gemetenKm = 0.0;   // opgeteld uit snelheid x tijd
        };
        std::deque<VerbruikMeting> m_brandstofVenster;

        // Alleen wat je RIJDEND verbruikt en rijdt telt mee in het
        // gemiddelde. Stationair draaien verbrandt wel liters maar levert
        // geen kilometers op; dat meetellen laat het gemiddelde eindeloos
        // oplopen zodra je even stilstaat. Het stationaire verbruik tonen we
        // apart, in l/uur -- net als de boordcomputer in het spel.
        double m_rijdendLiters = 0.0;
        double m_rijdendKm = 0.0;
        double m_meetKmTotaal = 0.0;          // doorlopende teller voor het venster
        double m_vorigMeetLiters = -1.0;      // -1 = nog geen meting gedaan
        double m_vorigMeetOdometerKm = -1.0;  // kilometerteller bij de vorige meting

        // Hoeveel kilometer de TELLER oploopt per "snelheidsmeter-kilometer".
        // GEMETEN 30-08: ongeveer 18,6. De wereld van ETS2 is verkleind, en
        // de teller telt echte kilometers terwijl de meter bij die verkleinde
        // wereld hoort. We meten dit tijdens het rijden in plaats van een
        // getal in te typen, want het verschilt per spelmodus.
        //
        // Nodig voor het l/uur-cijfer: dat stond een factor 2 te laag omdat
        // het door de TIJDschaal werd gedeeld in plaats van door deze.
        double m_afstandsFactor = 18.6;
        // Wat er als laatste is weggeschreven, zodat we niet bij elke meting
        // naar schijf gaan. Zie MetingPad() in TruckTracking.cxx.
        double m_bewaardeFactor = 18.6;
        void LaadMeting();
        static constexpr double AFSTANDSFACTOR_MIN = 1.0;
        static constexpr double AFSTANDSFACTOR_MAX = 40.0;

        // Gaspedaal 0..1, en of het bij de vorige meting ingedrukt was.
        // Zodra dat omslaat (gas erop of gas eraf) is alles wat er in het
        // meetvenster staat achterhaald: je verbruik verandert op datzelfde
        // moment, maar de meting kijkt terug in de tijd. We gooien het
        // venster dan leeg, zodat het cijfer binnen een seconde het NIEUWE
        // rijgedrag laat zien in plaats van het oude.
        double m_gaspedaal = 0.0;
        bool m_gasIngedrukt = false;
        bool m_gasOmslag = false;              // door de callback gezet, door de meting verwerkt
        static constexpr double GAS_DREMPEL = 0.05;
        std::chrono::steady_clock::time_point m_vorigMeetMoment;
        bool m_meetGestart = false;
        static constexpr double RIJDT_DREMPEL_KMH = 3.0;

        // Onder deze snelheid tonen we l/uur in plaats van km/l. Praktisch
        // gezien: alleen als je ECHT stilstaat. In km/l is stapvoets rijden
        // gewoon een laag, leesbaar cijfer (0,1 km/l) -- anders dan in
        // l/100km, waar daar 1000 stond en de drempel dus hoog moest liggen.
        // Zo schakelt het dashboard in de truck ook pas bij stilstand om.
        static constexpr double PER100_MIN_KMH = 0.1;


        // Gedempte (EMA) uitkomsten. Het venster hieronder is kort genoeg om
        // meteen te reageren, maar daardoor ook onrustig; deze demping maakt
        // er een leesbaar cijfer van. Bij een GROTE sprong -- gas los,
        // remmen, stilstand -- springt hij direct over in plaats van traag
        // uit te dempen, precies zoals Gladstrijken() dat bij de
        // aankomsttijd doet. Zonder dat zou het cijfer na het remmen nog
        // seconden op je rijdende verbruik blijven staan.
        double m_gladLiterPerUur = -1.0;   // per ECHT uur; omrekenen gebeurt bij weergave
        double m_gladVerbruikNu = -1.0;    // l/100km
        static double Demp( double huidig, double nieuw, double dtSec, double tauSec );

        // Hoe lang een verandering er ongeveer over doet om door te werken.
        // Samen met het venster hierboven bepaalt dit hoe snel het cijfer
        // bijtrekt. Vier seconden venster plus vier seconden demping is
        // ongeveer acht tellen -- genoeg om heuvels weg te middelen, kort
        // genoeg dat je optrekwaarde niet blijft plakken.
        static constexpr double DEMP_TAU_SECONDEN = 0.5;

        // Apart, KORT venster voor het l/uur-cijfer. Dat heeft geen afstand
        // nodig, dus het hoeft niet mee te lopen met het lange venster van
        // l/100km. Met het lange venster bleef na het stilzetten nog 15
        // seconden aan rijgegevens meetellen; er stond "94,1 l/uur" terwijl
        // de motor UIT was (gemeten 30-08).
        static constexpr double LUUR_VENSTER_SECONDEN = 0.8;

        // En een snellere demping erbij: zet je de motor uit, dan moet het
        // cijfer binnen een paar tellen op nul staan, niet er langzaam
        // naartoe kruipen.
        static constexpr double LUUR_TAU_SECONDEN = 0.3;

        // Reed hij de vorige meting? Bij de overgang rijden <-> stilstaan
        // wordt de demping overgeslagen, zodat het cijfer meteen omklapt in
        // plaats van er nog seconden overheen te doen.
        bool m_vorigRijdt = false;

        // Throttle voor de verbruikregel in debug.log.
        std::chrono::steady_clock::time_point m_laatsteVerbruikLog;

        // Eigen venstertijd voor het verbruik. KORT mag: de afstand komt
        // sinds versie 128 uit snelheid x tijd en is dus perfect glad -- het
        // lange venster was er alleen voor de grove kilometerstand, die we
        // niet meer gebruiken. En het brandstofkanaal zelf loopt netjes:
        // stationair zakt de tank elke 3 seconden exact 0,0051 liter
        // (gemeten 30-08). Een lang venster hield vooral je optrekwaarde
        // seconden lang in beeld. De demping doet het gladstrijken.
        // BEWUST een eigen constante: VENSTER_SECONDEN hoort bij m_kmVenster
        // en dus bij de IRL-aankomsttijd -- daar blijven we vanaf.
        static constexpr double VERBRUIK_VENSTER_SECONDEN = 1.0;

        // Minimale tijdspanne voor een bruikbare meting. Hieronder is het
        // literverschil zo klein dat je vooral de afrondingsstapjes van het
        // spel meet. 0,3 seconde is ongeveer de bodem: stationair verbrand
        // je dan nog 0,0005 liter, en dat is net genoeg boven de precisie
        // van het brandstofkanaal.
        static constexpr double MIN_SPAN_SECONDEN = 0.3;

        static constexpr double VENSTER_SECONDEN = 180.0;
    };
}

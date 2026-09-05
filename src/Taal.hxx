#pragma once
// Taal.hxx
//
// Two languages: Dutch and English. Dutch is the BASE -- the texts are
// simply in the code as they always were, and those are shown. English is
// looked up in the table below.
//
// Why this way, and not with codes like "SETTINGS_TITLE":
//
//   1. The code stays readable. TekstGedimd( T( "km te gaan" ) ) says
//      right away what is shown; TekstGedimd( T( "STR_KM_LEFT" ) ) does not.
//   2. NOTHING CAN BREAK. If a text is not in the table, the Dutch comes
//      out. Never an empty line or a bare code on your HUD.
//   3. New texts work immediately; translating can come later.
//
// Adding a third language is one more table and one line in Kies().

#include <string>
#include <unordered_map>

namespace Ritten
{
    enum class TaalKeuze
    {
        Nederlands = 0,
        Engels     = 1,
    };

    class Taal
    {
    public:
        static TaalKeuze Huidig() { return Gekozen(); }
        static void Kies( TaalKeuze keuze ) { Gekozen() = keuze; }

        // The translation, or the Dutch if there is no translation.
        static const char *Vertaal( const char *nederlands )
        {
            if( Gekozen() == TaalKeuze::Nederlands || nederlands == nullptr ) return nederlands;
            const auto &tabel = Engels();
            const auto it = tabel.find( nederlands );
            return ( it == tabel.end() ) ? nederlands : it->second;
        }

    private:
        static TaalKeuze &Gekozen()
        {
            static TaalKeuze keuze = TaalKeuze::Nederlands;
            return keuze;
        }

        static const std::unordered_map<std::string, const char *> &Engels()
        {
            static const std::unordered_map<std::string, const char *> tabel = {
                // --- Tabs and headers ------------------------------------
                { "Live",                       "Live" },
                { "Boordcomputer",              "Dashboard" },
                { "Spelers",                    "Players" },
                { "Geschiedenis",               "History" },
                { "Statistieken",               "Statistics" },
                { "Incident / Replay",          "Incident / Replay" },
                { "VTC",                        "VTC" },
                { "VTC-instellingen",           "VTC settings" },
                { "Instellingen",               "Settings" },
                { "BOORDCOMPUTER",              "DASHBOARD" },
                { "MIJN VTC",                   "MY VTC" },
                { "INCIDENT VASTGELEGD",        "INCIDENT RECORDED" },

                // --- Cards on Live and Board computer ---------------------
                { "SNELHEID",                   "SPEED" },
                { "BRANDSTOF",                  "FUEL" },
                { "CRUISE",                     "CRUISE" },
                { "CRUISE CONTROL",             "CRUISE CONTROL" },
                { "BEREIK",                     "RANGE" },
                { "VERBRUIK",                   "CONSUMPTION" },
                { "KM-STAND",                   "ODOMETER" },
                { "SCHADE",                     "DAMAGE" },
                { "AANHANGER",                  "TRAILER" },
                { "TACHOGRAAF",                 "TACHOGRAPH" },
                { "ONDERWEG",                   "ON THE ROAD" },
                { "BUSLIJN",                    "BUS LINE" },
                { "UIT",                        "OFF" },
                { "km/h",                       "km/h" },
                { "km te gaan",                 "km to go" },
                { "km totaal",                  "km total" },
                { "l/uur",                      "l/hour" },
                { "l/uur - stationair",         "l/hour - idling" },
                { "l/uur stat.",                "l/hour idle" },
                { "km/l",                       "km/l" },
                { "gereden",                    "driven" },
                { "chauffeurs",                 "drivers" },
                { "Chassis",                    "Chassis" },
                { "Motor",                      "Engine" },
                { "Bak",                        "Transmission" },
                { "Cabine",                     "Cabin" },
                { "Wielen",                     "Wheels" },
                { "Trailer",                    "Trailer" },
                { "Lading",                     "Cargo" },
                { "Alleen lading telt",         "Only cargo counts" },

                // --- Players ---------------------------------------------
                { "team/mod",                   "team/mod" },
                { "patron",                     "patron" },
                { "speler",                     "player" },
                { "eigen VTC",                  "own VTC" },
                { "MOD",                        "MOD" },
                { "TEAM",                       "TEAM" },
                { "PATRON",                     "PATRON" },
                { "LANG",                       "LONG" },
                { "Jouw VTC",                   "Your VTC" },
                { "Boven is recht vooruit. Peiling en rijrichting komen uit de voertuigposities.",
                  "Up is straight ahead. Bearing and heading come from vehicle positions." },

                // --- VTC --------------------------------------------------
                { "BEDRIJF",                    "COMPANY" },
                { "LEDEN",                      "MEMBERS" },
                { "EIGEN CONVOOIEN",            "OWN CONVOYS" },
                { "MELDT ZICH AAN VOOR",        "ATTENDING" },
                { "NIEUWS",                     "NEWS" },
                { "JOUW PLANNING",              "YOUR SCHEDULE" },
                { "Nog niet ingesteld",
                  "Not set up yet" },
                { "MIJN CONVOOIEN",             "MY CONVOYS" },
                { "VERBINDING",                 "CONNECTION" },
                { "WEERGAVE",                   "DISPLAY" },
                { "MARKEREN",                   "HIGHLIGHTING" },
                { "Zelf aanvinken",             "Pick them myself" },
                { "Vinkjes bij Statistieken",   "Checkboxes under Statistics" },
                { "Werkt zonder VTC",           "Works without a VTC" },
                { "VTC-gegevens ophalen",       "Fetch VTC data" },
                { "VTC-nummer",                 "VTC number" },
                { "Tag bij spelers tonen",      "Show tag with players" },
                { "Convooien tonen",            "Show convoys" },
                { "Eigen VTC markeren",         "Highlight own VTC" },
                { "Spelers opzoeken",           "Look up players" },
                { "Uit = alleen tags",          "Off = tags only" },
                { "Tags",                       "Tags" },
                { "Meerdere met komma's",       "Separate with commas" },
                { "Tot het nummer bekend is",   "Until the number is known" },
                { "Ik ga hier heen",            "I'm attending" },
                { "opgehaald",                  "fetched" },
                { "uit",                        "off" },
                { "wordt opgehaald...",         "fetching..." },
                { "nog niet opgehaald",         "not fetched yet" },
                { "VTC niet gevonden -- klopt het nummer?",
                  "VTC not found -- is the number correct?" },

                // --- Settings --------------------------------------------
                { "UITERLIJK",                  "APPEARANCE" },
                { "BRANDSTOFPRIJS",             "FUEL PRICE" },
                { "DISCORD",                    "DISCORD" },
                { "INCIDENT-RECORDER",          "INCIDENT RECORDER" },
                { "Doorzichtigheid (achtergrond)", "Opacity (background)" },
                { "Doorzichtigheid (zijbalk/logo/menu's)", "Opacity (sidebar/logo/menus)" },
                { "Accentkleur",                "Accent colour" },
                { "Klik het vakje voor meer kleuren",
                  "Click the swatch for more colours" },
                { "Werkwijze",                  "Mode" },
                { "Het spel volgen (11 uur)",   "Follow the game (11 hours)" },
                { "Eigen regels",               "Own rules" },
                { "Rijtijdenwet (ATW)",         "EU driving time rules" },
                { "Gelijk met de P-teller in het spel",
                  "Matches the P counter in the game" },
                { "Tijdschaal",                 "Time scale" },
                { "Automatisch meten",          "Measure automatically" },
                { "TruckersMP (6)",             "TruckersMP (6)" },
                { "Singleplayer (19)",          "Singleplayer (19)" },
                { "Land",                       "Country" },
                { "Eigen prijs hieronder",      "Own price below" },
                { "EUR per liter",              "EUR per litre" },
                { "Voor het omrekenen naar kosten",
                  "Used to convert usage to cost" },
                { "Direct opgeslagen in instellingen.json",
                  "Saved directly to instellingen.json" },
                { "NORMAAL",                    "NORMAL" },
                { "STATIONAIR",                 "IDLE" },
                { "Zuinig",                     "Economical" },
                { "Gewoon",                     "Normal" },
                { "Sportief",                   "Sporty" },
                { "Rijstijlregel tonen",        "Show driving style line" },
                { "Rit t.o.v. je gemiddelde",   "This trip vs your average" },
                { "Meldingen aan",              "Notifications on" },
                { "Plakken",                    "Paste" },
                { "Testbericht sturen",         "Send test message" },
                { "Taal",                       "Language" },
                { "Nederlands",                 "Dutch" },
                { "Engels",                     "English" },

                // --- Other -----------------------------------------------
                { "Alles",                      "All" },
                { "Vracht",                     "Freight" },
                { "Bus",                        "Bus" },
                { "Voltooid",                   "Completed" },
                { "Geannuleerd",                "Cancelled" },
                { "Insert = verbergen | Rechts = muis",
                  "Insert = hide | Right = mouse" },
                { "Uitgebreid logboek",         "Verbose logging" },
                { "GEMIDDELDE",                 "AVERAGE" },
                { "Reset gemiddelde",           "Reset average" },
                { "Druk samen met de reset op je dashboard",
                  "Press together with your dashboard reset" },
                { "Klok tonen (echte tijd)",    "Show clock (real time)" },
                { "Milliseconden erbij zodra het uitgebreide logboek aanstaat",
                  "Milliseconds are added when verbose logging is on" },
                { "Logregel elke (sec)",        "Log line every (sec)" },
                { "Alleen bij het zoeken van een fout",
                  "Only when tracking down a problem" },

                // --- Sixth round: times, tachograph bars, report window
                { "onbekend",                   "unknown" },
                { " uur ",                      "h " },
                { "voltooid",                   "done" },
                { "eerstvolgende",              "next" },
                { "nog te gaan",                "still to go" },
                { "rust",                       "rest" },
                { "pauze",                      "break" },
                { "dag",                        "day" },
                { "Reden(en): ",                "Reason(s): " },
                { "(nog niet ingevuld)",        "(not filled in yet)" },

                // --- Fifth round: everything that remained ----------------
                { " (jouw klok)",               " (your clock)" },
                { "Tol",                        "Toll" },
                { "Veerboot",                   "Ferry" },
                { "Trein",                      "Train" },
                { "BUSLIJN ONDERWEG",           "BUS LINE ON THE ROAD" },
                { "CC",                         "CC" },
                { "Boetes     EUR %lld",        "Fines      EUR %lld" },
                { "%.0f min over de eindtijd",  "%.0f min past the deadline" },
                { "-%.0f%% van de uitbetaling", "-%.0f%% of the payout" },
                { "Achter op schema, nog ",     "Behind schedule, " },
                { " min speling voor de boete ingaat.",
                  " min left before the penalty kicks in." },
                { "limiet %.0f",                "limit %.0f" },
                { "EUR ",                       "EUR " },
                { "   bij ",                    "   at " },
                { " km",                        " km" },
                { "pauze verplicht (9 uur rust)", "break required (9 hours rest)" },
                { "Alleen ladingsc...",         "Cargo only..." },
                { " speler(s) zonder positie -- niet op de kaart getekend.",
                  " player(s) without a position -- not drawn on the map." },
                { "%d collega",                 "%d colleague" },
                { "%d collega's",               "%d colleagues" },
                { "%.2f uur",                   "%.2f hours" },
                { "Servers: ",                  "Servers: " },
                { "  limiter",                  "  limiter" },
                { "Convooi over ",              "Convoy in " },
                { " min",                       " min" },
                { "Opgezocht: %d  |  in de rij: %d",
                  "Looked up: %d  |  queued: %d" },
                { "Invullen: ",                 "Fill in: " },
                { "De plugin kan dit NIET automatisch versturen (de SDK geeft",
                  "The plugin can NOT submit this automatically (the SDK does" },
                { "geen Report-functie door). Onderstaande knop bereidt alles",
                  "not expose a report function). The button below prepares" },
                { "voor op je klembord en opent de website -- verzenden doe jij zelf.",
                  "everything on your clipboard and opens the site -- you submit it." },
                { "(nog geen reden aangevinkt)", "(no reason ticked yet)" },

                // --- Fourth round: Live, Board computer, VTC, right-click menu
                { "RESTEREND (IRL)",            "REMAINING (IRL)" },
                { "BRANDSTOFKOSTEN",            "FUEL COST" },
                { "Aankomst rond ",             "Arriving around " },
                { " tot pauze",                 " until break" },
                { "Aan het rusten",             "Resting" },
                { "tag ",                       "tag " },
                { "Open Steam-profiel",         "Open Steam profile" },
                { "Open TruckersMP-profiel",    "Open TruckersMP profile" },
                { "Kopieer TruckersMP-ID",      "Copy TruckersMP ID" },
                { "Rapporteer speler...",       "Report player..." },

                // --- Third round: dropdowns, tables, server list ----------
                { "%d spelers in bereik",       "%d players nearby" },
                { " spelers",                   " players" },
                { "  wachtrij ",                "  queue " },
                { "  botsingen",                "  collisions" },
                { "  geen botsingen",           "  no collisions" },
                { "Alles",                      "All" },
                { "Vracht",                     "Freight" },
                { "Bus",                        "Bus" },
                { "Voltooid",                   "Completed" },
                { "Geannuleerd",                "Cancelled" },
                { "VRACHTRITTEN",               "FREIGHT TRIPS" },
                { "BUSLIJNRITTEN",              "BUS TRIPS" },
                { "VRACHT",                     "FREIGHT" },
                { "BUS",                        "BUS" },
                { "AFSTAND",                    "DISTANCE" },
                { "VERDIEND",                   "EARNED" },
                { "NETTO",                      "NET" },
                { "Aaneengesloten rijden",      "Continuous driving" },
                { "Pauzeduur",                  "Break length" },
                { "Rijtijd per dag",            "Driving time per day" },
                { "Dagelijkse rust",            "Daily rest" },
                { "Vast. Kies Eigen regels om te wijzigen",
                  "Fixed. Pick Own rules to change" },

                // --- Second round: card labels and format texts -----------
                { "%s | %.0f km gepland",       "%s | %.0f km planned" },
                { "EUR %.2f",                   "EUR %.2f" },
                { "-EUR %.2f",                  "-EUR %.2f" },
                { "halte %d / %d",              "stop %d / %d" },
                { "Buslijn -- %d haltes",       "Bus line -- %d stops" },
                { "Onderweg: %s",               "On the road: %s" },
                { "Onderweg: %s -- pauze telt niet mee",
                  "On the road: %s -- pauses not counted" },
                { "%s -- %.0f km -- rond %s",   "%s -- %.0f km -- around %s" },
                { "%s -- %.0f km vanaf start",  "%s -- %.0f km from the start" },
                { "Geschatte uitbetaling: %lld", "Estimated payout: %lld" },
                { "Tol        EUR %lld",        "Toll       EUR %lld" },
                { "Veerboot   EUR %lld",        "Ferry      EUR %lld" },
                { "Trein      EUR %lld",        "Train      EUR %lld" },
                { "Eerste uur gratis, daarna 0,333%/min",
                  "First hour free, then 0.333%/min" },
                { "Nickname: %s",               "Nickname: %s" },
                { "SteamID64: %s",              "SteamID64: %s" },
                { "TruckersMP ID: %s",          "TruckersMP ID: %s" },
                { "Omschrijving",               "Description" },
                { "Bewijs-link (video, verplicht bij de meeste categorieen)",
                  "Evidence link (video, required for most categories)" },
                { "(%d op de kaart)",           "(%d on the map)" },
                { "Lange combinatie: %.0f m aanhanger",
                  "Long combination: %.0f m trailer" },
                { "Uit brandstofprijzen.json: EUR %.2f",
                  "From brandstofprijzen.json: EUR %.2f" },
                { "Buffer-lengte (min)",        "Buffer length (min)" },
                { "Getankt: %dx, %.0f liter, EUR %.0f",
                  "Fuelled: %dx, %.0f litres, EUR %.0f" },
                { "EUR %.2f per liter (zelf ingesteld)",
                  "EUR %.2f per litre (set by you)" },
                { "Vermoedelijk betrokken: %s", "Likely involved: %s" },
                { "Spelers op dit moment (%d):", "Players at that moment (%d):" },
                { "Nu aangevinkt: %d",          "Currently ticked: %d" },

                // --- Fuel price from position (05-09) ----------------------
            { "Land automatisch (positie)",  "Country automatic (position)" },
            { "%s, %s: %.2f%s",              "%s, %s: %.2f%s" },
            { "Nu: geen positie, eigen prijs", "Now: no position, own price" },
            { " garage",                     " garage" },
            { "Kaartgegevens bijwerken via internet", "Update map data via internet" },

            // --- Added after converting the code ----------------------
                { "--",                         "--" },
                { "Totaal",                     "Total" },
                { "Gewicht",                    "Weight" },
                { "Boetes",                     "Fines" },
                { "TE LAAT",                    "LATE" },
                { "VOLGENDE HALTE",             "NEXT STOP" },
                { "GETANKT DEZE RIT",           "REFUELLED THIS TRIP" },
                { "ONKOSTEN DEZE RIT",          "EXPENSES THIS TRIP" },
                { "SERVERS (ETS2)",             "SERVERS (ETS2)" },
                { "TRUCKERSMP LIVE",            "TRUCKERSMP LIVE" },
                { "AANKOMENDE EVENEMENTEN",     "UPCOMING EVENTS" },
                { "Niet gekoppeld",             "Not linked" },
                { "Steam-profiel onbekend",     "Steam profile unknown" },
                { "TruckersMP-ID onbekend",     "TruckersMP ID unknown" },
                { "truckersmp.com/vtc/<nummer>", "truckersmp.com/vtc/<number>" },
                { "Geen actieve rit",
                  "No active trip" },
                { "Geen data voor dit moment.", "No data for this moment." },
                { "Nog geen ritten gelogd.",    "No trips logged yet." },
                { "Nog geen incident vastgelegd.", "No incident recorded yet." },
                { "Vult alleen bij een botsing",
                  "Only fills after a collision" },
                { "dichtstbijzijnde bij de schade -- geen bewijs",
                  "closest at the time of damage -- not proof" },
                { "Voorbereiden + report-pagina openen",
                  "Prepare and open the report page" },
                { "Bedragen uit het spel zelf",
                  "Amounts from the game itself" },
                { "Uitbetaling niet doorgegeven",
                  "Payout not provided" },
                { "Wacht op navigatiedata",     "Waiting for navigation data" },
                { "Servergegevens en evenementen ophalen",
                  "Fetch server status and events" },
                { "Stuurt een bericht naar een Discord-webhook zodra een rit is afgerond of geannuleerd.",
                  "Sends a message to a Discord webhook when a trip finishes or is cancelled." },
                { "Webhook-URL (Discord: kanaal bewerken > Integraties > Webhooks)",
                  "Webhook URL (Discord: edit channel > Integrations > Webhooks)" },
                { "Bewaarde data, bevriest bij schade",
                  "Data kept, frozen on damage" },
                { "Eigen tachograaf, niet die van ETS2",
                  "Your own tachograph, not the game one" },
                { "Spelminuten per echte minuut", "Game minutes per real minute" },
                { "Boven is vooruit. Wegen zijn decoratief.",
                  "Up is forward. Roads are decorative." },

            };
            return tabel;
        }
    };

    // Short in use: T( "km te gaan" )
    inline const char *T( const char *nederlands ) { return Taal::Vertaal( nederlands ); }
}

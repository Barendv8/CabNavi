#pragma once
// Taal.hxx
//
// Twee talen: Nederlands en Engels. Nederlands is de BASIS -- de teksten
// staan gewoon in de code zoals ze altijd stonden, en die worden ook
// getoond. Engels wordt opgezocht in de tabel hieronder.
//
// Waarom zo, en niet met codes als "SETTINGS_TITLE":
//
//   1. De code blijft leesbaar. TekstGedimd( T( "km te gaan" ) ) zegt
//      meteen wat er staat; TekstGedimd( T( "STR_KM_LEFT" ) ) niet.
//   2. ER KAN NIETS STUK. Staat een tekst niet in de tabel, dan komt het
//      Nederlands eruit. Nooit een lege regel of een losse code op je HUD.
//   3. Nieuwe teksten werken meteen; vertalen kan later.
//
// Een derde taal toevoegen is een tabel erbij en een regel in Kies().

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

        // De vertaling, of het Nederlands als er geen vertaling is.
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
                // --- Tabbladen en koppen ---------------------------------
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

                // --- Kaartjes op Live en Boordcomputer -------------------
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
                { "ONDERWEG",                   "EN ROUTE" },
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

                // --- Spelers ---------------------------------------------
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
                { "Nog niet ingesteld -- zie het tabblad hiernaast.",
                  "Not set up yet -- see the tab next to this one." },
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

                // --- Instellingen ----------------------------------------
                { "UITERLIJK",                  "APPEARANCE" },
                { "BRANDSTOFPRIJS",             "FUEL PRICE" },
                { "DISCORD",                    "DISCORD" },
                { "INCIDENT-RECORDER",          "INCIDENT RECORDER" },
                { "Doorzichtigheid (achtergrond)", "Opacity (background)" },
                { "Doorzichtigheid (zijbalk/logo/menu's)", "Opacity (sidebar/logo/menus)" },
                { "Accentkleur",                "Accent colour" },
                { "Klik op het kleurvakje voor de volledige kleurenkiezer.",
                  "Click the colour swatch for the full picker." },
                { "Werkwijze",                  "Mode" },
                { "Het spel volgen (11 uur)",   "Follow the game (11 hours)" },
                { "Eigen regels",               "Own rules" },
                { "Rijtijdenwet (ATW)",         "EU driving time rules" },
                { "Loopt gelijk met de P-teller in je Route Advisor.",
                  "Matches the P counter in your Route Advisor." },
                { "Tijdschaal",                 "Time scale" },
                { "Automatisch meten",          "Measure automatically" },
                { "TruckersMP (6)",             "TruckersMP (6)" },
                { "Singleplayer (19)",          "Singleplayer (19)" },
                { "Land",                       "Country" },
                { "Eigen prijs hieronder",      "Own price below" },
                { "EUR per liter",              "EUR per litre" },
                { "Wordt gebruikt om je verbruik om te rekenen naar kosten.",
                  "Used to turn your consumption into costs." },
                { "Wijziging wordt direct opgeslagen in",
                  "Changes are saved immediately to" },
                { "Direct opgeslagen in instellingen.json",
                  "Saved directly to instellingen.json" },
                { "Zuinigheidsregel tonen",     "Show efficiency line" },
                { "Rit t.o.v. je gemiddelde",   "This trip vs your average" },
                { "Meldingen aan",              "Notifications on" },
                { "Plakken",                    "Paste" },
                { "Testbericht sturen",         "Send test message" },
                { "Taal",                       "Language" },
                { "Nederlands",                 "Dutch" },
                { "Engels",                     "English" },

                // --- Overig ----------------------------------------------
                { "Alles",                      "All" },
                { "Vracht",                     "Freight" },
                { "Bus",                        "Bus" },
                { "Voltooid",                   "Completed" },
                { "Geannuleerd",                "Cancelled" },
                { "Insert = verbergen | Rechtermuisklik = muis aan/uit",
                  "Insert = hide | Right click = toggle mouse" },
                { "Uitgebreid logboek",         "Verbose logging" },
                { "Alleen aanzetten bij het uitzoeken van een probleem",
                  "Only enable while investigating a problem" },

                // --- Zesde ronde: tijden, tachograafbalken, rapportvenster
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

                // --- Vijfde ronde: alles wat nog resteerde ----------------
                { " (jouw klok)",               " (your clock)" },
                { "Tol",                        "Toll" },
                { "Veerboot",                   "Ferry" },
                { "Trein",                      "Train" },
                { "BUSLIJN ONDERWEG",           "BUS LINE EN ROUTE" },
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

                // --- Vierde ronde: Live, Boordcomputer, VTC, rechtsklikmenu
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
                { "Deze rit %.0f%% zuiniger dan je gemiddelde (%.1f tegen %.1f km/l)",
                  "This trip %.0f%% more efficient than your average (%.1f vs %.1f km/l)" },
                { "Deze rit %.0f%% onzuiniger dan je gemiddelde (%.1f tegen %.1f km/l)",
                  "This trip %.0f%% less efficient than your average (%.1f vs %.1f km/l)" },
                { "Deze rit gelijk aan je gemiddelde (%.1f km/l)",
                  "This trip matches your average (%.1f km/l)" },

                // --- Derde ronde: keuzelijsten, tabellen, serverlijst -----
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
                { "AFSTAND",                    "DISTANCE" },
                { "VERDIEND",                   "EARNED" },
                { "NETTO",                      "NET" },
                { "Aaneengesloten rijden",      "Continuous driving" },
                { "Pauzeduur",                  "Break length" },
                { "Rijtijd per dag",            "Driving time per day" },
                { "Dagelijkse rust",            "Daily rest" },
                { "Vaste waarden. Kies \"Eigen regels\" om ze aan te passen.",
                  "Fixed values. Choose \"Own rules\" to change them." },

                // --- Tweede ronde: kaartlabels en formatteksten -----------
                { "%s | %.0f km gepland",       "%s | %.0f km planned" },
                { "EUR %.2f",                   "EUR %.2f" },
                { "-EUR %.2f",                  "-EUR %.2f" },
                { "halte %d / %d",              "stop %d / %d" },
                { "Buslijn -- %d haltes",       "Bus line -- %d stops" },
                { "Onderweg: %s",               "En route: %s" },
                { "Onderweg: %s -- schatting o.b.v. recente snelheid, telt niet door tijdens pauze",
                  "En route: %s -- estimate based on recent speed, paused while the game is" },
                { "%s -- %.0f km -- rond %s",   "%s -- %.0f km -- around %s" },
                { "%s -- %.0f km vanaf start",  "%s -- %.0f km from the start" },
                { "Geschatte uitbetaling: %lld", "Estimated payout: %lld" },
                { "Tol        EUR %lld",        "Toll       EUR %lld" },
                { "Veerboot   EUR %lld",        "Ferry      EUR %lld" },
                { "Trein      EUR %lld",        "Train      EUR %lld" },
                { "Eerste uur vertraging is gratis; daarna 0,333% per minuut.",
                  "The first hour of delay is free; after that 0.333% per minute." },
                { "Nickname: %s",               "Nickname: %s" },
                { "SteamID64: %s",              "SteamID64: %s" },
                { "TruckersMP ID: %s",          "TruckersMP ID: %s" },
                { "Omschrijving",               "Description" },
                { "Bewijs-link (video, verplicht bij de meeste categorieen)",
                  "Evidence link (video, required for most categories)" },
                { "(%d op de kaart)",           "(%d on the map)" },
                { "Lange combinatie: %.0f m aanhanger",
                  "Long combination: %.0f m trailer" },
                { "Prijs uit brandstofprijzen.json: EUR %.2f per liter",
                  "Price from brandstofprijzen.json: EUR %.2f per litre" },
                { "Buffer-lengte (min)",        "Buffer length (min)" },
                { "Getankt sinds opstarten: %dx, %.0f liter, ongeveer EUR %.0f",
                  "Refuelled since startup: %dx, %.0f litres, about EUR %.0f" },
                { "Gerekend met EUR %.2f per liter (zelf ingesteld -- het spel geeft"
                  " de pompprijs niet door).",
                  "Calculated at EUR %.2f per litre (set by you -- the game does not"
                  " report the pump price)." },
                { "Vermoedelijk betrokken: %s", "Likely involved: %s" },
                { "Spelers op dit moment (%d):", "Players at that moment (%d):" },
                { "Nu aangevinkt: %d",          "Currently ticked: %d" },

                // --- Aangevuld na het omzetten van de code ---------------
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
                { "Geen actieve rit. Start een vrachtjob of buslijn.",
                  "No active trip. Start a freight job or bus line." },
                { "Geen data voor dit moment.", "No data for this moment." },
                { "Nog geen ritten gelogd.",    "No trips logged yet." },
                { "Nog geen incident vastgelegd.", "No incident recorded yet." },
                { "Zodra je schade oploopt (bv. een botsing), bevriest de plugin",
                  "As soon as you take damage (a collision for instance), the plugin freezes" },
                { "automatisch de laatste paar minuten aan spelersdata hierin.",
                  "the last few minutes of player data in here automatically." },
                { "(dichtstbijzijnde speler op het moment van schade -- geen garantie dat dit de dader is)",
                  "(closest player at the moment of damage -- no guarantee this is the one at fault)" },
                { "Voorbereiden + report-pagina openen",
                  "Prepare and open the report page" },
                { "Echte in-game bedragen, gemeld door het spel zelf.",
                  "Real in-game amounts, reported by the game itself." },
                { "Geschatte uitbetaling: niet doorgegeven bij ritstart",
                  "Estimated payout: not provided at trip start" },
                { "Vertraging nog niet te bepalen -- wacht op navigatiedata en een stukje rijden.",
                  "Delay cannot be determined yet -- waiting on navigation data and some driving." },
                { "Servergegevens en evenementen ophalen",
                  "Fetch server status and events" },
                { "Stuurt een bericht naar een Discord-webhook zodra een rit is afgerond of geannuleerd.",
                  "Sends a message to a Discord webhook when a trip finishes or is cancelled." },
                { "Webhook-URL (Discord: kanaal bewerken > Integraties > Webhooks)",
                  "Webhook URL (Discord: edit channel > Integrations > Webhooks)" },
                { "Hoeveel minuten spelersdata continu bewaard blijft (bevriest bij schade).",
                  "How many minutes of player data are kept (freezes on damage)." },
                { "Dit is JOUW tachograaf, niet die van het spel."
                  " ETS2 blijft zijn eigen elf uur hanteren.",
                  "This is YOUR tachograph, not the game's."
                  " ETS2 keeps using its own eleven hours." },
                { "Bepaalt hoeveel spelminuten er in een echte minuut gaan."
                  " Laat op automatisch staan tenzij de aankomsttijd er structureel naast zit.",
                  "Sets how many game minutes fit in a real minute."
                  " Leave on automatic unless arrival times are structurally off." },
                { "Boven is recht vooruit. Peiling en rijrichting komen uit de"
                  " voertuigposities van de SDK. Het wegenpatroon is decoratief:"
                  " het volgt je snelheid, maar is geen echte kaart.",
                  "Up is straight ahead. Bearing and heading come from the SDK's"
                  " vehicle positions. The road pattern is decorative:"
                  " it follows your speed but is not a real map." },

            };
            return tabel;
        }
    };

    // Kort in het gebruik: T( "km te gaan" )
    inline const char *T( const char *nederlands ) { return Taal::Vertaal( nederlands ); }
}

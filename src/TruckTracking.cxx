#include "TruckTracking.hxx"

#include "BusTracking.hxx"
#include "IncidentRecorder.hxx"
#include "PlayersNearby.hxx"
#include "Logboek.hxx"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Ritten
{
    static std::string NuAlsIso()
    {
        auto nu = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t( nu );
        std::tm tmBuf{};
#if defined( _WIN32 )
        gmtime_s( &tmBuf, &t );
#else
        gmtime_r( &t, &tmBuf );
#endif
        std::ostringstream ss;
        ss << std::put_time( &tmBuf, "%Y-%m-%dT%H:%M:%SZ" );
        return ss.str();
    }

    // Helper: haal een attribuut op uit een scs_named_value_t-lijst op naam.
    // Geeft nullptr terug als het niet gevonden is.
    static const scs_named_value_t *ZoekAttribuut( const scs_named_value_t *attributes, const char *naam )
    {
        for( const scs_named_value_t *attr = attributes; attr->name != nullptr; ++attr )
        {
            if( std::strcmp( attr->name, naam ) == 0 )
            {
                return attr;
            }
        }
        return nullptr;
    }

    namespace
    {
        // %APPDATA%\CabNavi\tachograaf.json -- naast de andere
        // instellingen, zodat een spel-update het niet opruimt.
        std::filesystem::path TachoPad()
        {
            std::filesystem::path pad;
            if( const char *appdata = std::getenv( "APPDATA" ) ) pad = appdata;
            else pad = std::filesystem::current_path();
            pad /= "CabNavi";
            std::error_code ec;
            std::filesystem::create_directories( pad, ec );
            return pad / "tachograaf.json";
        }

        // De GEMETEN afstandsfactor onthouden tussen sessies. Zonder dit
        // begint de plugin elke start op de standaardwaarde en klopt het
        // stationaire l/uur pas nadat je een stuk gereden hebt -- gemeten
        // 31-08: 1,0 na een verse start, 2,0 zodra de factor gemeten was,
        // bij precies hetzelfde verbruik.
        //
        // LET OP: dit verandert NIETS aan de berekening zelf. Het enige
        // verschil is dat de startwaarde uit de vorige sessie komt in
        // plaats van uit een aanname.
        std::filesystem::path MetingPad()
        {
            std::filesystem::path pad;
            if( const char *appdata = std::getenv( "APPDATA" ) ) pad = appdata;
            else pad = std::filesystem::current_path();
            pad /= "CabNavi";
            std::error_code ec;
            std::filesystem::create_directories( pad, ec );
            return pad / "meting.txt";
        }
    }

    // De laatst gemeten afstandsfactor terughalen. Ontbreekt het bestand,
    // dan blijft de standaardwaarde staan en gedraagt alles zich als
    // voorheen.
    void TruckTracking::LaadMeting()
    {
        // ALTIJD melden wat hier gebeurt, ook als er niets te lezen valt.
        // Anders weet je bij een lege uitkomst niet of het bestand ontbrak,
        // of dat de waarde is afgekeurd, of dat deze functie niet eens is
        // aangeroepen -- en dan zit je te gissen (gemeten 31-08).
        try
        {
            const std::filesystem::path pad = MetingPad();
            std::ifstream in( pad );
            if( !in )
            {
                Logboek::Schrijf( "start", "meting.txt niet gevonden op "
                                               + pad.string() + " -- standaard "
                                               + std::to_string( m_afstandsFactor ) );
                return;
            }
            double f = 0.0;
            in >> f;
            if( f > AFSTANDSFACTOR_MIN && f < AFSTANDSFACTOR_MAX )
            {
                m_afstandsFactor = f;
                m_bewaardeFactor = f;
                Logboek::Schrijf( "start", "afstandsfactor uit vorige sessie: "
                                               + std::to_string( f ) );
            }
            else
            {
                Logboek::Schrijf( "start", "afstandsfactor in meting.txt afgekeurd: "
                                               + std::to_string( f ) );
            }
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "start", std::string( "meting.txt niet te lezen: " ) + ex.what() );
        }
        catch( ... )
        {
            Logboek::Schrijf( "start", "meting.txt niet te lezen (onbekende fout)" );
        }
    }

    TruckTracking::TruckTracking( TripLogger &logger, FuelCosts &brandstof )
        : m_logger( logger ), m_brandstof( brandstof )
    {
        LaadTachoStand();
        LaadMeting();
    }

    void TruckTracking::LaadTachoStand()
    {
        std::ifstream in( TachoPad() );
        if( !in ) return;

        // Twee getallen: resterende spelminuten en de periodelengte. Geen
        // JSON-bibliotheek nodig, en niets dat stuk kan door een half
        // geschreven bestand.
        double rest = -1.0;
        double periode = 0.0;
        if( in >> rest >> periode )
        {
            if( rest >= 0.0 ) m_teHerstellenRest = rest;
            if( periode > 60.0 ) m_rustPeriodeMax = periode;
        }

        // De tacho-instelling staat op dezelfde regel erachter. Ontbreekt hij
        // (bestand van een oudere versie), dan blijft de standaardstand staan.
        int stand = 0;
        double maxRij = 0.0, pauze = 0.0, maxDag = 0.0, rust = 0.0;
        if( in >> stand >> maxRij >> pauze >> maxDag >> rust )
        {
            if( stand >= 0 && stand <= 2 ) m_tacho.stand = static_cast<TachoStand>( stand );
            if( maxRij > 10.0 ) m_tacho.maxAaneengeslotenRijden = maxRij;
            if( pauze > 5.0 )   m_tacho.pauzeDuur = pauze;
            if( maxDag > 10.0 ) m_tacho.maxDagRijden = maxDag;
            if( rust > 10.0 )   m_tacho.dagRust = rust;
        }
    }

    void TruckTracking::BewaarTachoStand() const
    {
        std::ofstream uit( TachoPad(), std::ios::trunc );
        if( !uit ) return;
        uit << m_getoondeRestMin << " " << m_rustPeriodeMax << " "
            << static_cast<int>( m_tacho.stand ) << " "
            << m_tacho.maxAaneengeslotenRijden << " " << m_tacho.pauzeDuur << " "
            << m_tacho.maxDagRijden << " " << m_tacho.dagRust << "\n";
        m_laatstBewaardeRest = m_getoondeRestMin;
    }

    void TruckTracking::RegistreerBijTelemetrie( const scs_telemetry_init_params_v101_t *params )
    {
        // Kanaalnamen als platte tekst -- zie de opmerking bovenaan
        // TruckTracking.hxx voor waarom.
        params->register_for_channel(
            "game.time", SCS_U32_NIL, SCS_VALUE_TYPE_u32,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::GameTimeCallback, this );

        // --- MEETPUNT: bestaat er een kanaal voor het huidige land? -------
        //
        // We weten het niet, dus we proberen het gewoon. Elk kanaal dat het
        // spel accepteert komt in debug.log te staan, met de waarde erbij.
        // Levert dit niets op, dan is automatische prijs-per-land niet
        // haalbaar en blijft de handmatige prijs staan -- dan hebben we vijf
        // minuten verloren in plaats van een systeem dat stilletjes verkeerde
        // bedragen toont.
        {
            static const char *kandidaten[] = {
                "game.country",
                "truck.country",
                "truck.navigation.country",
                "game.location.country",
                "job.cargo.source.country",
                "job.cargo.destination.country",
            };
            std::string gevonden;
            for( const char *naam : kandidaten )
            {
                if( params->register_for_channel( naam, SCS_U32_NIL, SCS_VALUE_TYPE_string,
                                                   SCS_TELEMETRY_CHANNEL_FLAG_none,
                                                   &TruckTracking::LandCallback, this ) == SCS_RESULT_ok )
                {
                    if( !gevonden.empty() ) gevonden += ", ";
                    gevonden += naam;
                }
            }
            Logboek::Schrijf( "start", gevonden.empty()
                                            ? "landkanalen: GEEN enkel kanaal geaccepteerd"
                                            : ( "landkanalen geaccepteerd: " + gevonden ) );
        }

        // Resterende rijtijd volgens het spel zelf. Dit WAS de tachograaf-bron;
        // in 1.60 bestaat het kanaal niet meer (gemeten: alle drie de typen
        // geweigerd), dus we tellen zelf. De registratiepoging blijft staan
        // voor het geval SCS het ooit terugbrengt.
        //
        // De EIGEN ETA van het spel naar de bestemming. Dit is verreweg de
        // beste bron: de Route Advisor houdt rekening met de werkelijke
        // route, snelheidslimieten en veerboten -- dingen die wij met een
        // gemiddelde snelheid nooit goed benaderen.
        params->register_for_channel(
            "truck.navigation.time", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::NavigatieTijdCallback, this );

        // Het rustkanaal gaf niets door ("kanaal --" op de HUD). Twee dingen
        // gefixt: we KIJKEN nu of de registratie lukt, en we proberen meerdere
        // typen. Als het spel dit kanaal als u32 of float aanbiedt in plaats
        // van s32, mislukt de registratie geruisloos -- precies wat we zagen.
        m_rustKanaalType = 0;
        if( params->register_for_channel(
                "game.next.rest.stop", SCS_U32_NIL, SCS_VALUE_TYPE_s32,
                SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::RustStopCallback, this ) == SCS_RESULT_ok )
        {
            m_rustKanaalType = 1; // s32
        }
        else if( params->register_for_channel(
                     "game.next.rest.stop", SCS_U32_NIL, SCS_VALUE_TYPE_u32,
                     SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::RustStopCallback, this ) == SCS_RESULT_ok )
        {
            m_rustKanaalType = 2; // u32
        }
        else if( params->register_for_channel(
                     "game.next.rest.stop", SCS_U32_NIL, SCS_VALUE_TYPE_float,
                     SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::RustStopCallback, this ) == SCS_RESULT_ok )
        {
            m_rustKanaalType = 3; // float
        }

        // Vastleggen wat het spel wel en niet aanbiedt. Scheelt bij een
        // volgende afwijking het hele uitzoekwerk: je ziet meteen of een
        // kanaal er is.
        {
            static const char *typeNaam[] = { "GEEN", "s32", "u32", "float" };
            Logboek::Schrijf( "start", std::string( "kanaal game.next.rest.stop -> " )
                                          + typeNaam[ m_rustKanaalType ] );
        }

        params->register_for_channel(
            "truck.speed", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::SnelheidCallback, this );

        params->register_for_channel(
            "truck.fuel.amount", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::BrandstofLitersCallback, this );

        params->register_for_channel(
            "truck.wear.chassis", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::SchadeCallback, this );

        // Nieuw: het spel's EIGEN, al-berekende resterende navigatie-
        // afstand (in meters, volgt de echte weg -- niet hemelsbreed).
        // Veel nauwkeuriger dan onze eigen optelsom via snelheid x tijd,
        // want het spel houdt al rekening met bochten/omwegen. Opgezocht
        // en bevestigd via de officiele SCS SDK-headers (RenCloud's
        // scs-sdk-plugin, publiek op GitHub) -- niet gegokt.
        params->register_for_channel(
            "truck.navigation.distance", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::NavigatieAfstandCallback, this );

        // Puur voor logging/verificatie -- NIET (nog) gebruikt in de
        // berekening. We willen eerst met echte cijfers zien of onze
        // huidige aanpak (afstand / echte snelheid) al klopt, voordat we
        // deze factor er blind bij zouden vermenigvuldigen. Zie de discussie
        // over waarom local_scale de kloktijd regelt, niet je rij-fysica.
        params->register_for_channel(
            "local_scale", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::LocalScaleCallback, this );

        // --- Nieuwe kanalen -------------------------------------------
        // Namen geverifieerd tegen de officiele SCS-header
        // scssdk_telemetry_truck_common_channels.h (via RenCloud's publieke
        // kopie) -- inclusief het feit dat de limiet "truck.navigation.speed.limit"
        // heet met PUNTEN, niet "speed_limit" met een liggend streepje.

        // Resterend bereik in km -- het spel rekent dit zelf al uit op basis
        // van je werkelijke verbruik, dus beter dan onze eigen deling.
        params->register_for_channel(
            "truck.fuel.range", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::BereikCallback, this );

        // LET OP: dit kanaal is liter per KM, niet per 100 km. We rekenen
        // pas om in HuidigeVoertuigStatus(). De SCS-documentatie waarschuwt
        // dat deze waarde in ETS2/ATS vrij statisch is en meer afhangt van
        // je trailer en driver-skills dan van je werkelijke rijgedrag --
        // dus zie het als indicatie, niet als exacte meting.
        params->register_for_channel(
            "truck.fuel.consumption.average", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::VerbruikCallback, this );

        params->register_for_channel(
            "truck.odometer", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::KilometerstandCallback, this );

        // Snelheidslimiet in m/s, zoals de Route Advisor hem toont. Volgt
        // dus ook jouw eigen "Route Advisor speed limit"-instelling in het
        // spel: staat die uit, dan komt hier niets binnen en blijft de
        // waarschuwing gewoon stil.
        params->register_for_channel(
            "truck.navigation.speed.limit", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::SnelheidslimietCallback, this );

        // Ingestelde cruise-snelheid in m/s; 0 betekent uitgeschakeld.
        params->register_for_channel(
            "truck.cruise_control", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::CruiseControlCallback, this );

        // Gaspedaal zoals de simulatie het gebruikt, 0..1. Hiermee weten we
        // METEEN dat je gas hebt losgelaten -- de brandstofmeting merkt dat
        // pas een paar tellen later, want die kijkt terug in de tijd. Bij
        // zo'n omslag gooien we het meetvenster leeg, zodat het cijfer niet
        // nog seconden op je oude rijgedrag blijft staan.
        params->register_for_channel(
            "truck.effective.throttle", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::GaspedaalCallback, this );

        // Schade per onderdeel, los van het chassis dat we al hadden.
        params->register_for_channel(
            "truck.wear.engine", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::SchadeMotorCallback, this );
        params->register_for_channel(
            "truck.wear.transmission", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::SchadeBakCallback, this );
        params->register_for_channel(
            "truck.wear.cabin", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::SchadeCabineCallback, this );
        params->register_for_channel(
            "truck.wear.wheels", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::SchadeWielenCallback, this );

        // Aanhanger. De trailer-kanalen zijn genummerd ("trailer.0.") sinds
        // het spel meerdere opleggers ondersteunt (doubles/triples); we
        // volgen alleen de eerste, want dat is degene met jouw lading erin.
        params->register_for_channel(
            "trailer.0.wear.chassis", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::AanhangerSchadeCallback, this );
        params->register_for_channel(
            "trailer.0.cargo.damage", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::LadingSchadeCallback, this );

        // Vertelt ons wanneer een job start (bron/bestemming/lading/afstand)
        // en wanneer het voertuig zelf verandert (tankinhoud).
        params->register_for_event( SCS_TELEMETRY_EVENT_configuration, &TruckTracking::ConfigCallback, this );

        // Vertelt ons wanneer een job daadwerkelijk AFGEROND wordt (inkomen,
        // schade, op tijd of niet, geannuleerd of niet) -- dit is de
        // betrouwbaarste bron hiervoor, betrouwbaarder dan alleen kijken of
        // de job-configuratie leeg wordt.
        params->register_for_event( SCS_TELEMETRY_EVENT_gameplay, &TruckTracking::GameplayEventCallback, this );

        // Pauze-detectie (zie opmerking bij GepauzeerdCallback in de header) --
        // zelfde events als in het officiele SCS-voorbeeld.
        params->register_for_event( SCS_TELEMETRY_EVENT_paused, &TruckTracking::GepauzeerdCallback, this );
        params->register_for_event( SCS_TELEMETRY_EVENT_started, &TruckTracking::HervatCallback, this );
    }

    // TIJDELIJK: schrijft elke configuratie-/gameplay-update (id + alle
    // attributen) naar %APPDATA%\CabNavi\debug.log. Zo kunnen we
    // precies zien wat het spel doorgeeft als iets niet klopt (job-detectie
    // of afronding), in plaats van te gokken. Simpel/synchroon omdat dit
    // weinig voorkomt -- geen aparte queue-thread nodig zoals bij TripLogger.
    static void SchrijfDebugAttributen( const char *titel, const char *id, const scs_named_value_t *attributes )
    {
        std::filesystem::path pad;
        if( const char *appdata = std::getenv( "APPDATA" ) )
        {
            pad = appdata;
        }
        pad /= "CabNavi";
        std::error_code ec;
        std::filesystem::create_directories( pad, ec );
        pad /= "debug.log";

        std::ofstream uit( pad, std::ios::app );
        if( !uit ) return;

        uit << "=== " << titel << ": " << id << " ===\n";
        for( const scs_named_value_t *attr = attributes; attr->name != nullptr; ++attr )
        {
            uit << "  " << attr->name;
            switch( attr->value.type )
            {
                case SCS_VALUE_TYPE_string:
                    uit << " (string) = " << ( attr->value.value_string.value ? attr->value.value_string.value : "(null)" );
                    break;
                case SCS_VALUE_TYPE_float:
                    uit << " (float) = " << attr->value.value_float.value;
                    break;
                case SCS_VALUE_TYPE_double:
                    uit << " (double) = " << attr->value.value_double.value;
                    break;
                case SCS_VALUE_TYPE_s32:
                    uit << " (s32) = " << attr->value.value_s32.value;
                    break;
                case SCS_VALUE_TYPE_u32:
                    uit << " (u32) = " << attr->value.value_u32.value;
                    break;
                case SCS_VALUE_TYPE_s64:
                    uit << " (s64) = " << attr->value.value_s64.value;
                    break;
                case SCS_VALUE_TYPE_u64:
                    uit << " (u64) = " << attr->value.value_u64.value;
                    break;
                case SCS_VALUE_TYPE_bool:
                    uit << " (bool) = " << ( attr->value.value_bool.value ? "true" : "false" );
                    break;
                default:
                    uit << " (ander type: " << (int)attr->value.type << ")";
                    break;
            }
            uit << "\n";
        }
        uit << "\n";
    }

    static void SchrijfDebugConfig( const scs_telemetry_configuration_t *cfg )
    {
        SchrijfDebugAttributen( "Configuration", cfg->id, cfg->attributes );
    }

    static void SchrijfDebugGameplayEvent( const scs_telemetry_gameplay_event_t *info )
    {
        SchrijfDebugAttributen( "Gameplay event", info->id, info->attributes );
    }

    void TruckTracking::OpVoertuigConfig( const scs_telemetry_configuration_t *cfg )
    {
        SchrijfDebugConfig( cfg );

        const std::string configId = cfg->id;

        if( configId == "truck" )
        {
            // Tankinhoud opvragen zodra de truck-configuratie langskomt
            // (bv. bij spawnen/wisselen van truck).
            if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "fuel.capacity" ) )
            {
                if( attr->value.type == SCS_VALUE_TYPE_float )
                {
                    m_tankInhoudLiters = attr->value.value_float.value;
                }
            }
            return;
        }

        if( configId != "job" )
        {
            return;
        }

        // Een niet-lege "cargo.id" betekent dat er een nieuwe job is geladen;
        // een lege configuratie betekent dat de job-slot leeg is (net
        // afgerond/geannuleerd -- de daadwerkelijke afronding met
        // inkomen/schade komt via het gameplay-event hieronder).
        const scs_named_value_t *cargoAttr = ZoekAttribuut( cfg->attributes, "cargo.id" );
        const bool heeftLading = cargoAttr != nullptr && cargoAttr->value.type == SCS_VALUE_TYPE_string
                                  && cargoAttr->value.value_string.value != nullptr
                                  && cargoAttr->value.value_string.value[ 0 ] != '\0';

        if( !heeftLading )
        {
            return;
        }

        // Het spel stuurt deze "job"-configuratie meerdere keren voor
        // dezelfde job (bv. eerst met cargo.loaded=false, daarna true zodra
        // je echt geladen bent) -- als we al bezig zijn met exact dezelfde
        // lading (op interne cargo.id, niet de weergavenaam), is dit een
        // herhaling en geen nieuwe job. Anders zou de ritklok en het
        // brandstofverbruik steeds opnieuw beginnen.
        if( m_actief && m_huidigeLadingId == cargoAttr->value.value_string.value )
        {
            return;
        }

        Trip nieuw;
        nieuw.type = TripType::Vracht;
        nieuw.status = TripStatus::Bezig;
        nieuw.id = "vracht-" + NuAlsIso();
        nieuw.startTijdIso = NuAlsIso();
        nieuw.economyStartTijd = m_economyTijd;
        nieuw.lading = cargoAttr->value.value_string.value; // fallback: interne id
        if( const scs_named_value_t *naamAttr = ZoekAttribuut( cfg->attributes, "cargo" ) )
        {
            // "cargo" is de nette weergavenaam (bv. "Cement"), "cargo.id" de
            // interne code (bv. "cement") -- toon de nette naam als die er is.
            if( naamAttr->value.type == SCS_VALUE_TYPE_string && naamAttr->value.value_string.value )
                nieuw.lading = naamAttr->value.value_string.value;
        }

        if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "source.city" ) )
        {
            if( attr->value.type == SCS_VALUE_TYPE_string && attr->value.value_string.value )
                nieuw.bronStad = attr->value.value_string.value;
        }
        if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "destination.city" ) )
        {
            if( attr->value.type == SCS_VALUE_TYPE_string && attr->value.value_string.value )
                nieuw.bestemmingStad = attr->value.value_string.value;
        }
        if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "source.company" ) )
        {
            if( attr->value.type == SCS_VALUE_TYPE_string && attr->value.value_string.value )
                nieuw.bronBedrijf = attr->value.value_string.value;
        }
        if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "destination.company" ) )
        {
            if( attr->value.type == SCS_VALUE_TYPE_string && attr->value.value_string.value )
                nieuw.bestemmingBedrijf = attr->value.value_string.value;
        }
        if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "planned_distance.km" ) )
        {
            if( attr->value.type == SCS_VALUE_TYPE_u32 )
                nieuw.geplandeAfstandKm = static_cast<double>( attr->value.value_u32.value );
        }
        // Ladinggewicht (kg) -- hoort bij idee #6, samen met de
        // aanhangerschade. Komt als float uit de job-config.
        if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "cargo.mass" ) )
        {
            if( attr->value.type == SCS_VALUE_TYPE_float )
            {
                nieuw.ladingGewichtKg = static_cast<double>( attr->value.value_float.value );
                m_ladingGewichtKg = nieuw.ladingGewichtKg;
            }
        }

        m_huidigeRit = nieuw;
        m_gladdeSchattingMin = -1.0; // verse rit, verse schatting
        m_huidigeLadingId = cargoAttr->value.value_string.value;
        m_actief = true;
        m_ritStartMoment = std::chrono::steady_clock::now();
        m_laatsteSnelheidMeting = m_ritStartMoment;
        m_gepauzeerd = false;
        m_totaalGepauzeerdSeconden = 0.0;
        m_kmVenster.clear();
        // Ook het verbruikvenster: StartNieuweRit hieronder zet de literteller
        // terug op nul, dus oude punten in het venster zouden een negatief
        // verschil geven.
        m_brandstofVenster.clear();
        m_rijdendLiters = 0.0;
        m_rijdendKm = 0.0;
        m_meetKmTotaal = 0.0;
        m_vorigMeetLiters = -1.0;
        m_vorigMeetOdometerKm = -1.0;
        m_meetGestart = false;
        m_gladLiterPerUur = -1.0;
        m_gladVerbruikNu = -1.0;
        m_vorigRijdt = false;
        m_brandstof.StartNieuweRit();
    }

    void TruckTracking::OpGameplayEvent( const scs_telemetry_gameplay_event_t *info )
    {
        SchrijfDebugGameplayEvent( info );

        // "job.delivered" bij een geslaagde levering, "job.cancelled" bij
        // annuleren. Beide zijn inmiddels geverifieerd tegen de officiele
        // SDK-constanten (SCS_TELEMETRY_GAMEPLAY_EVENT_job_delivered /
        // _job_cancelled) -- dit is dus geen aanname meer.
        const std::string eventId = info->id;

        // --- Onkosten tijdens de rit (ideeenlijst #3, #4, #5) ----------
        // Deze event-id's staan als SCS_TELEMETRY_GAMEPLAY_EVENT_* in de
        // officiele SDK-header; ik heb ze nagekeken in de publieke
        // scs-sdk-plugin die precies deze vier afhandelt. Ze komen ook
        // binnen als je NIET op een job zit (vrij rondrijden) -- dan slaan
        // we ze over, want ze horen dan bij geen enkele rit.
        if( eventId == "player.fined" )
        {
            if( !m_actief ) return;
            Boete b;
            if( const scs_named_value_t *attr = ZoekAttribuut( info->attributes, "fine.offence" ) )
            {
                if( attr->value.type == SCS_VALUE_TYPE_string && attr->value.value_string.value )
                    b.reden = attr->value.value_string.value;
            }
            if( const scs_named_value_t *attr = ZoekAttribuut( info->attributes, "fine.amount" ) )
            {
                if( attr->value.type == SCS_VALUE_TYPE_s64 )
                    b.bedrag = attr->value.value_s64.value;
            }
            m_huidigeRit.boetes.push_back( b );
            m_huidigeRit.boeteKosten += b.bedrag;
            return;
        }

        if( eventId == "player.tollgate.paid" || eventId == "player.use.ferry"
            || eventId == "player.use.train" )
        {
            if( !m_actief ) return;
            Doorgang d;
            d.type = ( eventId == "player.tollgate.paid" )  ? DoorgangType::Tol
                     : ( eventId == "player.use.ferry" )     ? DoorgangType::Veerboot
                                                              : DoorgangType::Trein;

            if( const scs_named_value_t *attr = ZoekAttribuut( info->attributes, "pay.amount" ) )
            {
                if( attr->value.type == SCS_VALUE_TYPE_s64 )
                    d.bedrag = attr->value.value_s64.value;
            }
            // Veerboot en trein melden ook waarvandaan en waarnaartoe;
            // een tolpoort heeft die attributen niet, dan blijven ze leeg.
            if( const scs_named_value_t *attr = ZoekAttribuut( info->attributes, "source.name" ) )
            {
                if( attr->value.type == SCS_VALUE_TYPE_string && attr->value.value_string.value )
                    d.vanaf = attr->value.value_string.value;
            }
            if( const scs_named_value_t *attr = ZoekAttribuut( info->attributes, "target.name" ) )
            {
                if( attr->value.type == SCS_VALUE_TYPE_string && attr->value.value_string.value )
                    d.naar = attr->value.value_string.value;
            }

            if( d.type == DoorgangType::Veerboot || d.type == DoorgangType::Trein )
            {
                // De klok springt zo meteen vooruit; dat is geen rust.
                m_negeerVolgendeSprong = true;
            }

            switch( d.type )
            {
                case DoorgangType::Tol:      m_huidigeRit.tolKosten += d.bedrag; break;
                case DoorgangType::Veerboot: m_huidigeRit.veerbootKosten += d.bedrag; break;
                case DoorgangType::Trein:    m_huidigeRit.treinKosten += d.bedrag; break;
            }
            m_huidigeRit.doorgangen.push_back( std::move( d ) );
            return;
        }

        const bool isAfgeleverd = eventId == "job.delivered";
        const bool isGeannuleerd = eventId == "job.cancelled";
        if( !isAfgeleverd && !isGeannuleerd )
        {
            return;
        }
        if( !m_actief )
        {
            return;
        }

        m_huidigeRit.status = isGeannuleerd ? TripStatus::Geannuleerd : TripStatus::Voltooid;
        m_huidigeRit.eindTijdIso = NuAlsIso();
        m_huidigeRit.economyEindTijd = m_economyTijd;

        if( const scs_named_value_t *attr = ZoekAttribuut( info->attributes, "revenue" ) )
        {
            if( attr->value.type == SCS_VALUE_TYPE_s64 )
                m_huidigeRit.inkomen = attr->value.value_s64.value;
        }
        if( const scs_named_value_t *attr = ZoekAttribuut( info->attributes, "distance.km" ) )
        {
            if( attr->value.type == SCS_VALUE_TYPE_float )
                m_huidigeRit.afgelegdeAfstandKm = attr->value.value_float.value;
        }

        BrandstofState bs = m_brandstof.HuidigeState();
        m_huidigeRit.brandstofVerbruikLiters = bs.verbruikSindsRitStartLiters;
        m_huidigeRit.brandstofKostenEuro = m_brandstof.SluitRitAf();

        m_logger.RegisterVoltooideRit( m_huidigeRit );
        m_huidigeRit = Trip{};
        m_huidigeLadingId.clear();
        m_actief = false;

        // Verbruikmeting opnieuw beginnen: wat hierna volgt is leeg rijden,
        // en dat hoort een eigen gemiddelde te krijgen in plaats van de rit
        // die net is afgesloten.
        m_brandstofVenster.clear();
        m_rijdendLiters = 0.0;
        m_rijdendKm = 0.0;
        m_meetKmTotaal = 0.0;
        m_vorigMeetLiters = -1.0;
        m_vorigMeetOdometerKm = -1.0;
        m_meetGestart = false;
        m_gladLiterPerUur = -1.0;
        m_gladVerbruikNu = -1.0;
        m_vorigRijdt = false;
    }

    double TruckTracking::VerstrekenMinutenEcht() const
    {
        if( !m_actief ) return 0.0;
        auto nu = std::chrono::steady_clock::now();
        double totaalSeconden = std::chrono::duration<double>( nu - m_ritStartMoment ).count();

        double gepauzeerdSeconden = m_totaalGepauzeerdSeconden;
        if( m_gepauzeerd )
        {
            // we zitten er nu middenin -- ook het lopende stuk meetellen
            gepauzeerdSeconden += std::chrono::duration<double>( nu - m_pauzeStartMoment ).count();
        }

        double echtVerstreken = totaalSeconden - gepauzeerdSeconden;
        return std::max( 0.0, echtVerstreken ) / 60.0;
    }

    double TruckTracking::GeschatteResterendeMinutenEcht() const
    {
        if( !m_actief || m_huidigeRit.geplandeAfstandKm <= 0.0 )
        {
            return -1.0;
        }

        // Bij voorkeur: het spel's EIGEN, al-berekende resterende
        // navigatie-afstand (via truck.navigation.distance, echte weg,
        // houdt al rekening met bochten) -- veel nauwkeuriger dan onze
        // eigen optelsom. Alleen gebruiken als het kanaal al minstens één
        // keer een waarde heeft doorgegeven (-1 = nog niet ontvangen).
        double resterendeKm;
        if( m_navigatieAfstandMeter >= 0.0 )
        {
            resterendeKm = m_navigatieAfstandMeter / 1000.0;
        }
        else
        {
            // Terugval: eigen optelsom (gepland - afgelegd), zoals voorheen.
            resterendeKm = m_huidigeRit.geplandeAfstandKm - m_huidigeRit.afgelegdeAfstandKm;
        }

        if( resterendeKm <= 0.0 )
        {
            return 0.0;
        }

        // ---- Bron 0: de ETA van het spel zelf --------------------------
        //
        // truck.navigation.time is de Route Advisor-schatting -- dezelfde die
        // onderin je scherm staat. Die kent de echte route, de limieten en de
        // veerboten, en verandert NIET als je even stilstaat voor een
        // stoplicht. Precies daarom is dit de enige goede bron.
        //
        // SCS' eigen header noemt seconden, dus dat is de aanname. Alleen als
        // die aanname een onmogelijke snelheid oplevert proberen we minuten.
        // Anders houden we seconden aan -- terugvallen op onze eigen
        // snelheidsmeting is namelijk erger dan een iets scheve ETA: sta je
        // stil, dan deelt die door bijna nul en krijg je "42 uur".
        if( m_navigatieTijd > 0.0 )
        {
            const double schaal = TijdSchaal();
            double spelUren = m_navigatieTijd / 3600.0; // aanname: seconden

            const double impliciet = spelUren > 0.0 ? resterendeKm / spelUren : 0.0;
            if( impliciet < 3.0 || impliciet > 200.0 )
            {
                // Onmogelijke snelheid -> waarschijnlijk toch minuten.
                const double alsMinuten = m_navigatieTijd / 60.0;
                const double implicietMin = alsMinuten > 0.0 ? resterendeKm / alsMinuten : 0.0;
                if( implicietMin >= 3.0 && implicietMin <= 200.0 )
                {
                    spelUren = alsMinuten;
                }
            }

            return Gladstrijken( ( spelUren * 60.0 ) / schaal );
        }

        double gemiddeldeSnelheid = 0.0;

        // 1) Bij voorkeur: voortschrijdend gemiddelde van de laatste
        //    ~3 minuten -- reageert snel op wisselende omstandigheden
        //    (net als Trucky, die continu herberekent i.p.v. één vast
        //    gemiddelde over de hele rit te gebruiken).
        if( m_kmVenster.size() >= 2 )
        {
            const auto &eerste = m_kmVenster.front();
            const auto &laatste = m_kmVenster.back();
            double tijdspanneUur = std::chrono::duration<double>( laatste.first - eerste.first ).count() / 3600.0;
            double kmVerschil = laatste.second - eerste.second;
            if( tijdspanneUur > ( 30.0 / 3600.0 ) ) // minstens 30 sec aan data
            {
                gemiddeldeSnelheid = kmVerschil / tijdspanneUur;
            }
        }

        // 2) Anders: gemiddelde over de hele rit tot nu toe.
        if( gemiddeldeSnelheid < 1.0 )
        {
            double verstrekenMin = VerstrekenMinutenEcht();
            if( verstrekenMin > 0.5 && m_huidigeRit.afgelegdeAfstandKm > 0.5 )
            {
                gemiddeldeSnelheid = m_huidigeRit.afgelegdeAfstandKm / ( verstrekenMin / 60.0 );
            }
        }

        // 3) Anders: de snelheidsmeter van dit moment.
        //
        // LET OP -- dit was de bron van de rare uitkomsten. De meter geeft
        // km per SPELuur, terwijl de twee bronnen hierboven km per ECHT uur
        // geven (afstand gedeeld door verstreken echte tijd). Die door
        // elkaar halen scheelt een factor 6 in TruckersMP: een ritje van een
        // kwartier werd dan als anderhalf uur getoond, en een lange rit als
        // 22 uur. We rekenen de metersnelheid daarom eerst om.
        if( gemiddeldeSnelheid < 1.0 )
        {
            // De meter geeft km per SPELuur; omrekenen naar km per ECHT uur.
            gemiddeldeSnelheid = m_huidigeRit.huidigeSnelheidKmh * TijdSchaal();
        }

        // Bodem onder de snelheid. Zonder dit deelt een stilstaande truck
        // (1 km/h voor een stoplicht) de resterende afstand door bijna nul,
        // en dan komt er "42 uur" uit -- precies wat er misging. We rekenen
        // in dat geval met een redelijke reissnelheid in plaats van met het
        // moment. De ETA van het spel hierboven is sowieso de betere bron;
        // dit is alleen nog vangnet voor als die er niet is.
        // Let op de EENHEID: `gemiddeldeSnelheid` is game-km per ECHT uur
        // (afstand gedeeld door verstreken echte tijd), terwijl 40 km/h een
        // METERstand is. Omrekenen doe je met de tijdschaal, niet erdoor
        // delen -- hier stond eerst "* schaal / 6", waardoor de bodem zes keer
        // te laag lag en een kruipende truck er alsnog doorheen glipte.
        const double BODEM_SNELHEID_ECHT = 40.0 * TijdSchaal(); // 40 km/h op de meter
        if( gemiddeldeSnelheid < BODEM_SNELHEID_ECHT )
        {
            if( m_gladdeSchattingMin > 0.0 )
            {
                return m_gladdeSchattingMin; // laatst bekende waarde vasthouden
            }
            gemiddeldeSnelheid = BODEM_SNELHEID_ECHT;
        }

        return Gladstrijken( ( resterendeKm / gemiddeldeSnelheid ) * 60.0 );
    }

    double TruckTracking::Demp( double huidig, double nieuw, double dtSec, double tauSec )
    {
        // TIJDgebaseerd, niet per aanroep. Deze functie wordt per frame
        // aangeroepen -- gemeten zo'n 59 keer per seconde. Met een vast
        // percentage per aanroep is het gemiddelde binnen een tiende seconde
        // bijgetrokken en dempt er dus niets. Met dt/(dt+tau) hangt de
        // demping af van de VERSTREKEN TIJD, en betekent tau hoe lang het
        // ongeveer duurt voordat een verandering is doorgewerkt.
        if( nieuw < 0.0 ) return huidig;
        if( huidig < 0.0 ) return nieuw; // eerste meting: gewoon overnemen

        // Alleen bij een ECHT grote stap direct overnemen. De oude grens
        // (helft eraf of erbij) sloeg bij bijna elke meting aan: het spel
        // verbruikt bergop tien keer zoveel als uitrollend, ook bij dezelfde
        // snelheid (gemeten 30-08). Daardoor werd de demping continu
        // overgeslagen. Deze ruimere grens laat gewone heuvels dempen en
        // vangt alleen echte stappen op.
        const double verhouding = nieuw / std::max( 0.001, huidig );
        if( verhouding > 4.0 || verhouding < 0.25 ) return nieuw;

        if( dtSec <= 0.0 || tauSec <= 0.0 ) return nieuw;
        const double alpha = dtSec / ( dtSec + tauSec );
        return huidig + alpha * ( nieuw - huidig );
    }

    double TruckTracking::Gladstrijken( double ruweMinuten ) const
    {
        // Zonder dit springt de schatting bij elk stoplicht of elke
        // inhaalactie heen en weer. We schuiven per aanroep een stukje op
        // naar de nieuwe waarde in plaats van er meteen naartoe te knallen.
        if( m_gladdeSchattingMin < 0.0 )
        {
            m_gladdeSchattingMin = ruweMinuten; // eerste keer: gewoon overnemen
        }
        else
        {
            // Grote sprongen (meer dan de helft eraf of erbij) nemen we wel
            // direct over -- dan is er echt iets veranderd, bijvoorbeeld een
            // nieuwe route, en is traag bijsturen juist verwarrend.
            const double verhouding = ruweMinuten / std::max( 1.0, m_gladdeSchattingMin );
            if( verhouding > 1.5 || verhouding < 0.5 )
            {
                m_gladdeSchattingMin = ruweMinuten;
            }
            else
            {
                m_gladdeSchattingMin = m_gladdeSchattingMin * 0.85 + ruweMinuten * 0.15;
            }
        }
        return m_gladdeSchattingMin;
    }

    // ---- Pauze-callbacks --------------------------------------------

    SCSAPI_VOID TruckTracking::GepauzeerdCallback( const scs_event_t, const void *, scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( !self->m_gepauzeerd )
        {
            self->m_gepauzeerd = true;
            self->m_pauzeStartMoment = std::chrono::steady_clock::now();
        }
    }

    SCSAPI_VOID TruckTracking::HervatCallback( const scs_event_t, const void *, scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( self->m_gepauzeerd )
        {
            auto nu = std::chrono::steady_clock::now();
            self->m_totaalGepauzeerdSeconden += std::chrono::duration<double>( nu - self->m_pauzeStartMoment ).count();
            self->m_gepauzeerd = false;
            // Voorkom een kunstmatige snelheids-piek in de live-km-integratie
            // vlak na het hervatten (er zat immers een "gat" in de tijd).
            self->m_laatsteSnelheidMeting = nu;
        }
    }

    // ---- SCS callback trampolines --------------------------------------

    SCSAPI_VOID TruckTracking::LandCallback( const scs_string_t naam, scs_u32_t,
                                              const scs_value_t *value, scs_context_t context )
    {
        if( !value || value->type != SCS_VALUE_TYPE_string || !value->value_string.value ) return;
        auto *self = static_cast<TruckTracking *>( context );

        const std::string land = value->value_string.value;
        if( land.empty() ) return;

        // Alleen loggen als het VERANDERT, anders loopt debug.log vol.
        static std::string laatst;
        if( land != laatst )
        {
            laatst = land;
            Logboek::Schrijf( "gebeurt", std::string( "land via '" ) + ( naam ? naam : "?" ) + "': " + land );
        }

        self->m_brandstof.ZetHuidigLand( land );
    }

    SCSAPI_VOID TruckTracking::NavigatieTijdCallback( const scs_string_t, scs_u32_t,
                                                       const scs_value_t *value, scs_context_t context )
    {
        if( !value ) return;
        static_cast<TruckTracking *>( context )->m_navigatieTijd = value->value_float.value;
    }

    SCSAPI_VOID TruckTracking::RustStopCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                  scs_context_t context )
    {
        if( !value ) return;
        auto *self = static_cast<TruckTracking *>( context );

        // Type uitlezen zoals het spel het aanbiedt, niet zoals wij het
        // dachten: het veld `type` vertelt wat er werkelijk in staat.
        double minuten;
        switch( value->type )
        {
            case SCS_VALUE_TYPE_s32:   minuten = static_cast<double>( value->value_s32.value ); break;
            case SCS_VALUE_TYPE_u32:   minuten = static_cast<double>( value->value_u32.value ); break;
            case SCS_VALUE_TYPE_float: minuten = static_cast<double>( value->value_float.value ); break;
            default: return;
        }
        if( minuten < 0.0 ) return; // ongeldig / vermoeidheid uit

        // Meten wat dit kanaal doet. Springt hij ooit omhoog, dan is er
        // gerust en kunnen we daarop resetten. Blijft hij alleen maar dalen,
        // dan gebeurt dat op deze server niet en heeft synchroniseren met de
        // in-game P-teller sowieso geen zin.
        if( self->m_minutenTotRust >= 0.0 && minuten > self->m_minutenTotRust + 30.0 )
        {
            ++self->m_rustResets;
            self->m_pauzeRijSpelMin = 0.0; // teller sprong omhoog = er is gerust
        }
        if( self->m_rustLaagst < 0.0 || minuten < self->m_rustLaagst )
        {
            self->m_rustLaagst = minuten;
        }

        self->m_minutenTotRust = minuten;
        // De hoogste waarde is de lengte van een volle periode. Na een rust
        // springt de teller weer omhoog, en dan klopt de schaal vanzelf.
        if( minuten > self->m_rustPeriodeMax ) self->m_rustPeriodeMax = minuten;
    }

    SCSAPI_VOID TruckTracking::GameTimeCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                  scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( !value ) return;

        const std::uint32_t minuten = value->value_u32.value;
        if( minuten == self->m_economyTijd ) return; // geen verandering: niets te leren

        const auto nu = std::chrono::steady_clock::now();

        if( self->m_schaalGestart )
        {
            // Losse stap beoordelen. Een normale stap is 1 spelminuut per
            // ~10 echte seconden. Springt de klok ineens veel verder (server
            // die de tijd synchroniseert, rust, of gepauzeerd gestaan), dan is
            // die stap onbruikbaar EN vervuilt hij de hele meting -- want het
            // anker schoof nooit op, dus zo'n sprong bleef eeuwig doorwerken.
            // Daarom: bij een rare stap opnieuw beginnen met meten.
            const double stapSpel = static_cast<double>( minuten ) - static_cast<double>( self->m_economyTijd );

            // RUST HERKENNEN. Negen uur stilstaan doe je niet met de motor
            // stationair -- je slaapt. Voor het spel betekent dat: de klok
            // springt in een keer uren vooruit terwijl je niet rijdt. Dat is
            // het enige betrouwbare signaal dat we hebben, want de
            // Mandatory-Break-teller zelf komt niet door de telemetrie.
            //
            // Drempel op 45 minuten. Sinds 1.60 mag je zelf je wektijd kiezen,
            // dus een rust kan kort zijn -- op vier uur miste hij die.
            //
            // Veerboten en treinen springen ook vooruit, maar die herkennen we
            // aan hun eigen gameplay-event (zie de onkosten-afhandeling) en
            // slaan we over via `m_negeerVolgendeSprong`. Dat is preciezer dan
            // een tijdsdrempel: een veerboot van vijf uur telt zo nog steeds
            // niet als rust, terwijl een dutje van een uur dat wel doet.
            if( stapSpel >= 45.0 && self->m_negeerVolgendeSprong )
            {
                self->m_negeerVolgendeSprong = false; // was de veerboot/trein
            }
            else if( stapSpel >= 45.0 && self->m_liveSnelheidKmh < 2.0 )
            {
                self->m_pauzeRijSpelMin = 0.0;
                self->m_pauzeStilstandSpelMin = stapSpel;
                self->m_laatsteRustSpelMinuten = stapSpel;
                self->m_tachoRijSecondenSindsRust = 0.0;
                self->m_tachoInRust = true;
                // Nulpunt vastleggen: vanaf hier telt de spelklok opnieuw.
                self->m_economyTijdLaatsteRust = minuten;
                self->m_rustMomentBekend = true;
                self->m_getoondeRestMin = -1.0;    // rust: mag weer omhoog

                // Voor de EIGEN tachograaf (stand 2 en 3) telt hoe LANG de
                // sprong was: een korte onderbreking is een pauze, een lange
                // is dagrust. Zo hoeven we niets te raden over wat je deed.
                if( stapSpel >= self->m_tacho.dagRust * 0.8 )
                {
                    self->m_eigenLaatsteDagrust = minuten;
                    self->m_eigenLaatstePauze = minuten; // dagrust telt ook als pauze
                }
                else if( stapSpel >= self->m_tacho.pauzeDuur * 0.9 )
                {
                    self->m_eigenLaatstePauze = minuten;
                }
                self->m_getoondeRijtijdMin = -1.0; // en de rijtijd weer terug naar nul
                self->BewaarTachoStand();          // meteen vastleggen op schijf
            }
            const double stapEcht =
                std::chrono::duration<double>( nu - self->m_schaalLaatsteEcht ).count() / 60.0;

            const bool rareStap = stapSpel < 0.0 ||          // klok terug
                                   stapSpel > 5.0 ||          // grote sprong vooruit
                                   ( stapEcht > 0.0 && ( stapSpel / stapEcht ) > 30.0 );
            if( rareStap )
            {
                self->m_schaalEersteEconomy = minuten;
                self->m_schaalEersteEcht = nu;
            }
            else
            {
                // Anker elke 15 minuten verversen, zodat een oude meting niet
                // eeuwig blijft meewegen als de omstandigheden veranderen.
                const double sindsAnker =
                    std::chrono::duration<double>( nu - self->m_schaalEersteEcht ).count() / 60.0;
                if( sindsAnker > 15.0 )
                {
                    self->m_schaalEersteEconomy = minuten;
                    self->m_schaalEersteEcht = nu;
                }
            }
        }
        else
        {
            self->m_schaalEersteEconomy = minuten;
            self->m_schaalEersteEcht = nu;
            self->m_schaalGestart = true;

            if( !self->m_rustMomentBekend )
            {
                const double periode = self->m_rustPeriodeMax > 60.0
                                           ? self->m_rustPeriodeMax : MAX_RIJ_SPELMINUTEN;

                if( self->m_teHerstellenRest >= 0.0 )
                {
                    // Er stond een bewaarde stand: verankeren op de klok van NU,
                    // zodat je exact verder gaat waar je gebleven was -- ook als
                    // de serverklok ondertussen dagen is doorgelopen.
                    self->m_economyTijdLaatsteRust = static_cast<std::uint32_t>(
                        static_cast<double>( minuten ) - ( periode - self->m_teHerstellenRest ) );
                    self->m_teHerstellenRest = -1.0;
                }
                else
                {
                    // Niets bewaard: dit is ons nulpunt tot er een rust komt.
                    self->m_economyTijdLaatsteRust = minuten;
                }
                self->m_rustMomentBekend = true;
            }

            if( !self->m_eigenGestart )
            {
                self->m_eigenLaatstePauze = minuten;
                self->m_eigenLaatsteDagrust = minuten;
                self->m_eigenGestart = true;
            }
            // Een bewaarde stand blijft staan, punt. Ook een oude: de teller
            // hoort gelijk te lopen met je in-game P-klok, en die onthoudt het
            // ook. Sluit je vandaag af en rijd je morgen verder, dan klopt hij
            // gewoon nog.
            //
            // Laad je een ander profiel waar de klok heel anders staat, dan
            // neem je zelf even rust om weer gelijk te komen. Dat is de
            // handicap van het feit dat we de echte P-teller niet kunnen
            // uitlezen -- en beter dan dat de plugin zelf gaat rommelen in een
            // stand die misschien juist wel klopt.
        }

        self->m_schaalLaatsteEcht = nu;
        self->m_economyTijd = minuten;
    }

    double TruckTracking::TijdSchaal() const
    {
        // TruckersMP houdt de klok voor de hele server gelijk en gebruikt
        // daarvoor een vaste schaal: 1 spelminuut per 10 echte seconden,
        // oftewel 6 spelminuten per echte minuut. Dat is onze basiswaarde --
        // en meteen de reden dat we nooit zonder antwoord zitten.
        //
        // We METEN daarnaast, maar puur als correctie voor het geval de
        // schaal ooit verandert (TMP heeft er in 0.7.5.0 nog aan gezeten) of
        // je offline speelt, waar het spel wel wisselt tussen snelweg en stad.
        //
        // De meting mag de basiswaarde alleen overrulen als ze DICHT IN DE
        // BUURT ligt. Een tijdsprong door serversynchronisatie of een pauze
        // levert anders een absurde schaal op, en dan schiet de aankomsttijd
        // alle kanten op -- precies wat we willen voorkomen.
        constexpr double STANDAARD_SCHAAL = 6.0;   // TruckersMP: 10 echte sec = 1 spelminuut
        // Grenzen ruim genoeg voor BEIDE speelwijzen:
        //   TruckersMP   -> 10 echte seconden per spelminuut  = 6
        //   Singleplayer -> 1 speluur per ~3,16 echte minuten = ~19
        //
        // Ze stonden even op 4..9 om het "flipperen" te dempen, maar daarmee
        // werd singleplayer (19) verworpen en rekende hij daar met 6 -- een
        // factor drie mis. Nu de schaal na de meting wordt VASTGEZET is die
        // demping niet meer nodig: tijdens het rijden beweegt er toch niets.
        // Alles buiten deze band is een tijdsprong, geen echte schaal.
        constexpr double ONDERGRENS = 3.0;
        constexpr double BOVENGRENS = 25.0;

        // Handmatig ingesteld? Dan is dat het, punt.
        if( m_handmatigeSchaal > 0.0 ) return m_handmatigeSchaal;

        // Eenmaal vastgezet blijft hij vast: tijdens het rijden mag dit getal
        // niet meer bewegen, anders stuitert de aankomsttijd mee.
        if( m_vastgezetteSchaal > 0.0 ) return m_vastgezetteSchaal;

        if( !m_schaalGestart ) return STANDAARD_SCHAAL;

        const double echteMinuten =
            std::chrono::duration<double>( std::chrono::steady_clock::now() - m_schaalEersteEcht ).count() / 60.0;

        // Onder de twee minuten is de meting te grof: game.time verspringt
        // met hele minuten tegelijk, dus een enkele stap geeft een schijnbaar
        // enorme of juist minieme snelheid.
        if( echteMinuten < 2.0 ) return STANDAARD_SCHAAL;

        const double spelMinuten =
            static_cast<double>( m_economyTijd ) - static_cast<double>( m_schaalEersteEconomy );
        if( spelMinuten <= 0.0 ) return STANDAARD_SCHAAL;

        const double gemeten = spelMinuten / echteMinuten;
        if( gemeten < ONDERGRENS || gemeten > BOVENGRENS )
        {
            return STANDAARD_SCHAAL; // meting niet te vertrouwen
        }

        // Na vijf minuten schone meting is het genoeg: vastzetten. Vanaf dat
        // moment is dit net zo stabiel als een hardgecodeerde 6, maar dan wel
        // met de waarde die deze server werkelijk gebruikt.
        if( echteMinuten >= 5.0 )
        {
            m_vastgezetteSchaal = gemeten;
        }
        return gemeten;
    }

    void TruckTracking::ZetTachoInstelling( const TachoInstelling &nieuw )
    {
        m_tacho = nieuw;
        BewaarTachoStand();
    }

    double TruckTracking::MinutenTotPauzeEigen() const
    {
        if( m_tacho.stand == TachoStand::SpelVolgen || !m_eigenGestart ) return -1.0;
        const double verstreken =
            static_cast<double>( m_economyTijd ) - static_cast<double>( m_eigenLaatstePauze );
        if( verstreken < 0.0 ) return m_tacho.maxAaneengeslotenRijden;
        return m_tacho.maxAaneengeslotenRijden - verstreken;
    }

    double TruckTracking::MinutenDagrijtijdOver() const
    {
        if( m_tacho.stand == TachoStand::SpelVolgen || !m_eigenGestart ) return -1.0;
        const double verstreken =
            static_cast<double>( m_economyTijd ) - static_cast<double>( m_eigenLaatsteDagrust );
        if( verstreken < 0.0 ) return m_tacho.maxDagRijden;
        return m_tacho.maxDagRijden - verstreken;
    }

    double TruckTracking::MinutenTotVerplichtePauze() const
    {
        // Rechtstreeks uit de spelklok: hoeveel spelminuten zijn er verstreken
        // sinds je laatste rust? Dat is precies wat de P-teller in het spel
        // ook doet -- die telt verstreken tijd, of je nu rijdt of stilstaat.
        if( m_rustMomentBekend )
        {
            const double periodeNu = m_rustPeriodeMax > 60.0 ? m_rustPeriodeMax : MAX_RIJ_SPELMINUTEN;
            const double verstreken =
                static_cast<double>( m_economyTijd ) - static_cast<double>( m_economyTijdLaatsteRust );
            if( verstreken >= 0.0 )
            {
                // Nooit meer tonen dan een volle periode. Staat de klok lager
                // dan het bewaarde moment (ander profiel), dan zou je anders
                // "14 uur" zien staan. De opgeslagen stand blijft ongemoeid --
                // dit begrenst alleen wat er OP HET SCHERM komt.
                const double rest = std::min( periodeNu, periodeNu - verstreken );

                if( m_getoondeRestMin < 0.0 )
                {
                    m_getoondeRestMin = rest; // eerste waarde: gewoon overnemen
                    BewaarTachoStand();
                    return m_getoondeRestMin;
                }

                // Twee remmen op het getal, zodat het nooit zichtbaar
                // schokt door een serversynchronisatie of een ping-piek:
                //
                // 1. ALLEEN OMLAAG. Springt de klok een minuut terug, dan
                //    zou de resterende tijd omhoog wippen -- dat ziet eruit
                //    als een fout. Een aftelteller hoort te dalen.
                //
                // 2. ALLEEN IN HELE MINUTEN. We nemen een nieuwe waarde pas
                //    over als hij minstens een volle minuut lager ligt.
                //    Kleinere verschillen laten we staan; zo staat het getal
                //    stil in plaats van heen en weer te trillen.
                //
                // Bij een echte rust gaat de rem er expliciet af (zie
                // GameTimeCallback), want dan MOET hij terug omhoog.
                if( rest <= m_getoondeRestMin - 1.0 )
                {
                    m_getoondeRestMin = rest;

                    // Elke volle minuut wegschrijven. Zo staat de laatste stand
                    // altijd op schijf, ook als het spel of de plugin er
                    // onverwacht uit klapt -- er is geen afsluit-moment waar we
                    // op kunnen rekenen.
                    BewaarTachoStand();
                }
                return m_getoondeRestMin;
            }
        }

        // De periode NIET hardcoderen. SCS noemt 10 uur voor singleplayer,
        // maar servers kunnen dat aanpassen -- er zijn meldingen van 11 uur
        // op TruckersMP. In plaats van te kiezen laten we het spel het zelf
        // vertellen: de hoogste waarde die het rustkanaal ooit doorgaf IS de
        // lengte van een volle periode. Zo klopt het op elke server, en ook
        // als SCS of TMP het morgen weer wijzigt.
        //
        // Zolang dat kanaal niets bruikbaars heeft gegeven, vallen we terug
        // op de 10 uur uit de SCS-aankondiging.
        const double periode = m_rustPeriodeMax > 60.0 ? m_rustPeriodeMax : MAX_RIJ_SPELMINUTEN;
        return periode - m_pauzeRijSpelMin;
    }

    double TruckTracking::RijPeriodeSpelMinuten() const
    {
        return m_rustPeriodeMax > 60.0 ? m_rustPeriodeMax : MAX_RIJ_SPELMINUTEN;
    }

    double TruckTracking::TachograafRijtijdMinuten() const
    {
        // Op de SERVERKLOK, niet op echte seconden. In TruckersMP dicteert de
        // server de speltijd; door het verschil met het moment van je laatste
        // rust te nemen, loopt deze teller per definitie gelijk met de server
        // -- ook als je pc even hapert of de plugin een frame overslaat.
        //
        // Hier stond een optelsom van echte seconden. Die liep bij de
        // TMP-tijdschaal zes keer te langzaam en dreef bovendien af.
        if( m_rustMomentBekend )
        {
            const double verstreken =
                static_cast<double>( m_economyTijd ) - static_cast<double>( m_economyTijdLaatsteRust );
            if( verstreken >= 0.0 )
            {
                if( m_getoondeRijtijdMin < 0.0 )
                {
                    m_getoondeRijtijdMin = verstreken;
                }
                // Deze teller loopt OP, dus hier alleen omhoog -- en pas als
                // er een volle minuut bij komt. Zo staat het getal stil in
                // plaats van te trillen als de serverklok even terugspringt.
                else if( verstreken >= m_getoondeRijtijdMin + 1.0 )
                {
                    m_getoondeRijtijdMin = verstreken;
                }
                return m_getoondeRijtijdMin;
            }
        }
        return m_tachoRijSecondenSindsRust / 60.0; // terugval tot de klok binnen is
    }

    void TruckTracking::TachograafUpdate( double snelheidKmh )
    {
        auto nu = std::chrono::steady_clock::now();
        if( !m_tachoGeinitialiseerd )
        {
            m_tachoLaatsteMeting = nu;
            m_tachoGeinitialiseerd = true;
            return;
        }

        double verstrekenSeconden = std::chrono::duration<double>( nu - m_tachoLaatsteMeting ).count();
        m_tachoLaatsteMeting = nu;
        if( verstrekenSeconden <= 0.0 || verstrekenSeconden > 10.0 ) return; // sanity check

        const double STILSTAND_DREMPEL_KMH = 2.0;

        // Geen tijdsdrempel meer voor stilstand. Die stond eerst op een
        // minuut (waardoor tanken je rijtijd wiste) en daarna op een volle
        // rustperiode -- maar zelfs dat was een achterdeurtje. Stilstaan is
        // geen rust, punt. De enige reset loopt via de tijdsprong.

        // De verplichte-pauzeteller wordt NIET meer hier opgeteld. Hij leest
        // rechtstreeks het verschil tussen de serverklok en het moment van je
        // laatste rust (zie MinutenTotVerplichtePauze). Dat loopt per definitie
        // gelijk met TruckersMP: de server dicteert die klok.
        //
        // Hier stond een optelsom van echte seconden maal de tijdschaal. Dat
        // werkte, maar kon afdrijven bij haperingen en hing af van hoe goed we
        // de schaal hadden gemeten -- allebei onnodig als je de klok gewoon
        // kunt aflezen.

        if( snelheidKmh < STILSTAND_DREMPEL_KMH )
        {
            // Alleen bijhouden HOE LANG je stilstaat; dat zet de rijtijd niet
            // meer op nul. De rust-vlag wordt gezet door de tijdsprong in
            // GameTimeCallback, want dat is het enige signaal dat een ECHTE
            // rustactie onderscheidt van gewoon geparkeerd staan.
            m_tachoStilstandSeconden += verstrekenSeconden * TijdSchaal() / 60.0;
        }
        else
        {
            m_tachoStilstandSeconden = 0.0;
            m_tachoInRust = false;
            m_tachoRijSecondenSindsRust += verstrekenSeconden;
        }
    }

    SCSAPI_VOID TruckTracking::SnelheidCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                  scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( !value ) return;

        double snelheidKmh = value->value_float.value * 3.6; // m/s -> km/h
        self->m_liveSnelheidKmh = snelheidKmh;

        // Altijd doorsturen naar de buslijn-tracking, OOK als er geen
        // vrachtjob actief is (bv. tijdens een buslijn) -- dit kanaal is
        // niet job-type-specifiek, het is gewoon de snelheid van het
        // voertuig dat je op dit moment bestuurt.
        if( self->m_busTracking != nullptr )
        {
            self->m_busTracking->OpLiveSnelheid( snelheidKmh, self->m_gepauzeerd );
            // De bus heeft geen eigen SCS-registratie; hij krijgt de
            // navigatiegegevens via deze weg, net als de snelheid.
            self->m_busTracking->ZetNavigatie( self->m_navigatieTijd,
                                                self->m_navigatieAfstandMeter >= 0.0
                                                    ? self->m_navigatieAfstandMeter / 1000.0
                                                    : -1.0 );
        }

        // Tachograaf loopt altijd door, ongeacht of er een job actief is
        // (je kan ook "gewoon rondrijden" zonder lading).
        if( !self->m_gepauzeerd )
        {
            self->TachograafUpdate( snelheidKmh );
        }

        // --- Verbruikmeting: loopt ALTIJD, ook zonder actieve rit ----------
        // Bewust boven de "geen rit"-afslag hieronder: de Boordcomputer-tab
        // is er juist ook voor als je leeg rondrijdt. Tijdens pauze slaan we
        // over, net als bij de andere metingen: dan zit je in het menu.
        //
        // De afstand komt uit SNELHEID x TIJD, niet uit de kilometerstand.
        // Zo doet het spel het zelf ook: het rekent verbruik uit als liters
        // per UUR (motorinhoud, toerental, gaspedaal), en l/100km is daar de
        // afgeleide van -- l/uur gedeeld door km/h. Tijd is een perfecte
        // noemer, de kilometerstand is dat niet: over een paar seconden is
        // die te grof, en dat maakte het cijfer zowel te hoog als onrustig.
        if( !self->m_gepauzeerd )
        {
            const auto nuMeting = std::chrono::steady_clock::now();
            const double verbruiktNu = self->m_brandstof.HuidigeState().verbruikSindsRitStartLiters;

            // Vasthouden VOORDAT m_vorigMeetMoment hieronder wordt
            // overschreven -- de demping verderop heeft deze tijdstap nodig.
            const auto vorigeMeetMoment = self->m_vorigMeetMoment;
            const bool hadVorigeMeting = self->m_meetGestart;

            if( self->m_meetGestart && self->m_vorigMeetLiters >= 0.0 )
            {
                const double dtUur =
                    std::chrono::duration<double>( nuMeting - self->m_vorigMeetMoment ).count() / 3600.0;
                const double dLiters = verbruiktNu - self->m_vorigMeetLiters;

                // Sanity check tegen rare sprongen, zelfde grens als bij de
                // ritafstand hierboven.
                if( dtUur > 0.0 && dtUur < 0.1 )
                {
                    // GEMETEN 30-08: de kilometerteller van het spel is de
                    // waarheid, en die liep 3,2x sneller op dan onze eigen
                    // som "snelheid x tijdschaal x tijd". Reden: de tijdschaal
                    // (klok, ~6) is NIET dezelfde als de kaartschaal (~19).
                    // De teller telt echte kilometers, de snelheidsmeter
                    // hoort bij de verkleinde wereld. In plaats van er weer
                    // een factor bij te verzinnen nemen we gewoon de teller
                    // zelf -- die kan per definitie niet verkeerd zijn.
                    double dKm = 0.0;
                    if( self->m_vorigMeetOdometerKm >= 0.0 )
                    {
                        const double stap = self->m_kilometerstandKm - self->m_vorigMeetOdometerKm;
                        // Alleen vooruit, en niet meer dan wat in deze
                        // tijdstap mogelijk is (vangt teleport/laden af).
                        if( stap > 0.0 && stap < 5.0 ) dKm = stap;
                    }
                    self->m_meetKmTotaal += dKm;

                    // Afstandsfactor meten: hoeveel telt de kilometerteller
                    // per "snelheidsmeter-kilometer"? Alleen bij fatsoenlijke
                    // snelheid, anders is de deling te onnauwkeurig. Rustig
                    // bijstellen -- dit is een eigenschap van het spel, geen
                    // waarde die van moment tot moment hoort te wisselen.
                    if( snelheidKmh >= 25.0 && dKm > 0.0 )
                    {
                        const double meterKm = snelheidKmh * dtUur;
                        if( meterKm > 0.0 )
                        {
                            const double ruweFactor = dKm / meterKm;
                            if( ruweFactor > AFSTANDSFACTOR_MIN && ruweFactor < AFSTANDSFACTOR_MAX )
                            {
                                self->m_afstandsFactor =
                                    Demp( self->m_afstandsFactor, ruweFactor,
                                          dtUur * 3600.0, 10.0 );

                                // Hooguit eens per minuut wegschrijven, en
                                // alleen als hij echt verschoven is. Elke
                                // meting opslaan zou onnodig naar schijf
                                // schrijven terwijl je rijdt.
                                if( ( self->m_afstandsFactor - self->m_bewaardeFactor ) >  0.2 ||
                                    ( self->m_afstandsFactor - self->m_bewaardeFactor ) < -0.2 )
                                {
                                    self->m_bewaardeFactor = self->m_afstandsFactor;
                                    try
                                    {
                                        // Eén getal, meer is het niet. Dit
                                        // bestand gebruikt verder geen JSON,
                                        // dus daar ook geen bibliotheek voor
                                        // binnenhalen.
                                        std::ofstream uit( MetingPad() );
                                        if( uit ) uit << self->m_afstandsFactor;
                                    }
                                    catch( ... ) { /* opslaan mag nooit storen */ }
                                }
                            }
                        }
                    }

                    // Negatieve literstappen overslaan: de literteller begint
                    // opnieuw bij een nieuwe rit.
                    if( snelheidKmh >= RIJDT_DREMPEL_KMH && dLiters >= 0.0 )
                    {
                        self->m_rijdendLiters += dLiters;
                        self->m_rijdendKm += dKm;
                    }
                }
            }
            self->m_vorigMeetLiters = verbruiktNu;
            self->m_vorigMeetOdometerKm = self->m_kilometerstandKm;
            self->m_vorigMeetMoment = nuMeting;
            self->m_meetGestart = true;

            VerbruikMeting m;
            m.moment = nuMeting;
            m.verbruiktLiters = verbruiktNu;
            m.gemetenKm = self->m_meetKmTotaal;

            // Gas erop of eraf? Dan is alles in het venster achterhaald: je
            // verbruik verandert NU, maar het venster kijkt terug. Leeggooien
            // en opnieuw beginnen -- binnen een seconde staat er weer een
            // cijfer, en dan klopt het bij wat je voet doet.
            if( self->m_gasOmslag )
            {
                self->m_gasOmslag = false;
                self->m_brandstofVenster.clear();
            }

            self->m_brandstofVenster.push_back( m );
            while( !self->m_brandstofVenster.empty()
                   && std::chrono::duration<double>( nuMeting - self->m_brandstofVenster.front().moment ).count()
                          > VERBRUIK_VENSTER_SECONDEN )
            {
                self->m_brandstofVenster.pop_front();
            }

            // --- Ruw cijfer uit het venster, daarna dempen ------------------
            if( self->m_brandstofVenster.size() >= 2 )
            {
                const auto &eerste = self->m_brandstofVenster.front();
                const auto &laatste = self->m_brandstofVenster.back();
                const double spanSec =
                    std::chrono::duration<double>( laatste.moment - eerste.moment ).count();
                const double dL = laatste.verbruiktLiters - eerste.verbruiktLiters;
                const double dKmVenster = laatste.gemetenKm - eerste.gemetenKm;

                // Negatief literverschil = de teller begon opnieuw (nieuwe
                // rit). Overslaan; het venster is zo weer schoon.
                if( spanSec > MIN_SPAN_SECONDEN && dL >= 0.0 )
                {
                    // TWEE cijfers, elk met een eigen venster:
                    //
                    //  - literPerUurLang hoort BIJ dKmVenster (zelfde punten,
                    //    zelfde tijdspanne) en gaat naar l/100km. Die twee
                    //    moeten uit hetzelfde venster komen, anders klopt de
                    //    deling niet.
                    //  - literPerUurKort is voor de l/uur-weergave. Daar is
                    //    geen afstand bij nodig, dus dat kan over een veel
                    //    korter stukje. Met het lange venster bleef na het
                    //    stilzetten nog 15 seconden aan rijgegevens meetellen
                    //    -- gemeten 30-08: "94,1 l/uur" met de motor UIT.
                    const double literPerUurLang = dL / ( spanSec / 3600.0 );

                    double literPerUurKort = literPerUurLang;
                    {
                        const auto &nieuwste = self->m_brandstofVenster.back();
                        for( auto it = self->m_brandstofVenster.rbegin();
                             it != self->m_brandstofVenster.rend(); ++it )
                        {
                            const double leeftijd =
                                std::chrono::duration<double>( nieuwste.moment - it->moment ).count();
                            if( leeftijd >= LUUR_VENSTER_SECONDEN )
                            {
                                const double dLkort = nieuwste.verbruiktLiters - it->verbruiktLiters;
                                if( dLkort >= 0.0 && leeftijd > 0.0 )
                                {
                                    literPerUurKort = dLkort / ( leeftijd / 3600.0 );
                                }
                                break;
                            }
                        }
                    }

                    // Verstreken tijd sinds de vorige meting. Nodig omdat de
                    // demping op tijd werkt en niet op het aantal aanroepen
                    // (dit blok draait per frame, zo'n 59 keer per seconde).
                    const double dempDt = hadVorigeMeting
                        ? std::chrono::duration<double>( nuMeting - vorigeMeetMoment ).count()
                        : 0.0;

                    // Overgang rijden <-> stilstaan: demping overslaan, zodat
                    // het cijfer meteen omklapt in plaats van er seconden
                    // over te doen.
                    const bool rijdtNu = ( snelheidKmh >= RIJDT_DREMPEL_KMH );
                    if( rijdtNu != self->m_vorigRijdt )
                    {
                        self->m_gladLiterPerUur = literPerUurKort;
                        self->m_vorigRijdt = rijdtNu;
                    }
                    else
                    {
                        self->m_gladLiterPerUur = Demp( self->m_gladLiterPerUur, literPerUurKort,
                                                        dempDt, LUUR_TAU_SECONDEN );
                    }

                    // l/100km alleen bijwerken als je hard genoeg rijdt om
                    // het zinvol te maken (zie PER100_MIN_KMH). Daaronder
                    // laten we de laatste waarde met rust; er wordt dan toch
                    // l/uur getoond.
                    const double schaal = self->TijdSchaal();
                    const double gemSnelheid = dKmVenster / ( spanSec / 3600.0 );
                    if( gemSnelheid >= PER100_MIN_KMH * schaal && literPerUurLang > 0.0 )
                    {
                        const double ruwPer100 = literPerUurLang / gemSnelheid * 100.0;
                        self->m_gladVerbruikNu = Demp( self->m_gladVerbruikNu, ruwPer100,
                                                        dempDt, DEMP_TAU_SECONDEN );
                    }

                    // Debugregel, hooguit eens per drie seconden. Hiermee is
                    // in een ritje terug te zien WELK getal ontspoort, in
                    // plaats van dat we een theorie moeten verzinnen.
                    if( std::chrono::duration<double>( nuMeting - self->m_laatsteVerbruikLog ).count() > 3.0 )
                    {
                        self->m_laatsteVerbruikLog = nuMeting;
                        const auto bs = self->m_brandstof.HuidigeState();
                        char regel[ 520 ];
                        std::snprintf( regel, sizeof( regel ),
                            "meter=%.1f schaal=%.2f span=%.2fs n=%d tank=%.4f teller=%.4f "
                            "dL=%.4f dKm=%.4f luur_kort=%.1f luur_lang=%.1f ruw_per100=%.1f "
                            "glad_l_per_uur=%.1f glad_per100=%.1f rijdendL=%.3f rijdendKm=%.3f "
                            "odo=%.3f meetKm=%.3f afstandsfactor=%.2f gas=%.2f",
                            snelheidKmh, schaal, spanSec,
                            static_cast<int>( self->m_brandstofVenster.size() ),
                            bs.huidigeLiters, bs.verbruikSindsRitStartLiters,
                            dL, dKmVenster,
                            literPerUurKort, literPerUurLang,
                            ( gemSnelheid > 0.0 ) ? literPerUurLang / gemSnelheid * 100.0 : -1.0,
                            self->m_gladLiterPerUur, self->m_gladVerbruikNu,
                            self->m_rijdendLiters, self->m_rijdendKm,
                            self->m_kilometerstandKm, self->m_meetKmTotaal, self->m_afstandsFactor,
                            self->m_gaspedaal );
                        Logboek::Schrijf( "verbruik", regel );
                    }
                }
            }
        }

        if( !self->m_actief ) return;

        self->m_huidigeRit.huidigeSnelheidKmh = snelheidKmh;

        // Tijdens een pauze (zie GepauzeerdCallback) geen kilometers
        // "bijverzinnen" en de meetklok niet laten doortikken -- anders
        // denkt de schatting dat je heel langzaam reed terwijl je gewoon in
        // het menu zat.
        if( self->m_gepauzeerd )
        {
            return;
        }

        // Live "bijgehouden" afstand door snelheid x verstreken tijd op te
        // tellen sinds de vorige meting -- zie de opmerking bij
        // m_laatsteSnelheidMeting in TruckTracking.hxx.
        auto nu = std::chrono::steady_clock::now();
        double verstrekenUur = std::chrono::duration<double>( nu - self->m_laatsteSnelheidMeting ).count() / 3600.0;
        if( verstrekenUur > 0.0 && verstrekenUur < 0.1 ) // sanity check tegen rare sprongen
        {
            self->m_huidigeRit.afgelegdeAfstandKm += snelheidKmh * verstrekenUur;
        }
        self->m_laatsteSnelheidMeting = nu;

        // Voortschrijdend-gemiddelde-venster bijwerken: nieuw punt toevoegen,
        // en alles ouder dan VENSTER_SECONDEN weggooien.
        self->m_kmVenster.emplace_back( nu, self->m_huidigeRit.afgelegdeAfstandKm );
        while( !self->m_kmVenster.empty()
               && std::chrono::duration<double>( nu - self->m_kmVenster.front().first ).count() > VENSTER_SECONDEN )
        {
            self->m_kmVenster.pop_front();
        }
    }

    SCSAPI_VOID TruckTracking::BrandstofLitersCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                          scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( value )
        {
            // Kilometerstand meegeven, zodat een tankbeurt weet waar hij was.
            self->m_brandstof.ZetKilometerstand( self->m_kilometerstandKm );
            self->m_brandstof.ZetLiters( value->value_float.value, self->m_tankInhoudLiters );
            BrandstofState bs = self->m_brandstof.HuidigeState();
            self->m_huidigeRit.brandstofPercentage =
                self->m_tankInhoudLiters > 0.0 ? ( value->value_float.value / self->m_tankInhoudLiters ) * 100.0 : 0.0;

            // Debug: eerste keer loggen wat er echt binnenkomt (raw
            // liters, tankinhoud, berekend percentage) -- zelfde principe
            // als bij navigation.distance: eerst zien, dan vertrouwen.
            static bool eersteKeerGelogd = false;
            if( !eersteKeerGelogd )
            {
                eersteKeerGelogd = true;
                std::filesystem::path pad;
                if( const char *appdata = std::getenv( "APPDATA" ) ) pad = appdata;
                pad /= "CabNavi";
                std::error_code ec;
                std::filesystem::create_directories( pad, ec );
                pad /= "debug.log";
                std::ofstream uit( pad, std::ios::app );
                if( uit )
                {
                    uit << "[Brandstof] truck.fuel raw waarde: " << value->value_float.value
                        << " liter, tankinhoud (m_tankInhoudLiters): " << self->m_tankInhoudLiters
                        << " liter, berekend percentage: " << self->m_huidigeRit.brandstofPercentage << "%\n";
                }
            }
        }
    }

    SCSAPI_VOID TruckTracking::SchadeCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( !value ) return;

        double nieuwePercentage = value->value_float.value * 100.0;
        double sprong = nieuwePercentage - self->m_vorigeSchadePercentage;
        self->m_vorigeSchadePercentage = nieuwePercentage;
        self->m_huidigeRit.schadeChassisPercentage = nieuwePercentage;

        // Plotselinge sprong (drempel 1.5 procentpunt in een keer) -->
        // waarschijnlijk een botsing, meld dit aan de incident-recorder.
        if( sprong > 1.5 && self->m_incidentRecorder != nullptr )
        {
            std::string vermoedelijkeSpeler = "onbekend";
            if( self->m_spelersVoorIncident != nullptr )
            {
                std::vector<SpelerRecord> spelers = self->m_spelersVoorIncident->GeefSpelers();
                if( !spelers.empty() )
                {
                    // GeefSpelers() sorteert al op afstand, dus de eerste is
                    // de dichtstbijzijnde -- geen garantie dat dit de dader
                    // is, wel de meest waarschijnlijke kandidaat.
                    vermoedelijkeSpeler = spelers.front().gebruikersnaam
                        + " (TMP-ID " + std::to_string( spelers.front().accountId ) + ")";
                }
            }
            self->m_incidentRecorder->MeldSchade( vermoedelijkeSpeler );
        }
    }

    SCSAPI_VOID TruckTracking::NavigatieAfstandCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                           scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( !value ) return;

        double afstandMeter = value->value_float.value;

        // Debug-logregel de EERSTE keer dat dit kanaal een waarde geeft --
        // net als bij scs_telemetry_init destijds: eerst bevestigen dat het
        // echt binnenkomt, niet blind vertrouwen.
        static bool eersteKeerGelogd = false;
        if( !eersteKeerGelogd )
        {
            eersteKeerGelogd = true;
            std::filesystem::path pad;
            if( const char *appdata = std::getenv( "APPDATA" ) ) pad = appdata;
            pad /= "CabNavi";
            std::error_code ec;
            std::filesystem::create_directories( pad, ec );
            pad /= "debug.log";
            std::ofstream uit( pad, std::ios::app );
            if( uit )
            {
                uit << "[Navigatie] truck.navigation.distance kanaal ontvangen, eerste waarde: "
                    << afstandMeter << " meter\n";
            }
        }

        self->m_navigatieAfstandMeter = afstandMeter;
    }

    // --- Nieuwe kanaal-callbacks --------------------------------------
    //
    // Allemaal hetzelfde patroon: waarde overnemen, meer niet. Deze draaien
    // op de game-thread en worden vaak aangeroepen, dus hier gebeurt bewust
    // geen rekenwerk, geen logging en geen schijf-toegang -- het omrekenen
    // (m/s naar km/h, 0-1 naar procenten) doen we pas in
    // HuidigeVoertuigStatus(), dat alleen draait als de overlay open is.

    SCSAPI_VOID TruckTracking::BereikCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                scs_context_t context )
    {
        if( !value ) return;
        static_cast<TruckTracking *>( context )->m_bereikKm = value->value_float.value;
    }

    SCSAPI_VOID TruckTracking::VerbruikCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                  scs_context_t context )
    {
        if( !value ) return;
        static_cast<TruckTracking *>( context )->m_verbruikLiterPerKm = value->value_float.value;
    }

    SCSAPI_VOID TruckTracking::KilometerstandCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                         scs_context_t context )
    {
        if( !value ) return;
        static_cast<TruckTracking *>( context )->m_kilometerstandKm = value->value_float.value;
    }

    SCSAPI_VOID TruckTracking::SnelheidslimietCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                          scs_context_t context )
    {
        if( !value ) return;
        // Het spel stuurt 0 als er geen limiet bekend is (bv. buiten de
        // route, of als je de Route Advisor-limiet hebt uitgezet). Dat is
        // iets anders dan "limiet is nul", dus zetten we hem dan terug op
        // -1 = onbekend, anders zou de overlay "0 km/h" gaan tonen en zou
        // de te-hard-waarschuwing continu afgaan.
        float ruw = value->value_float.value;
        static_cast<TruckTracking *>( context )->m_snelheidslimietMs = ( ruw > 0.1f ) ? ruw : -1.0;
    }

    SCSAPI_VOID TruckTracking::CruiseControlCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                        scs_context_t context )
    {
        if( !value ) return;
        static_cast<TruckTracking *>( context )->m_cruiseControlMs = value->value_float.value;
    }

    SCSAPI_VOID TruckTracking::GaspedaalCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                    scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( !value ) return;
        self->m_gaspedaal = value->value_float.value;

        // Alleen de OMSLAG telt: gas erop of gas eraf. Kleine bewegingen van
        // je voet moeten het venster niet steeds legen.
        const bool ingedrukt = ( self->m_gaspedaal > GAS_DREMPEL );
        if( ingedrukt != self->m_gasIngedrukt )
        {
            self->m_gasIngedrukt = ingedrukt;
            self->m_gasOmslag = true; // wordt in SnelheidCallback afgehandeld
        }
    }

    SCSAPI_VOID TruckTracking::SchadeMotorCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                      scs_context_t context )
    {
        if( !value ) return;
        static_cast<TruckTracking *>( context )->m_schadeMotor = value->value_float.value * 100.0;
    }

    SCSAPI_VOID TruckTracking::SchadeBakCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                   scs_context_t context )
    {
        if( !value ) return;
        static_cast<TruckTracking *>( context )->m_schadeBak = value->value_float.value * 100.0;
    }

    SCSAPI_VOID TruckTracking::SchadeCabineCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                       scs_context_t context )
    {
        if( !value ) return;
        static_cast<TruckTracking *>( context )->m_schadeCabine = value->value_float.value * 100.0;
    }

    SCSAPI_VOID TruckTracking::SchadeWielenCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                       scs_context_t context )
    {
        if( !value ) return;
        static_cast<TruckTracking *>( context )->m_schadeWielen = value->value_float.value * 100.0;
    }

    SCSAPI_VOID TruckTracking::AanhangerSchadeCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                          scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( !value )
        {
            // Geen waarde = aanhanger losgekoppeld. Terug naar "onbekend",
            // anders blijft de laatste schadewaarde van een oude trailer
            // hangen terwijl je zonder rijdt.
            self->m_aanhangerSchade = -1.0;
            self->m_heeftAanhanger = false;
            return;
        }
        self->m_aanhangerSchade = value->value_float.value * 100.0;
        self->m_heeftAanhanger = true;
        self->m_huidigeRit.aanhangerSchadePercentage = self->m_aanhangerSchade;
    }

    SCSAPI_VOID TruckTracking::LadingSchadeCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                       scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( !value )
        {
            self->m_ladingSchade = -1.0;
            return;
        }
        self->m_ladingSchade = value->value_float.value * 100.0;
        self->m_huidigeRit.ladingSchadePercentage = self->m_ladingSchade;
    }

    TruckTracking::VoertuigStatus TruckTracking::HuidigeVoertuigStatus() const
    {
        VoertuigStatus s;
        s.bereikKm = m_bereikKm;
        // Ruw kanaal is liter per km -> omrekenen naar de gebruikelijke
        // l/100km die je op een echt dashboard ziet.
        s.verbruikLiterPer100Km = ( m_verbruikLiterPerKm >= 0.0 ) ? m_verbruikLiterPerKm * 100.0 : -1.0;

        // Eigen, betrouwbaardere berekening op basis van het brandstofniveau
        // (dat IS loepzuiver, zie overdracht) i.p.v. het SCS-kanaal hierboven,
        // dat een tripcomputer-cijfer van het spel zelf is en niet je
        // werkelijke rijgedrag weerspiegelt (zie onderzoek 30-08).
        //
        // Het rekenwerk zelf gebeurt in SnelheidCallback, waar de metingen
        // binnenkomen; hier lezen we alleen de uitkomst. Dat scheelt elk
        // frame hetzelfde venster opnieuw doorrekenen.
        {
            // Onder PER100_MIN_KMH tonen we l/uur: daarboven zegt l/100km
            // pas iets. "Stationair" alleen als je echt stilstaat.
            s.echtStil = ( m_liveSnelheidKmh < RIJDT_DREMPEL_KMH );
            s.staatStil = ( m_liveSnelheidKmh < PER100_MIN_KMH );

            // --- Gemiddelde: alleen wat je RIJDEND verbruikte ---
            // Stationair draaien telt bewust NIET mee; anders klimt dit cijfer
            // eindeloos zodra je even stilstaat (gemeten: liep op tot 178).
            if( m_rijdendKm > 0.005 && m_rijdendLiters > 0.0 )
            {
                s.verbruikGemiddeldLiterPer100Km = m_rijdendLiters / m_rijdendKm * 100.0;
            }

            // l/uur voor de weergave omrekenen. NIET door de tijdschaal
            // delen: dat gaf een factor 2 te laag (gemeten 30-08: 8,0 waar
            // het dashboard 16,9 aangaf, en 1,0 waar het 2,0 aangaf). De
            // juiste verhouding is die tussen de kilometerteller en de
            // snelheidsmeter -- dezelfde die het rijdende cijfer goed maakt.
            if( m_gladLiterPerUur >= 0.0 )
            {
                const double schaal = TijdSchaal();
                const double deler = ( m_afstandsFactor > 0.0 && schaal > 0.0 )
                    ? ( m_afstandsFactor / schaal )
                    : 1.0;
                s.verbruikLiterPerUur = ( deler > 0.0 ) ? m_gladLiterPerUur / deler : m_gladLiterPerUur;
            }

            s.verbruikNuLiterPer100Km = m_gladVerbruikNu;
            if( s.verbruikNuLiterPer100Km < 0.0 )
            {
                s.verbruikNuLiterPer100Km = s.verbruikGemiddeldLiterPer100Km; // terugval
            }
            // Geen bovengrens meer hier: het kaartje toont km/l en begrenst
            // daar op 99,9 km/l. In l/100km afkappen zou juist de
            // zuinige kant kapotmaken -- uitrollen hoort een HOOG km/l te
            // geven, en dat komt uit een LAGE l/100km.
        }

        s.kilometerstandKm = m_kilometerstandKm;
        s.snelheidslimietKmh = ( m_snelheidslimietMs >= 0.0 ) ? m_snelheidslimietMs * 3.6 : -1.0;
        s.cruiseControlKmh = m_cruiseControlMs * 3.6;
        s.schadeMotor = m_schadeMotor;
        s.schadeBak = m_schadeBak;
        s.schadeCabine = m_schadeCabine;
        s.schadeWielen = m_schadeWielen;
        s.schadeChassis = m_vorigeSchadePercentage;
        s.aanhangerSchade = m_aanhangerSchade;
        s.ladingSchade = m_ladingSchade;
        s.ladingGewichtKg = m_ladingGewichtKg;
        s.heeftAanhanger = m_heeftAanhanger;
        return s;
    }

    bool TruckTracking::RijdtTeHard() const
    {
        if( m_snelheidslimietMs < 0.0 ) return false;
        double limietKmh = m_snelheidslimietMs * 3.6;
        // 3 km/h marge: bij exact op de limiet rijden schommelt de snelheid
        // altijd een beetje, en dan zou de waarschuwing gaan knipperen.
        return m_liveSnelheidKmh > limietKmh + 3.0;
    }

    SCSAPI_VOID TruckTracking::LocalScaleCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                     scs_context_t )
    {
        if( !value ) return;
        double schaal = value->value_float.value;

        // Elke ~10 seconden een regel loggen (niet elke frame, dat wordt
        // te veel) zodat we kunnen zien hoe deze waarde varieert tussen
        // stad en snelweg, en het kunnen vergelijken met onze eigen
        // schatting versus de werkelijke verstreken tijd.
        static auto laatsteLog = std::chrono::steady_clock::time_point{};
        auto nu = std::chrono::steady_clock::now();
        if( laatsteLog.time_since_epoch().count() == 0
            || std::chrono::duration<double>( nu - laatsteLog ).count() > 10.0 )
        {
            laatsteLog = nu;
            std::filesystem::path pad;
            if( const char *appdata = std::getenv( "APPDATA" ) ) pad = appdata;
            pad /= "CabNavi";
            std::error_code ec;
            std::filesystem::create_directories( pad, ec );
            pad /= "debug.log";
            std::ofstream uit( pad, std::ios::app );
            if( uit )
            {
                uit << "[LocalScale] huidige waarde: " << schaal << "\n";
            }
        }
    }

    SCSAPI_VOID TruckTracking::ConfigCallback( const scs_event_t, const void *event_info, scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        auto *cfg = static_cast<const scs_telemetry_configuration_t *>( event_info );
        self->OpVoertuigConfig( cfg );
    }

    SCSAPI_VOID TruckTracking::GameplayEventCallback( const scs_event_t, const void *event_info, scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        auto *info = static_cast<const scs_telemetry_gameplay_event_t *>( event_info );
        self->OpGameplayEvent( info );
    }
}

#include "FuelCosts.hxx"
#include "Kaartdata.hxx"
#include "Logboek.hxx"
#include "ScsArchief.hxx"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>

using json = nlohmann::json;

namespace Ritten
{
    std::filesystem::path FuelCosts::InstellingenPad()
    {
        std::filesystem::path basis;
        if( const char *appdata = std::getenv( "APPDATA" ) )
        {
            basis = appdata;
        }
        else
        {
            basis = std::filesystem::current_path();
        }
        basis /= "CabNavi";
        std::error_code ec;
        std::filesystem::create_directories( basis, ec );
        return basis / "instellingen.json";
    }

    FuelCosts::FuelCosts()
    {
        LaadInstellingen();
        LaadPrijzenPerLand();
    }

    void FuelCosts::LaadInstellingen()
    {
        std::ifstream in( InstellingenPad() );
        if( !in ) return;
        try
        {
            json j; in >> j;
            m_instellingen.prijsPerLiterEuro = j.value( "prijs_per_liter_euro", m_instellingen.prijsPerLiterEuro );
            m_instellingen.landAutomatisch = j.value( "land_automatisch", m_instellingen.landAutomatisch );
        }
        catch( ... ) { /* corrupt bestand: default gebruiken */ }
    }

    void FuelCosts::SlaInstellingenOp() const
    {
        std::ofstream uit( InstellingenPad() );
        if( !uit ) return;
        json j;
        j[ "prijs_per_liter_euro" ] = m_instellingen.prijsPerLiterEuro;
        j[ "land_automatisch" ] = m_instellingen.landAutomatisch;
        uit << j.dump( 2 );
    }

    void FuelCosts::ZetPrijsPerLiter( double prijs )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_instellingen.prijsPerLiterEuro = prijs;
        SlaInstellingenOp();
    }

    std::vector<FuelCosts::Tankbeurt> FuelCosts::TankbeurtenDezeRit() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_tankbeurten;
    }

    int FuelCosts::AantalTankbeurten() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_tankbeurtenTotaal;
    }

    double FuelCosts::TotaalGetanktLiters() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_getanktTotaalLiters;
    }

    void FuelCosts::ZetKilometerstand( double km )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_kmStand = km;
    }

    std::filesystem::path FuelCosts::PrijzenPad()
    {
        std::filesystem::path pad;
        if( const char *appdata = std::getenv( "APPDATA" ) ) pad = appdata;
        else pad = std::filesystem::current_path();
        pad /= "CabNavi";
        std::error_code ec;
        std::filesystem::create_directories( pad, ec );
        return pad / "brandstofprijzen.json";
    }

    void FuelCosts::LaadPrijzenPerLand()
    {
        std::lock_guard<std::mutex> lock( m_mutex );

        // If the file does not exist yet, we write one with the prices ETS2
        // itself uses: from def/country/*.sii (fuel_price), read from base.scs
        // of 1.60. MEASURED 04-09: the old indicative prices were ~30% below
        // (Netherlands 1.63 vs 2.25). The SDK does not pass the pump price,
        // but the amount you pay in the game is exactly this number times
        // the litres.
        const auto pad = PrijzenPad();
        if( !std::filesystem::exists( pad ) )
        {
            std::ofstream uit( pad );
            if( uit )
            {
                uit << "{\n"
                    << "  \"_uitleg\": \"Literprijs per land zoals ETS2 1.60 hem zelf gebruikt (def/country/*.sii, fuel_price). Verandert SCS de prijzen, pas dan hier de getallen aan.\",\n"
                    << "  \"albania\": 2.214,\n"
                    << "  \"austria\": 2.109,\n"
                    << "  \"belgium\": 2.289,\n"
                    << "  \"bosnia\": 1.633,\n"
                    << "  \"bulgaria\": 1.527,\n"
                    << "  \"croatia\": 1.672,\n"
                    << "  \"czech\": 1.911,\n"
                    << "  \"denmark\": 2.555,\n"
                    << "  \"estonia\": 2.071,\n"
                    << "  \"finland\": 2.117,\n"
                    << "  \"france\": 2.109,\n"
                    << "  \"germany\": 2.273,\n"
                    << "  \"greece\": 2.055,\n"
                    << "  \"hungary\": 1.574,\n"
                    << "  \"iceland\": 2.000,\n"
                    << "  \"italy\": 1.967,\n"
                    << "  \"kosovo\": 1.475,\n"
                    << "  \"latvia\": 2.070,\n"
                    << "  \"lithuania\": 1.952,\n"
                    << "  \"luxembourg\": 2.077,\n"
                    << "  \"macedonia\": 1.549,\n"
                    << "  \"montenegro\": 1.570,\n"
                    << "  \"netherlands\": 2.253,\n"
                    << "  \"norway\": 2.247,\n"
                    << "  \"poland\": 1.781,\n"
                    << "  \"portugal\": 2.046,\n"
                    << "  \"romania\": 1.961,\n"
                    << "  \"russia\": 0.829,\n"
                    << "  \"serbia\": 1.804,\n"
                    << "  \"slovakia\": 1.528,\n"
                    << "  \"slovenia\": 1.696,\n"
                    << "  \"spain\": 1.771,\n"
                    << "  \"sweden\": 2.220,\n"
                    << "  \"switzerland\": 2.360,\n"
                    << "  \"turkey\": 1.509,\n"
                    << "  \"uk\": 1.928\n"
                    << "}\n";
            }
        }

        m_prijsPerLand.clear();
        try
        {
            std::ifstream in( pad );
            if( !in ) return;
            nlohmann::json j;
            in >> j;
            m_prijzenVersie = j.value( "_versie", std::string() );
            m_prijzenBron = j.value( "_bron", std::string() );
            m_garageKorting = j.value( "_garage_korting", m_garageKorting );
            m_landPosities.clear();
            if( j.contains( "_landpos" ) && j[ "_landpos" ].is_object() )
            {
                for( auto it = j[ "_landpos" ].begin(); it != j[ "_landpos" ].end(); ++it )
                {
                    // [x, z] -- iterate like TripLogger does with its arrays
                    if( !it.value().is_array() ) continue;
                    XZ xz; int n = 0;
                    for( const auto &v : it.value() )
                    {
                        if( !v.is_number() ) { n = -1; break; }
                        if( n == 0 ) xz.x = v.get<double>(); else if( n == 1 ) xz.z = v.get<double>();
                        ++n;
                    }
                    if( n == 2 ) m_landPosities[ it.key() ] = xz;
                }
            }
            for( auto it = j.begin(); it != j.end(); ++it )
            {
                if( it.key().rfind( "_", 0 ) == 0 ) continue;  // lines starting with _ are explanation
                if( it.value().is_number() )
                {
                    m_prijsPerLand[ it.key() ] = it.value().get<double>();
                }
            }
        }
        catch( ... )
        {
            // Broken file: then simply use the manual price.
            m_prijsPerLand.clear();
        }
    }

    double FuelCosts::PrijsVoorLand( const std::string &landcode ) const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if( !landcode.empty() )
        {
            const auto it = m_prijsPerLand.find( landcode );
            if( it != m_prijsPerLand.end() ) return it->second;
        }
        return m_instellingen.prijsPerLiterEuro;  // unknown country -> your own price
    }

    void FuelCosts::ZetHuidigLand( const std::string &landcode )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_huidigLand = landcode;
    }

    std::string FuelCosts::HuidigLand() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_huidigLand;
    }

    std::vector<std::string> FuelCosts::BekendeLanden() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        std::vector<std::string> uit;
        uit.reserve( m_prijsPerLand.size() );
        for( const auto &p : m_prijsPerLand ) uit.push_back( p.first );
        return uit;  // std::map is already sorted
    }

    double FuelCosts::PrijsPerLiter() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_instellingen.prijsPerLiterEuro;
    }

    void FuelCosts::StartNieuweRit()
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        SluitOpenTankbeurt();  // a stop still collecting belongs to the previous trip
        m_litersBijRitStart = m_state.huidigeLiters;
        m_state.verbruikSindsRitStartLiters = 0.0;
        m_state.kostenDezeRitEuro = 0.0;

        // Also clear the refuelling list: that belongs to THIS trip. The
        // totals stay, because they cover the whole session.
        m_tankbeurten.clear();
        m_vorigNiveau = m_state.huidigeLiters;
    }

    void FuelCosts::ZetLiters( double liters, double tankInhoud )
    {
        std::lock_guard<std::mutex> lock( m_mutex );

        if( m_litersBijRitStart < 0.0 )
        {
            m_litersBijRitStart = liters;  // very first reading
        }

        // Consumption = decrease relative to the maximum seen since trip
        // start. That way refuelling on the road does not count as "negative
        // consumption": every time the level is above the previous minimum,
        // we shift the reference up (you paid at the pump, not here -- this
        // system measures consumption, not refuelling/prices).
        if( m_vorigNiveau < 0.0 ) m_vorigNiveau = liters;

        // Right after a truck switch the level is simply the other truck's
        // tank. Take it as the new baseline for five seconds: no refuel, no
        // consumption, and a refuel that was still collecting is dropped --
        // it was the jump itself.
        if( m_naWissel )
        {
            if( std::chrono::duration<double>( std::chrono::steady_clock::now() - m_wisselMoment ).count() < 5.0 )
            {
                if( liters > m_vorigNiveau ) m_litersBijRitStart += liters - m_vorigNiveau;
                else m_litersBijRitStart -= m_vorigNiveau - liters;
                m_open.actief = false;
                m_vorigNiveau = liters;
                m_state.huidigeLiters = liters;
                m_state.tankInhoudLiters = tankInhoud;
                m_state.verbruikSindsRitStartLiters = std::max( 0.0, m_litersBijRitStart - liters );
                return;
            }
            m_naWissel = false;
        }

        if( liters > m_vorigNiveau )
        {
            const double bij = liters - m_vorigNiveau;

            // Shift the reference, otherwise refuelling looks like "free" km.
            m_litersBijRitStart += bij;

            // Collect into the open refuelling stop. The country, garage and
            // price are fixed at the first step: you do not move while pumping.
            const auto nu = std::chrono::steady_clock::now();
            if( !m_open.actief )
            {
                m_open = OpenTankbeurt{};
                m_open.actief = true;
                Tankbeurt &t = m_open.t;

                // Which country? Preferably from the position: the nearest city
                // on the map knows its country. Otherwise the manual choice. And
                // the price of that country if we have it, else your own price.
                // (m_mutex is already held, so do not call PrijsVoorLand here.)
                std::string land = m_huidigLand;
                if( m_instellingen.landAutomatisch && m_posBekend )
                {
                    std::string l, st; bool g = false;
                    BepaalLand( l, st, g );
                    if( !l.empty() ) { land = l; t.stad = st; }
                    // Refuelling inside a large-garage yard is only possible at
                    // your OWN garage: that pump is the owner discount.
                    // MEASURED 04-09: x0.85 in NL and DE, to the cent -- and
                    // economy_data.sii says fuel_discount_in_garage: 0.15.
                    t.garage = g;
                }
                double prijs = m_instellingen.prijsPerLiterEuro;
                if( !land.empty() )
                {
                    const auto it = m_prijsPerLand.find( land );
                    if( it != m_prijsPerLand.end() ) prijs = it->second;
                }
                if( t.garage ) prijs *= ( 1.0 - m_garageKorting );
                t.land = land;
                t.prijsPerLiter = prijs;
                t.kmStand = m_kmStand;
            }
            m_open.liters += bij;
            m_open.laatsteStap = nu;
        }
        // Level stopped rising for three seconds: the pump is done. Below 5
        // litres in total it is noise or a small jump by the game, not a stop.
        if( m_open.actief && std::chrono::duration<double>( std::chrono::steady_clock::now() - m_open.laatsteStap ).count() > 3.0 )
        {
            SluitOpenTankbeurt();
        }
        // Session counter: add every DECREASE, ignore increases (refuelling).
        // m_vorigNiveau is set to the current level at StartNieuweRit, so
        // there is no jump there -- this counter runs neatly across trip
        // boundaries.
        if( m_vorigNiveau >= 0.0 && liters < m_vorigNiveau )
        {
            m_state.verbruiktSessieLiters += m_vorigNiveau - liters;
        }
        m_vorigNiveau = liters;

        m_state.huidigeLiters = liters;
        m_state.tankInhoudLiters = tankInhoud;
        m_state.verbruikSindsRitStartLiters = std::max( 0.0, m_litersBijRitStart - liters );
        m_state.kostenDezeRitEuro = m_state.verbruikSindsRitStartLiters * m_instellingen.prijsPerLiterEuro;
    }

    double FuelCosts::SluitRitAf()
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        SluitOpenTankbeurt();
        double kosten = m_state.kostenDezeRitEuro;
        m_state.totaalVerbruikLiters += m_state.verbruikSindsRitStartLiters;
        m_state.totaalKostenEuro += kosten;
        return kosten;
    }

    BrandstofState FuelCosts::HuidigeState() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_state;
    }

    void FuelCosts::SluitOpenTankbeurt()
    {
        if( !m_open.actief ) return;
        m_open.actief = false;
        if( m_open.liters <= 5.0 ) return;

        Tankbeurt t = m_open.t;
        t.liters = m_open.liters;
        t.kostenEuro = t.liters * t.prijsPerLiter;

        char regel[ 200 ];
        std::snprintf( regel, sizeof( regel ), "refuel: %.1f L near %s (%s)%s, %.3f/L -> %.2f",
                       t.liters, t.stad.empty() ? "?" : t.stad.c_str(), t.land.empty() ? "manual price" : t.land.c_str(),
                       t.garage ? ", own garage" : "", t.prijsPerLiter, t.kostenEuro );
        Logboek::Schrijf( "event", regel );
        m_tankbeurten.insert( m_tankbeurten.begin(), t );  // newest first
        if( m_tankbeurten.size() > 10 ) m_tankbeurten.pop_back();
        ++m_tankbeurtenTotaal;
        m_getanktTotaalLiters += t.liters;
    }

    void FuelCosts::VoertuigGewisseld()
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_wisselMoment = std::chrono::steady_clock::now();
        m_naWissel = true;
        // The switch may arrive AFTER the level jump already opened a refuel
        // (the fuel channel is not ordered against the configuration event).
        // That open one was the jump: drop it.
        if( m_open.actief && std::chrono::duration<double>( m_wisselMoment - m_open.laatsteStap ).count() < 5.0 )
            m_open.actief = false;
    }

    // ---- Position, automatic country ------------------------------------------

    void FuelCosts::ZetPositie( const double x, const double z, const bool bekend )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_posX = x; m_posZ = z; m_posBekend = bekend;
    }

    void FuelCosts::ZetLandAutomatisch( const bool aan )
    {
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            m_instellingen.landAutomatisch = aan;
        }
        SlaInstellingenOp();
    }

    bool FuelCosts::LandAutomatisch() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_instellingen.landAutomatisch;
    }

    FuelCosts::Plaatsbepaling FuelCosts::HuidigePlaats() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        Plaatsbepaling p;
        if( !m_posBekend ) return p;
        std::string land, stad; bool garage = false;
        BepaalLand( land, stad, garage );
        if( land.empty() ) return p;
        p.bekend = true; p.stad = stad.empty() ? "?" : stad; p.land = land; p.bijGarage = garage;
        p.prijs = m_instellingen.prijsPerLiterEuro;
        const auto it = m_prijsPerLand.find( land );
        if( it != m_prijsPerLand.end() ) p.prijs = it->second;
        if( garage ) p.prijs *= ( 1.0 - m_garageKorting );
        return p;
    }

    void FuelCosts::BepaalLand( std::string &land, std::string &stad, bool &bijGarage ) const
    {
        land.clear(); stad.clear(); bijGarage = false;
        const Kaartdata::Plaats pl = Kaartdata::Bepaal( m_posX, m_posZ );
        bijGarage = pl.bijGarage;
        // Cities on the map are 5-10 km (game metres) apart. More than 15 km
        // from every city the table knows means a region it predates -- a new
        // map DLC. Then the country centres, read from the same game files
        // that gave the prices (so new countries are in there too), decide.
        if( !pl.land.empty() && pl.afstandStadM <= 15000.0 ) { land = pl.land; stad = pl.stad; return; }
        if( !m_landPosities.empty() )
        {
            double besteKw = -1.0;
            for( const auto &[ token, xz ] : m_landPosities )
            {
                const double dx = xz.x - m_posX, dz = xz.z - m_posZ, kw = dx * dx + dz * dz;
                if( besteKw < 0.0 || kw < besteKw ) { besteKw = kw; land = token; }
            }
            return;
        }
        if( !pl.land.empty() ) { land = pl.land; stad = pl.stad; }  // no centres: nearest city, however far
    }

    // ---- Prices straight from the game files -------------------------------------

    std::string FuelCosts::PrijzenBron() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_prijzenBron;
    }

    void FuelCosts::StartSpelPrijzen( const std::filesystem::path &spelmap, const std::string &versieSleutel )
    {
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            if( !versieSleutel.empty() && m_prijzenVersie == versieSleutel && !m_prijsPerLand.empty() )
            {
                Logboek::Schrijf( "start", "fuel prices: cached for this game version (" + std::to_string( m_prijsPerLand.size() ) + " countries)" );
                return;
            }
        }
        if( m_spelThread.joinable() ) m_spelThread.join();
        try
        {
            m_spelThread = std::thread( &FuelCosts::LeesSpelPrijzen, this, spelmap, versieSleutel );
        }
        catch( ... )
        {
            Logboek::Schrijf( "event", "fuel prices: background thread could not start, keeping file" );
        }
    }

    void FuelCosts::LeesSpelPrijzen( std::filesystem::path spelmap, std::string versieSleutel )
    {
        // Runs on its own thread. Nothing here touches the game thread's state
        // until the very end, under the mutex. No Logboek calls in the loop
        // either; one summary at the end.
        std::map<std::string, double> prijzen;
        std::map<std::string, XZ> posities;
        double korting = -1.0;
        std::string bron;
        std::error_code ec;
        std::vector<std::filesystem::path> archieven;
        for( const auto &e : std::filesystem::directory_iterator( spelmap, ec ) )
        {
            if( !e.is_regular_file( ec ) ) continue;
            const std::string naam = e.path().filename().string();
            if( e.path().extension() != ".scs" ) continue;
            // def.scs and base.scs hold the base game; dlc_*.scs add countries.
            // Skip the big texture/model/locale archives: they have no def/country.
            if( naam == "def.scs" || naam == "base.scs" || naam.rfind( "dlc_", 0 ) == 0 ) archieven.push_back( e.path() );
        }
        std::sort( archieven.begin(), archieven.end() );  // base first, then DLCs override
        std::string fouten;
        for( const auto &pad : archieven )
        {
            try
            {
                ScsArchief a;
                std::string fout;
                if( !a.Open( pad.string(), fout ) ) { fouten += pad.filename().string() + ": " + fout + "; "; continue; }
                if( korting < 0.0 ) { const double k = a.GarageKorting(); if( k >= 0.0 && k < 1.0 ) korting = k; }
                if( !a.Bevat( "def/country" ) ) continue;
                const auto p = a.Brandstofprijzen();
                if( p.empty() ) continue;
                for( const auto &[ land, prijs ] : p ) prijzen[ land ] = prijs;
                for( const auto &[ land, xz ] : a.Landposities() ) posities[ land ] = XZ{ xz.x, xz.z };
                bron += ( bron.empty() ? "" : "+" ) + pad.filename().string();
            }
            catch( ... ) { fouten += pad.filename().string() + ": exception; "; }
        }

        if( prijzen.empty() )
        {
            Logboek::Schrijf( "event", "fuel prices: nothing found in game archives, keeping file" + ( fouten.empty() ? std::string() : " (" + fouten + ")" ) );
            return;
        }
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            m_prijsPerLand = prijzen;
            m_prijzenVersie = versieSleutel;
            m_prijzenBron = bron;
            if( korting >= 0.0 ) m_garageKorting = korting;
            if( !posities.empty() ) m_landPosities = posities;
        }
        SchrijfPrijzenBestand( prijzen, versieSleutel, bron );
        char extra[ 96 ];
        std::snprintf( extra, sizeof( extra ), ", garage discount %.0f%%, %zu country centres", ( korting >= 0.0 ? korting : m_garageKorting ) * 100.0, posities.size() );
        Logboek::Schrijf( "event", "fuel prices from game files: " + std::to_string( prijzen.size() ) + " countries (" + bron + ")" + extra + ( fouten.empty() ? std::string() : " -- skipped: " + fouten ) );
    }

    void FuelCosts::SchrijfPrijzenBestand( const std::map<std::string, double> &prijzen, const std::string &versie, const std::string &bron ) const
    {
        // The only write in this feature, and it goes to OUR file in AppData.
        double korting; std::map<std::string, XZ> posities;
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            korting = m_garageKorting; posities = m_landPosities;
        }
        std::ofstream uit( PrijzenPad(), std::ios::trunc );
        if( !uit ) return;
        uit << std::fixed;
        uit << "{\n"
            << "  \"_uitleg\": \"Fuel price per litre per country, read from the game's own files (def/country, fuel_price). Rewritten automatically when the game version changes.\",\n"
            << "  \"_versie\": \"" << versie << "\",\n"
            << "  \"_bron\": \"" << bron << "\",\n"
            << "  \"_garage_korting\": " << std::setprecision( 3 ) << korting << ",\n"
            << "  \"_landpos\": {";
        std::size_t k = 0;
        for( const auto &[ land, xz ] : posities )
            uit << ( k++ ? ", " : "" ) << "\"" << land << "\": [" << std::setprecision( 0 ) << xz.x << ", " << xz.z << "]";
        uit << "},\n";
        std::size_t n = 0;
        for( const auto &[ land, prijs ] : prijzen )
        {
            uit << "  \"" << land << "\": " << std::fixed << std::setprecision( 3 ) << prijs << ( ++n < prijzen.size() ? ",\n" : "\n" );
        }
        uit << "}\n";
    }

    FuelCosts::~FuelCosts()
    {
        if( m_spelThread.joinable() ) m_spelThread.join();
        std::lock_guard<std::mutex> lock( m_mutex );
        SluitOpenTankbeurt();  // so a refuel right before quitting still reaches the log
    }
}

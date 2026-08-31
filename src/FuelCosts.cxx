#include "FuelCosts.hxx"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>

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
        }
        catch( ... ) { /* corrupt bestand: default gebruiken */ }
    }

    void FuelCosts::SlaInstellingenOp() const
    {
        std::ofstream uit( InstellingenPad() );
        if( !uit ) return;
        json j;
        j[ "prijs_per_liter_euro" ] = m_instellingen.prijsPerLiterEuro;
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

    namespace
    {
        std::filesystem::path PrijzenPad()
        {
            std::filesystem::path pad;
            if( const char *appdata = std::getenv( "APPDATA" ) ) pad = appdata;
            else pad = std::filesystem::current_path();
            pad /= "CabNavi";
            std::error_code ec;
            std::filesystem::create_directories( pad, ec );
            return pad / "brandstofprijzen.json";
        }
    }

    void FuelCosts::LaadPrijzenPerLand()
    {
        std::lock_guard<std::mutex> lock( m_mutex );

        // Bestaat het bestand nog niet, dan schrijven we er een met
        // richtprijzen. Bewust "ongeveer": de echte pompprijs geeft het spel
        // niet door, dus we doen niet alsof dit exact is. Pas het gerust aan.
        const auto pad = PrijzenPad();
        if( !std::filesystem::exists( pad ) )
        {
            std::ofstream uit( pad );
            if( uit )
            {
                uit << "{\n"
                    << "  \"_uitleg\": \"Richtprijzen per liter. Het spel geeft de echte pompprijs niet door; pas deze getallen gerust aan.\",\n"
                    << "  \"austria\": 1.42,\n"
                    << "  \"belgium\": 1.55,\n"
                    << "  \"czech\": 1.28,\n"
                    << "  \"denmark\": 1.48,\n"
                    << "  \"france\": 1.52,\n"
                    << "  \"germany\": 1.45,\n"
                    << "  \"hungary\": 1.30,\n"
                    << "  \"italy\": 1.60,\n"
                    << "  \"luxembourg\": 1.24,\n"
                    << "  \"netherlands\": 1.63,\n"
                    << "  \"norway\": 1.70,\n"
                    << "  \"poland\": 1.32,\n"
                    << "  \"slovakia\": 1.34,\n"
                    << "  \"sweden\": 1.58,\n"
                    << "  \"switzerland\": 1.66,\n"
                    << "  \"uk\": 1.68\n"
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
            for( auto it = j.begin(); it != j.end(); ++it )
            {
                if( it.key().rfind( "_", 0 ) == 0 ) continue; // regels die met _ beginnen zijn uitleg
                if( it.value().is_number() )
                {
                    m_prijsPerLand[ it.key() ] = it.value().get<double>();
                }
            }
        }
        catch( ... )
        {
            // Kapot bestand: dan gewoon de handmatige prijs gebruiken.
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
        return m_instellingen.prijsPerLiterEuro; // onbekend land -> jouw eigen prijs
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
        return uit; // std::map is al gesorteerd
    }

    double FuelCosts::PrijsPerLiter() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_instellingen.prijsPerLiterEuro;
    }

    void FuelCosts::StartNieuweRit()
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_litersBijRitStart = m_state.huidigeLiters;
        m_state.verbruikSindsRitStartLiters = 0.0;
        m_state.kostenDezeRitEuro = 0.0;

        // Ook de tankbeurten-lijst leegmaken: die hoort bij DEZE rit. De
        // totalen blijven wel staan, want die gaan over de hele sessie.
        m_tankbeurten.clear();
        m_vorigNiveau = m_state.huidigeLiters;
    }

    void FuelCosts::ZetLiters( double liters, double tankInhoud )
    {
        std::lock_guard<std::mutex> lock( m_mutex );

        if( m_litersBijRitStart < 0.0 )
        {
            m_litersBijRitStart = liters; // eerste meting ooit
        }

        // Verbruik = afname t.o.v. het maximum dat sinds ritstart gezien is.
        // Zo telt bijtanken onderweg niet mee als "negatief verbruik": elke
        // keer dat het niveau hoger ligt dan het vorige minimum, schuiven we
        // de referentie mee omhoog (je hebt betaald bij het tanken, niet
        // hier -- dit systeem meet verbruik, geen tankbeurten/prijzen).
        if( m_vorigNiveau < 0.0 ) m_vorigNiveau = liters;

        if( liters > m_vorigNiveau )
        {
            const double bij = liters - m_vorigNiveau;

            // Referentie meeschuiven, anders lijkt tanken op "gratis" km's.
            m_litersBijRitStart += bij;

            // Boven de 5 liter noemen we het een TANKBEURT. Daaronder is het
            // meetruis of een klein sprongetje van het spel; die willen we
            // niet als tankbeurt in je logboek.
            if( bij > 5.0 )
            {
                Tankbeurt t;
                t.liters = bij;

                // Prijs van het land waar je staat, als we dat weten. Anders
                // jouw eigen ingestelde prijs. (m_mutex staat al vast, dus
                // hier niet PrijsVoorLand aanroepen -- die pakt hem opnieuw.)
                double prijs = m_instellingen.prijsPerLiterEuro;
                if( !m_huidigLand.empty() )
                {
                    const auto it = m_prijsPerLand.find( m_huidigLand );
                    if( it != m_prijsPerLand.end() ) prijs = it->second;
                }
                t.land = m_huidigLand;
                t.kostenEuro = bij * prijs;
                t.kmStand = m_kmStand;
                m_tankbeurten.insert( m_tankbeurten.begin(), t ); // nieuwste eerst
                if( m_tankbeurten.size() > 10 ) m_tankbeurten.pop_back();

                ++m_tankbeurtenTotaal;
                m_getanktTotaalLiters += bij;
            }
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
}

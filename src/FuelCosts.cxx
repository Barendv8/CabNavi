#include "FuelCosts.hxx"
#include "Kaartdata.hxx"
#include "Logboek.hxx"
#include "ScsArchief.hxx"
#include "Spel.hxx"

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
        LaadTruckPrijzen();
        LaadTotalen();
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
        catch( ... ) { }
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
        return m_tankEerderAantal + m_tankbeurtenTotaal;
    }

    double FuelCosts::TotaalGetanktLiters() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_tankEerderLiters + m_getanktTotaalLiters;
    }

    double FuelCosts::TotaalGetanktKosten() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_tankEerderKosten + m_getanktTotaalKosten;
    }

    void FuelCosts::ZetKilometerstand( double km )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if( m_kmVorig >= 0.0 && !m_ritActief )
        {
            const double stap = km - m_kmVorig;
            if( stap > 0.0 && stap < 5.0 )
            {
                if( m_busActief ) m_busKm += stap;
                else { m_leeg.km += stap; m_leegSindsVorigeRit.km += stap; m_totalenGewijzigd = true; }
            }
        }
        m_kmVorig = km;
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
                if( it.key().rfind( "_", 0 ) == 0 ) continue;
                if( it.value().is_number() )
                {
                    m_prijsPerLand[ it.key() ] = it.value().get<double>();
                }
            }
        }
        catch( ... )
        {
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
        return m_instellingen.prijsPerLiterEuro;
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
        return uit;
    }

    double FuelCosts::PrijsPerLiter() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_instellingen.prijsPerLiterEuro;
    }

    void FuelCosts::StartNieuweRit()
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        SluitOpenTankbeurt();
        m_litersBijRitStart = m_state.huidigeLiters;
        m_state.verbruikSindsRitStartLiters = 0.0;
        m_state.kostenDezeRitEuro = 0.0;
        m_kostenBijRitStart = m_kostenTeller;
        m_leegVoorDezeRit = m_leegSindsVorigeRit;
        m_leegSindsVorigeRit = LeegRijden{};
        m_ritActief = true;

        m_tankbeurten.clear();
        m_vorigNiveau = m_state.huidigeLiters;
    }

    void FuelCosts::ZetLiters( double liters, double tankInhoud )
    {
        std::lock_guard<std::mutex> lock( m_mutex );

        if( m_litersBijRitStart < 0.0 )
        {
            m_litersBijRitStart = liters;
        }

        if( m_vorigNiveau < 0.0 ) m_vorigNiveau = liters;

        if( m_naWissel )
        {
            if( std::chrono::duration<double>( std::chrono::steady_clock::now() - m_wisselMoment ).count() < 5.0
                || !m_wisselMetingGezien )
            {
                if( !m_wisselMetingGezien ) m_wisselMoment = std::chrono::steady_clock::now();
                m_wisselMetingGezien = true;
                if( liters > m_vorigNiveau ) m_litersBijRitStart += liters - m_vorigNiveau;
                else m_litersBijRitStart -= m_vorigNiveau - liters;
                m_open.actief = false;
                m_vorigNiveau = liters;
                m_vorigNiveauVoorKosten = liters;
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

            m_litersBijRitStart += bij;

            const auto nu = std::chrono::steady_clock::now();
            if( !m_open.actief )
            {
                m_open = OpenTankbeurt{};
                m_open.actief = true;
                Tankbeurt &t = m_open.t;

                std::string land = m_huidigLand;
                if( m_instellingen.landAutomatisch && m_posBekend )
                {
                    std::string l, st; bool g = false;
                    BepaalLand( l, st, g );
                    if( !l.empty() ) { land = l; t.stad = st; m_laatstBekendLand = l; m_laatstBekendeStad = st; }
                    t.garage = g;
                }
                if( land.empty() && m_instellingen.landAutomatisch && !m_laatstBekendLand.empty() )
                {
                    land = m_laatstBekendLand;
                    t.stad = m_laatstBekendeStad;
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
        if( m_open.actief && std::chrono::duration<double>( std::chrono::steady_clock::now() - m_open.laatsteStap ).count() > 3.0 )
        {
            SluitOpenTankbeurt();
        }
        if( m_vorigNiveau >= 0.0 && liters < m_vorigNiveau )
        {
            m_state.verbruiktSessieLiters += m_vorigNiveau - liters;
        }
        m_vorigNiveau = liters;

        if( m_vorigNiveauVoorKosten >= 0.0 && liters < m_vorigNiveauVoorKosten
            && m_vorigNiveauVoorKosten - liters < 20.0 )
        {
            const double delta = m_vorigNiveauVoorKosten - liters;
            const double prijs = PrijsInfoOnderSlot().prijs;
            m_kostenTeller += delta * prijs;
            m_literTeller += delta;
            if( !m_ritActief && !m_busActief )
            {
                m_leeg.liters += delta;               m_leeg.kosten += delta * prijs;
                m_leegSindsVorigeRit.liters += delta; m_leegSindsVorigeRit.kosten += delta * prijs;
                m_totalenGewijzigd = true;
            }
        }
        m_vorigNiveauVoorKosten = liters;
        BewaarTotalen( false );

        m_state.huidigeLiters = liters;
        m_state.tankInhoudLiters = tankInhoud;
        m_state.verbruikSindsRitStartLiters = std::max( 0.0, m_litersBijRitStart - liters );
        m_state.kostenDezeRitEuro = m_kostenTeller - m_kostenBijRitStart;
    }

    double FuelCosts::SluitRitAf()
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        SluitOpenTankbeurt();
        double kosten = m_state.kostenDezeRitEuro;
        m_state.totaalVerbruikLiters += m_state.verbruikSindsRitStartLiters;
        m_state.totaalKostenEuro += kosten;
        m_ritActief = false;
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
        m_tankbeurten.insert( m_tankbeurten.begin(), t );
        if( m_tankbeurten.size() > 10 ) m_tankbeurten.pop_back();
        ++m_tankbeurtenTotaal;
        m_getanktTotaalLiters += t.liters;
        m_getanktTotaalKosten += t.kostenEuro;
        m_totalenGewijzigd = true;
        BewaarTotalen( true );

        if( !m_voertuigSleutel.empty() )
        {
            TruckPrijs tp; tp.prijs = t.prijsPerLiter; tp.land = t.land; tp.stad = t.stad; tp.garage = t.garage;
            m_truckPrijzen[ m_voertuigSleutel ] = tp;
            BewaarTruckPrijzen();
        }
    }

    std::filesystem::path FuelCosts::TruckPrijzenPad()
    {
        return InstellingenPad().parent_path() / "brandstof_trucks.json";
    }

    void FuelCosts::LaadTruckPrijzen()
    {
        try
        {
            std::ifstream in( TruckPrijzenPad() );
            if( !in ) return;
            nlohmann::json j; in >> j;
            std::lock_guard<std::mutex> lock( m_mutex );
            for( auto it = j.begin(); it != j.end(); ++it )
            {
                if( !it.value().is_object() ) continue;
                TruckPrijs tp;
                tp.prijs  = it.value().value( "prijs", 0.0 );
                tp.land   = it.value().value( "land", std::string() );
                tp.stad   = it.value().value( "stad", std::string() );
                tp.garage = it.value().value( "garage", false );
                if( tp.prijs > 0.0 ) m_truckPrijzen[ it.key() ] = tp;
            }
        }
        catch( ... ) { }
    }

    void FuelCosts::BewaarTruckPrijzen() const
    {
        try
        {
            nlohmann::json j = nlohmann::json::object();
            for( const auto &[ sleutel, tp ] : m_truckPrijzen )
                j[ sleutel ] = { { "prijs", tp.prijs }, { "land", tp.land }, { "stad", tp.stad }, { "garage", tp.garage } };
            std::ofstream uit( TruckPrijzenPad() );
            if( uit ) uit << j.dump( 2 );
        }
        catch( ... ) { }
    }

    void FuelCosts::ZetVoertuigSleutel( const std::string &sleutel )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_voertuigSleutel = std::string( SpelInfo::IsAts() ? "ats|" : "ets2|" ) + sleutel;
    }

    FuelCosts::PrijsInfo FuelCosts::PrijsInfoOnderSlot() const
    {
        PrijsInfo p;
        if( !m_voertuigSleutel.empty() )
        {
            const auto it = m_truckPrijzen.find( m_voertuigSleutel );
            if( it != m_truckPrijzen.end() && it->second.prijs > 0.0 )
            {
                p.prijs = it->second.prijs; p.bron = 0;
                p.land = it->second.land; p.stad = it->second.stad; p.garage = it->second.garage;
                return p;
            }
        }
        if( m_instellingen.landAutomatisch && m_posBekend )
        {
            std::string land, stad; bool garage = false;
            BepaalLand( land, stad, garage );
            if( !land.empty() )
            {
                const auto it = m_prijsPerLand.find( land );
                if( it != m_prijsPerLand.end() )
                {
                    p.prijs = it->second * ( garage ? ( 1.0 - m_garageKorting ) : 1.0 );
                    p.bron = 1; p.land = land; p.stad = stad; p.garage = garage;
                    return p;
                }
            }
        }
        p.prijs = m_instellingen.prijsPerLiterEuro; p.bron = 2;
        return p;
    }

    FuelCosts::PrijsInfo FuelCosts::HuidigePrijsInfo() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return PrijsInfoOnderSlot();
    }

    FuelCosts::LeegRijden FuelCosts::LeegTotaal() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        LeegRijden l = m_leeg;
        l.km += m_leegEerder.km; l.liters += m_leegEerder.liters; l.kosten += m_leegEerder.kosten;
        return l;
    }

    FuelCosts::LeegRijden FuelCosts::LeegVoorDezeRit() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_leegVoorDezeRit;
    }

    FuelCosts::RitStand FuelCosts::BusStandOnderSlot() const
    {
        RitStand s = m_busVooraf;
        s.liters += m_literTeller - m_literBijBusStart;
        s.kosten += m_kostenTeller - m_kostenBijBusStart;
        s.km += m_busKm;
        return s;
    }

    void FuelCosts::VerwerpBusRitOnderSlot()
    {
        if( !m_busActief ) return;
        const RitStand s = BusStandOnderSlot();
        m_leeg.km += s.km; m_leeg.liters += s.liters; m_leeg.kosten += s.kosten;
        m_leegSindsVorigeRit = m_leegVoorBusRit;
        m_leegSindsVorigeRit.km += s.km; m_leegSindsVorigeRit.liters += s.liters; m_leegSindsVorigeRit.kosten += s.kosten;
        m_busActief = false;
        m_totalenGewijzigd = true;
    }

    void FuelCosts::StartBusRit()
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        VerwerpBusRitOnderSlot();
        SluitOpenTankbeurt();
        m_busActief = true;
        m_literBijBusStart = m_literTeller;
        m_kostenBijBusStart = m_kostenTeller;
        m_busKm = 0.0;
        m_busVooraf = RitStand{};
        m_busTankbeurtenVooraf.clear();
        m_leegVoorBusRit = m_leegSindsVorigeRit;
        m_leegSindsVorigeRit = LeegRijden{};
        m_tankbeurtenBijBusStart = m_tankbeurtenTotaal;
    }

    FuelCosts::RitStand FuelCosts::BusRitStand() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_busActief ? BusStandOnderSlot() : RitStand{};
    }

    FuelCosts::RitStand FuelCosts::SluitBusRitAf()
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if( !m_busActief ) return RitStand{};
        SluitOpenTankbeurt();
        const RitStand s = BusStandOnderSlot();
        m_busActief = false;
        return s;
    }

    void FuelCosts::VerwerpBusRit()
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        VerwerpBusRitOnderSlot();
    }

    bool FuelCosts::BusRitActief() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_busActief;
    }

    FuelCosts::LeegRijden FuelCosts::LeegVoorBusRit() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_leegVoorBusRit;
    }

    std::vector<FuelCosts::Tankbeurt> FuelCosts::TankbeurtenBusRit() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        const std::size_t sinds = static_cast<std::size_t>( std::max( 0, m_tankbeurtenTotaal - m_tankbeurtenBijBusStart ) );
        const std::size_t n = std::min( sinds, m_tankbeurten.size() );
        std::vector<Tankbeurt> uit( m_tankbeurten.begin(), m_tankbeurten.begin() + static_cast<std::ptrdiff_t>( n ) );
        uit.insert( uit.end(), m_busTankbeurtenVooraf.begin(), m_busTankbeurtenVooraf.end() );
        return uit;
    }

    void FuelCosts::HervatRit( const double liters, const double kosten, const LeegRijden &leegVoor,
                               const std::vector<Tankbeurt> &tankbeurten )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if( !m_ritActief ) return;
        if( liters > 0.0 ) m_litersBijRitStart += liters;
        if( kosten > 0.0 ) m_kostenBijRitStart -= kosten;
        m_state.verbruikSindsRitStartLiters = std::max( 0.0, m_litersBijRitStart - m_state.huidigeLiters );
        m_state.kostenDezeRitEuro = m_kostenTeller - m_kostenBijRitStart;
        m_leegVoorDezeRit = leegVoor;
        m_tankbeurten.insert( m_tankbeurten.end(), tankbeurten.begin(), tankbeurten.end() );
        if( m_tankbeurten.size() > 10 ) m_tankbeurten.resize( 10 );
    }

    void FuelCosts::HervatBusRit( const RitStand &vooraf, const LeegRijden &leegVoor,
                                  const std::vector<Tankbeurt> &tankbeurten )
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if( !m_busActief ) return;
        m_busVooraf = vooraf;
        m_leegVoorBusRit = leegVoor;
        m_busTankbeurtenVooraf = tankbeurten;
    }

    std::filesystem::path FuelCosts::TotalenPad()
    {
        return InstellingenPad().parent_path() / "brandstof_totalen.json";
    }

    void FuelCosts::LaadTotalen()
    {
        try
        {
            std::ifstream in( TotalenPad() );
            if( !in ) return;
            nlohmann::json j; in >> j;
            const char *spel = SpelInfo::IsAts() ? "ats" : "ets2";
            if( !j.is_object() || !j.contains( spel ) || !j[ spel ].is_object() ) return;
            const nlohmann::json &g = j[ spel ];
            std::lock_guard<std::mutex> lock( m_mutex );
            m_leegEerder.km     = std::max( 0.0, g.value( "leeg_km", 0.0 ) );
            m_leegEerder.liters = std::max( 0.0, g.value( "leeg_liters", 0.0 ) );
            m_leegEerder.kosten = std::max( 0.0, g.value( "leeg_kosten", 0.0 ) );
            m_tankEerderAantal  = std::max( 0, g.value( "getankt_aantal", 0 ) );
            m_tankEerderLiters  = std::max( 0.0, g.value( "getankt_liters", 0.0 ) );
            m_tankEerderKosten  = std::max( 0.0, g.value( "getankt_kosten", 0.0 ) );
        }
        catch( ... ) { }
    }

    void FuelCosts::BewaarTotalen( const bool forceer )
    {
        if( !m_totalenGewijzigd ) return;
        const auto nu = std::chrono::steady_clock::now();
        if( !forceer && std::chrono::duration<double>( nu - m_totalenLaatstBewaard ).count() < 60.0 ) return;
        m_totalenLaatstBewaard = nu;
        m_totalenGewijzigd = false;
        try
        {
            nlohmann::json j = nlohmann::json::object();
            {
                std::ifstream in( TotalenPad() );
                if( in )
                {
                    try { in >> j; } catch( ... ) { j = nlohmann::json::object(); }
                    if( !j.is_object() ) j = nlohmann::json::object();
                }
            }
            j[ SpelInfo::IsAts() ? "ats" : "ets2" ] = {
                { "leeg_km", m_leegEerder.km + m_leeg.km },
                { "leeg_liters", m_leegEerder.liters + m_leeg.liters },
                { "leeg_kosten", m_leegEerder.kosten + m_leeg.kosten },
                { "getankt_aantal", m_tankEerderAantal + m_tankbeurtenTotaal },
                { "getankt_liters", m_tankEerderLiters + m_getanktTotaalLiters },
                { "getankt_kosten", m_tankEerderKosten + m_getanktTotaalKosten },
            };
            const std::filesystem::path doel = TotalenPad();
            std::filesystem::path tijdelijk = doel; tijdelijk += ".tmp";
            {
                std::ofstream uit( tijdelijk, std::ios::trunc );
                if( !uit ) return;
                uit << j.dump( 2 );
            }
            std::error_code ec;
            std::filesystem::rename( tijdelijk, doel, ec );
        }
        catch( ... ) { }
    }

    void FuelCosts::VoertuigGewisseld()
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_wisselMoment = std::chrono::steady_clock::now();
        m_naWissel = true;
        m_wisselMetingGezien = false;
        if( m_open.actief && std::chrono::duration<double>( m_wisselMoment - m_open.laatsteStap ).count() < 5.0 )
            m_open.actief = false;
    }

    void FuelCosts::SpelGepauzeerd()
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        SluitOpenTankbeurt();
    }

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
        if( !pl.land.empty() ) { land = pl.land; stad = pl.stad; }
    }

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
            if( naam == "def.scs" || naam == "base.scs" || naam.rfind( "dlc_", 0 ) == 0 ) archieven.push_back( e.path() );
        }
        std::sort( archieven.begin(), archieven.end() );
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
        SluitOpenTankbeurt();
        BewaarTotalen( true );
    }
}

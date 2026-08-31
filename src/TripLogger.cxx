#include "TripLogger.hxx"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace Ritten
{
    // ---- (de)serialisatie ---------------------------------------------

    static std::string TripTypeStr( TripType t )
    {
        return t == TripType::Bus ? "bus" : "vracht";
    }

    static std::string TripStatusStr( TripStatus s )
    {
        switch( s )
        {
            case TripStatus::Voltooid:    return "voltooid";
            case TripStatus::Geannuleerd: return "geannuleerd";
            default:                      return "bezig";
        }
    }

    static json ToJson( const Trip &t )
    {
        json j;
        j[ "id" ] = t.id;
        j[ "type" ] = TripTypeStr( t.type );
        j[ "status" ] = TripStatusStr( t.status );
        j[ "start_tijd" ] = t.startTijdIso;
        j[ "eind_tijd" ] = t.eindTijdIso;
        j[ "server" ] = t.serverNaam;
        j[ "voertuig_merk" ] = t.voertuigMerk;
        j[ "voertuig_model" ] = t.voertuigModel;

        j[ "lading" ] = t.lading;
        j[ "bron_stad" ] = t.bronStad;
        j[ "bestemming_stad" ] = t.bestemmingStad;
        j[ "bron_bedrijf" ] = t.bronBedrijf;
        j[ "bestemming_bedrijf" ] = t.bestemmingBedrijf;
        j[ "geplande_afstand_km" ] = t.geplandeAfstandKm;
        j[ "afgelegde_afstand_km" ] = t.afgelegdeAfstandKm;
        j[ "inkomen" ] = t.inkomen;
        j[ "op_tijd" ] = t.opTijd;
        j[ "brandstof_verbruik_liters" ] = t.brandstofVerbruikLiters;
        j[ "brandstof_kosten_euro" ] = t.brandstofKostenEuro;

        // Onkosten die het spel zelf gemeld heeft (echte in-game bedragen)
        j[ "tol_kosten" ] = t.tolKosten;
        j[ "veerboot_kosten" ] = t.veerbootKosten;
        j[ "trein_kosten" ] = t.treinKosten;
        j[ "boete_kosten" ] = t.boeteKosten;

        json boetes = json::array();
        for( const Boete &b : t.boetes )
        {
            boetes.push_back( { { "reden", b.reden }, { "bedrag", b.bedrag } } );
        }
        j[ "boetes" ] = boetes;

        json doorgangen = json::array();
        for( const Doorgang &d : t.doorgangen )
        {
            const char *soort = d.type == DoorgangType::Tol        ? "tol"
                                 : d.type == DoorgangType::Veerboot ? "veerboot"
                                                                     : "trein";
            doorgangen.push_back( { { "soort", soort },
                                     { "bedrag", d.bedrag },
                                     { "vanaf", d.vanaf },
                                     { "naar", d.naar } } );
        }
        j[ "doorgangen" ] = doorgangen;

        j[ "aanhanger_schade_pct" ] = t.aanhangerSchadePercentage;
        j[ "lading_schade_pct" ] = t.ladingSchadePercentage;
        j[ "lading_gewicht_kg" ] = t.ladingGewichtKg;

        j[ "geschatte_uitbetaling" ] = t.geschatUitbetaling;
        j[ "annulering_reden" ] = t.annuleringsReden;

        json haltes = json::array();
        for( const StopInfo &s : t.haltes )
        {
            haltes.push_back( { { "naam", s.naam },
                                 { "voltooid", s.voltooid },
                                 { "afgelegde_afstand_km", s.afgelegdeAfstandKm } } );
        }
        j[ "haltes" ] = haltes;

        return j;
    }

    // ---- opslagpad -------------------------------------------------------

    std::filesystem::path TripLogger::BepaalOpslagPad()
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
        return basis / "trips.jsonl";
    }

    // ---- levenscyclus ------------------------------------------------

    TripLogger::TripLogger()
        : m_bestandsPad( BepaalOpslagPad() )
    {
        m_worker = std::thread( &TripLogger::WorkerLoop, this );
    }

    TripLogger::~TripLogger()
    {
        m_stoppen = true;
        m_queueCv.notify_all();
        if( m_worker.joinable() )
        {
            m_worker.join();
        }
    }

    void TripLogger::RegisterVoltooideRit( Trip trip )
    {
        // Direct de in-memory totalen bijwerken zodat de overlay meteen
        // klopt, ook al moet het wegschrijven naar disk nog gebeuren.
        {
            std::lock_guard<std::mutex> lock( m_dataMutex );
            m_geschiedenis.push_back( trip );
            if( trip.type == TripType::Bus )
            {
                m_totalen.aantalBusRitten++;
            }
            else
            {
                m_totalen.aantalVrachtRitten++;
            }
            m_totalen.totaalAfstandKm += trip.afgelegdeAfstandKm;
            m_totalen.totaalInkomen += trip.inkomen != 0 ? trip.inkomen : trip.geschatUitbetaling;
            m_totalen.totaalBrandstofKostenEuro += trip.brandstofKostenEuro;
            if( trip.brandstofVerbruikLiters > 0.0 && trip.afgelegdeAfstandKm > 1.0 )
            {
                m_totalen.gemetenLiters += trip.brandstofVerbruikLiters;
                m_totalen.gemetenKm += trip.afgelegdeAfstandKm;
            }
        }

        if( m_voltooidCallback )
        {
            m_voltooidCallback( trip );
        }

        {
            std::lock_guard<std::mutex> lock( m_queueMutex );
            m_wachtrij.push_back( std::move( trip ) );
        }
        m_queueCv.notify_one();
    }

    void TripLogger::WorkerLoop()
    {
        while( !m_stoppen )
        {
            std::deque<Trip> batch;
            {
                std::unique_lock<std::mutex> lock( m_queueMutex );
                m_queueCv.wait( lock, [ this ] { return m_stoppen || !m_wachtrij.empty(); } );
                batch.swap( m_wachtrij );
            }
            for( const Trip &t : batch )
            {
                SchrijfNaarDisk( t );
            }
        }
    }

    void TripLogger::SchrijfNaarDisk( const Trip &trip )
    {
        std::ofstream uit( m_bestandsPad, std::ios::app );
        if( !uit )
        {
            return;
        }
        uit << ToJson( trip ).dump() << '\n';
    }

    void TripLogger::LaadGeschiedenis()
    {
        std::ifstream in( m_bestandsPad );
        if( !in )
        {
            return;
        }

        std::vector<Trip> geladen;
        std::string regel;
        while( std::getline( in, regel ) )
        {
            if( regel.empty() )
            {
                continue;
            }
            try
            {
                json j = json::parse( regel );
                Trip t;
                t.id = j.value( "id", "" );
                t.type = j.value( "type", "vracht" ) == "bus" ? TripType::Bus : TripType::Vracht;
                std::string statusStr = j.value( "status", "voltooid" );
                t.status = statusStr == "geannuleerd" ? TripStatus::Geannuleerd : TripStatus::Voltooid;
                t.startTijdIso = j.value( "start_tijd", "" );
                t.eindTijdIso = j.value( "eind_tijd", "" );
                t.serverNaam = j.value( "server", "" );
                t.voertuigMerk = j.value( "voertuig_merk", "" );
                t.voertuigModel = j.value( "voertuig_model", "" );
                t.lading = j.value( "lading", "" );
                t.bronStad = j.value( "bron_stad", "" );
                t.bestemmingStad = j.value( "bestemming_stad", "" );
                t.geplandeAfstandKm = j.value( "geplande_afstand_km", 0.0 );
                t.afgelegdeAfstandKm = j.value( "afgelegde_afstand_km", 0.0 );
                t.inkomen = j.value( "inkomen", (std::int64_t)0 );
                t.geschatUitbetaling = j.value( "geschatte_uitbetaling", (std::int64_t)0 );
                t.brandstofVerbruikLiters = j.value( "brandstof_verbruik_liters", 0.0 );
                t.brandstofKostenEuro = j.value( "brandstof_kosten_euro", 0.0 );
                // Nieuwe velden -- oude regels in trips.jsonl hebben deze
                // niet, vandaar de standaardwaarde 0. Die ritten tellen dus
                // met 0 onkosten mee, wat klopt: we hebben ze toen simpelweg
                // niet gemeten.
                t.tolKosten = j.value( "tol_kosten", (std::int64_t)0 );
                t.veerbootKosten = j.value( "veerboot_kosten", (std::int64_t)0 );
                t.treinKosten = j.value( "trein_kosten", (std::int64_t)0 );
                t.boeteKosten = j.value( "boete_kosten", (std::int64_t)0 );
                t.aanhangerSchadePercentage = j.value( "aanhanger_schade_pct", 0.0 );
                t.ladingSchadePercentage = j.value( "lading_schade_pct", 0.0 );
                t.ladingGewichtKg = j.value( "lading_gewicht_kg", 0.0 );
                if( j.contains( "boetes" ) && j[ "boetes" ].is_array() )
                {
                    for( const auto &b : j[ "boetes" ] )
                    {
                        Boete boete;
                        boete.reden = b.value( "reden", "" );
                        boete.bedrag = b.value( "bedrag", (std::int64_t)0 );
                        t.boetes.push_back( std::move( boete ) );
                    }
                }
                geladen.push_back( std::move( t ) );
            }
            catch( ... )
            {
                // corrupte regel overslaan
            }
        }

        std::lock_guard<std::mutex> lock( m_dataMutex );
        m_geschiedenis = std::move( geladen );
        m_totalen = Totals{};
        for( const Trip &t : m_geschiedenis )
        {
            if( t.type == TripType::Bus ) m_totalen.aantalBusRitten++;
            else m_totalen.aantalVrachtRitten++;
            m_totalen.totaalAfstandKm += t.afgelegdeAfstandKm;
            m_totalen.totaalInkomen += t.inkomen != 0 ? t.inkomen : t.geschatUitbetaling;
            m_totalen.totaalBrandstofKostenEuro += t.brandstofKostenEuro;
            if( t.brandstofVerbruikLiters > 0.0 && t.afgelegdeAfstandKm > 1.0 )
            {
                m_totalen.gemetenLiters += t.brandstofVerbruikLiters;
                m_totalen.gemetenKm += t.afgelegdeAfstandKm;
            }
        }
    }

    std::vector<Trip> TripLogger::GeefRecenteRitten( std::size_t maxAantal ) const
    {
        std::lock_guard<std::mutex> lock( m_dataMutex );
        std::size_t n = std::min( maxAantal, m_geschiedenis.size() );
        return std::vector<Trip>( m_geschiedenis.end() - n, m_geschiedenis.end() );
    }

    Totals TripLogger::GeefTotalen() const
    {
        std::lock_guard<std::mutex> lock( m_dataMutex );
        return m_totalen;
    }
}

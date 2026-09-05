#include "TripLogger.hxx"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace Ritten
{
    // ---- (de)serialisation --------------------------------------------

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

        // Expenses the game itself reported (real in-game amounts)
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
        j[ "chassis_schade_pct" ] = t.schadeChassisPercentage;
        j[ "lading_schade_pct" ] = t.ladingSchadePercentage;
        j[ "lading_gewicht_kg" ] = t.ladingGewichtKg;

        j[ "geschatte_uitbetaling" ] = t.geschatUitbetaling;
        j[ "annulering_reden" ] = t.annuleringsReden;

        json haltes = json::array();
        for( const StopInfo &s : t.haltes )
        {
            haltes.push_back( { { "naam", s.naam },
                                 { "city_identifier", s.cityIdentifier },
                                 { "voltooid", s.voltooid },
                                 { "afgelegde_afstand_km", s.afgelegdeAfstandKm },
                                 { "geplande_afstand_km", s.geplandeAfstandKm },
                                 { "instappers", s.instappers },
                                 { "uitstappers", s.uitstappers } } );
        }
        j[ "haltes" ] = haltes;
        j[ "passagiers" ] = t.passagiers;

        return j;
    }

    // ---- storage path -------------------------------------------------

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

    // ---- lifecycle ----------------------------------------------------

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

    // Add one trip to the totals. DELIBERATELY in one place: this used
    // to happen twice separately (when completing a trip and when loading
    // trips.jsonl), and then the two versions drift apart sooner or later.
    //
    // Two rules that were not there before:
    //  1. Cancelled trips do NOT count in counts, distance or money. They
    //     were counted, so the counters were higher than the number of
    //     trips you actually drove.
    //  2. Only REAL income counts. There was a fallback to the ESTIMATED
    //     payout when income was zero -- exactly what happens for a
    //     cancelled trip. MEASURED in a real trips.jsonl: the statistics
    //     tab showed 117,637 earned where the real income was 54,055.
    void TripLogger::TelMee( const Trip &t )
    {
        if( t.status == TripStatus::Geannuleerd )
        {
            m_totalen.aantalGeannuleerd++;
            return;
        }

        if( t.type == TripType::Bus ) m_totalen.aantalBusRitten++;
        else m_totalen.aantalVrachtRitten++;

        m_totalen.totaalAfstandKm += t.afgelegdeAfstandKm;
        m_totalen.totaalInkomen += t.inkomen;
        m_totalen.totaalBrandstofKostenEuro += t.brandstofKostenEuro;
        m_totalen.totaalBoeteKosten += t.boeteKosten;
        m_totalen.totaalTolKosten += t.tolKosten;
        m_totalen.totaalVeerbootKosten += t.veerbootKosten;
        m_totalen.totaalTreinKosten += t.treinKosten;

        if( t.brandstofVerbruikLiters > 0.0 && t.afgelegdeAfstandKm > 1.0 )
        {
            m_totalen.gemetenLiters += t.brandstofVerbruikLiters;
            m_totalen.gemetenKm += t.afgelegdeAfstandKm;
        }
    }

    void TripLogger::RegisterVoltooideRit( Trip trip )
    {
        // Update the in-memory totals right away so the overlay is correct
        // immediately, even though writing to disk still has to happen.
        {
            std::lock_guard<std::mutex> lock( m_dataMutex );
            m_geschiedenis.push_back( trip );
            TelMee( trip );
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
                // New fields -- old lines in trips.jsonl do not have them, hence the
                // default 0. Those trips count with 0 expenses, which is correct: we
                // simply did not measure them back then.
                t.tolKosten = j.value( "tol_kosten", (std::int64_t)0 );
                t.veerbootKosten = j.value( "veerboot_kosten", (std::int64_t)0 );
                t.treinKosten = j.value( "trein_kosten", (std::int64_t)0 );
                t.boeteKosten = j.value( "boete_kosten", (std::int64_t)0 );
                t.aanhangerSchadePercentage = j.value( "aanhanger_schade_pct", 0.0 );
                t.schadeChassisPercentage = j.value( "chassis_schade_pct", 0.0 );
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
                // Read the stops back. They were WRITTEN but never read, so the
                // history of a bus trip was empty after a restart: zero stops, zero
                // passengers.
                if( j.contains( "haltes" ) && j[ "haltes" ].is_array() )
                {
                    for( const auto &h : j[ "haltes" ] )
                    {
                        StopInfo halte;
                        halte.naam = h.value( "naam", "" );
                        halte.cityIdentifier = h.value( "city_identifier", "" );
                        halte.voltooid = h.value( "voltooid", false );
                        halte.afgelegdeAfstandKm = h.value( "afgelegde_afstand_km", 0.0 );
                        halte.geplandeAfstandKm = h.value( "geplande_afstand_km", 0.0 );
                        halte.instappers = h.value( "instappers", 0 );
                        halte.uitstappers = h.value( "uitstappers", 0 );
                        t.haltes.push_back( std::move( halte ) );
                    }
                }
                t.passagiers = j.value( "passagiers", (std::uint32_t)0 );

                geladen.push_back( std::move( t ) );
            }
            catch( ... )
            {
                // skip a corrupt line
            }
        }

        std::lock_guard<std::mutex> lock( m_dataMutex );
        m_geschiedenis = std::move( geladen );
        m_totalen = Totals{};
        for( const Trip &t : m_geschiedenis )
        {
            TelMee( t );
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

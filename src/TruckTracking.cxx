#include "TruckTracking.hxx"

#include "BusTracking.hxx"
#include "IncidentRecorder.hxx"
#include "PlayersNearby.hxx"
#include "Logboek.hxx"

#include <algorithm>
#include <chrono>
#include <cmath>
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

    // Helper: fetch an attribute from a scs_named_value_t list by name.
    // Returns nullptr if not found.
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
        // %APPDATA%\CabNavi\tachograaf.json -- next to the other settings,
        // so a game update does not clean it up.
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

        // Remember the MEASURED distance factor between sessions. Without this
        // the plugin starts at the default every time and idle l/h is only
        // right after you have driven a while -- measured 31-08: 1.0 after a
        // fresh start, 2.0 once the factor was measured, at exactly the same
        // consumption.
        //
        // NOTE: this changes NOTHING in the calculation itself. The only
        // difference is that the start value comes from the previous session
        // instead of from an assumption.
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

        // Per-vehicle counter, next to meting.txt. Plain text, one line per
        // vehicle: merk|model|kmStand|liters|km. No JSON library needed for
        // five numbers.
        std::filesystem::path VoertuigenPad()
        {
            return MetingPad().parent_path() / "voertuigen.txt";
        }
    }

    // Restore the last measured distance factor. If the file is missing,
    // the default stays and everything behaves as before.
    void TruckTracking::LaadMeting()
    {
        // ALWAYS report what happens here, even if there is nothing to read.
        // Otherwise, on an empty result, you cannot tell whether the file was
        // missing, the value was rejected, or this function was not even
        // called -- and then you are guessing (measured 31-08).
        try
        {
            const std::filesystem::path pad = MetingPad();
            std::ifstream in( pad );
            if( !in )
            {
                Logboek::Schrijf( "start", "meting.txt not found at "
                                               + Logboek::KortPad( pad ) + " -- default "
                                               + std::to_string( m_afstandsFactor ) );
                return;
            }
            double f = 0.0;
            in >> f;
            if( f > AFSTANDSFACTOR_MIN && f < AFSTANDSFACTOR_MAX )
            {
                m_afstandsFactor = f;
                m_bewaardeFactor = f;
                Logboek::Schrijf( "start", "distance factor from previous session: "
                                               + std::to_string( f ) );
            }
            else
            {
                Logboek::Schrijf( "start", "distance factor in meting.txt rejected: "
                                               + std::to_string( f ) );
            }
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "start", std::string( "meting.txt not readable: " ) + Logboek::KorteFout( ex.what() ) );
        }
        catch( ... )
        {
            Logboek::Schrijf( "start", "meting.txt not readable (unknown error)" );
        }
    }

    TruckTracking::~TruckTracking()
    {
        if( m_saveThread.joinable() ) m_saveThread.join();
        BewaarVoertuigen( true );
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

        // Two numbers: remaining game minutes and the period length. No JSON
        // library needed, and nothing that can break on a half-written file.
        double rest = -1.0;
        double periode = 0.0;
        if( in >> rest >> periode )
        {
            if( rest >= 0.0 ) m_teHerstellenRest = rest;
            if( periode > 60.0 ) m_rustPeriodeMax = periode;
        }

        // The tacho setting is on the same line after it. If missing (file
        // from an older version), the default mode stays.
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

    // ---- Per-vehicle counter -----------------------------------------------

    void TruckTracking::LaadVoertuigen()
    {
        m_voertuigenGeladen = true;
        std::ifstream in( VoertuigenPad() );
        if( !in ) return;
        std::string regel;
        while( std::getline( in, regel ) )
        {
            // merk|model|kmStand|liters|km
            std::vector<std::string> delen;
            std::size_t begin = 0;
            for( ;; )
            {
                const std::size_t p = regel.find( '|', begin );
                delen.push_back( regel.substr( begin, p == std::string::npos ? std::string::npos : p - begin ) );
                if( p == std::string::npos ) break;
                begin = p + 1;
            }
            // 5 fields = old format (only litres/km), 21 = with driving style.
            if( delen.size() != 5 && delen.size() != 21 ) continue;
            try
            {
                VoertuigTeller v;
                v.merk = delen[ 0 ];
                v.model = delen[ 1 ];
                v.kmStand = std::stod( delen[ 2 ] );
                v.liters = std::stod( delen[ 3 ] );
                v.km = std::stod( delen[ 4 ] );
                if( delen.size() == 21 )
                {
                    auto lees = [ &delen ]( RijstijlTelling &t, std::size_t i )
                    {
                        t.rijdendSec = std::stod( delen[ i ] );
                        t.uitrolSec = std::stod( delen[ i + 1 ] );
                        t.volgasSec = std::stod( delen[ i + 2 ] );
                        t.stationairSec = std::stod( delen[ i + 3 ] );
                        t.totaalSec = std::stod( delen[ i + 4 ] );
                        t.km = std::stod( delen[ i + 5 ] );
                        t.geladenSec = std::stod( delen[ i + 6 ] );
                        t.remmingen = std::stoi( delen[ i + 7 ] );
                    };
                    lees( v.leeg, 5 );
                    lees( v.geladen, 13 );
                }
                if( v.kmStand >= 0.0 && v.liters >= 0.0 && v.km >= 0.0 ) m_voertuigen.push_back( v );
            }
            catch( ... ) { /* kapotte regel overslaan, de rest blijft bruikbaar */ }
        }
        Logboek::Schrijf( "start", "voertuigen.txt: " + std::to_string( m_voertuigen.size() ) + " vehicle(s) loaded" );
    }

    void TruckTracking::BewaarVoertuigen( bool forceer )
    {
        if( !m_voertuigenGewijzigd ) return;
        // At most once a minute to disk, unless it really must (reset,
        // shutdown). Writing every reading would be needless I/O.
        const auto nu = std::chrono::steady_clock::now();
        if( !forceer && std::chrono::duration<double>( nu - m_voertuigenLaatstBewaard ).count() < 60.0 ) return;
        try
        {
            std::ofstream uit( VoertuigenPad(), std::ios::trunc );
            if( !uit ) return;
            // Three fixed decimals, otherwise the stream writes 19013.393 as
            // "19013" and the fractional part of the odometer is lost.
            uit << std::fixed << std::setprecision( 3 );
            auto telling = [ &uit ]( const RijstijlTelling &t )
            {
                uit << '|' << t.rijdendSec << '|' << t.uitrolSec << '|' << t.volgasSec << '|' << t.stationairSec
                    << '|' << t.totaalSec << '|' << t.km << '|' << t.geladenSec << '|' << t.remmingen;
            };
            for( const VoertuigTeller &v : m_voertuigen )
            {
                // merk|model|kmStand|liters|km|  leeg: 8 fields  |  geladen: 8 fields
                uit << v.merk << '|' << v.model << '|' << v.kmStand << '|' << v.liters << '|' << v.km;
                telling( v.leeg );
                telling( v.geladen );
                uit << '\n';
            }
            m_voertuigenLaatstBewaard = nu;
            m_voertuigenGewijzigd = false;
        }
        catch( ... ) { /* opslaan mag nooit storen */ }
    }

    void TruckTracking::IdentificeerVoertuig()
    {
        // Needed: brand and model from the configuration, and an odometer.
        if( m_configMerk.empty() || m_kilometerstandKm <= 0.0 ) return;
        if( m_gepauzeerd ) return;  // in garage or menu: the reading is not this truck's yet
        if( !m_kmStandVersNaConfig )
        {
            // No different reading seen yet. After three seconds go ahead anyway:
            // then they apparently really are equal, or it is the first truck of
            // the session (then there was no previous one).
            if( std::chrono::duration<double>( std::chrono::steady_clock::now() - m_configMoment ).count() < 3.0 )
                return;
        }
        if( !m_voertuigenGeladen ) LaadVoertuigen();

        // Find the same brand and model with an odometer that is NOT above
        // the current one (a counter only goes up) and is closest to it. That
        // keeps two identical trucks apart: their readings never cross.
        int beste = -1;
        double kleinsteVerschil = 1e18;
        for( std::size_t i = 0; i < m_voertuigen.size(); ++i )
        {
            const VoertuigTeller &v = m_voertuigen[ i ];
            if( v.merk != m_configMerk || v.model != m_configModel ) continue;
            const double verschil = m_kilometerstandKm - v.kmStand;
            if( verschil < -1.0 ) continue;  // counter lower than saved: different truck
            if( verschil < kleinsteVerschil ) { kleinsteVerschil = verschil; beste = static_cast<int>( i ); }
        }
        if( beste < 0 )
        {
            VoertuigTeller v;
            v.merk = m_configMerk;
            v.model = m_configModel;
            v.kmStand = m_kilometerstandKm;
            m_voertuigen.push_back( v );
            beste = static_cast<int>( m_voertuigen.size() ) - 1;
            m_voertuigenGewijzigd = true;
            Logboek::Schrijf( "event", "new vehicle: " + v.merk + " " + v.model
                                            + " at " + std::to_string( static_cast<long long>( m_kilometerstandKm ) ) + " km" );
        }
        else
        {
            Logboek::Schrijf( "event", "vehicle recognised: " + m_voertuigen[ beste ].merk + " "
                                            + m_voertuigen[ beste ].model + " ("
                                            + std::to_string( static_cast<long long>( m_voertuigen[ beste ].km ) ) + " km counted)" );
        }
        m_huidigVoertuig = beste;

        // Now the truck is known and the odometer fresh: read the save. At
        // this moment the live reading is exactly the one it was loaded with.
        m_saveHerkansingen = 0;
        StartSaveLezen( m_kilometerstandKm );
    }

    void TruckTracking::ResetVoertuigTeller()
    {
        if( m_huidigVoertuig < 0 || m_huidigVoertuig >= static_cast<int>( m_voertuigen.size() ) ) return;
        VoertuigTeller &v = m_voertuigen[ m_huidigVoertuig ];
        v.liters = 0.0;
        v.km = 0.0;
        v.leeg = RijstijlTelling{};
        v.geladen = RijstijlTelling{};
        m_rijstijlVenster.clear();
        m_rijstijlVensterSom = RijstijlTelling{};
        m_rijstijlPending = RijstijlMeting{};
        m_voertuigenGewijzigd = true;
        BewaarVoertuigen( true );
        Logboek::Schrijf( "event", "vehicle counter reset: " + v.merk + " " + v.model );
    }

    // ---- Trip counter from the save -----------------------------------------

    void TruckTracking::StartSaveLezen( const double kmStandBijLaden )
    {
        if( m_huidigVoertuig < 0 || m_huidigVoertuig >= static_cast<int>( m_voertuigen.size() ) ) return;
        if( m_saveBezig )
        {
            // Another read still in progress: queue this one, do not drop it.
            // VerwerkSaveResultaat picks it up as soon as the previous one is
            // done.
            m_herlaadKmStand = kmStandBijLaden;
            m_herlaadMoment = std::chrono::steady_clock::now() - std::chrono::seconds( 2 );
            return;
        }
        if( m_saveThread.joinable() ) m_saveThread.join();

        m_saveBezig = true;
        m_saveKlaar = false;
        m_saveResultaat.reset();
        m_saveFout.clear();
        m_saveVoertuig = m_huidigVoertuig;
        m_saveKmStandGevraagd = kmStandBijLaden;
        m_saveLitersBijStart = m_voertuigen[ m_huidigVoertuig ].liters;
        m_saveKmBijStart = m_voertuigen[ m_huidigVoertuig ].km;

        // In the background: 100-300 ms per save, never on the render thread.
        // std::thread can (rarely) throw, and we are inside an SCS callback.
        try
        {
            m_saveThread = std::thread( [ this, kmStandBijLaden ]()
            {
                std::string fout;
                std::optional<SaveTripteller> r;
                try { r = SaveLezer::ZoekInAlleSaves( kmStandBijLaden, fout ); }
                catch( ... ) { fout = "exception while reading"; }
                std::lock_guard<std::mutex> slot( m_saveMutex );
                m_saveResultaat = std::move( r );
                m_saveFout = std::move( fout );
                m_saveKlaar = true;
            } );
        }
        catch( ... )
        {
            m_saveBezig = false;
            Logboek::Schrijf( "event", "save: background thread could not start, own counter" );
        }
    }

    void TruckTracking::VerwerkSaveResultaat()
    {
        const auto nu = std::chrono::steady_clock::now();

        // Scheduled read after a jump back: only after two seconds, and only
        // if there has been no truck switch meanwhile.
        if( m_herlaadKmStand >= 0.0 && !m_saveBezig && m_huidigVoertuig >= 0
            && std::chrono::duration<double>( nu - m_herlaadMoment ).count() >= 2.0 )
        {
            const double stand = m_herlaadKmStand;
            m_herlaadKmStand = -1.0;
            m_saveHerkansingen = 0;
            StartSaveLezen( stand );
            return;
        }
        // Retry: the save was half written (autosave in progress).
        if( m_saveHerkansingen == 1 && !m_saveBezig && m_huidigVoertuig >= 0
            && std::chrono::duration<double>( nu - m_saveHerkansingMoment ).count() >= 10.0 )
        {
            m_saveHerkansingen = 2;  // not again after this
            StartSaveLezen( m_saveKmStandGevraagd );
            return;
        }

        if( !m_saveBezig ) return;
        std::optional<SaveTripteller> r;
        std::string fout;
        {
            std::lock_guard<std::mutex> slot( m_saveMutex );
            if( !m_saveKlaar ) return;
            r = m_saveResultaat;
            fout = m_saveFout;
        }
        m_saveBezig = false;
        if( m_saveThread.joinable() ) m_saveThread.join();

        if( !r )
        {
            if( !fout.empty() && m_saveHerkansingen == 0 )
            {
                // Read error: the save was probably just being written. Once more in
                // ten seconds, then give up.
                m_saveHerkansingen = 1;
                m_saveHerkansingMoment = nu;
                Logboek::Schrijf( "event", "save: not read (" + fout + "), retrying in 10 s" );
                return;
            }
            // No block at this reading: bus, Scout, quick-job truck, Steam Cloud
            // save, or a save that was not written. Then simply keep our own
            // counter. Only log, do not complain.
            Logboek::Schrijf( "event", fout.empty() ? "save: no truck at this odometer, own counter"
                                                      : "save: not read (" + fout + "), own counter" );
            return;
        }
        if( m_saveVoertuig != m_huidigVoertuig || m_saveVoertuig < 0
            || m_saveVoertuig >= static_cast<int>( m_voertuigen.size() ) )
        {
            Logboek::Schrijf( "event", "save: result for a different vehicle, ignored" );
            return;
        }

        // Counter = dashboard counter from the save + what has been counted
        // since the read started. That way nothing of the interval is lost.
        VoertuigTeller &v = m_voertuigen[ m_saveVoertuig ];
        const double sindsLiters = v.liters - m_saveLitersBijStart;
        const double sindsKm = v.km - m_saveKmBijStart;
        v.liters = r->tripLiters + std::max( 0.0, sindsLiters );
        v.km = r->tripKm + std::max( 0.0, sindsKm );
        m_voertuigenGewijzigd = true;
        BewaarVoertuigen( true );

        char buf[ 160 ];
        std::snprintf( buf, sizeof( buf ), "save (%s): dashboard trip %.2f L / %.2f km taken over, odometer %.3f",
                       r->bron.c_str(), r->tripLiters, r->tripKm, r->kilometerstandKm );
        Logboek::Schrijf( "event", buf );
    }

    std::string TruckTracking::HuidigVoertuigNaam() const
    {
        if( m_huidigVoertuig < 0 || m_huidigVoertuig >= static_cast<int>( m_voertuigen.size() ) ) return {};
        const VoertuigTeller &v = m_voertuigen[ m_huidigVoertuig ];
        return v.merk + " " + v.model;
    }

    // ---- Driving style ------------------------------------------------------

    void TruckTracking::TelOp( RijstijlTelling &t, const RijstijlMeting &m, const int teken )
    {
        t.totaalSec += teken * m.dSec;
        t.km += teken * m.dKm;
        t.rijdendSec += teken * m.rijdendSec;
        t.uitrolSec += teken * m.uitrolSec;
        t.volgasSec += teken * m.volgasSec;
        t.stationairSec += teken * m.stationairSec;
        t.geladenSec += teken * m.geladenSec;
        t.remmingen += teken * m.remming;
    }

    void TruckTracking::RijstijlTellen( const double snelheidKmh, const double gas, const double dKm, const double dSec )
    {
        if( dSec <= 0.0 || dSec > 5.0 ) return;  // gap in the measurement: skip

        // Add this reading to the running second.
        const double snelheidAbs = std::fabs( snelheidKmh );
        const bool rijdend = snelheidAbs > 10.0;
        RijstijlMeting &p = m_rijstijlPending;
        p.dKm += dKm;
        p.dSec += dSec;
        if( rijdend ) p.rijdendSec += dSec;
        if( rijdend && gas < 0.10 ) p.uitrolSec += dSec;
        if( gas > 0.90 ) p.volgasSec += dSec;
        // Still and fuel is going down: then the engine is running. Measured,
        // not assumed -- no engine-on channel is registered.
        if( snelheidAbs < 1.0 && m_gladLiterPerUur > 0.3 ) p.stationairSec += dSec;
        if( m_heeftAanhanger ) p.geladenSec += dSec;
        if( p.dSec < 1.0 ) return;  // second not full yet

        // Second is full: hard braking = more than 8 km/h lost since the
        // start of the previous second. An event, not a duration.
        if( m_vorigeSnelheidVoorRem >= 0.0 && ( m_vorigeSnelheidVoorRem - snelheidAbs ) > 8.0 ) p.remming = 1;
        m_vorigeSnelheidVoorRem = snelheidAbs;

        // Add to the window, and drop off the front as soon as it gets too long.
        m_rijstijlVenster.push_back( p );
        TelOp( m_rijstijlVensterSom, p, +1 );
        while( m_rijstijlVenster.size() > 1
               && ( m_rijstijlVensterSom.rijdendSec > RIJSTIJL_VENSTER_RIJDEND_SEC
                    || m_rijstijlVensterSom.totaalSec > RIJSTIJL_VENSTER_SEC ) )
        {
            TelOp( m_rijstijlVensterSom, m_rijstijlVenster.front(), -1 );
            m_rijstijlVenster.pop_front();
        }

        // And into this vehicle's reference, in the bucket of this situation.
        if( m_huidigVoertuig >= 0 && m_huidigVoertuig < static_cast<int>( m_voertuigen.size() ) )
        {
            VoertuigTeller &v = m_voertuigen[ m_huidigVoertuig ];
            TelOp( ( p.geladenSec > 0.5 * p.dSec ) ? v.geladen : v.leeg, p, +1 );
            m_voertuigenGewijzigd = true;
        }
        p = RijstijlMeting{};
    }

    TruckTracking::RijstijlStatus TruckTracking::HuidigeRijstijl() const
    {
        RijstijlStatus r;

        // ---- Layer 1: the direct meter. From the last second, always. ----
        {
            const double snelheid = std::fabs( m_liveSnelheidKmh );
            const double gas = m_gaspedaal;
            if( snelheid < 1.0 )
            {
                if( m_gladLiterPerUur > 0.3 ) r.nu = RijstijlStatus::StilMotorAan;
            }
            else if( snelheid > 10.0 && gas < 0.10 ) r.nu = RijstijlStatus::Uitrollen;
            else if( gas < 0.30 ) r.nu = RijstijlStatus::ZuinigNu;
            else if( gas < 0.70 ) r.nu = RijstijlStatus::Normaal;
            else r.nu = RijstijlStatus::Trekken;
        }

        // ---- Layer 2: the assessment. Only when there is a reference. ----
        const RijstijlTelling &w = m_rijstijlVensterSom;
        if( w.rijdendSec < 30.0 ) return r;
        if( m_huidigVoertuig < 0 || m_huidigVoertuig >= static_cast<int>( m_voertuigen.size() ) ) return r;

        r.geladen = w.geladenSec > 0.5 * w.totaalSec;
        const VoertuigTeller &v = m_voertuigen[ m_huidigVoertuig ];
        RijstijlTelling ref = r.geladen ? v.geladen : v.leeg;
        // Subtract the window: it is also in the reference, and you do not
        // compare with yourself. Only the seconds that fell in THIS situation.
        for( const RijstijlMeting &m : m_rijstijlVenster )
        {
            const bool mGeladen = m.geladenSec > 0.5 * m.dSec;
            if( mGeladen == r.geladen ) TelOp( ref, m, -1 );
        }
        if( ref.rijdendSec < RIJSTIJL_REFERENTIE_SEC ) return r;  // no reference yet: only layer 1

        // Braking per ten minutes of driving -- not per kilometre, that is map scale.
        const double uNu = w.uitrolSec / w.rijdendSec * 100.0;
        const double vNu = w.volgasSec / w.rijdendSec * 100.0;
        const double rNu = w.remmingen / w.rijdendSec * 600.0;
        const double uRef = ref.uitrolSec / ref.rijdendSec * 100.0;
        const double vRef = ref.volgasSec / ref.rijdendSec * 100.0;
        const double rRef = ref.remmingen / ref.rijdendSec * 600.0;

        // Majority, no weights. Margin 5 percentage points / 2 braking events
        // per ten minutes.
        int beter = 0, slechter = 0;
        if( uNu > uRef + 5.0 ) ++beter; else if( uNu < uRef - 5.0 ) ++slechter;
        if( vNu < vRef - 5.0 ) ++beter; else if( vNu > vRef + 5.0 ) ++slechter;
        if( rNu < rRef - 2.0 ) ++beter; else if( rNu > rRef + 2.0 ) ++slechter;

        if( beter >= 2 && slechter == 0 ) r.stand = RijstijlStatus::Zuinig;
        else if( slechter >= 2 && beter == 0 ) r.stand = RijstijlStatus::Sportief;
        else r.stand = RijstijlStatus::Gewoon;
        return r;
    }

    void TruckTracking::RegistreerBijTelemetrie( const scs_telemetry_init_params_v101_t *params )
    {
        // Channel names as plain text -- see the note at the top of
        // TruckTracking.hxx for why.
        params->register_for_channel(
            "game.time", SCS_U32_NIL, SCS_VALUE_TYPE_u32,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::GameTimeCallback, this );

        // --- MEASUREMENT POINT: is there a channel for the current country? --
        //
        // We do not know, so we simply try. Every channel the game accepts
        // ends up in debug.log, with its value. If this yields nothing,
        // automatic price-per-country is not feasible and the manual price
        // stays -- then we lost five minutes instead of a system that quietly
        // shows wrong amounts.
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
                                            ? "country channels: NO channel accepted"
                                            : ( "country channels accepted: " + gevonden ) );
        }

        // Remaining driving time according to the game itself. This WAS the
        // tachograph source; in 1.60 the channel no longer exists (measured:
        // all three types refused), so we count ourselves. The registration
        // attempt stays in case SCS ever brings it back.
        //
        // The game's OWN ETA to the destination. By far the best source: the
        // Route Advisor accounts for the actual route, speed limits and
        // ferries -- things we never approximate well with an average speed.
        params->register_for_channel(
            "truck.navigation.time", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::NavigatieTijdCallback, this );

        // The rest channel passed nothing ("kanaal --" on the HUD). Two
        // things fixed: we now CHECK whether registration succeeds, and we try
        // several types. If the game offers this channel as u32 or float
        // instead of s32, registration fails silently -- exactly what we saw.
        m_rustKanaalType = 0;
        if( params->register_for_channel(
                "game.next.rest.stop", SCS_U32_NIL, SCS_VALUE_TYPE_s32,
                SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::RustStopCallback, this ) == SCS_RESULT_ok )
        {
            m_rustKanaalType = 1;  // s32
        }
        else if( params->register_for_channel(
                     "game.next.rest.stop", SCS_U32_NIL, SCS_VALUE_TYPE_u32,
                     SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::RustStopCallback, this ) == SCS_RESULT_ok )
        {
            m_rustKanaalType = 2;  // u32
        }
        else if( params->register_for_channel(
                     "game.next.rest.stop", SCS_U32_NIL, SCS_VALUE_TYPE_float,
                     SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::RustStopCallback, this ) == SCS_RESULT_ok )
        {
            m_rustKanaalType = 3;  // float
        }

        // Record what the game does and does not offer. Saves the whole
        // investigation on the next deviation: you see right away whether a
        // channel is there.
        {
            static const char *typeNaam[] = { "GEEN", "s32", "u32", "float" };
            Logboek::Schrijf( "start", std::string( "channel game.next.rest.stop -> " )
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

        // New: the game's OWN, already computed remaining navigation distance
        // (in metres, follows the real road -- not as the crow flies). Much
        // more accurate than our own sum via speed x time, because the game
        // already accounts for bends/detours. Looked up and confirmed via the
        // official SCS SDK headers (RenCloud's scs-sdk-plugin, public on
        // GitHub) -- not guessed.
        params->register_for_channel(
            "truck.navigation.distance", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::NavigatieAfstandCallback, this );

        // Purely for logging/verification -- NOT (yet) used in the
        // calculation. We first want to see with real figures whether our
        // current approach (distance / real speed) is already right, before
        // blindly multiplying this factor in. See the discussion on why
        // local_scale governs the clock, not your driving physics.
        params->register_for_channel(
            "local_scale", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::LocalScaleCallback, this );

        // --- New channels ---------------------------------------------
        // Names verified against the official SCS header
        // scssdk_telemetry_truck_common_channels.h (via RenCloud's public
        // copy) -- including the fact that the limit is called
        // "truck.navigation.speed.limit" with DOTS, not "speed_limit" with
        // an underscore.

        // Remaining range in km -- the game already computes this itself
        // based on your actual consumption, so better than our own division.
        params->register_for_channel(
            "truck.fuel.range", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::BereikCallback, this );

        // NOTE: this channel is litres per KM, not per 100 km. We only
        // convert in HuidigeVoertuigStatus(). The SCS documentation warns
        // that this value is fairly static in ETS2/ATS and depends more on
        // your trailer and driver skills than on your actual driving -- so
        // treat it as an indication, not an exact measurement.
        params->register_for_channel(
            "truck.fuel.consumption.average", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::VerbruikCallback, this );

        params->register_for_channel(
            "truck.odometer", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::KilometerstandCallback, this );

        // Speed limit in m/s, as the Route Advisor shows it. So it also
        // follows your own "Route Advisor speed limit" setting in the game:
        // if that is off, nothing arrives here and the warning simply stays
        // quiet.
        params->register_for_channel(
            "truck.navigation.speed.limit", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::SnelheidslimietCallback, this );

        // Set cruise speed in m/s; 0 means off.
        params->register_for_channel(
            "truck.cruise_control", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::CruiseControlCallback, this );

        // Throttle as the simulation uses it, 0..1. With this we know
        // IMMEDIATELY that you released the throttle -- the fuel measurement
        // only notices a few counts later, because it looks back in time. On
        // such a flip we empty the measurement window, so the figure does not
        // stay on your old driving behaviour for seconds.
        params->register_for_channel(
            "truck.effective.throttle", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::GaspedaalCallback, this );

        // Damage per component, apart from the chassis we already had.
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

        // Trailer. The trailer channels are numbered ("trailer.0.") since
        // the game supports multiple trailers (doubles/triples); we only
        // follow the first, because that is the one with your cargo in it.
        params->register_for_channel(
            "trailer.0.wear.chassis", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::AanhangerSchadeCallback, this );
        params->register_for_channel(
            "trailer.0.cargo.damage", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::LadingSchadeCallback, this );

        // Tells us when a job starts (source/destination/cargo/distance) and
        // when the vehicle itself changes (tank capacity).
        params->register_for_event( SCS_TELEMETRY_EVENT_configuration, &TruckTracking::ConfigCallback, this );

        // Tells us when a job is actually COMPLETED (income, damage, on time
        // or not, cancelled or not) -- this is the most reliable source for
        // that, more reliable than just watching the job configuration go
        // empty.
        params->register_for_event( SCS_TELEMETRY_EVENT_gameplay, &TruckTracking::GameplayEventCallback, this );

        // Pause detection (see note at GepauzeerdCallback in the header) --
        // same events as in the official SCS example.
        params->register_for_event( SCS_TELEMETRY_EVENT_paused, &TruckTracking::GepauzeerdCallback, this );
        params->register_for_event( SCS_TELEMETRY_EVENT_started, &TruckTracking::HervatCallback, this );
    }

    // TEMPORARY: writes every configuration/gameplay update (id + all
    // attributes) to %APPDATA%\CabNavi\debug.log. That way we can see
    // exactly what the game passes when something is off (job detection
    // or completion), instead of guessing. Simple/synchronous because this
    // is rare -- no separate queue thread needed like TripLogger.
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
                    uit << " (other type: " << (int)attr->value.type << ")";
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
            // Request the tank capacity as soon as the truck configuration comes
            // by (e.g. on spawning/switching truck).
            if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "fuel.capacity" ) )
            {
                if( attr->value.type == SCS_VALUE_TYPE_float )
                {
                    m_tankInhoudLiters = attr->value.value_float.value;
                }
            }

            // Brand and model for the per-vehicle counter. MEASURED 02-09 in the
            // configuration dump: "brand" = Scania, "name" = Streamline. The
            // recognition itself only happens at the next reading, because then
            // the odometer is there too -- and we need that to keep two identical
            // trucks apart.
            std::string merk, model;
            if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "brand" ) )
                if( attr->value.type == SCS_VALUE_TYPE_string && attr->value.value_string.value )
                    merk = attr->value.value_string.value;
            if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "name" ) )
                if( attr->value.type == SCS_VALUE_TYPE_string && attr->value.value_string.value )
                    model = attr->value.value_string.value;
            if( !merk.empty() && ( merk != m_configMerk || model != m_configModel || m_huidigVoertuig < 0 ) )
            {
                // Different truck (or the first): save the previous one's counter
                // and recognise again at the next reading.
                BewaarVoertuigen( true );
                m_configMerk = merk;
                m_configModel = model;
                m_huidigVoertuig = -1;
                m_herlaadKmStand = -1.0;  // this was a switch, not a reload
                m_brandstof.VoertuigGewisseld();  // the tank level jump is the other truck, not a refuel
                m_rijstijlVenster.clear();
                m_rijstijlVensterSom = RijstijlTelling{};
                m_rijstijlPending = RijstijlMeting{};
                m_vorigeSnelheidVoorRem = -1.0;
                // First wait for a DIFFERENT odometer than the current one -- that is
                // still the previous truck's. With a time limit in case two trucks
                // happen to be equal.
                m_kmStandVersNaConfig = false;
                m_kmStandBijConfig = m_kilometerstandKm;
                m_configMoment = std::chrono::steady_clock::now();
            }
            // Also record it in the trip, so webhook and history finally show a
            // truck -- those fields were always empty.
            if( !merk.empty() ) { m_huidigeRit.voertuigMerk = merk; m_huidigeRit.voertuigModel = model; }
            return;
        }

        if( configId != "job" )
        {
            return;
        }

        // A non-empty "cargo.id" means a new job has been loaded; an empty
        // configuration means the job slot is empty (just completed/cancelled
        // -- the actual completion with income/damage comes via the gameplay
        // event below).
        const scs_named_value_t *cargoAttr = ZoekAttribuut( cfg->attributes, "cargo.id" );
        const bool heeftLading = cargoAttr != nullptr && cargoAttr->value.type == SCS_VALUE_TYPE_string
                                  && cargoAttr->value.value_string.value != nullptr
                                  && cargoAttr->value.value_string.value[ 0 ] != '\0';

        if( !heeftLading )
        {
            return;
        }

        // The game sends this "job" configuration several times for the same
        // job (e.g. first with cargo.loaded=false, then true once you are
        // really loaded) -- if we are already on exactly the same cargo (by
        // internal cargo.id, not the display name), this is a repeat and not
        // a new job. Otherwise the trip clock and fuel consumption would keep
        // starting over.
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
        nieuw.lading = cargoAttr->value.value_string.value;  // fallback: internal id
        if( const scs_named_value_t *naamAttr = ZoekAttribuut( cfg->attributes, "cargo" ) )
        {
            // "cargo" is the tidy display name (e.g. "Cement"), "cargo.id" the
            // internal code (e.g. "cement") -- show the tidy name if present.
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
        // Cargo weight (kg) -- belongs to idea #6, together with the trailer
        // damage. Comes as float from the job config.
        if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "cargo.mass" ) )
        {
            if( attr->value.type == SCS_VALUE_TYPE_float )
            {
                nieuw.ladingGewichtKg = static_cast<double>( attr->value.value_float.value );
                m_ladingGewichtKg = nieuw.ladingGewichtKg;
            }
        }

        m_huidigeRit = nieuw;
        // Take brand and model from the latest truck configuration, otherwise
        // those fields in webhook and history are empty again.
        m_huidigeRit.voertuigMerk = m_configMerk;
        m_huidigeRit.voertuigModel = m_configModel;
        m_gladdeSchattingMin = -1.0;  // fresh trip, fresh estimate
        m_huidigeLadingId = cargoAttr->value.value_string.value;
        m_actief = true;
        m_ritStartMoment = std::chrono::steady_clock::now();
        m_laatsteSnelheidMeting = m_ritStartMoment;
        m_gepauzeerd = false;
        m_totaalGepauzeerdSeconden = 0.0;
        m_kmVenster.clear();

        // ONLY the trip counter to zero. The consumption measurement itself
        // -- the window, the smoothed values, the last reading -- keeps
        // running: it runs on FuelCosts' session counter and belongs to the
        // truck, not the trip. This used to wipe everything, so idle and
        // instantaneous had to rebuild at every trip start while the engine
        // just kept running.
        m_rijdendLiters = 0.0;
        m_rijdendKm = 0.0;
        m_meetKmTotaal = 0.0;
        m_brandstof.StartNieuweRit();
    }

    void TruckTracking::OpGameplayEvent( const scs_telemetry_gameplay_event_t *info )
    {
        SchrijfDebugGameplayEvent( info );

        // "job.delivered" on a successful delivery, "job.cancelled" on
        // cancel. Both have now been verified against the official SDK
        // constants (SCS_TELEMETRY_GAMEPLAY_EVENT_job_delivered /
        // _job_cancelled) -- so no longer an assumption.
        const std::string eventId = info->id;

        // --- Expenses during the trip (idea list #3, #4, #5) -----------
        // These event ids are in the official SDK header as
        // SCS_TELEMETRY_GAMEPLAY_EVENT_*; I checked them in the public
        // scs-sdk-plugin that handles exactly these four. They also arrive
        // when you are NOT on a job (free roaming) -- then we skip them,
        // because they belong to no trip.
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
            // Ferry and train also report from where and to where; a toll gate
            // does not have those attributes, then they stay empty.
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
                // The clock is about to jump ahead; that is not rest.
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

        // ETA check: at trip start the first estimate was written, here the
        // ACTUAL end. That lets you check afterwards whether the arrival time
        // in real time is right and whether the error grows with distance.
        Logboek::Schrijf( "eta", std::string( isGeannuleerd ? "trip CANCELLED" : "trip DELIVERED" )
            + " -- driven=" + std::to_string( m_huidigeRit.afgelegdeAfstandKm ) + " km"
            + " planned=" + std::to_string( m_huidigeRit.geplandeAfstandKm ) + " km"
            + " duration=" + std::to_string( VerstrekenMinutenEcht() ) + " min real"
            + " firstEstimate=" + std::to_string( m_eersteSchattingMinuten ) + " min" );

        m_logger.RegisterVoltooideRit( m_huidigeRit );
        m_huidigeRit = Trip{};
        m_huidigeLadingId.clear();
        m_actief = false;
        m_eersteSchattingMinuten = -1.0;

        // Only the trip counter to zero; the measurement itself keeps running
        // (see StartRecord). The average on screen comes from the per-vehicle
        // counter and no longer from this trip counter, so "give empty driving
        // afterwards its own average" no longer happens there -- exactly as
        // the truck's dashboard does not either.
        m_rijdendLiters = 0.0;
        m_rijdendKm = 0.0;
        m_meetKmTotaal = 0.0;
    }

    double TruckTracking::VerstrekenMinutenEcht() const
    {
        if( !m_actief ) return 0.0;
        auto nu = std::chrono::steady_clock::now();
        double totaalSeconden = std::chrono::duration<double>( nu - m_ritStartMoment ).count();

        double gepauzeerdSeconden = m_totaalGepauzeerdSeconden;
        if( m_gepauzeerd )
        {
            // we are in the middle of it now -- count the running part too
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

        // Preferably: the game's OWN, already computed remaining navigation
        // distance (via truck.navigation.distance, real road, already
        // accounts for bends) -- much more accurate than our own sum. Only
        // use it once the channel has passed a value at least once (-1 = not
        // received yet).
        double resterendeKm;
        if( m_navigatieAfstandMeter >= 0.0 )
        {
            resterendeKm = m_navigatieAfstandMeter / 1000.0;
        }
        else
        {
            // Fallback: own sum (planned - driven), as before.
            resterendeKm = m_huidigeRit.geplandeAfstandKm - m_huidigeRit.afgelegdeAfstandKm;
        }

        if( resterendeKm <= 0.0 )
        {
            return 0.0;
        }

        // ---- Source 0: the game's own ETA -------------------------------
        //
        // truck.navigation.time is the Route Advisor estimate -- the same one
        // at the bottom of your screen. It knows the real route, the limits
        // and the ferries, and does NOT change when you briefly stop for a
        // traffic light. That is exactly why it is the only good source.
        //
        // SCS' own header says seconds, so that is the assumption. Only if
        // that assumption yields an impossible speed do we try minutes.
        // Otherwise we stick to seconds -- falling back on our own speed
        // reading is worse than a slightly skewed ETA: standing still, it
        // divides by almost zero and you get "42 hours".
        if( m_navigatieTijd > 0.0 )
        {
            const double schaal = TijdSchaal();
            double spelUren = m_navigatieTijd / 3600.0;  // assumption: seconds

            const double impliciet = spelUren > 0.0 ? resterendeKm / spelUren : 0.0;
            if( impliciet < 3.0 || impliciet > 200.0 )
            {
                // Impossible speed -> probably minutes after all.
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

        // 1) Preferably: moving average of the last ~3 minutes -- reacts
        //    quickly to changing conditions (like Trucky, which recomputes
        //    continuously instead of using one fixed average over the whole
        //    trip).
        if( m_kmVenster.size() >= 2 )
        {
            const auto &eerste = m_kmVenster.front();
            const auto &laatste = m_kmVenster.back();
            double tijdspanneUur = std::chrono::duration<double>( laatste.first - eerste.first ).count() / 3600.0;
            double kmVerschil = laatste.second - eerste.second;
            if( tijdspanneUur > ( 30.0 / 3600.0 ) )  // at least 30 sec of data
            {
                gemiddeldeSnelheid = kmVerschil / tijdspanneUur;
            }
        }

        // 2) Otherwise: average over the whole trip so far.
        if( gemiddeldeSnelheid < 1.0 )
        {
            double verstrekenMin = VerstrekenMinutenEcht();
            if( verstrekenMin > 0.5 && m_huidigeRit.afgelegdeAfstandKm > 0.5 )
            {
                gemiddeldeSnelheid = m_huidigeRit.afgelegdeAfstandKm / ( verstrekenMin / 60.0 );
            }
        }

        // 3) Otherwise: the speedometer at this moment.
        //
        // NOTE -- this was the source of the odd results. The speedometer
        // gives km per GAME hour, while the two sources above give km per
        // REAL hour (distance divided by elapsed real time). Mixing those up
        // is off by a factor 6 in TruckersMP: a fifteen-minute hop was shown
        // as an hour and a half, and a long trip as 22 hours. So we convert
        // the speedometer speed first.
        if( gemiddeldeSnelheid < 1.0 )
        {
            // The speedometer gives km per GAME hour; convert to km per REAL hour.
            gemiddeldeSnelheid = m_huidigeRit.huidigeSnelheidKmh * TijdSchaal();
        }

        // Floor under the speed. Without this a truck standing still (1 km/h
        // at a traffic light) divides the remaining distance by almost zero,
        // and out comes "42 hours" -- exactly what went wrong. In that case
        // we reckon with a reasonable travel speed instead of the moment. The
        // game's ETA above is the better source anyway; this is only a safety
        // net for when it is missing.
        // Mind the UNIT: `gemiddeldeSnelheid` is game km per REAL hour
        // (distance divided by elapsed real time), while 40 km/h is a
        // SPEEDOMETER reading. You convert with the time scale, not divide by
        // it -- this used to say "* schaal / 6", so the floor was six times
        // too low and a crawling truck still slipped through.
        const double BODEM_SNELHEID_ECHT = 40.0 * TijdSchaal();  // 40 km/h on the speedometer
        if( gemiddeldeSnelheid < BODEM_SNELHEID_ECHT )
        {
            if( m_gladdeSchattingMin > 0.0 )
            {
                return m_gladdeSchattingMin;  // keep the last known value
            }
            gemiddeldeSnelheid = BODEM_SNELHEID_ECHT;
        }

        const double schatting = Gladstrijken( ( resterendeKm / gemiddeldeSnelheid ) * 60.0 );

        // Remember the VERY FIRST usable estimate of this trip. At completion
        // it is placed next to the actual duration; that shows whether the
        // error grows with distance.
        if( m_eersteSchattingMinuten < 0.0 && schatting > 0.0 )
        {
            m_eersteSchattingMinuten = schatting;
            Logboek::Schrijf( "eta", "first estimate: " + std::to_string( schatting )
                                        + " min real, remaining=" + std::to_string( resterendeKm ) + " km"
                                        + ", planned=" + std::to_string( m_huidigeRit.geplandeAfstandKm ) + " km" );
        }

        return schatting;
    }

    double TruckTracking::Demp( double huidig, double nieuw, double dtSec, double tauSec )
    {
        // TIME based, not per call. This function is called per frame --
        // measured about 59 times a second. With a fixed percentage per call
        // the average catches up within a tenth of a second and nothing is
        // damped. With dt/(dt+tau) the damping depends on the ELAPSED TIME,
        // and tau means roughly how long a change takes to work through.
        if( nieuw < 0.0 ) return huidig;
        if( huidig < 0.0 ) return nieuw;  // first reading: just take it

        // Only take over directly on a REALLY big step. The old limit (half
        // off or on) fired on almost every reading: the game burns ten times
        // as much uphill as coasting, even at the same speed (measured 30-08).
        // So the damping was skipped continuously. This wider limit lets
        // ordinary hills damp and only catches real steps.
        const double verhouding = nieuw / std::max( 0.001, huidig );
        if( verhouding > 4.0 || verhouding < 0.25 ) return nieuw;

        if( dtSec <= 0.0 || tauSec <= 0.0 ) return nieuw;
        const double alpha = dtSec / ( dtSec + tauSec );
        return huidig + alpha * ( nieuw - huidig );
    }

    double TruckTracking::Gladstrijken( double ruweMinuten ) const
    {
        // Without this the estimate bounces back and forth at every traffic
        // light or overtake. Per call we move a little towards the new value
        // instead of snapping to it.
        if( m_gladdeSchattingMin < 0.0 )
        {
            m_gladdeSchattingMin = ruweMinuten;  // first time: just take it
        }
        else
        {
            // Big jumps (more than half off or on) we do take over directly --
            // then something really changed, for example a new route, and slow
            // adjustment is only confusing.
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

    // ---- Pause callbacks --------------------------------------------

    SCSAPI_VOID TruckTracking::GepauzeerdCallback( const scs_event_t, const void *, scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( !self->m_gepauzeerd )
        {
            self->m_gepauzeerd = true;
            self->m_pauzeStartMoment = std::chrono::steady_clock::now();
            // Measurement: does this event fire on TruckersMP in the garage and
            // the pause menu? The mouse release hangs on it.
            Logboek::Schrijf( "flags", "SCS reports: paused" );
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
            Logboek::Schrijf( "flags", "SCS reports: resumed" );
            // Is a truck waiting for recognition? Then the wait time for the
            // odometer starts NOW, not in the garage.
            // MEASURED 04-09: config arrived in the garage (paused), the three
            // seconds ran out there, and 3 ms after resuming the Scania was
            // recognised at the Volvo's reading -- the real reading only came
            // 0.3 s later.
            if( self->m_huidigVoertuig < 0 ) self->m_configMoment = std::chrono::steady_clock::now();
            // Prevent an artificial speed spike in the live km integration right
            // after resuming (there was, after all, a "gap" in time).
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

        // Only log when it CHANGES, otherwise debug.log fills up.
        static std::string laatst;
        if( land != laatst )
        {
            laatst = land;
            Logboek::Schrijf( "event", std::string( "country via '" ) + ( naam ? naam : "?" ) + "': " + land );
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

        // Read the type as the game offers it, not as we thought: the `type`
        // field tells what is really in there.
        double minuten;
        switch( value->type )
        {
            case SCS_VALUE_TYPE_s32:   minuten = static_cast<double>( value->value_s32.value ); break;
            case SCS_VALUE_TYPE_u32:   minuten = static_cast<double>( value->value_u32.value ); break;
            case SCS_VALUE_TYPE_float: minuten = static_cast<double>( value->value_float.value ); break;
            default: return;
        }
        if( minuten < 0.0 ) return;  // invalid / fatigue off

        // Measure what this channel does. If it ever jumps up, there was rest
        // and we can reset on it. If it only ever falls, that does not happen
        // on this server and syncing with the in-game P counter is pointless
        // anyway.
        if( self->m_minutenTotRust >= 0.0 && minuten > self->m_minutenTotRust + 30.0 )
        {
            ++self->m_rustResets;
            self->m_pauzeRijSpelMin = 0.0;  // counter jumped up = there was rest
        }
        if( self->m_rustLaagst < 0.0 || minuten < self->m_rustLaagst )
        {
            self->m_rustLaagst = minuten;
        }

        self->m_minutenTotRust = minuten;
        // The highest value is the length of a full period. After a rest the
        // counter jumps up again, and then the scale is right by itself.
        if( minuten > self->m_rustPeriodeMax ) self->m_rustPeriodeMax = minuten;
    }

    SCSAPI_VOID TruckTracking::GameTimeCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                  scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( !value ) return;

        // Record the thread ID once. Together with the line from OnPostRender
        // (see Plugin.cxx) this answers whether the SCS callbacks and the
        // drawing run on DIFFERENT threads. If so, two threads read and write
        // the same fields in this class without a lock, and synchronisation is
        // needed. If the two numbers are equal, that does not apply.
        static bool telemetrieThreadGelogd = false;
        if( !telemetrieThreadGelogd )
        {
            telemetrieThreadGelogd = true;
            Logboek::Schrijf( "flags", "thread id SCS telemetry callback: "
                                            + std::to_string( Logboek::HuidigeThreadId() ) );
        }

        const std::uint32_t minuten = value->value_u32.value;
        if( minuten == self->m_economyTijd ) return;  // no change: nothing to learn

        const auto nu = std::chrono::steady_clock::now();

        if( self->m_schaalGestart )
        {
            // Assess a single step. A normal step is 1 game minute per ~10 real
            // seconds. If the clock suddenly jumps much further (server syncing
            // time, rest, or having been paused), that step is unusable AND
            // pollutes the whole measurement -- because the anchor never moved,
            // so such a jump kept having effect forever. Hence: on an odd step,
            // start measuring again.
            const double stapSpel = static_cast<double>( minuten ) - static_cast<double>( self->m_economyTijd );

            // RECOGNISE REST. You do not stand still for nine hours with the
            // engine idling -- you sleep. For the game that means: the clock jumps
            // hours ahead in one go while you are not driving. That is the only
            // reliable signal we have, because the Mandatory Break counter itself
            // does not come through the telemetry.
            //
            // Threshold at 45 minutes. Since 1.60 you can choose your own wake
            // time, so a rest can be short -- at four hours it was missed.
            //
            // Ferries and trains jump ahead too, but we recognise those by their
            // own gameplay event (see the expense handling) and skip them via
            // `m_negeerVolgendeSprong`. That is more precise than a time
            // threshold: a five-hour ferry still does not count as rest, while a
            // one-hour nap does.
            if( stapSpel >= 45.0 && self->m_negeerVolgendeSprong )
            {
                self->m_negeerVolgendeSprong = false;  // was the ferry/train
            }
            else if( stapSpel >= 45.0 && self->m_liveSnelheidKmh < 2.0 )
            {
                self->m_pauzeRijSpelMin = 0.0;
                self->m_pauzeStilstandSpelMin = stapSpel;
                self->m_laatsteRustSpelMinuten = stapSpel;
                self->m_tachoRijSecondenSindsRust = 0.0;
                self->m_tachoInRust = true;
                // Set the zero point: from here the game clock counts again.
                self->m_economyTijdLaatsteRust = minuten;
                self->m_rustMomentBekend = true;
                self->m_getoondeRestMin = -1.0;  // rest: may go up again

                // For the OWN tachograph (modes 2 and 3) it matters HOW LONG the jump
                // was: a short interruption is a break, a long one is daily rest. So
                // we need not guess what you did.
                if( stapSpel >= self->m_tacho.dagRust * 0.8 )
                {
                    self->m_eigenLaatsteDagrust = minuten;
                    self->m_eigenLaatstePauze = minuten;  // daily rest also counts as a break
                }
                else if( stapSpel >= self->m_tacho.pauzeDuur * 0.9 )
                {
                    self->m_eigenLaatstePauze = minuten;
                }
                self->m_getoondeRijtijdMin = -1.0;  // and the driving time back to zero
                self->BewaarTachoStand();  // record to disk immediately
            }
            const double stapEcht =
                std::chrono::duration<double>( nu - self->m_schaalLaatsteEcht ).count() / 60.0;

            const bool rareStap = stapSpel < 0.0 ||  // clock back
                                   stapSpel > 5.0 ||  // big jump ahead
                                   ( stapEcht > 0.0 && ( stapSpel / stapEcht ) > 30.0 );
            if( rareStap )
            {
                self->m_schaalEersteEconomy = minuten;
                self->m_schaalEersteEcht = nu;
            }
            else
            {
                // Refresh the anchor every 15 minutes, so an old reading does not
                // keep weighing in forever when conditions change.
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
                    // There was a saved value: anchor it to the clock of NOW, so you
                    // continue exactly where you left off -- even if the server clock has
                    // run on for days meanwhile.
                    self->m_economyTijdLaatsteRust = static_cast<std::uint32_t>(
                        static_cast<double>( minuten ) - ( periode - self->m_teHerstellenRest ) );
                    self->m_teHerstellenRest = -1.0;
                }
                else
                {
                    // Nothing saved: this is our zero point until a rest comes.
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
            // A saved value stays, full stop. Even an old one: the counter should
            // run in step with your in-game P clock, and that remembers it too.
            // Quit today and drive on tomorrow, and it is still right.
            //
            // Load a different profile where the clock is very different, and you
            // take a rest yourself to get back in step. That is the handicap of
            // not being able to read the real P counter -- and better than the
            // plugin fiddling with a value that may well be correct.
        }

        self->m_schaalLaatsteEcht = nu;
        self->m_economyTijd = minuten;
    }

    double TruckTracking::TijdSchaal() const
    {
        // TruckersMP keeps the clock equal for the whole server and uses a
        // fixed scale for that: 1 game minute per 10 real seconds, i.e. 6
        // game minutes per real minute. That is our base value -- and at once
        // the reason we are never without an answer.
        //
        // We MEASURE as well, but purely as a correction in case the scale
        // ever changes (TMP touched it in 0.7.5.0) or you play offline, where
        // the game does switch between motorway and city.
        //
        // The measurement may only override the base value if it is CLOSE TO
        // it. A time jump from server sync or a pause otherwise yields an
        // absurd scale, and then the arrival time flies all over the place --
        // exactly what we want to prevent.
        constexpr double STANDAARD_SCHAAL = 6.0;  // TruckersMP: 10 real sec = 1 game minute
        // Bounds wide enough for BOTH ways of playing:
        //   TruckersMP   -> 10 real seconds per game minute  = 6
        //   Singleplayer -> 1 game hour per ~3.16 real minutes = ~19
        //
        // They were briefly 4..9 to damp the "flipping", but that rejected
        // singleplayer (19) and reckoned with 6 there -- a factor three off.
        // Now that the scale is LOCKED after measuring, that damping is no
        // longer needed: nothing moves while driving anyway. Anything outside
        // this band is a time jump, not a real scale.
        constexpr double ONDERGRENS = 3.0;
        constexpr double BOVENGRENS = 25.0;

        // Set manually? Then that is it, full stop.
        if( m_handmatigeSchaal > 0.0 ) return m_handmatigeSchaal;

        // Once locked it stays locked: while driving this number must not
        // move any more, otherwise the arrival time bounces with it.
        if( m_vastgezetteSchaal > 0.0 ) return m_vastgezetteSchaal;

        if( !m_schaalGestart ) return STANDAARD_SCHAAL;

        const double echteMinuten =
            std::chrono::duration<double>( std::chrono::steady_clock::now() - m_schaalEersteEcht ).count() / 60.0;

        // Below two minutes the measurement is too coarse: game.time jumps
        // in whole minutes, so a single step gives a seemingly huge or tiny
        // speed.
        if( echteMinuten < 2.0 ) return STANDAARD_SCHAAL;

        const double spelMinuten =
            static_cast<double>( m_economyTijd ) - static_cast<double>( m_schaalEersteEconomy );
        if( spelMinuten <= 0.0 ) return STANDAARD_SCHAAL;

        const double gemeten = spelMinuten / echteMinuten;
        if( gemeten < ONDERGRENS || gemeten > BOVENGRENS )
        {
            return STANDAARD_SCHAAL;  // measurement not trustworthy
        }

        // After five minutes of clean measurement it is enough: lock. From
        // that moment this is as stable as a hard-coded 6, but with the value
        // this server really uses.
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
        // Straight from the game clock: how many game minutes have passed
        // since your last rest? That is exactly what the P counter in the game
        // does too -- it counts elapsed time, whether you drive or stand still.
        if( m_rustMomentBekend )
        {
            const double periodeNu = m_rustPeriodeMax > 60.0 ? m_rustPeriodeMax : MAX_RIJ_SPELMINUTEN;
            const double verstreken =
                static_cast<double>( m_economyTijd ) - static_cast<double>( m_economyTijdLaatsteRust );
            if( verstreken >= 0.0 )
            {
                // Never show more than a full period. If the clock is below the saved
                // moment (different profile) you would otherwise see "14 hours". The
                // saved value is left alone -- this only limits what appears ON
                // SCREEN.
                const double rest = std::min( periodeNu, periodeNu - verstreken );

                if( m_getoondeRestMin < 0.0 )
                {
                    m_getoondeRestMin = rest;  // first value: just take it
                    BewaarTachoStand();
                    return m_getoondeRestMin;
                }

                // Two brakes on the number, so it never visibly jolts from a server
                // sync or a ping spike:
                //
                // 1. ONLY DOWN. If the clock jumps back a minute, the remaining time
                //    would bob up -- that looks like a bug. A countdown should fall.
                //
                // 2. ONLY IN WHOLE MINUTES. We only take a new value once it is at
                //    least a full minute lower. Smaller differences we leave; that
                //    way the number stands still instead of trembling back and forth.
                //
                // On a real rest the brake comes off explicitly (see
                // GameTimeCallback), because then it MUST go back up.
                if( rest <= m_getoondeRestMin - 1.0 )
                {
                    m_getoondeRestMin = rest;

                    // Write every full minute. That way the latest value is always on
                    // disk, even if the game or the plugin crashes unexpectedly -- there
                    // is no shutdown moment we can count on.
                    BewaarTachoStand();
                }
                return m_getoondeRestMin;
            }
        }

        // Do NOT hard-code the period. SCS says 10 hours for singleplayer,
        // but servers can change that -- there are reports of 11 hours on
        // TruckersMP. Instead of choosing we let the game tell us: the highest
        // value the rest channel ever passed IS the length of a full period.
        // That way it is right on every server, and also if SCS or TMP change
        // it again tomorrow.
        //
        // As long as that channel has given nothing usable, we fall back on
        // the 10 hours from the SCS announcement.
        const double periode = m_rustPeriodeMax > 60.0 ? m_rustPeriodeMax : MAX_RIJ_SPELMINUTEN;
        return periode - m_pauzeRijSpelMin;
    }

    double TruckTracking::RijPeriodeSpelMinuten() const
    {
        return m_rustPeriodeMax > 60.0 ? m_rustPeriodeMax : MAX_RIJ_SPELMINUTEN;
    }

    double TruckTracking::TachograafRijtijdMinuten() const
    {
        // On the SERVER CLOCK, not on real seconds. In TruckersMP the server
        // dictates game time; by taking the difference with the moment of
        // your last rest, this counter runs in step with the server by
        // definition -- even if your PC stutters or the plugin skips a frame.
        //
        // This used to be a sum of real seconds. At the TMP time scale that
        // ran six times too slow and drifted as well.
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
                // This counter counts UP, so here only upwards -- and only once a
                // full minute is added. That way the number stands still instead of
                // trembling when the server clock briefly jumps back.
                else if( verstreken >= m_getoondeRijtijdMin + 1.0 )
                {
                    m_getoondeRijtijdMin = verstreken;
                }
                return m_getoondeRijtijdMin;
            }
        }
        return m_tachoRijSecondenSindsRust / 60.0;  // fallback until the clock arrives
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
        if( verstrekenSeconden <= 0.0 || verstrekenSeconden > 10.0 ) return;  // sanity check

        const double STILSTAND_DREMPEL_KMH = 2.0;

        // No more time threshold for standing still. It was first a minute
        // (so refuelling wiped your driving time) and then a full rest period
        // -- but even that was a back door. Standing still is not rest, full
        // stop. The only reset runs via the time jump.

        // The mandatory-break counter is NO LONGER summed here. It reads the
        // difference between the server clock and the moment of your last
        // rest directly (see MinutenTotVerplichtePauze). That runs in step
        // with TruckersMP by definition: the server dictates that clock.
        //
        // This used to be a sum of real seconds times the time scale. That
        // worked, but could drift on stutters and depended on how well we had
        // measured the scale -- both unnecessary if you can just read the
        // clock.

        if( snelheidKmh < STILSTAND_DREMPEL_KMH )
        {
            // Only track HOW LONG you stand still; that no longer zeroes the
            // driving time. The rest flag is set by the time jump in
            // GameTimeCallback, because that is the only signal that separates a
            // REAL rest from just being parked.
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

        double snelheidKmh = value->value_float.value * 3.6;  // m/s -> km/h
        self->m_liveSnelheidKmh = snelheidKmh;

        // Always forward to the bus line tracking, EVEN when no cargo job is
        // active (e.g. during a bus line) -- this channel is not job-type
        // specific, it is simply the speed of the vehicle you are driving
        // right now.
        if( self->m_busTracking != nullptr )
        {
            self->m_busTracking->OpLiveSnelheid( snelheidKmh, self->m_gepauzeerd );
            // The bus has no SCS registration of its own; it gets the navigation
            // data via this route, like the speed.
            self->m_busTracking->ZetNavigatie( self->m_navigatieTijd,
                                                self->m_navigatieAfstandMeter >= 0.0
                                                    ? self->m_navigatieAfstandMeter / 1000.0
                                                    : -1.0 );
        }

        // Tachograph always keeps running, regardless of whether a job is
        // active (you can also "just drive around" without cargo).
        if( !self->m_gepauzeerd )
        {
            self->TachograafUpdate( snelheidKmh );
        }

        // --- Consumption measurement: ALWAYS runs, also without an active trip --
        // Deliberately above the "no trip" exit below: the board computer tab
        // is there precisely for when you drive around empty. During pause we
        // skip, like the other measurements: then you are in the menu.
        //
        // The distance comes from SPEED x TIME, not from the odometer. That
        // is how the game itself does it: it computes consumption as litres
        // per HOUR (displacement, rpm, throttle), and l/100km is derived from
        // that -- l/h divided by km/h. Time is a perfect denominator, the
        // odometer is not: over a few seconds it is too coarse, and that made
        // the figure both too high and restless.
        if( !self->m_gepauzeerd )
        {
            const auto nuMeting = std::chrono::steady_clock::now();
            // The SESSION counter, not the trip counter. It keeps running across
            // trip boundaries, so this measurement no longer needs to be reset at
            // trip start and completion. Idle and instantaneous belong to the
            // truck, not to a trip.
            const double verbruiktNu = self->m_brandstof.HuidigeState().verbruiktSessieLiters;

            // Hold on to it BEFORE m_vorigMeetMoment is overwritten below -- the
            // damping further down needs this time step.
            const auto vorigeMeetMoment = self->m_vorigMeetMoment;
            const bool hadVorigeMeting = self->m_meetGestart;

            if( self->m_meetGestart && self->m_vorigMeetLiters >= 0.0 )
            {
                const double dtUur =
                    std::chrono::duration<double>( nuMeting - self->m_vorigMeetMoment ).count() / 3600.0;
                const double dLiters = verbruiktNu - self->m_vorigMeetLiters;

                // Sanity check against odd jumps, same limit as for the trip
                // distance above.
                if( dtUur > 0.0 && dtUur < 0.1 )
                {
                    // MEASURED 30-08: the game's odometer is the truth, and it advanced
                    // 3.2x faster than our own sum "speed x time scale x time". Reason:
                    // the time scale (clock, ~6) is NOT the same as the map scale (~19).
                    // The odometer counts real kilometres, the speedometer belongs to the
                    // shrunken world. Instead of inventing yet another factor we simply
                    // take the odometer itself -- that cannot be wrong by definition.
                    double dKm = 0.0;
                    if( self->m_vorigMeetOdometerKm >= 0.0 )
                    {
                        const double stap = self->m_kilometerstandKm - self->m_vorigMeetOdometerKm;
                        // Only forward, and no more than possible in this time step (catches
                        // teleport/loading).
                        if( stap > 0.0 && stap < 5.0 ) dKm = stap;
                    }
                    self->m_meetKmTotaal += dKm;

                    // Measure the distance factor: how much does the odometer count per
                    // "speedometer kilometre"? Only at decent speed, otherwise the
                    // division is too imprecise. Adjust gently -- this is a property of
                    // the game, not a value that should change from moment to moment.
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

                                // Write at most once a minute, and only if it really shifted. Saving
                                // every reading would write to disk needlessly while you drive.
                                if( ( self->m_afstandsFactor - self->m_bewaardeFactor ) >  0.2 ||
                                    ( self->m_afstandsFactor - self->m_bewaardeFactor ) < -0.2 )
                                {
                                    self->m_bewaardeFactor = self->m_afstandsFactor;
                                    try
                                    {
                                        // One number, that is all. This file uses no JSON otherwise, so no
                                        // library for that either.
                                        std::ofstream uit( MetingPad() );
                                        if( uit ) uit << self->m_afstandsFactor;
                                    }
                                    catch( ... ) { /* opslaan mag nooit storen */ }
                                }
                            }
                        }
                    }

                    // Skip negative litre steps: the litre counter starts over at a new
                    // trip.
                    if( snelheidKmh >= RIJDT_DREMPEL_KMH && dLiters >= 0.0 )
                    {
                        self->m_rijdendLiters += dLiters;
                        self->m_rijdendKm += dKm;
                    }

                    // Per-vehicle counter: ALL litres, including idle, and all kilometres
                    // -- like the dashboard does. Recognise as soon as brand and odometer
                    // are both there.
                    if( self->m_huidigVoertuig < 0 ) self->IdentificeerVoertuig();
                    if( self->m_huidigVoertuig >= 0 && dLiters >= 0.0 )
                    {
                        VoertuigTeller &v = self->m_voertuigen[ self->m_huidigVoertuig ];
                        v.liters += dLiters;
                        v.km += dKm;
                        v.kmStand = self->m_kilometerstandKm;
                        self->m_voertuigenGewijzigd = true;
                        self->BewaarVoertuigen( false );
                    }

                    // Driving style: same reading, four counts added.
                    self->RijstijlTellen( snelheidKmh, self->m_gaspedaal, dKm, dtUur * 3600.0 );

                    // Is the save thread done? Then take over the counter.
                    self->VerwerkSaveResultaat();
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

            // Throttle on or off? Then everything in the window is stale: your
            // consumption changes NOW, but the window looks back. Empty it and
            // start over -- within a second there is a figure again, and then it
            // matches what your foot does.
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

            // --- Raw figure from the window, then damp ----------------------
            if( self->m_brandstofVenster.size() >= 2 )
            {
                const auto &eerste = self->m_brandstofVenster.front();
                const auto &laatste = self->m_brandstofVenster.back();
                const double spanSec =
                    std::chrono::duration<double>( laatste.moment - eerste.moment ).count();
                const double dL = laatste.verbruiktLiters - eerste.verbruiktLiters;
                const double dKmVenster = laatste.gemetenKm - eerste.gemetenKm;

                // Negative litre difference = the counter started over (new trip).
                // Skip; the window is clean again right away.
                if( spanSec > MIN_SPAN_SECONDEN && dL >= 0.0 )
                {
                    // TWO figures, each with its own window:
                    //
                    //  - literPerUurLang belongs WITH dKmVenster (same points, same time
                    //    span) and goes to l/100km. Those two must come from the same
                    //    window, otherwise the division is off.
                    //  - literPerUurKort is for the l/h display. No distance needed
                    //    there, so that can be over a much shorter stretch. With the long
                    //    window, 15 seconds of driving data kept counting after stopping
                    //    -- measured 30-08: "94.1 l/h" with the engine OFF.
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

                    // Elapsed time since the previous reading. Needed because the damping
                    // works on time and not on the number of calls (this block runs per
                    // frame, about 59 times a second).
                    const double dempDt = hadVorigeMeting
                        ? std::chrono::duration<double>( nuMeting - vorigeMeetMoment ).count()
                        : 0.0;

                    // Transition driving <-> standstill: skip the damping, so the figure
                    // flips right away instead of taking seconds.
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

                    // Only update l/100km when you drive fast enough to make it
                    // meaningful (see PER100_MIN_KMH). Below that we leave the last value
                    // alone; l/h is shown anyway then.
                    const double schaal = self->TijdSchaal();
                    const double gemSnelheid = dKmVenster / ( spanSec / 3600.0 );
                    if( gemSnelheid >= PER100_MIN_KMH * schaal && literPerUurLang > 0.0 )
                    {
                        const double ruwPer100 = literPerUurLang / gemSnelheid * 100.0;
                        self->m_gladVerbruikNu = Demp( self->m_gladVerbruikNu, ruwPer100,
                                                        dempDt, DEMP_TAU_SECONDEN );
                    }

                    // Debug line, at most once every three seconds. With this you can see
                    // afterwards WHICH number derails, instead of having to invent a
                    // theory.
                    if( std::chrono::duration<double>( nuMeting - self->m_laatsteVerbruikLog ).count()
                            > VerbruikLogInterval() )
                    {
                        self->m_laatsteVerbruikLog = nuMeting;
                        const auto bs = self->m_brandstof.HuidigeState();
                        char regel[ 520 ];
                        std::snprintf( regel, sizeof( regel ),
                            "speed=%.1f scale=%.2f span=%.2fs n=%d tank=%.4f counter=%.4f "
                            "dL=%.4f dKm=%.4f luur_kort=%.1f luur_lang=%.1f ruw_per100=%.1f "
                            "smooth_l_per_h=%.1f smooth_per100=%.1f drivingL=%.3f drivingKm=%.3f "
                            "odo=%.3f meetKm=%.3f afstandsfactor=%.2f gas=%.2f "
                            "sdk_per100=%.1f sdk_range=%.0f",
                            snelheidKmh, schaal, spanSec,
                            static_cast<int>( self->m_brandstofVenster.size() ),
                            bs.huidigeLiters, bs.verbruikSindsRitStartLiters,
                            dL, dKmVenster,
                            literPerUurKort, literPerUurLang,
                            ( gemSnelheid > 0.0 ) ? literPerUurLang / gemSnelheid * 100.0 : -1.0,
                            self->m_gladLiterPerUur, self->m_gladVerbruikNu,
                            self->m_rijdendLiters, self->m_rijdendKm,
                            self->m_kilometerstandKm, self->m_meetKmTotaal, self->m_afstandsFactor,
                            self->m_gaspedaal,
                            // What the GAME itself says: its own consumption estimate (l/km, here
                            // times 100) and the range it derives from it. Measurement of whether
                            // this channel is populated on TruckersMP and usable as a seed.
                            ( self->m_verbruikLiterPerKm >= 0.0 ) ? self->m_verbruikLiterPerKm * 100.0 : -1.0,
                            self->m_bereikKm );
                        Logboek::Schrijf( "fuel", regel );
                    }
                }
            }
        }

        if( !self->m_actief ) return;

        self->m_huidigeRit.huidigeSnelheidKmh = snelheidKmh;

        // During a pause (see GepauzeerdCallback) do not "invent" kilometres
        // and do not let the measurement clock tick -- otherwise the estimate
        // thinks you drove very slowly while you were just in the menu.
        if( self->m_gepauzeerd )
        {
            return;
        }

        // Live "tracked" distance by summing speed x elapsed time since the
        // previous reading -- see the note at m_laatsteSnelheidMeting in
        // TruckTracking.hxx.
        auto nu = std::chrono::steady_clock::now();
        double verstrekenUur = std::chrono::duration<double>( nu - self->m_laatsteSnelheidMeting ).count() / 3600.0;
        if( verstrekenUur > 0.0 && verstrekenUur < 0.1 )  // sanity check against odd jumps
        {
            self->m_huidigeRit.afgelegdeAfstandKm += snelheidKmh * verstrekenUur;
        }
        self->m_laatsteSnelheidMeting = nu;

        // Update the moving-average window: add a new point, and discard
        // everything older than VENSTER_SECONDEN.
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
            // Pass the odometer, so a refuelling stop knows where it was.
            self->m_brandstof.ZetKilometerstand( self->m_kilometerstandKm );
            // Position along, so a refuelling stop knows which country and
            // whether it is at your own garage. Same fix the radar uses.
            if( self->m_spelersVoorIncident )
            {
                double px = 0.0, pz = 0.0;
                const bool bekend = self->m_spelersVoorIncident->EigenPositie( px, pz );
                self->m_brandstof.ZetPositie( px, pz, bekend );
            }
            self->m_brandstof.ZetLiters( value->value_float.value, self->m_tankInhoudLiters );
            self->m_huidigeRit.brandstofPercentage =
                self->m_tankInhoudLiters > 0.0 ? ( value->value_float.value / self->m_tankInhoudLiters ) * 100.0 : 0.0;

            // Debug: log the first time what really comes in (raw litres, tank
            // capacity, computed percentage) -- same principle as with
            // navigation.distance: see first, then trust.
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
                    uit << "[Fuel] truck.fuel raw value: " << value->value_float.value
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

        // Skip the VERY FIRST reading. At load the value jumps from zero to
        // the actual damage of your truck, and that is not a collision. That
        // was exactly why every session started with an empty incident: the
        // buffer was still empty and no player had streamed in yet.
        if( !self->m_schadeGemeten )
        {
            self->m_schadeGemeten = true;
            return;
        }

        // Sudden jump --> probably a collision, report it to the incident
        // recorder.
        //
        // MEASURED 02-09: with the old threshold of 1.5 percentage points it
        // almost never fired on a real collision. Against a wall the damage
        // rises GRADUALLY -- half a point per reading while you scrape along
        // -- and then no single step reaches 1.5. What did get through was
        // the jump at LOAD, from zero to the actual value. Hence every
        // recorded incident came from the first twenty seconds and was empty.
        //
        // 0.3 does catch a real knock. The ten-second lockout afterwards
        // prevents a long scrape from making twenty recordings in a row and
        // immediately overwriting the first (with the run-up in it).
        constexpr double SCHADE_DREMPEL = 0.3;
        constexpr double HERHAAL_BLOKKADE_SEC = 10.0;
        const auto nu = std::chrono::steady_clock::now();
        const double sindsVorige =
            std::chrono::duration<double>( nu - self->m_laatsteSchademelding ).count();

        if( sprong > SCHADE_DREMPEL && sindsVorige > HERHAAL_BLOKKADE_SEC
            && self->m_incidentRecorder != nullptr )
        {
            self->m_laatsteSchademelding = nu;
            std::string vermoedelijkeSpeler = "onbekend";
            if( self->m_spelersVoorIncident != nullptr )
            {
                std::vector<SpelerRecord> spelers = self->m_spelersVoorIncident->GeefSpelers();
                if( !spelers.empty() )
                {
                    // GeefSpelers() already sorts by distance, so the first is the
                    // nearest -- no guarantee this is the culprit, but the most likely
                    // candidate.
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

        // Debug log line the FIRST time this channel gives a value -- like
        // with scs_telemetry_init back then: first confirm it really arrives,
        // do not trust blindly.
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
                uit << "[Navigation] truck.navigation.distance channel received, first value: "
                    << afstandMeter << " meter\n";
            }
        }

        self->m_navigatieAfstandMeter = afstandMeter;
    }

    // --- New channel callbacks ----------------------------------------
    //
    // All the same pattern: take the value, nothing more. These run on the
    // game thread and are called often, so deliberately no computation,
    // no logging and no disk access here -- the conversion (m/s to km/h,
    // 0-1 to percent) is done in HuidigeVoertuigStatus(), which only runs
    // when the overlay is open.

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
        auto *self = static_cast<TruckTracking *>( context );
        const double vorige = self->m_kilometerstandKm;
        self->m_kilometerstandKm = value->value_float.value;
        // If the reading jumps more than 200 metres BACK, an autosave has been
        // reloaded. The new reading is exactly that of the loaded save: read
        // the save again so the counter jumps back with it, like the
        // dashboard does.
        if( vorige > 0.0 && self->m_kilometerstandKm < vorige - 0.2 && self->m_huidigVoertuig >= 0 )
        {
            Logboek::Schrijf( "event", "odometer jumped back: save reloaded? (waiting 2 s)" );
            self->m_rijstijlVenster.clear();
            self->m_rijstijlVensterSom = RijstijlTelling{};
            self->m_rijstijlPending = RijstijlMeting{};
            self->m_herlaadKmStand = self->m_kilometerstandKm;
            self->m_herlaadMoment = std::chrono::steady_clock::now();
        }
        // Only when the reading is REALLY different from the one at the truck
        // switch may the new truck be recognised. The channel arrives every
        // frame, also with an unchanged value, so "there is a value" says
        // nothing. MEASURED 03-09: on a switch the configuration came 0.3 s
        // BEFORE the new odometer, and recognition ran on the previous
        // truck's.
        if( !self->m_kmStandVersNaConfig
            && std::fabs( self->m_kilometerstandKm - self->m_kmStandBijConfig ) > 0.05 )
        {
            self->m_kmStandVersNaConfig = true;
        }
    }

    SCSAPI_VOID TruckTracking::SnelheidslimietCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                          scs_context_t context )
    {
        if( !value ) return;
        // The game sends 0 when no limit is known (e.g. off route, or if you
        // turned the Route Advisor limit off). That is different from "limit
        // is zero", so we set it back to -1 = unknown, otherwise the overlay
        // would show "0 km/h" and the speeding warning would fire
        // continuously.
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

        // Only the FLIP counts: throttle on or off. Small movements of your
        // foot should not keep emptying the window.
        const bool ingedrukt = ( self->m_gaspedaal > GAS_DREMPEL );
        if( ingedrukt != self->m_gasIngedrukt )
        {
            self->m_gasIngedrukt = ingedrukt;
            self->m_gasOmslag = true;  // handled in SnelheidCallback
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
            // No value = trailer detached. Back to "unknown", otherwise the last
            // damage value of an old trailer lingers while you drive without.
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
        // Raw channel is litres per km -> convert to the usual l/100km you
        // see on a real dashboard.
        s.verbruikLiterPer100Km = ( m_verbruikLiterPerKm >= 0.0 ) ? m_verbruikLiterPerKm * 100.0 : -1.0;

        // Own, more reliable calculation based on the fuel level (which IS
        // spot on, see handover) instead of the SCS channel above, which is a
        // trip computer figure of the game itself and does not reflect your
        // actual driving (see research 30-08).
        //
        // The computation itself happens in SnelheidCallback, where the
        // readings arrive; here we only read the result. That saves
        // recomputing the same window every frame.
        {
            // Below PER100_MIN_KMH we show l/h: only above it does l/100km mean
            // anything. "Stationair" only when you really stand still.
            // ABSOLUTE speed, because reversing gives a NEGATIVE speedometer.
            // Without fabs, -9.7 km/h read as "less than 0.1" and thus as
            // standstill: then "stationair" appeared below while you were
            // shunting. MEASURED 01-09-2026 20:09:29 to 20:09:35, speedometer
            // -9.7 / -10.9 / -7.2 while uncoupling.
            const double snelheidAbs = std::fabs( m_liveSnelheidKmh );
            s.echtStil = ( snelheidAbs < RIJDT_DREMPEL_KMH );
            s.staatStil = ( snelheidAbs < PER100_MIN_KMH );

            // --- Average: only what you consumed WHILE DRIVING ---
            // Idling deliberately does NOT count; otherwise this figure climbs
            // endlessly as soon as you stand still for a bit (measured: rose to
            // 178).
            // "gem" comes from the per-VEHICLE counter, no longer from the trip
            // counter. The dashboard counts since the last reset and then simply
            // keeps going; so does this counter. Only after five metres, otherwise
            // you divide by almost zero.
            if( m_huidigVoertuig >= 0 && m_huidigVoertuig < static_cast<int>( m_voertuigen.size() )
                && m_voertuigen[ m_huidigVoertuig ].km > 0.005 && m_voertuigen[ m_huidigVoertuig ].liters > 0.0 )
            {
                const VoertuigTeller &v = m_voertuigen[ m_huidigVoertuig ];
                s.verbruikGemiddeldLiterPer100Km = v.liters / v.km * 100.0;
                s.verbruikRitLiterPer100Km = ( m_rijdendKm > 0.005 && m_rijdendLiters > 0.0 )
                                               ? m_rijdendLiters / m_rijdendKm * 100.0 : -1.0;
            }
            else if( m_rijdendKm > 0.005 && m_rijdendLiters > 0.0 )
            {
                // Vehicle not recognised yet (no configuration received): then show
                // something anyway, and that is the trip counter.
                s.verbruikGemiddeldLiterPer100Km = m_rijdendLiters / m_rijdendKm * 100.0;
                s.verbruikRitLiterPer100Km = s.verbruikGemiddeldLiterPer100Km;
            }

            // Convert l/h for display with a FIXED divisor.
            //
            // MEASURED 01-09-2026 on two very different trucks:
            //   Scania V8      : glad_l_per_uur 6.0  -> dashboard 2.0  -> 3.00
            //   DAF MX-11 370  : glad_l_per_uur 4.0  -> dashboard 1.3  -> 3.08
            //   DAF, full throttle in neutral: ~25.3 -> dashboard 8.4  -> 3.01
            // Consumption from 4 to 25 litres per REAL hour, two brands, two
            // engine sizes -- always the same divisor. So this is a game constant
            // (a time conversion), not an engine property.
            //
            // Do NOT derive it from m_afstandsFactor / TijdSchaal(), as it used
            // to be here. For the same idling DAF, factor 14.76 displayed 1.63,
            // factor 15.60 displayed 1.50 and factor 22.03 displayed 1.09 --
            // while the engine burned 4.0 all along. Idling has no distance, so
            // the map scale does not belong here.
            if( m_gladLiterPerUur >= 0.0 )
            {
                s.verbruikLiterPerUur = m_gladLiterPerUur / LUUR_DELER;
            }

            s.verbruikNuLiterPer100Km = m_gladVerbruikNu;

            // Raw counters for the economy line: it compares this trip with your
            // average in THIS truck, without the trip itself in it.
            s.ritLiters = m_rijdendLiters;
            s.ritKm = m_rijdendKm;
            if( m_huidigVoertuig >= 0 && m_huidigVoertuig < static_cast<int>( m_voertuigen.size() ) )
            {
                s.voertuigLiters = m_voertuigen[ m_huidigVoertuig ].liters;
                s.voertuigKm = m_voertuigen[ m_huidigVoertuig ].km;
            }
            if( s.verbruikNuLiterPer100Km < 0.0 )
            {
                s.verbruikNuLiterPer100Km = s.verbruikGemiddeldLiterPer100Km;  // fallback
            }
            // No upper bound here any more: the card shows km/l and caps there at
            // 99.9 km/l. Capping in l/100km would break exactly the economical
            // side -- coasting should give a HIGH km/l, and that comes from a LOW
            // l/100km.
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
        // 3 km/h margin: driving exactly at the limit the speed always
        // wobbles a bit, and then the warning would flicker.
        return m_liveSnelheidKmh > limietKmh + 3.0;
    }

    SCSAPI_VOID TruckTracking::LocalScaleCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                     scs_context_t )
    {
        if( !value ) return;
        double schaal = value->value_float.value;

        // Log a line every ~10 seconds (not every frame, that is too much) so
        // we can see how this value varies between city and motorway, and
        // compare it with our own estimate versus the actual elapsed time.
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
                uit << "[LocalScale] current value: " << schaal << "\n";
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

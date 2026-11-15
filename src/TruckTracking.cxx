#include "TruckTracking.hxx"
#include "Spel.hxx"

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

        std::filesystem::path MetingPad()
        {
            std::filesystem::path pad;
            if( const char *appdata = std::getenv( "APPDATA" ) ) pad = appdata;
            else pad = std::filesystem::current_path();
            pad /= "CabNavi";
            std::error_code ec;
            std::filesystem::create_directories( pad, ec );
            return pad / ( SpelInfo::IsAts() ? "meting_ats.txt" : "meting.txt" );
        }

        std::filesystem::path VoertuigenPad()
        {
            return MetingPad().parent_path() / "voertuigen.txt";
        }
    }

    void TruckTracking::LaadMeting()
    {
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
        BoekBewaar( true );
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

        double rest = -1.0;
        double periode = 0.0;
        if( in >> rest >> periode )
        {
            if( rest >= 0.0 ) m_teHerstellenRest = rest;
            if( periode > 60.0 ) m_rustPeriodeMax = periode;
        }

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

    void TruckTracking::LaadVoertuigen()
    {
        m_voertuigenGeladen = true;
        std::ifstream in( VoertuigenPad() );
        if( !in ) return;
        std::string regel;
        while( std::getline( in, regel ) )
        {
            std::vector<std::string> delen;
            std::size_t begin = 0;
            for( ;; )
            {
                const std::size_t p = regel.find( '|', begin );
                delen.push_back( regel.substr( begin, p == std::string::npos ? std::string::npos : p - begin ) );
                if( p == std::string::npos ) break;
                begin = p + 1;
            }
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
            catch( ... ) { }
        }
        Logboek::Schrijf( "start", "voertuigen.txt: " + std::to_string( m_voertuigen.size() ) + " vehicle(s) loaded" );
    }

    void TruckTracking::BewaarVoertuigen( bool forceer )
    {
        if( !m_voertuigenGewijzigd ) return;
        const auto nu = std::chrono::steady_clock::now();
        if( !forceer && std::chrono::duration<double>( nu - m_voertuigenLaatstBewaard ).count() < 60.0 ) return;
        try
        {
            std::ofstream uit( VoertuigenPad(), std::ios::trunc );
            if( !uit ) return;
            uit << std::fixed << std::setprecision( 3 );
            auto telling = [ &uit ]( const RijstijlTelling &t )
            {
                uit << '|' << t.rijdendSec << '|' << t.uitrolSec << '|' << t.volgasSec << '|' << t.stationairSec
                    << '|' << t.totaalSec << '|' << t.km << '|' << t.geladenSec << '|' << t.remmingen;
            };
            for( const VoertuigTeller &v : m_voertuigen )
            {
                uit << v.merk << '|' << v.model << '|' << v.kmStand << '|' << v.liters << '|' << v.km;
                telling( v.leeg );
                telling( v.geladen );
                uit << '\n';
            }
            m_voertuigenLaatstBewaard = nu;
            m_voertuigenGewijzigd = false;
        }
        catch( ... ) { }
    }

    void TruckTracking::IdentificeerVoertuig()
    {
        if( m_configMerk.empty() || m_kilometerstandKm <= 0.0 ) return;
        if( m_gepauzeerd ) return;
        if( !m_kmStandVersNaConfig )
        {
            if( std::chrono::duration<double>( std::chrono::steady_clock::now() - m_configMoment ).count() < 3.0 )
                return;
        }
        if( !m_voertuigenGeladen ) LaadVoertuigen();

        int beste = -1;
        double kleinsteVerschil = 1e18;
        for( std::size_t i = 0; i < m_voertuigen.size(); ++i )
        {
            const VoertuigTeller &v = m_voertuigen[ i ];
            if( v.merk != m_configMerk || v.model != m_configModel ) continue;
            const double verschil = m_kilometerstandKm - v.kmStand;
            if( verschil < -1.0 ) continue;
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
        m_brandstof.ZetVoertuigSleutel( m_voertuigen[ beste ].merk + "|" + m_voertuigen[ beste ].model + "|" + std::to_string( beste ) );

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

    void TruckTracking::StartSaveLezen( const double kmStandBijLaden )
    {
        if( m_huidigVoertuig < 0 || m_huidigVoertuig >= static_cast<int>( m_voertuigen.size() ) ) return;
        if( m_saveBezig )
        {
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

        if( m_herlaadKmStand >= 0.0 && !m_saveBezig && m_huidigVoertuig >= 0
            && std::chrono::duration<double>( nu - m_herlaadMoment ).count() >= 2.0 )
        {
            const double stand = m_herlaadKmStand;
            m_herlaadKmStand = -1.0;
            m_saveHerkansingen = 0;
            StartSaveLezen( stand );
            return;
        }
        if( m_saveHerkansingen == 1 && !m_saveBezig && m_huidigVoertuig >= 0
            && std::chrono::duration<double>( nu - m_saveHerkansingMoment ).count() >= 10.0 )
        {
            m_saveHerkansingen = 2;
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
                m_saveHerkansingen = 1;
                m_saveHerkansingMoment = nu;
                Logboek::Schrijf( "event", "save: not read (" + fout + "), retrying in 10 s" );
                return;
            }
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
        if( dSec <= 0.0 || dSec > 5.0 ) return;

        const double snelheidAbs = std::fabs( snelheidKmh );
        const bool rijdend = snelheidAbs > 10.0;
        RijstijlMeting &p = m_rijstijlPending;
        p.dKm += dKm;
        p.dSec += dSec;
        if( rijdend ) p.rijdendSec += dSec;
        if( rijdend && gas < 0.10 ) p.uitrolSec += dSec;
        if( gas > 0.90 ) p.volgasSec += dSec;
        if( snelheidAbs < 1.0 && m_gladLiterPerUur > 0.3 ) p.stationairSec += dSec;
        if( m_heeftAanhanger ) p.geladenSec += dSec;
        if( p.dSec < 1.0 ) return;

        if( m_vorigeSnelheidVoorRem >= 0.0 && ( m_vorigeSnelheidVoorRem - snelheidAbs ) > 8.0 ) p.remming = 1;
        m_vorigeSnelheidVoorRem = snelheidAbs;

        m_rijstijlVenster.push_back( p );
        TelOp( m_rijstijlVensterSom, p, +1 );
        while( m_rijstijlVenster.size() > 1
               && ( m_rijstijlVensterSom.rijdendSec > RIJSTIJL_VENSTER_RIJDEND_SEC
                    || m_rijstijlVensterSom.totaalSec > RIJSTIJL_VENSTER_SEC ) )
        {
            TelOp( m_rijstijlVensterSom, m_rijstijlVenster.front(), -1 );
            m_rijstijlVenster.pop_front();
        }

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

        const RijstijlTelling &w = m_rijstijlVensterSom;
        if( w.rijdendSec < 30.0 ) return r;
        if( m_huidigVoertuig < 0 || m_huidigVoertuig >= static_cast<int>( m_voertuigen.size() ) ) return r;

        r.geladen = w.geladenSec > 0.5 * w.totaalSec;
        const VoertuigTeller &v = m_voertuigen[ m_huidigVoertuig ];
        RijstijlTelling ref = r.geladen ? v.geladen : v.leeg;
        for( const RijstijlMeting &m : m_rijstijlVenster )
        {
            const bool mGeladen = m.geladenSec > 0.5 * m.dSec;
            if( mGeladen == r.geladen ) TelOp( ref, m, -1 );
        }
        if( ref.rijdendSec < RIJSTIJL_REFERENTIE_SEC ) return r;

        const double uNu = w.uitrolSec / w.rijdendSec * 100.0;
        const double vNu = w.volgasSec / w.rijdendSec * 100.0;
        const double rNu = w.remmingen / w.rijdendSec * 600.0;
        const double uRef = ref.uitrolSec / ref.rijdendSec * 100.0;
        const double vRef = ref.volgasSec / ref.rijdendSec * 100.0;
        const double rRef = ref.remmingen / ref.rijdendSec * 600.0;

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
        params->register_for_channel(
            "game.time", SCS_U32_NIL, SCS_VALUE_TYPE_u32,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::GameTimeCallback, this );

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

        params->register_for_channel(
            "truck.navigation.time", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::NavigatieTijdCallback, this );

        m_rustKanaalType = 0;
        if( params->register_for_channel(
                "game.next.rest.stop", SCS_U32_NIL, SCS_VALUE_TYPE_s32,
                SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::RustStopCallback, this ) == SCS_RESULT_ok )
        {
            m_rustKanaalType = 1;
        }
        else if( params->register_for_channel(
                     "game.next.rest.stop", SCS_U32_NIL, SCS_VALUE_TYPE_u32,
                     SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::RustStopCallback, this ) == SCS_RESULT_ok )
        {
            m_rustKanaalType = 2;
        }
        else if( params->register_for_channel(
                     "game.next.rest.stop", SCS_U32_NIL, SCS_VALUE_TYPE_float,
                     SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::RustStopCallback, this ) == SCS_RESULT_ok )
        {
            m_rustKanaalType = 3;
        }

        {
            static const char *typeNaam[] = { "GEEN", "s32", "u32", "float" };
            Logboek::Schrijf( "start", std::string( "channel game.next.rest.stop -> " )
                                          + typeNaam[ m_rustKanaalType ] );
        }

        {
            const bool ok = params->register_for_channel(
                "bus_job.average_satisfaction", SCS_U32_NIL, SCS_VALUE_TYPE_float,
                SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::BusTevredenheidCallback, this ) == SCS_RESULT_ok;
            Logboek::Schrijf( "start", std::string( "channel bus_job.average_satisfaction -> " )
                                          + ( ok ? "float" : "GEEN" ) );
        }

        {
            const bool rust = params->register_for_channel(
                "rest.stop", SCS_U32_NIL, SCS_VALUE_TYPE_s32,
                SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::MeekijkRustCallback, this ) == SCS_RESULT_ok;
            const bool pauze = params->register_for_channel(
                "mandatory.break", SCS_U32_NIL, SCS_VALUE_TYPE_s32,
                SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::MeekijkPauzeCallback, this ) == SCS_RESULT_ok;
            const bool verschil = params->register_for_channel(
                "multiplayer.time.offset", SCS_U32_NIL, SCS_VALUE_TYPE_s32,
                SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::MeekijkTijdVerschilCallback, this ) == SCS_RESULT_ok;
            Logboek::Schrijf( "start", std::string( "listen-only channels: rest.stop -> " ) + ( rust ? "s32" : "GEEN" )
                                          + ", mandatory.break -> " + ( pauze ? "s32" : "GEEN" )
                                          + ", multiplayer.time.offset -> " + ( verschil ? "s32" : "GEEN" ) );
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

        params->register_for_channel(
            "truck.navigation.distance", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::NavigatieAfstandCallback, this );

        params->register_for_channel(
            "local_scale", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::LocalScaleCallback, this );

        params->register_for_channel(
            "truck.fuel.range", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::BereikCallback, this );

        params->register_for_channel(
            "truck.fuel.consumption.average", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::VerbruikCallback, this );

        params->register_for_channel(
            "truck.odometer", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::KilometerstandCallback, this );

        params->register_for_channel(
            "truck.navigation.speed.limit", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::SnelheidslimietCallback, this );

        params->register_for_channel(
            "truck.cruise_control", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::CruiseControlCallback, this );

        params->register_for_channel(
            "truck.effective.throttle", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::GaspedaalCallback, this );

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

        params->register_for_channel(
            "trailer.0.wear.chassis", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::AanhangerSchadeCallback, this );
        params->register_for_channel(
            "trailer.0.cargo.damage", SCS_U32_NIL, SCS_VALUE_TYPE_float,
            SCS_TELEMETRY_CHANNEL_FLAG_none, &TruckTracking::LadingSchadeCallback, this );

        params->register_for_event( SCS_TELEMETRY_EVENT_configuration, &TruckTracking::ConfigCallback, this );

        params->register_for_event( SCS_TELEMETRY_EVENT_gameplay, &TruckTracking::GameplayEventCallback, this );

        params->register_for_event( SCS_TELEMETRY_EVENT_paused, &TruckTracking::GepauzeerdCallback, this );
        params->register_for_event( SCS_TELEMETRY_EVENT_started, &TruckTracking::HervatCallback, this );
    }

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
            if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "fuel.capacity" ) )
            {
                if( attr->value.type == SCS_VALUE_TYPE_float )
                {
                    m_tankInhoudLiters = attr->value.value_float.value;
                }
            }

            std::string merk, model;
            if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "brand" ) )
                if( attr->value.type == SCS_VALUE_TYPE_string && attr->value.value_string.value )
                    merk = attr->value.value_string.value;
            if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "name" ) )
                if( attr->value.type == SCS_VALUE_TYPE_string && attr->value.value_string.value )
                    model = attr->value.value_string.value;
            if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "license.plate" ) )
                if( attr->value.type == SCS_VALUE_TYPE_string && attr->value.value_string.value )
                    m_kenteken = attr->value.value_string.value;
            if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "license.plate.country.id" ) )
                if( attr->value.type == SCS_VALUE_TYPE_string && attr->value.value_string.value )
                    m_kentekenLand = attr->value.value_string.value;
            if( !merk.empty() && ( merk != m_configMerk || model != m_configModel || m_huidigVoertuig < 0 ) )
            {
                BewaarVoertuigen( true );
                m_configMerk = merk;
                m_configModel = model;
                m_huidigVoertuig = -1;
                m_herlaadKmStand = -1.0;
                m_brandstof.VoertuigGewisseld();
                m_rijstijlVenster.clear();
                m_rijstijlVensterSom = RijstijlTelling{};
                m_rijstijlPending = RijstijlMeting{};
                m_vorigeSnelheidVoorRem = -1.0;
                m_kmStandVersNaConfig = false;
                m_kmStandBijConfig = m_kilometerstandKm;
                m_configMoment = std::chrono::steady_clock::now();
            }
            if( !merk.empty() ) { m_huidigeRit.voertuigMerk = merk; m_huidigeRit.voertuigModel = model; }
            if( !merk.empty() && m_busTracking != nullptr ) m_busTracking->ZetVoertuig( merk, model );
            return;
        }

        if( configId == "bus_job" )
        {
            if( m_busTracking != nullptr ) m_busTracking->OpScsBusConfig( cfg );
            return;
        }

        if( configId != "job" )
        {
            return;
        }

        const scs_named_value_t *cargoAttr = ZoekAttribuut( cfg->attributes, "cargo.id" );
        const bool heeftLading = cargoAttr != nullptr && cargoAttr->value.type == SCS_VALUE_TYPE_string
                                  && cargoAttr->value.value_string.value != nullptr
                                  && cargoAttr->value.value_string.value[ 0 ] != '\0';

        if( !heeftLading )
        {
            return;
        }

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
        nieuw.lading = cargoAttr->value.value_string.value;
        if( const scs_named_value_t *naamAttr = ZoekAttribuut( cfg->attributes, "cargo" ) )
        {
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
        if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "cargo.mass" ) )
        {
            if( attr->value.type == SCS_VALUE_TYPE_float )
            {
                nieuw.ladingGewichtKg = static_cast<double>( attr->value.value_float.value );
                m_ladingGewichtKg = nieuw.ladingGewichtKg;
            }
        }

        m_ritUitersteTijd = 0;
        if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "job.market" ) )
        {
            if( attr->value.type == SCS_VALUE_TYPE_string && attr->value.value_string.value )
                nieuw.markt = attr->value.value_string.value;
        }
        if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "is.special.job" ) )
        {
            if( attr->value.type == SCS_VALUE_TYPE_bool )
                nieuw.speciaal = attr->value.value_bool.value != 0;
        }
        if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "delivery.time" ) )
        {
            if( attr->value.type == SCS_VALUE_TYPE_u32 )
                m_ritUitersteTijd = attr->value.value_u32.value;
        }

        std::string boekSleutel = cargoAttr->value.value_string.value;
        for( const char *naam : { "source.company.id", "destination.company.id", "source.city.id", "destination.city.id" } )
        {
            boekSleutel += "|";
            if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, naam ) )
                if( attr->value.type == SCS_VALUE_TYPE_string && attr->value.value_string.value )
                    boekSleutel += attr->value.value_string.value;
        }
        boekSleutel += "|";
        if( const scs_named_value_t *attr = ZoekAttribuut( cfg->attributes, "income" ) )
            if( attr->value.type == SCS_VALUE_TYPE_u64 )
                boekSleutel += std::to_string( attr->value.value_u64.value );
        boekSleutel += "|" + std::to_string( m_ritUitersteTijd )
                       + "|" + std::to_string( static_cast<long long>( nieuw.geplandeAfstandKm ) );
        if( m_ritUitersteTijd == 0 ) boekSleutel += "|geen-" + nieuw.id;

        m_huidigeRit = nieuw;
        m_huidigeRit.voertuigMerk = m_configMerk;
        m_huidigeRit.voertuigModel = m_configModel;
        m_gladdeSchattingMin = -1.0;
        m_huidigeLadingId = cargoAttr->value.value_string.value;
        m_actief = true;
        m_ritStartMoment = std::chrono::steady_clock::now();
        m_laatsteSnelheidMeting = m_ritStartMoment;
        m_gepauzeerd = false;
        m_totaalGepauzeerdSeconden = 0.0;
        m_kmVenster.clear();

        m_rijdendLiters = 0.0;
        m_rijdendKm = 0.0;
        m_meetKmTotaal = 0.0;
        m_brandstof.StartNieuweRit();

        m_ritSleutel = boekSleutel;
        m_hervatWacht = false;
        m_hervatWachtSec = 0.0;
        m_boekMomenten.clear();
        m_boekMomentVoertuig = -1;
        m_boekLaatsteTik = std::chrono::steady_clock::now();
        m_boekLaatstBewaard = m_boekLaatsteTik;
        {
            LopendeRit oud;
            if( RitBoek::Lees( RitBoek::Soort::Vracht, oud ) && oud.sleutel == m_ritSleutel )
            {
                m_hervatKandidaat = oud;
                m_hervatWacht = true;
                Logboek::Schrijf( "rit", "job found in the book from " + oud.bewaardIso
                                            + ", waiting for the odometer before picking it up" );
            }
            else
            {
                BoekBewaar( true );
            }
        }
    }

    void TruckTracking::ZetBusTracking( BusTracking *bus )
    {
        m_busTracking = bus;
        if( bus == nullptr ) return;
        bus->KoppelBrandstof( &m_brandstof );
        if( !m_configMerk.empty() ) bus->ZetVoertuig( m_configMerk, m_configModel );
    }

    void TruckTracking::BoekBewaar( const bool forceer )
    {
        if( !m_actief || m_ritSleutel.empty() ) return;
        if( m_hervatWacht ) return;
        const auto nu = std::chrono::steady_clock::now();
        if( !forceer && std::chrono::duration<double>( nu - m_boekLaatstBewaard ).count() < 60.0 ) return;
        m_boekLaatstBewaard = nu;

        LopendeRit r;
        r.sleutel = m_ritSleutel;
        r.id = m_huidigeRit.id;
        r.startTijdIso = m_huidigeRit.startTijdIso;
        r.economyStartTijd = m_huidigeRit.economyStartTijd;
        r.voertuigMerk = m_configMerk;
        r.voertuigModel = m_configModel;
        const RitMoment nu_ = BoekStandNu();
        r.kmStand = nu_.kmStand;
        r.liters = nu_.liters;
        r.kosten = nu_.kosten;
        r.momenten = m_boekMomenten;
        r.leegVoor = m_brandstof.LeegVoorDezeRit();
        r.tankbeurten = m_brandstof.TankbeurtenDezeRit();
        r.boetes = m_huidigeRit.boetes;
        r.doorgangen = m_huidigeRit.doorgangen;
        r.boeteKosten = m_huidigeRit.boeteKosten;
        r.tolKosten = m_huidigeRit.tolKosten;
        r.veerbootKosten = m_huidigeRit.veerbootKosten;
        r.treinKosten = m_huidigeRit.treinKosten;
        r.bewaardIso = NuAlsIso();
        RitBoek::Bewaar( RitBoek::Soort::Vracht, r );
    }

    void TruckTracking::BoekHervatBeslis( const bool nuBeslissen )
    {
        if( !m_hervatWacht ) return;
        const bool standVers = m_huidigVoertuig >= 0 && m_kilometerstandKm > 0.0;
        if( !standVers && !nuBeslissen ) return;
        m_hervatWacht = false;

        if( m_kilometerstandKm <= 0.0 )
        {
            Logboek::Schrijf( "rit", "job NOT picked up: no odometer reading to check it against" );
            return;
        }
        const bool zelfdeTruck = !m_configMerk.empty() && m_hervatKandidaat.voertuigMerk == m_configMerk
                                 && m_hervatKandidaat.voertuigModel == m_configModel;
        if( !zelfdeTruck )
        {
            Logboek::Schrijf( "rit", "job NOT picked up: the book was written with '" + m_hervatKandidaat.voertuigMerk + " "
                                        + m_hervatKandidaat.voertuigModel + "', this is '" + m_configMerk + " " + m_configModel + "'" );
            return;
        }
        LopendeRit o;
        if( !RitBoek::KnipTerug( m_hervatKandidaat, m_kilometerstandKm, o ) )
        {
            Logboek::Schrijf( "rit", "job NOT picked up: odometer " + std::to_string( m_kilometerstandKm )
                                        + " does not fit the book (it covers "
                                        + std::to_string( m_hervatKandidaat.momenten.empty() ? m_hervatKandidaat.kmStand
                                                                                             : m_hervatKandidaat.momenten.front().kmStand )
                                        + " to " + std::to_string( m_hervatKandidaat.kmStand ) + ")" );
            return;
        }
        if( o.kmStand < m_hervatKandidaat.kmStand )
            Logboek::Schrijf( "rit", "the game came back at an older save: odometer " + std::to_string( m_kilometerstandKm )
                                        + ", the book went to " + std::to_string( m_hervatKandidaat.kmStand )
                                        + " -- continuing from the moment at " + std::to_string( o.kmStand ) );

        if( !o.id.empty() ) m_huidigeRit.id = o.id;
        if( !o.startTijdIso.empty() ) m_huidigeRit.startTijdIso = o.startTijdIso;
        if( o.economyStartTijd != 0 ) m_huidigeRit.economyStartTijd = o.economyStartTijd;
        m_huidigeRit.boetes.insert( m_huidigeRit.boetes.begin(), o.boetes.begin(), o.boetes.end() );
        m_huidigeRit.doorgangen.insert( m_huidigeRit.doorgangen.begin(), o.doorgangen.begin(), o.doorgangen.end() );
        m_huidigeRit.boeteKosten += o.boeteKosten;
        m_huidigeRit.tolKosten += o.tolKosten;
        m_huidigeRit.veerbootKosten += o.veerbootKosten;
        m_huidigeRit.treinKosten += o.treinKosten;
        m_brandstof.HervatRit( o.liters, o.kosten, o.leegVoor, o.tankbeurten );
        m_boekMomenten = o.momenten;
        m_boekMomentVoertuig = m_huidigVoertuig;

        Logboek::Schrijf( "rit", "job PICKED UP after a restart -- started=" + m_huidigeRit.startTijdIso
                                    + " carried " + std::to_string( o.liters ) + " L, "
                                    + std::to_string( o.kosten ) + " cost, fines=" + std::to_string( o.boeteKosten )
                                    + " toll=" + std::to_string( o.tolKosten ) );
    }

    void TruckTracking::BoekTik()
    {
        if( !m_actief || m_ritSleutel.empty() ) return;
        const auto nu = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>( nu - m_boekLaatsteTik ).count();
        m_boekLaatsteTik = nu;

        if( m_hervatWacht )
        {
            if( !m_gepauzeerd && dt > 0.0 ) m_hervatWachtSec += std::min( dt, 1.0 );
            BoekHervatBeslis( m_hervatWachtSec >= HERVAT_WACHT_SECONDEN );
            if( !m_hervatWacht ) BoekBewaar( true );
            return;
        }
        BoekMoment( false );
        BoekBewaar( false );
    }

    RitMoment TruckTracking::BoekStandNu() const
    {
        RitMoment m;
        m.kmStand = ( m_huidigVoertuig >= 0 && m_kilometerstandKm > 0.0 ) ? m_kilometerstandKm : 0.0;
        const BrandstofState bs = m_brandstof.HuidigeState();
        m.liters = bs.verbruikSindsRitStartLiters;
        m.kosten = bs.kostenDezeRitEuro;
        m.boetes = static_cast<std::uint32_t>( m_huidigeRit.boetes.size() );
        m.doorgangen = static_cast<std::uint32_t>( m_huidigeRit.doorgangen.size() );
        m.tankbeurten = static_cast<std::uint32_t>( m_brandstof.TankbeurtenDezeRit().size() );
        return m;
    }

    void TruckTracking::BoekMoment( const bool nu )
    {
        if( m_huidigVoertuig < 0 || m_kilometerstandKm <= 0.0 ) return;
        if( m_boekMomentVoertuig != m_huidigVoertuig )
        {
            if( m_boekMomentVoertuig >= 0 ) m_boekMomenten.clear();
            m_boekMomentVoertuig = m_huidigVoertuig;
        }
        while( !m_boekMomenten.empty() && m_boekMomenten.back().kmStand > m_kilometerstandKm + 0.5 ) m_boekMomenten.pop_back();
        if( !nu && !m_boekMomenten.empty() && m_kilometerstandKm < m_boekMomenten.back().kmStand + RitBoek::MOMENT_KM ) return;
        RitBoek::VoegMomentToe( m_boekMomenten, BoekStandNu() );
    }

    void TruckTracking::OpGameplayEvent( const scs_telemetry_gameplay_event_t *info )
    {
        SchrijfDebugGameplayEvent( info );

        const std::string eventId = info->id;

        if( eventId == "bus_job.completed" || eventId == "bus_job.cancelled" )
        {
            if( m_busTracking != nullptr ) m_busTracking->OpScsBusEvent( info );
            return;
        }

        if( eventId == "player.fined" )
        {
            const bool busRit = !m_actief && m_busTracking != nullptr && m_busTracking->HeeftActieveRit();
            if( !m_actief && !busRit ) return;
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
            if( busRit ) { m_busTracking->BoekBoete( b ); return; }
            m_huidigeRit.boetes.push_back( b );
            m_huidigeRit.boeteKosten += b.bedrag;
            BoekMoment( true );
            BoekBewaar( true );
            return;
        }

        if( eventId == "player.tollgate.paid" || eventId == "player.use.ferry"
            || eventId == "player.use.train" )
        {
            const bool busRit = !m_actief && m_busTracking != nullptr && m_busTracking->HeeftActieveRit();
            if( !m_actief && !busRit ) return;
            Doorgang d;
            d.type = ( eventId == "player.tollgate.paid" )  ? DoorgangType::Tol
                     : ( eventId == "player.use.ferry" )     ? DoorgangType::Veerboot
                                                              : DoorgangType::Trein;

            if( const scs_named_value_t *attr = ZoekAttribuut( info->attributes, "pay.amount" ) )
            {
                if( attr->value.type == SCS_VALUE_TYPE_s64 )
                    d.bedrag = attr->value.value_s64.value;
            }
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

            if( busRit ) { m_busTracking->BoekDoorgang( d ); return; }

            if( d.type == DoorgangType::Veerboot || d.type == DoorgangType::Trein )
            {
                m_negeerVolgendeSprong = true;
            }

            switch( d.type )
            {
                case DoorgangType::Tol:      m_huidigeRit.tolKosten += d.bedrag; break;
                case DoorgangType::Veerboot: m_huidigeRit.veerbootKosten += d.bedrag; break;
                case DoorgangType::Trein:    m_huidigeRit.treinKosten += d.bedrag; break;
            }
            m_huidigeRit.doorgangen.push_back( std::move( d ) );
            BoekMoment( true );
            BoekBewaar( true );
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

        BoekHervatBeslis( true );

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

        if( isAfgeleverd )
        {
            if( const scs_named_value_t *attr = ZoekAttribuut( info->attributes, "earned.xp" ) )
            {
                if( attr->value.type == SCS_VALUE_TYPE_s32 )
                    m_huidigeRit.verdiendeXp = attr->value.value_s32.value;
            }
            const bool extern_ = m_huidigeRit.markt == "external_contracts" || m_huidigeRit.markt == "external_market";
            if( m_ritUitersteTijd > 0 && m_economyTijd > 0 && !extern_ )
            {
                const long long over = static_cast<long long>( m_ritUitersteTijd ) - static_cast<long long>( m_economyTijd );
                if( over > -100000 && over < 100000 )
                {
                    m_huidigeRit.minutenOver = static_cast<std::int32_t>( over );
                    m_huidigeRit.minutenOverBekend = true;
                    m_huidigeRit.opTijd = over >= 0;
                }
            }
        }
        else
        {
            if( const scs_named_value_t *attr = ZoekAttribuut( info->attributes, "cancel.penalty" ) )
            {
                if( attr->value.type == SCS_VALUE_TYPE_s64 )
                    m_huidigeRit.annuleerBoete = attr->value.value_s64.value;
            }
        }

        BrandstofState bs = m_brandstof.HuidigeState();
        m_huidigeRit.brandstofVerbruikLiters = bs.verbruikSindsRitStartLiters;
        m_huidigeRit.brandstofKostenEuro = m_brandstof.SluitRitAf();

        Logboek::Schrijf( "rit", std::string( isGeannuleerd ? "job CANCELLED" : "job DELIVERED" )
            + " -- revenue=" + std::to_string( m_huidigeRit.inkomen )
            + " xp=" + std::to_string( m_huidigeRit.verdiendeXp )
            + " market=" + ( m_huidigeRit.markt.empty() ? "?" : m_huidigeRit.markt )
            + " special=" + ( m_huidigeRit.speciaal ? "yes" : "no" )
            + " window=" + std::to_string( m_ritUitersteTijd )
            + " gameTime=" + std::to_string( m_economyTijd )
            + " minutesLeft=" + ( m_huidigeRit.minutenOverBekend ? std::to_string( m_huidigeRit.minutenOver ) : std::string( "?" ) )
            + " penalty=" + std::to_string( m_huidigeRit.annuleerBoete )
            + " fines=" + std::to_string( m_huidigeRit.boeteKosten )
            + " toll=" + std::to_string( m_huidigeRit.tolKosten )
            + " litres=" + std::to_string( m_huidigeRit.brandstofVerbruikLiters )
            + " fuelCost=" + std::to_string( m_huidigeRit.brandstofKostenEuro ) );

        RitBoek::Wis( RitBoek::Soort::Vracht );
        m_ritSleutel.clear();
        m_hervatWacht = false;
        m_ritUitersteTijd = 0;

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

        double resterendeKm;
        if( m_navigatieAfstandMeter >= 0.0 )
        {
            resterendeKm = m_navigatieAfstandMeter / 1000.0;
        }
        else
        {
            resterendeKm = m_huidigeRit.geplandeAfstandKm - m_huidigeRit.afgelegdeAfstandKm;
        }

        if( resterendeKm <= 0.0 )
        {
            return 0.0;
        }

        if( m_navigatieTijd > 0.0 )
        {
            const double schaal = TijdSchaal();
            double spelUren = m_navigatieTijd / 3600.0;

            const double impliciet = spelUren > 0.0 ? resterendeKm / spelUren : 0.0;
            if( impliciet < 3.0 || impliciet > 200.0 )
            {
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

        if( m_kmVenster.size() >= 2 )
        {
            const auto &eerste = m_kmVenster.front();
            const auto &laatste = m_kmVenster.back();
            double tijdspanneUur = std::chrono::duration<double>( laatste.first - eerste.first ).count() / 3600.0;
            double kmVerschil = laatste.second - eerste.second;
            if( tijdspanneUur > ( 30.0 / 3600.0 ) )
            {
                gemiddeldeSnelheid = kmVerschil / tijdspanneUur;
            }
        }

        if( gemiddeldeSnelheid < 1.0 )
        {
            double verstrekenMin = VerstrekenMinutenEcht();
            if( verstrekenMin > 0.5 && m_huidigeRit.afgelegdeAfstandKm > 0.5 )
            {
                gemiddeldeSnelheid = m_huidigeRit.afgelegdeAfstandKm / ( verstrekenMin / 60.0 );
            }
        }

        if( gemiddeldeSnelheid < 1.0 )
        {
            gemiddeldeSnelheid = m_huidigeRit.huidigeSnelheidKmh * TijdSchaal();
        }

        const double BODEM_SNELHEID_ECHT = 40.0 * TijdSchaal();
        if( gemiddeldeSnelheid < BODEM_SNELHEID_ECHT )
        {
            if( m_gladdeSchattingMin > 0.0 )
            {
                return m_gladdeSchattingMin;
            }
            gemiddeldeSnelheid = BODEM_SNELHEID_ECHT;
        }

        const double schatting = Gladstrijken( ( resterendeKm / gemiddeldeSnelheid ) * 60.0 );

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
        if( nieuw < 0.0 ) return huidig;
        if( huidig < 0.0 ) return nieuw;

        const double verhouding = nieuw / std::max( 0.001, huidig );
        if( verhouding > 4.0 || verhouding < 0.25 ) return nieuw;

        if( dtSec <= 0.0 || tauSec <= 0.0 ) return nieuw;
        const double alpha = dtSec / ( dtSec + tauSec );
        return huidig + alpha * ( nieuw - huidig );
    }

    double TruckTracking::Gladstrijken( double ruweMinuten ) const
    {
        if( m_gladdeSchattingMin < 0.0 )
        {
            m_gladdeSchattingMin = ruweMinuten;
        }
        else
        {
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

    SCSAPI_VOID TruckTracking::GepauzeerdCallback( const scs_event_t, const void *, scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( !self->m_gepauzeerd )
        {
            self->m_gepauzeerd = true;
            self->m_pauzeStartMoment = std::chrono::steady_clock::now();
            Logboek::Schrijf( "flags", "SCS reports: paused" );
            self->m_brandstof.SpelGepauzeerd();
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
            if( self->m_huidigVoertuig < 0 ) self->m_configMoment = std::chrono::steady_clock::now();
            self->m_laatsteSnelheidMeting = nu;
        }
    }

    SCSAPI_VOID TruckTracking::LandCallback( const scs_string_t naam, scs_u32_t,
                                              const scs_value_t *value, scs_context_t context )
    {
        if( !value || value->type != SCS_VALUE_TYPE_string || !value->value_string.value ) return;
        auto *self = static_cast<TruckTracking *>( context );

        const std::string land = value->value_string.value;
        if( land.empty() ) return;

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

        double minuten;
        switch( value->type )
        {
            case SCS_VALUE_TYPE_s32:   minuten = static_cast<double>( value->value_s32.value ); break;
            case SCS_VALUE_TYPE_u32:   minuten = static_cast<double>( value->value_u32.value ); break;
            case SCS_VALUE_TYPE_float: minuten = static_cast<double>( value->value_float.value ); break;
            default: return;
        }
        if( minuten < 0.0 ) return;

        if( self->m_minutenTotRust >= 0.0 && minuten > self->m_minutenTotRust + 30.0 )
        {
            ++self->m_rustResets;
            self->m_pauzeRijSpelMin = 0.0;
        }
        if( self->m_rustLaagst < 0.0 || minuten < self->m_rustLaagst )
        {
            self->m_rustLaagst = minuten;
        }

        self->m_minutenTotRust = minuten;
        if( minuten > self->m_rustPeriodeMax ) self->m_rustPeriodeMax = minuten;
    }

    SCSAPI_VOID TruckTracking::GameTimeCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                  scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( !value ) return;

        static bool telemetrieThreadGelogd = false;
        if( !telemetrieThreadGelogd )
        {
            telemetrieThreadGelogd = true;
            Logboek::Schrijf( "flags", "thread id SCS telemetry callback: "
                                            + std::to_string( Logboek::HuidigeThreadId() ) );
        }

        const std::uint32_t minuten = value->value_u32.value;
        if( minuten == self->m_economyTijd ) return;

        if( self->m_actief && self->m_huidigeRit.economyStartTijd == 0 && minuten > 0 )
            self->m_huidigeRit.economyStartTijd = minuten;

        {
            const long long klokStap = static_cast<long long>( minuten ) - static_cast<long long>( self->m_economyTijd );
            self->m_volgRust.KlokStap( klokStap );
            self->m_volgPauze.KlokStap( klokStap );
            self->SpelTellerMeld();
        }

        const auto nu = std::chrono::steady_clock::now();

        if( self->m_schaalGestart )
        {
            const double stapSpel = static_cast<double>( minuten ) - static_cast<double>( self->m_economyTijd );

            if( stapSpel >= 45.0 && self->m_negeerVolgendeSprong )
            {
                self->m_negeerVolgendeSprong = false;
            }
            else if( stapSpel >= 45.0 && self->m_liveSnelheidKmh < 2.0 )
            {
                self->m_pauzeRijSpelMin = 0.0;
                self->m_pauzeStilstandSpelMin = stapSpel;
                self->m_laatsteRustSpelMinuten = stapSpel;
                self->m_tachoRijSecondenSindsRust = 0.0;
                self->m_tachoInRust = true;
                self->m_economyTijdLaatsteRust = minuten;
                self->m_rustMomentBekend = true;
                self->m_getoondeRestMin = -1.0;

                if( stapSpel >= self->m_tacho.dagRust * 0.8 )
                {
                    self->m_eigenLaatsteDagrust = minuten;
                    self->m_eigenLaatstePauze = minuten;
                }
                else if( stapSpel >= self->m_tacho.pauzeDuur * 0.9 )
                {
                    self->m_eigenLaatstePauze = minuten;
                }
                self->m_getoondeRijtijdMin = -1.0;
                self->BewaarTachoStand();
            }
            const double stapEcht =
                std::chrono::duration<double>( nu - self->m_schaalLaatsteEcht ).count() / 60.0;

            const bool rareStap = stapSpel < 0.0 ||
                                   stapSpel > 5.0 ||
                                   ( stapEcht > 0.0 && ( stapSpel / stapEcht ) > 30.0 );
            if( rareStap )
            {
                self->m_schaalEersteEconomy = minuten;
                self->m_schaalEersteEcht = nu;
            }
            else
            {
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
                    self->m_economyTijdLaatsteRust = static_cast<std::uint32_t>(
                        static_cast<double>( minuten ) - ( periode - self->m_teHerstellenRest ) );
                    self->m_teHerstellenRest = -1.0;
                }
                else
                {
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
        }

        self->m_schaalLaatsteEcht = nu;
        self->m_economyTijd = minuten;

        if( self->m_busTracking != nullptr ) self->m_busTracking->ZetScsSpelTijd( minuten );
    }

    double TruckTracking::TijdSchaal() const
    {
        constexpr double STANDAARD_SCHAAL = 6.0;
        constexpr double ONDERGRENS = 3.0;
        constexpr double BOVENGRENS = 25.0;

        if( m_handmatigeSchaal > 0.0 ) return m_handmatigeSchaal;

        if( m_vastgezetteSchaal > 0.0 ) return m_vastgezetteSchaal;

        if( !m_schaalGestart ) return STANDAARD_SCHAAL;

        const double echteMinuten =
            std::chrono::duration<double>( std::chrono::steady_clock::now() - m_schaalEersteEcht ).count() / 60.0;

        if( echteMinuten < 2.0 ) return STANDAARD_SCHAAL;

        const double spelMinuten =
            static_cast<double>( m_economyTijd ) - static_cast<double>( m_schaalEersteEconomy );
        if( spelMinuten <= 0.0 ) return STANDAARD_SCHAAL;

        const double gemeten = spelMinuten / echteMinuten;
        if( gemeten < ONDERGRENS || gemeten > BOVENGRENS )
        {
            return STANDAARD_SCHAAL;
        }

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
        if( m_rustMomentBekend )
        {
            const double periodeNu = m_rustPeriodeMax > 60.0 ? m_rustPeriodeMax : MAX_RIJ_SPELMINUTEN;
            const double verstreken =
                static_cast<double>( m_economyTijd ) - static_cast<double>( m_economyTijdLaatsteRust );
            if( verstreken >= 0.0 )
            {
                const double rest = std::min( periodeNu, periodeNu - verstreken );

                if( m_getoondeRestMin < 0.0 )
                {
                    m_getoondeRestMin = rest;
                    BewaarTachoStand();
                    return m_getoondeRestMin;
                }

                if( rest <= m_getoondeRestMin - 1.0 )
                {
                    m_getoondeRestMin = rest;

                    BewaarTachoStand();
                }
                return m_getoondeRestMin;
            }
        }

        const double periode = m_rustPeriodeMax > 60.0 ? m_rustPeriodeMax : MAX_RIJ_SPELMINUTEN;
        return periode - m_pauzeRijSpelMin;
    }

    TruckTracking::SpelTeller TruckTracking::SpelTellerPauze() const
    {
        SpelTeller t;
        if( m_tacho.stand != TachoStand::SpelVolgen ) return t;
        t.loopt = m_volgPauze.Loopt();
        t.minuten = m_volgPauze.Minuten();
        return t;
    }

    TruckTracking::SpelTeller TruckTracking::SpelTellerRust() const
    {
        SpelTeller t;
        if( m_tacho.stand != TachoStand::SpelVolgen ) return t;
        t.loopt = m_volgRust.Loopt();
        t.minuten = m_volgRust.Minuten();
        return t;
    }

    void TruckTracking::SpelTellerMeld()
    {
        if( m_volgPauze.Loopt() != m_volgPauzeGemeld )
        {
            m_volgPauzeGemeld = m_volgPauze.Loopt();
            Logboek::Schrijf( "tacho", std::string( "game counter mandatory.break " )
                                          + ( m_volgPauzeGemeld ? "running" : "stopped" )
                                          + " at " + std::to_string( m_volgPauze.Minuten() ) );
        }
        if( m_volgRust.Loopt() != m_volgRustGemeld )
        {
            m_volgRustGemeld = m_volgRust.Loopt();
            Logboek::Schrijf( "tacho", std::string( "game counter rest.stop " )
                                          + ( m_volgRustGemeld ? "running" : "stopped" )
                                          + " at " + std::to_string( m_volgRust.Minuten() ) );
        }
    }

    double TruckTracking::RijPeriodeSpelMinuten() const
    {
        return m_rustPeriodeMax > 60.0 ? m_rustPeriodeMax : MAX_RIJ_SPELMINUTEN;
    }

    double TruckTracking::TachograafRijtijdMinuten() const
    {
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
                else if( verstreken >= m_getoondeRijtijdMin + 1.0 )
                {
                    m_getoondeRijtijdMin = verstreken;
                }
                return m_getoondeRijtijdMin;
            }
        }
        return m_tachoRijSecondenSindsRust / 60.0;
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
        if( verstrekenSeconden <= 0.0 || verstrekenSeconden > 10.0 ) return;

        const double STILSTAND_DREMPEL_KMH = 2.0;

        if( snelheidKmh < STILSTAND_DREMPEL_KMH )
        {
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

        double snelheidKmh = value->value_float.value * 3.6;
        self->m_liveSnelheidKmh = snelheidKmh;

        if( self->m_busTracking != nullptr )
        {
            self->m_busTracking->OpLiveSnelheid( snelheidKmh, self->m_gepauzeerd );
            self->m_busTracking->ZetNavigatie( self->m_navigatieTijd,
                                                self->m_navigatieAfstandMeter >= 0.0
                                                    ? self->m_navigatieAfstandMeter / 1000.0
                                                    : -1.0 );
            self->m_busTracking->ZetKilometerstand( self->m_kilometerstandKm, self->m_huidigVoertuig >= 0 );
        }

        self->BoekTik();

        if( !self->m_gepauzeerd )
        {
            self->TachograafUpdate( snelheidKmh );
        }

        if( !self->m_gepauzeerd )
        {
            const auto nuMeting = std::chrono::steady_clock::now();
            const double verbruiktNu = self->m_brandstof.HuidigeState().verbruiktSessieLiters;

            const auto vorigeMeetMoment = self->m_vorigMeetMoment;
            const bool hadVorigeMeting = self->m_meetGestart;

            if( self->m_meetGestart && self->m_vorigMeetLiters >= 0.0 )
            {
                const double dtUur =
                    std::chrono::duration<double>( nuMeting - self->m_vorigMeetMoment ).count() / 3600.0;
                const double dLiters = verbruiktNu - self->m_vorigMeetLiters;

                if( dtUur > 0.0 && dtUur < 0.1 )
                {
                    double dKm = 0.0;
                    if( self->m_vorigMeetOdometerKm >= 0.0 )
                    {
                        const double stap = self->m_kilometerstandKm - self->m_vorigMeetOdometerKm;
                        if( stap > 0.0 && stap < 5.0 ) dKm = stap;
                    }
                    self->m_meetKmTotaal += dKm;

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

                                if( ( self->m_afstandsFactor - self->m_bewaardeFactor ) >  0.2 ||
                                    ( self->m_afstandsFactor - self->m_bewaardeFactor ) < -0.2 )
                                {
                                    self->m_bewaardeFactor = self->m_afstandsFactor;
                                    try
                                    {
                                        std::ofstream uit( MetingPad() );
                                        if( uit ) uit << self->m_afstandsFactor;
                                    }
                                    catch( ... ) { }
                                }
                            }
                        }
                    }

                    if( snelheidKmh >= RIJDT_DREMPEL_KMH && dLiters >= 0.0 )
                    {
                        self->m_rijdendLiters += dLiters;
                        self->m_rijdendKm += dKm;
                    }

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

                    self->RijstijlTellen( snelheidKmh, self->m_gaspedaal, dKm, dtUur * 3600.0 );

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

            if( self->m_brandstofVenster.size() >= 2 )
            {
                const auto &eerste = self->m_brandstofVenster.front();
                const auto &laatste = self->m_brandstofVenster.back();
                const double spanSec =
                    std::chrono::duration<double>( laatste.moment - eerste.moment ).count();
                const double dL = laatste.verbruiktLiters - eerste.verbruiktLiters;
                const double dKmVenster = laatste.gemetenKm - eerste.gemetenKm;

                if( spanSec > MIN_SPAN_SECONDEN && dL >= 0.0 )
                {
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

                    const double dempDt = hadVorigeMeting
                        ? std::chrono::duration<double>( nuMeting - vorigeMeetMoment ).count()
                        : 0.0;

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

                    const double schaal = self->TijdSchaal();
                    const double gemSnelheid = dKmVenster / ( spanSec / 3600.0 );
                    if( gemSnelheid >= PER100_MIN_KMH * schaal && literPerUurLang > 0.0 )
                    {
                        const double ruwPer100 = literPerUurLang / gemSnelheid * 100.0;
                        self->m_gladVerbruikNu = Demp( self->m_gladVerbruikNu, ruwPer100,
                                                        dempDt, DEMP_TAU_SECONDEN );
                    }

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
                            ( self->m_verbruikLiterPerKm >= 0.0 ) ? self->m_verbruikLiterPerKm * 100.0 : -1.0,
                            self->m_bereikKm );
                        Logboek::Schrijf( "fuel", regel );
                    }
                }
            }
        }

        if( !self->m_actief ) return;

        self->m_huidigeRit.huidigeSnelheidKmh = snelheidKmh;

        if( self->m_gepauzeerd )
        {
            return;
        }

        auto nu = std::chrono::steady_clock::now();
        double verstrekenUur = std::chrono::duration<double>( nu - self->m_laatsteSnelheidMeting ).count() / 3600.0;
        if( verstrekenUur > 0.0 && verstrekenUur < 0.1 )
        {
            self->m_huidigeRit.afgelegdeAfstandKm += snelheidKmh * verstrekenUur;
        }
        self->m_laatsteSnelheidMeting = nu;

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
            self->m_brandstof.ZetKilometerstand( self->m_kilometerstandKm );
            if( self->m_spelersVoorIncident )
            {
                double px = 0.0, pz = 0.0;
                const bool bekend = self->m_spelersVoorIncident->EigenPositie( px, pz );
                self->m_brandstof.ZetPositie( px, pz, bekend );
            }
            self->m_brandstof.ZetLiters( value->value_float.value, self->m_tankInhoudLiters );
            self->m_huidigeRit.brandstofPercentage =
                self->m_tankInhoudLiters > 0.0 ? ( value->value_float.value / self->m_tankInhoudLiters ) * 100.0 : 0.0;

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

        if( !self->m_schadeGemeten )
        {
            self->m_schadeGemeten = true;
            return;
        }

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
        if( vorige > 0.0 && self->m_kilometerstandKm < vorige - 0.2 && self->m_huidigVoertuig >= 0 )
        {
            Logboek::Schrijf( "event", "odometer jumped back: save reloaded? (waiting 2 s)" );
            self->m_rijstijlVenster.clear();
            self->m_rijstijlVensterSom = RijstijlTelling{};
            self->m_rijstijlPending = RijstijlMeting{};
            self->m_herlaadKmStand = self->m_kilometerstandKm;
            self->m_herlaadMoment = std::chrono::steady_clock::now();
        }
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

        const bool ingedrukt = ( self->m_gaspedaal > GAS_DREMPEL );
        if( ingedrukt != self->m_gasIngedrukt )
        {
            self->m_gasIngedrukt = ingedrukt;
            self->m_gasOmslag = true;
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
        s.verbruikLiterPer100Km = ( m_verbruikLiterPerKm >= 0.0 ) ? m_verbruikLiterPerKm * 100.0 : -1.0;

        {
            const double snelheidAbs = std::fabs( m_liveSnelheidKmh );
            s.echtStil = ( snelheidAbs < RIJDT_DREMPEL_KMH );
            s.staatStil = ( snelheidAbs < PER100_MIN_KMH );

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
                s.verbruikGemiddeldLiterPer100Km = m_rijdendLiters / m_rijdendKm * 100.0;
                s.verbruikRitLiterPer100Km = s.verbruikGemiddeldLiterPer100Km;
            }

            if( m_gladLiterPerUur >= 0.0 )
            {
                s.verbruikLiterPerUur = m_gladLiterPerUur / LUUR_DELER;
            }

            s.verbruikNuLiterPer100Km = m_gladVerbruikNu;

            s.ritLiters = m_rijdendLiters;
            s.ritKm = m_rijdendKm;
            if( m_huidigVoertuig >= 0 && m_huidigVoertuig < static_cast<int>( m_voertuigen.size() ) )
            {
                s.voertuigLiters = m_voertuigen[ m_huidigVoertuig ].liters;
                s.voertuigKm = m_voertuigen[ m_huidigVoertuig ].km;
            }
            if( s.verbruikNuLiterPer100Km < 0.0 )
            {
                s.verbruikNuLiterPer100Km = s.verbruikGemiddeldLiterPer100Km;
            }
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
        return m_liveSnelheidKmh > limietKmh + 3.0;
    }

    SCSAPI_VOID TruckTracking::LocalScaleCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                     scs_context_t )
    {
        if( !value ) return;
        double schaal = value->value_float.value;

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

    SCSAPI_VOID TruckTracking::BusTevredenheidCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                         scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( !value || value->type != SCS_VALUE_TYPE_float ) return;
        if( self->m_busTracking != nullptr )
            self->m_busTracking->ZetScsTevredenheid( value->value_float.value );
    }

    void TruckTracking::MeekijkLog()
    {
        const auto nu = std::chrono::steady_clock::now();
        if( m_meekijkLaatsteLog.time_since_epoch().count() != 0
            && std::chrono::duration<double>( nu - m_meekijkLaatsteLog ).count() < 60.0 )
        {
            return;
        }
        m_meekijkLaatsteLog = nu;
        Logboek::Schrijf( "meekijk",
            std::string( "rest.stop=" ) + ( m_meekijkRustGezien ? std::to_string( m_meekijkRust ) : "-" )
            + " mandatory.break=" + ( m_meekijkPauzeGezien ? std::to_string( m_meekijkPauze ) : "-" )
            + " multiplayer.time.offset=" + ( m_meekijkTijdVerschilGezien ? std::to_string( m_meekijkTijdVerschil ) : "-" )
            + " game.time=" + std::to_string( m_economyTijd ) );
    }

    void TruckTracking::MeekijkSprong( const char *naam, const int oud, const int nieuw )
    {
        const int stap = nieuw - oud;
        if( stap <= 10 && stap >= -10 ) return;
        Logboek::Schrijf( "meekijk", std::string( "JUMP " ) + naam + " " + std::to_string( oud ) + " -> " + std::to_string( nieuw )
            + " game.time " + std::to_string( m_meekijkSpelTijdVorige ) + " -> " + std::to_string( m_economyTijd )
            + " rest.stop=" + ( m_meekijkRustGezien ? std::to_string( m_meekijkRust ) : "-" )
            + " mandatory.break=" + ( m_meekijkPauzeGezien ? std::to_string( m_meekijkPauze ) : "-" ) );
    }

    SCSAPI_VOID TruckTracking::MeekijkRustCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                     scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( !value || value->type != SCS_VALUE_TYPE_s32 ) return;
        const int oud = self->m_meekijkRust;
        const bool hadWaarde = self->m_meekijkRustGezien;
        self->m_meekijkRust = value->value_s32.value;
        self->m_meekijkRustGezien = true;
        self->m_volgRust.Waarde( self->m_meekijkRust );
        self->SpelTellerMeld();
        if( hadWaarde ) self->MeekijkSprong( "rest.stop", oud, self->m_meekijkRust );
        self->m_meekijkSpelTijdVorige = self->m_economyTijd;
        self->MeekijkLog();
    }

    SCSAPI_VOID TruckTracking::MeekijkPauzeCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                      scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( !value || value->type != SCS_VALUE_TYPE_s32 ) return;
        const int oud = self->m_meekijkPauze;
        const bool hadWaarde = self->m_meekijkPauzeGezien;
        self->m_meekijkPauze = value->value_s32.value;
        self->m_meekijkPauzeGezien = true;
        self->m_volgPauze.Waarde( self->m_meekijkPauze );
        self->SpelTellerMeld();
        if( hadWaarde ) self->MeekijkSprong( "mandatory.break", oud, self->m_meekijkPauze );
        self->m_meekijkSpelTijdVorige = self->m_economyTijd;
        self->MeekijkLog();
    }

    SCSAPI_VOID TruckTracking::MeekijkTijdVerschilCallback( const scs_string_t, scs_u32_t, const scs_value_t *value,
                                                             scs_context_t context )
    {
        auto *self = static_cast<TruckTracking *>( context );
        if( !value || value->type != SCS_VALUE_TYPE_s32 ) return;
        self->m_meekijkTijdVerschil = value->value_s32.value;
        self->m_meekijkTijdVerschilGezien = true;
        self->MeekijkLog();
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

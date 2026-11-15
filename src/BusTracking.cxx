#include "BusTracking.hxx"
#include "Spel.hxx"

#include "CallbackHulp.hxx"
#include "Logboek.hxx"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <map>
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

    static std::string RedenAlsTekst( TruckersMP::BusJobCancellationReason reden )
    {
        using R = TruckersMP::BusJobCancellationReason;
        switch( reden )
        {
            case R::ExistingJob:    return "nieuwe job gestart terwijl er al een actief was";
            case R::Abandon:        return "zelf afgebroken";
            case R::Incompatible:   return "opgeslagen spel kwam niet meer overeen met de rit";
            case R::ExternalSource: return "geannuleerd vanuit een andere bron";
            default:                return "onbekende reden";
        }
    }

    BusTracking::BusTracking( TruckersMP::Session &session, TripLogger &logger )
        : m_logger( logger )
    {
        m_busModule = SpelInfo::IsAts() ? nullptr : TruckersMP::BusModule::Attach( session );
        if( !m_busModule )
        {
            return;
        }

        if( const auto lopendeJob = m_busModule->GetJob() )
        {
            StartRecord( *lopendeJob );
        }

        m_busModule->OnJobStarted.Register( Beschermd( "OnJobStarted", [ this ]( TruckersMP::BusJobStartedEvent &e )
        {
            StartRecord( e.GetJob() );
            m_huidigeRit.geschatUitbetaling = e.GetEstimatedPayout();
            Logboek::Schrijf( "bus", "estimated payout at start: "
                                        + std::to_string( m_huidigeRit.geschatUitbetaling ) );
        } ) );

        m_busModule->OnJobDataReady.Register( Beschermd( "OnJobDataReady", [ this ]( TruckersMP::BusJobDataReadyEvent &e )
        {
            const std::vector<TruckersMP::BusStop> stops = e.GetStops();
            double cumulatief = 0.0;
            double cumulatiefTijd = 0.0;
            for( std::size_t i = 0; i < stops.size() && i < m_huidigeRit.haltes.size(); ++i )
            {
                double afstandNaarDezeHalte = stops[ i ].GetPlannedDistance().value_or( 0.f );
                cumulatief += afstandNaarDezeHalte;
                m_huidigeRit.haltes[ i ].geplandeAfstandKm = cumulatief;
                m_huidigeRit.geplandeAfstandKm += afstandNaarDezeHalte;

                cumulatiefTijd += static_cast<double>( stops[ i ].GetScheduledTime().value_or( 0u ) );
                m_huidigeRit.haltes[ i ].geplandeTijdMin = cumulatiefTijd;
                m_huidigeRit.haltes[ i ].instappers  = stops[ i ].GetBoardingPassengers().value_or( 0 );
                m_huidigeRit.haltes[ i ].uitstappers = stops[ i ].GetLeavingPassengers().value_or( 0 );

                Logboek::Schrijf( "bus", "  data stop " + std::to_string( i )
                    + ": leg=" + std::to_string( afstandNaarDezeHalte ) + " km"
                    + " cumulative=" + std::to_string( cumulatief ) + " km"
                    + " legTime=" + std::to_string( stops[ i ].GetScheduledTime().value_or( 0u ) ) + " min"
                    + " cumulativeTime=" + std::to_string( cumulatiefTijd ) + " min"
                    + " boarding=" + std::to_string( stops[ i ].GetBoardingPassengers().value_or( 0 ) )
                    + " alighting=" + std::to_string( stops[ i ].GetLeavingPassengers().value_or( 0 ) ) );
            }
        } ) );

        m_busModule->OnStopCompleted.Register( Beschermd( "OnStopCompleted", [ this ]( TruckersMP::BusStopCompletedEvent &e )
        {
            m_huidigeRit.passagiers = e.GetJob().GetPassengerCount().value_or( m_huidigeRit.passagiers );

            const std::string identifier = e.GetStop().GetCityIdentifier().value_or( "" );
            for( StopInfo &stop : m_huidigeRit.haltes )
            {
                if( stop.voltooid || stop.cityIdentifier != identifier )
                {
                    continue;
                }
                stop.voltooid = true;
                stop.afgelegdeAfstandKm = e.GetDrivenDistance();
                m_huidigeRit.afgelegdeAfstandKm += stop.afgelegdeAfstandKm;
                m_liveKmSindsLaatsteHalte = 0.0;

                Logboek::Schrijf( "bus", "stop COMPLETED: " + stop.naam
                    + " (" + identifier + ")"
                    + " driven=" + std::to_string( stop.afgelegdeAfstandKm ) + " km"
                    + " planned=" + std::to_string( stop.geplandeAfstandKm ) + " km"
                    + " elapsed=" + std::to_string( VerstrekenMinutenEcht() ) + " min real" );
                break;
            }
        } ) );

        m_busModule->OnJobFinished.Register( Beschermd( "OnJobFinished", [ this ]( TruckersMP::BusJobFinishedEvent &e )
        {
            m_huidigeRit.status = TripStatus::Voltooid;
            m_huidigeRit.eindTijdIso = NuAlsIso();
            m_huidigeRit.economyEindTijd = m_economyTijd;
            m_huidigeRit.geschatUitbetaling = e.GetPayout();
            Logboek::Schrijf( "bus", "trip COMPLETED -- payout=" + std::to_string( m_huidigeRit.geschatUitbetaling )
                                        + " driven=" + std::to_string( m_huidigeRit.afgelegdeAfstandKm ) + " km"
                                        + " planned=" + std::to_string( m_huidigeRit.geplandeAfstandKm ) + " km"
                                        + " duration=" + std::to_string( VerstrekenMinutenEcht() ) + " min real" );
            BoekSluit();
            m_logger.RegisterVoltooideRit( m_huidigeRit );
            m_huidigeRit = Trip{};
            m_actief = false;
        } ) );

        m_busModule->OnJobCanceled.Register( Beschermd( "OnJobCanceled", [ this ]( TruckersMP::BusJobCanceledEvent &e )
        {
            m_huidigeRit.status = TripStatus::Geannuleerd;
            m_huidigeRit.eindTijdIso = NuAlsIso();
            m_huidigeRit.economyEindTijd = m_economyTijd;
            m_huidigeRit.annuleringsReden = RedenAlsTekst( e.GetReason() );
            Logboek::Schrijf( "bus", "trip CANCELLED -- reason=" + m_huidigeRit.annuleringsReden );
            BoekSluit();
            m_logger.RegisterVoltooideRit( m_huidigeRit );
            m_huidigeRit = Trip{};
            m_actief = false;
        } ) );
    }

    BusTracking::~BusTracking()
    {
        if( m_etappeDraad.joinable() ) m_etappeDraad.join();
        BoekBewaar( true );
    }

    void BusTracking::StartRecord( const TruckersMP::BusJob &job )
    {
        m_scsRit = false;
        m_scsLeegGezien = false;
        m_scsEtappes.clear();
        m_scsSleutel.clear();
        m_scsEtappesExact = false;
        m_scsGemetenPositie = -1.0;
        BoekLaatLos();
        m_huidigeRit = Trip{};
        m_actief = false;
        if( !job.IsValid() )
        {
            return;
        }

        m_actief = true;
        m_ritStartMoment = std::chrono::steady_clock::now();
        m_liveKmSindsLaatsteHalte = 0.0;
        m_snelheidVenster.clear();
        m_gladdeSchattingMin = -1.0;
        m_huidigeRit.type = TripType::Bus;
        m_huidigeRit.status = TripStatus::Bezig;
        m_huidigeRit.id = "bus-" + NuAlsIso();
        m_huidigeRit.startTijdIso = NuAlsIso();
        m_huidigeRit.economyStartTijd = job.GetStartTime().value_or( 0 );
        m_huidigeRit.passagiers = job.GetPassengerCount().value_or( 0 );

        for( const TruckersMP::BusStop &stop : job.GetStops().value_or( std::vector<TruckersMP::BusStop>{} ) )
        {
            StopInfo info;
            info.naam = stop.GetName().value_or( "" );
            info.cityIdentifier = stop.GetCityIdentifier().value_or( "" );
            m_huidigeRit.haltes.push_back( std::move( info ) );
        }

        Logboek::Schrijf( "bus", "trip started -- stops=" + std::to_string( m_huidigeRit.haltes.size() )
                                    + " passengers=" + std::to_string( job.GetPassengerCount().value_or( 0 ) )
                                    + " economyStart=" + std::to_string( m_huidigeRit.economyStartTijd ) );
        for( std::size_t i = 0; i < m_huidigeRit.haltes.size(); ++i )
        {
            Logboek::Schrijf( "bus", "  stop " + std::to_string( i ) + ": "
                                        + m_huidigeRit.haltes[ i ].naam
                                        + " (" + m_huidigeRit.haltes[ i ].cityIdentifier + ")" );
        }

        std::string sleutel = "tmp|" + std::to_string( m_huidigeRit.economyStartTijd );
        for( const StopInfo &h : m_huidigeRit.haltes ) sleutel += "|" + h.cityIdentifier;
        if( m_huidigeRit.economyStartTijd == 0 ) sleutel += "|geen-" + m_huidigeRit.id;
        BoekStart( sleutel );
    }

    double BusTracking::VerstrekenMinutenEcht() const
    {
        if( !m_actief ) return 0.0;
        auto nu = std::chrono::steady_clock::now();
        double totaalSeconden = std::chrono::duration<double>( nu - m_ritStartMoment ).count();

        double gepauzeerdSeconden = m_totaalGepauzeerdSeconden;
        if( m_gepauzeerd )
        {
            gepauzeerdSeconden += std::chrono::duration<double>( nu - m_pauzeStartMoment ).count();
        }

        return std::max( 0.0, totaalSeconden - gepauzeerdSeconden ) / 60.0;
    }

    void BusTracking::OpLiveSnelheid( double snelheidKmh, bool gepauzeerd )
    {
        auto nu = std::chrono::steady_clock::now();
        if( gepauzeerd && !m_gepauzeerd )
        {
            m_gepauzeerd = true;
            m_pauzeStartMoment = nu;
        }
        else if( !gepauzeerd && m_gepauzeerd )
        {
            m_totaalGepauzeerdSeconden += std::chrono::duration<double>( nu - m_pauzeStartMoment ).count();
            m_gepauzeerd = false;
        }

        ScsEtappesVerwerk();

        BoekTik( gepauzeerd );

        if( !m_actief || gepauzeerd )
        {
            return;
        }

        m_huidigeRit.huidigeSnelheidKmh = snelheidKmh;

        if( m_laatsteSnelheidMeting.time_since_epoch().count() != 0 )
        {
            double verstrekenUur = std::chrono::duration<double>( nu - m_laatsteSnelheidMeting ).count() / 3600.0;
            if( verstrekenUur > 0.0 && verstrekenUur < 0.1 )
            {
                m_liveKmSindsLaatsteHalte += snelheidKmh * verstrekenUur;
            }
        }
        m_laatsteSnelheidMeting = nu;

        m_snelheidVenster.emplace_back( nu, snelheidKmh );
        while( !m_snelheidVenster.empty()
               && std::chrono::duration<double>( nu - m_snelheidVenster.front().first ).count() > VENSTER_SECONDEN )
        {
            m_snelheidVenster.pop_front();
        }

        if( std::chrono::duration<double>( nu - m_laatsteHalteLog ).count() > LOG_INTERVAL_SECONDEN )
        {
            m_laatsteHalteLog = nu;
            std::string regel = "prediction t=" + std::to_string( VerstrekenMinutenEcht() ) + " min real"
                                + " speed=" + std::to_string( snelheidKmh );
            for( std::size_t i = 0; i < m_huidigeRit.haltes.size(); ++i )
            {
                if( m_huidigeRit.haltes[ i ].voltooid ) continue;
                regel += " | h" + std::to_string( i ) + "="
                       + std::to_string( GeschatteMinutenTotHalte( i ) ) + "min";
            }
            Logboek::Schrijf( "bus", regel );
        }
    }

    void BusTracking::ZetEconomyTijd( std::uint32_t minuten )
    {
        if( minuten != m_economyTijd )
        {
            const auto nu = std::chrono::steady_clock::now();
            const double stapSpel = static_cast<double>( minuten ) - static_cast<double>( m_economyTijd );
            const double sindsAnker = m_schaalGestart
                ? std::chrono::duration<double>( nu - m_schaalEersteEcht ).count() / 60.0
                : 0.0;
            if( !m_schaalGestart || stapSpel < 0.0 || stapSpel > 5.0 || sindsAnker > 15.0 )
            {
                m_schaalEersteEconomy = minuten;
                m_schaalEersteEcht = nu;
                m_schaalGestart = true;
            }
            m_economyTijd = minuten;
        }
    }

    double BusTracking::TijdSchaal() const
    {
        constexpr double STANDAARD_SCHAAL = 6.0;
        if( m_vastgezetteSchaal > 0.0 ) return m_vastgezetteSchaal;
        if( !m_schaalGestart ) return STANDAARD_SCHAAL;

        const double echteMinuten =
            std::chrono::duration<double>( std::chrono::steady_clock::now() - m_schaalEersteEcht ).count() / 60.0;
        if( echteMinuten < 2.0 ) return STANDAARD_SCHAAL;

        const double economyMinuten =
            static_cast<double>( m_economyTijd ) - static_cast<double>( m_schaalEersteEconomy );
        if( economyMinuten <= 0.0 ) return STANDAARD_SCHAAL;

        const double gemeten = economyMinuten / echteMinuten;
        if( gemeten < 3.0 || gemeten > 25.0 ) return STANDAARD_SCHAAL;
        if( echteMinuten >= 5.0 ) m_vastgezetteSchaal = gemeten;
        return gemeten;
    }

    void BusTracking::ZetNavigatie( double navTijdRuw, double navAfstandKm )
    {
        m_navTijdRuw = navTijdRuw;
        m_navAfstandKm = navAfstandKm;
    }

    double BusTracking::Gladstrijken( double ruweMinuten ) const
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

    double BusTracking::EffectieveSnelheidEcht() const
    {
        double v = 0.0;
        if( m_snelheidVenster.size() >= 2 )
        {
            double som = 0.0;
            for( const auto &sample : m_snelheidVenster ) som += sample.second;
            v = ( som / m_snelheidVenster.size() ) * TijdSchaal();
        }
        if( v < 1.0 )
        {
            const double verstrekenMin = VerstrekenMinutenEcht();
            const double gereden = m_huidigeRit.afgelegdeAfstandKm + m_liveKmSindsLaatsteHalte;
            if( verstrekenMin > 0.5 && gereden > 0.5 )
            {
                v = gereden / ( verstrekenMin / 60.0 );
            }
        }
        if( v < 1.0 ) v = m_huidigeRit.huidigeSnelheidKmh * TijdSchaal();

        const double bodem = 40.0 * TijdSchaal();
        return v < bodem ? bodem : v;
    }

    double BusTracking::GeschatteMinutenTotHalte( std::size_t index ) const
    {
        if( !m_actief || index >= m_huidigeRit.haltes.size() ) return -1.0;
        if( m_huidigeRit.haltes[ index ].voltooid ) return -1.0;

        std::size_t eerstvolgende = m_huidigeRit.haltes.size();
        for( std::size_t i = 0; i < m_huidigeRit.haltes.size(); ++i )
        {
            if( !m_huidigeRit.haltes[ i ].voltooid ) { eerstvolgende = i; break; }
        }
        if( eerstvolgende >= m_huidigeRit.haltes.size() ) return -1.0;

        const double basis = GeschatteResterendeMinutenEcht();
        if( basis < 0.0 ) return -1.0;
        if( index == eerstvolgende ) return basis;

        const double extraKm = m_huidigeRit.haltes[ index ].geplandeAfstandKm
                                - m_huidigeRit.haltes[ eerstvolgende ].geplandeAfstandKm;
        if( extraKm <= 0.0 ) return basis;

        double snelheidEcht = 0.0;
        if( m_navTijdRuw > 0.0 && m_navAfstandKm > 1.0 )
        {
            const double spelUren = m_navTijdRuw / 3600.0;
            const double kmhSpel = spelUren > 0.0 ? m_navAfstandKm / spelUren : 0.0;
            if( kmhSpel >= 3.0 && kmhSpel <= 200.0 )
            {
                snelheidEcht = kmhSpel * TijdSchaal();
            }
        }
        if( snelheidEcht <= 0.0 ) snelheidEcht = EffectieveSnelheidEcht();

        return basis + ( extraKm / snelheidEcht ) * 60.0;
    }

    double BusTracking::GeschatteVertragingMinuten() const
    {
        if( !m_actief || m_huidigeRit.haltes.empty() ) return -1e9;

        const double geplandTotEind = m_huidigeRit.haltes.back().geplandeTijdMin;
        if( geplandTotEind <= 0.0 ) return -1e9;

        const double deadline =
            static_cast<double>( m_huidigeRit.economyStartTijd ) + geplandTotEind;
        const double economyNu = static_cast<double>( m_economyTijd );

        const double resterendeKm =
            m_huidigeRit.geplandeAfstandKm - m_huidigeRit.afgelegdeAfstandKm - m_liveKmSindsLaatsteHalte;
        if( resterendeKm <= 0.0 )
        {
            return economyNu - deadline;
        }

        const double vEcht = EffectieveSnelheidEcht();
        if( vEcht <= 0.0 ) return -1e9;

        const double echteMinutenNodig = ( resterendeKm / vEcht ) * 60.0;
        const double spelMinutenNodig = echteMinutenNodig * TijdSchaal();
        const double verwachteAankomst = economyNu + spelMinutenNodig;

        return verwachteAankomst - deadline;
    }

    double BusTracking::GeschatteBoetePercentage() const
    {
        if( m_scsRit ) return 0.0;

        const double vertraging = GeschatteVertragingMinuten();
        if( vertraging <= -1e8 ) return 0.0;

        const double strafbaar = vertraging - 60.0;
        if( strafbaar <= 0.0 ) return 0.0;

        return std::min( 100.0, strafbaar * 0.333 );
    }

    double BusTracking::GeschatteResterendeMinutenEcht() const
    {
        if( !m_actief ) return -1.0;

        double geplandTotEerstvolgende = -1.0;
        for( const StopInfo &s : m_huidigeRit.haltes )
        {
            if( !s.voltooid )
            {
                geplandTotEerstvolgende = s.geplandeAfstandKm;
                break;
            }
        }
        if( geplandTotEerstvolgende < 0.0 ) return m_scsRit ? 0.0 : -1.0;

        double gereden;
        const double geplandTotEind = m_huidigeRit.haltes.back().geplandeAfstandKm;
        if( m_navAfstandKm >= 0.0 && geplandTotEind > 1.0 )
        {
            gereden = geplandTotEind - m_navAfstandKm;
            if( gereden < 0.0 ) gereden = 0.0;
        }
        else
        {
            gereden = m_huidigeRit.afgelegdeAfstandKm + m_liveKmSindsLaatsteHalte;
        }

        double resterendeKm = geplandTotEerstvolgende - gereden;
        if( resterendeKm <= 0.3 ) return 0.0;

        if( m_navTijdRuw > 0.0 && m_navAfstandKm > 1.0 )
        {
            const double schaal = TijdSchaal();
            const double spelUren = m_navTijdRuw / 3600.0;

            double snelheid = spelUren > 0.0 ? m_navAfstandKm / spelUren : 0.0;
            if( snelheid < 3.0 || snelheid > 200.0 )
            {
                const double alsMinuten = m_navTijdRuw / 60.0;
                const double snelheidMin = alsMinuten > 0.0 ? m_navAfstandKm / alsMinuten : 0.0;
                if( snelheidMin >= 3.0 && snelheidMin <= 200.0 ) snelheid = snelheidMin;
            }

            if( snelheid >= 3.0 && snelheid <= 200.0 )
            {
                return Gladstrijken( ( resterendeKm / snelheid ) * 60.0 / schaal );
            }
        }

        double gemiddeldeSnelheid = 0.0;

        if( m_snelheidVenster.size() >= 2 )
        {
            double som = 0.0;
            for( const auto &sample : m_snelheidVenster ) som += sample.second;
            gemiddeldeSnelheid = ( som / m_snelheidVenster.size() ) * TijdSchaal();
        }

        if( gemiddeldeSnelheid < 1.0 )
        {
            const double verstrekenMin = VerstrekenMinutenEcht();
            const double gereden = m_huidigeRit.afgelegdeAfstandKm + m_liveKmSindsLaatsteHalte;
            if( verstrekenMin > 0.5 && gereden > 0.5 )
            {
                gemiddeldeSnelheid = gereden / ( verstrekenMin / 60.0 );
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

        return Gladstrijken( ( resterendeKm / gemiddeldeSnelheid ) * 60.0 );
    }

    namespace
    {
        std::string ScsTekst( const scs_named_value_t *attr )
        {
            if( attr->value.type == SCS_VALUE_TYPE_string && attr->value.value_string.value )
                return attr->value.value_string.value;
            return std::string();
        }

        long long ScsGetal( const scs_named_value_t *attr )
        {
            switch( attr->value.type )
            {
                case SCS_VALUE_TYPE_u32: return static_cast<long long>( attr->value.value_u32.value );
                case SCS_VALUE_TYPE_u64: return static_cast<long long>( attr->value.value_u64.value );
                case SCS_VALUE_TYPE_s32: return static_cast<long long>( attr->value.value_s32.value );
                case SCS_VALUE_TYPE_s64: return static_cast<long long>( attr->value.value_s64.value );
                case SCS_VALUE_TYPE_float: return static_cast<long long>( attr->value.value_float.value );
                case SCS_VALUE_TYPE_double: return static_cast<long long>( attr->value.value_double.value );
                default: return 0;
            }
        }

        double ScsKomma( const scs_named_value_t *attr )
        {
            if( attr->value.type == SCS_VALUE_TYPE_float ) return attr->value.value_float.value;
            if( attr->value.type == SCS_VALUE_TYPE_double ) return attr->value.value_double.value;
            return static_cast<double>( ScsGetal( attr ) );
        }
    }

    bool BusTracking::ScsRitVerlopen() const
    {
        if( !m_scsRit || !m_scsLeegGezien ) return false;
        return std::chrono::duration<double>( std::chrono::steady_clock::now() - m_scsLeegMoment ).count()
               > SCS_LEEG_WACHT_SECONDEN;
    }

    void BusTracking::ScsWisRit()
    {
        if( !m_scsRit ) return;
        BoekLaatLos();
        m_scsRit = false;
        m_scsLeegGezien = false;
        m_scsEtappes.clear();
        m_scsSleutel.clear();
        m_scsGeplandeKm = 0;
        m_scsEtappesExact = false;
        m_scsGemetenPositie = -1.0;
        m_huidigeRit = Trip{};
        m_actief = false;
    }

    void BusTracking::ZetScsSpelTijd( std::uint32_t minuten )
    {
        m_scsSpelTijd = minuten;
        if( ScsRitVerlopen() )
        {
            Logboek::Schrijf( "bus", "SCS ride dropped: empty configuration, no end event followed" );
            ScsWisRit();
        }
    }

    void BusTracking::ZetScsTevredenheid( double waarde )
    {
        if( waarde < 0.0 ) waarde = 0.0;
        if( waarde > 1.0 ) waarde = 1.0;
        m_scsTevredenheidKanaal = waarde;
        if( !m_actief || !m_scsRit ) return;
        m_huidigeRit.tevredenheid = waarde;
    }

    void BusTracking::OpScsBusConfig( const scs_telemetry_configuration_t *cfg )
    {
        if( cfg == nullptr || cfg->attributes == nullptr ) return;

        if( m_actief && !m_scsRit ) return;

        std::vector<ScsEtappe> etappes;
        std::string markt;
        long long inkomen = 0;
        long long geplandeKm = 0;
        bool ietsGelezen = false;

        std::map<std::string, std::uint32_t> gezien;
        auto etappeVoor = [ & ]( const scs_named_value_t *attr ) -> ScsEtappe &
        {
            std::uint32_t &keer = gezien[ attr->name ];
            std::uint32_t nr = attr->index;
            if( nr == SCS_U32_NIL || nr < keer ) nr = keer;
            ++keer;
            if( nr > 63 ) nr = 63;
            if( etappes.size() <= nr ) etappes.resize( nr + 1 );
            return etappes[ nr ];
        };

        for( const scs_named_value_t *attr = cfg->attributes; attr->name != nullptr; ++attr )
        {
            ietsGelezen = true;
            const char *n = attr->name;
            if( std::strcmp( n, "income" ) == 0 )                    inkomen = ScsGetal( attr );
            else if( std::strcmp( n, "planned_distance.km" ) == 0 )  geplandeKm = ScsGetal( attr );
            else if( std::strcmp( n, "bus_job.market" ) == 0 )       markt = ScsTekst( attr );
            else if( std::strcmp( n, "source_stop.id" ) == 0 )       etappeVoor( attr ).bronId = ScsTekst( attr );
            else if( std::strcmp( n, "source_stop.name" ) == 0 )     etappeVoor( attr ).bronNaam = ScsTekst( attr );
            else if( std::strcmp( n, "source_stop.type" ) == 0 )     etappeVoor( attr ).bronType = ScsTekst( attr );
            else if( std::strcmp( n, "destination_stop.id" ) == 0 )   etappeVoor( attr ).doelId = ScsTekst( attr );
            else if( std::strcmp( n, "destination_stop.name" ) == 0 ) etappeVoor( attr ).doelNaam = ScsTekst( attr );
            else if( std::strcmp( n, "destination_stop.type" ) == 0 ) etappeVoor( attr ).doelType = ScsTekst( attr );
            else if( std::strcmp( n, "source_stop.departure_time.planned" ) == 0 )
                etappeVoor( attr ).vertrekGepland = static_cast<std::uint32_t>( ScsGetal( attr ) );
            else if( std::strcmp( n, "source_stop.departure_time.actual" ) == 0 )
                etappeVoor( attr ).vertrekWerkelijk = static_cast<std::uint32_t>( ScsGetal( attr ) );
            else if( std::strcmp( n, "destination_stop.arrival_time.planned" ) == 0 )
                etappeVoor( attr ).aankomstGepland = static_cast<std::uint32_t>( ScsGetal( attr ) );
            else if( std::strcmp( n, "destination_stop.arrival_time.actual" ) == 0 )
                etappeVoor( attr ).aankomstWerkelijk = static_cast<std::uint32_t>( ScsGetal( attr ) );
            else if( std::strcmp( n, "source_stop.entering_passengers.planned" ) == 0 )
                etappeVoor( attr ).instappers = static_cast<std::uint32_t>( ScsGetal( attr ) );
            else if( std::strcmp( n, "destination_stop.leaving_passengers.planned" ) == 0 )
                etappeVoor( attr ).uitstappers = static_cast<std::uint32_t>( ScsGetal( attr ) );
        }

        if( !ietsGelezen || etappes.empty() )
        {
            if( m_scsRit && !m_scsLeegGezien )
            {
                m_scsLeegGezien = true;
                m_scsLeegMoment = std::chrono::steady_clock::now();
            }
            return;
        }

        const std::string sleutel = etappes[ 0 ].bronId + "|" + std::to_string( etappes[ 0 ].vertrekGepland );
        const bool nieuweRit = !m_scsRit || sleutel != m_scsSleutel;
        if( nieuweRit )
        {
            if( m_scsRit )
            {
                Logboek::Schrijf( "bus", "SCS ride replaced by another one, previous dropped" );
            }
            ScsStartRit( etappes, sleutel );
            m_huidigeRit.busMarkt = markt.empty() ? std::string( "company_job" ) : markt;
        }
        m_scsLeegGezien = false;

        if( inkomen > 0 ) m_huidigeRit.geschatUitbetaling = inkomen;
        if( geplandeKm > 0 && static_cast<std::uint32_t>( geplandeKm ) != m_scsGeplandeKm )
        {
            if( m_scsGeplandeKm != 0 ) ScsEtappesOpnieuw();
            m_scsGeplandeKm = static_cast<std::uint32_t>( geplandeKm );
            m_huidigeRit.geplandeAfstandKm = static_cast<double>( geplandeKm );
            double totaleRijtijd = 0.0;
            for( const ScsEtappe &e : etappes )
                if( e.aankomstGepland > e.vertrekGepland )
                    totaleRijtijd += static_cast<double>( e.aankomstGepland - e.vertrekGepland );
            double cumulatief = 0.0;
            for( std::size_t i = 0; i < etappes.size() && i < m_huidigeRit.haltes.size(); ++i )
            {
                double deel = 1.0 / static_cast<double>( etappes.size() );
                if( totaleRijtijd > 0.0 && etappes[ i ].aankomstGepland > etappes[ i ].vertrekGepland )
                    deel = static_cast<double>( etappes[ i ].aankomstGepland - etappes[ i ].vertrekGepland ) / totaleRijtijd;
                else if( totaleRijtijd > 0.0 )
                    deel = 0.0;
                cumulatief += deel * static_cast<double>( geplandeKm );
                m_huidigeRit.haltes[ i ].geplandeAfstandKm = cumulatief;
            }
            if( !m_huidigeRit.haltes.empty() )
                m_huidigeRit.haltes.back().geplandeAfstandKm = static_cast<double>( geplandeKm );
        }

        ScsWerkBij( etappes );

        if( nieuweRit )
        {
            Logboek::Schrijf( "bus", "SCS ride started -- market=" + m_huidigeRit.busMarkt
                + " stops=" + std::to_string( m_huidigeRit.haltes.size() )
                + " planned=" + std::to_string( m_scsGeplandeKm ) + " km"
                + " income=" + std::to_string( m_huidigeRit.geschatUitbetaling )
                + " gameTimeNow=" + std::to_string( m_scsSpelTijd )
                + " plannedDeparture=" + std::to_string( etappes[ 0 ].vertrekGepland ) );
            for( std::size_t i = 0; i < m_huidigeRit.haltes.size(); ++i )
            {
                const StopInfo &h = m_huidigeRit.haltes[ i ];
                Logboek::Schrijf( "bus", "  SCS stop " + std::to_string( i ) + ": " + h.naam
                    + " (" + h.cityIdentifier + ")"
                    + " plannedArrival=" + std::to_string( h.geplandAankomst )
                    + " cumulative=" + std::to_string( h.geplandeAfstandKm ) + " km"
                    + " boarding=" + std::to_string( h.instappers )
                    + " alighting=" + std::to_string( h.uitstappers ) );
            }
        }
    }

    void BusTracking::ScsStartRit( const std::vector<ScsEtappe> &etappes, const std::string &sleutel )
    {
        BoekLaatLos();
        m_huidigeRit = Trip{};
        m_actief = true;
        m_scsRit = true;
        m_scsSleutel = sleutel;
        m_scsEtappes.clear();
        m_scsGeplandeKm = 0;
        m_scsLeegGezien = false;
        ScsEtappesOpnieuw();

        const auto nu = std::chrono::steady_clock::now();
        double alGepauzeerd = m_totaalGepauzeerdSeconden;
        if( m_gepauzeerd ) alGepauzeerd += std::chrono::duration<double>( nu - m_pauzeStartMoment ).count();
        m_ritStartMoment = nu - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                    std::chrono::duration<double>( alGepauzeerd ) );

        m_liveKmSindsLaatsteHalte = 0.0;
        m_snelheidVenster.clear();
        m_gladdeSchattingMin = -1.0;

        m_huidigeRit.type = TripType::Bus;
        m_huidigeRit.status = TripStatus::Bezig;
        m_huidigeRit.id = "bus-" + NuAlsIso();
        m_huidigeRit.startTijdIso = NuAlsIso();
        m_huidigeRit.economyStartTijd = etappes[ 0 ].vertrekGepland;
        m_huidigeRit.bronStad = etappes[ 0 ].bronNaam;
        m_huidigeRit.bestemmingStad = etappes.back().doelNaam;

        std::uint32_t vervoerd = 0;
        for( std::size_t i = 0; i < etappes.size(); ++i )
        {
            vervoerd += etappes[ i ].instappers;

            StopInfo info;
            info.naam = etappes[ i ].doelNaam;
            info.cityIdentifier = etappes[ i ].doelId;
            info.uitstappers = static_cast<int>( etappes[ i ].uitstappers );
            info.instappers = ( i + 1 < etappes.size() ) ? static_cast<int>( etappes[ i + 1 ].instappers ) : 0;
            if( etappes[ i ].aankomstGepland > etappes[ 0 ].vertrekGepland )
                info.geplandeTijdMin = static_cast<double>( etappes[ i ].aankomstGepland - etappes[ 0 ].vertrekGepland );
            m_huidigeRit.haltes.push_back( std::move( info ) );
        }
        m_huidigeRit.passagiersVervoerd = vervoerd;

        std::string boekSleutel = "scs|" + sleutel;
        for( const ScsEtappe &e : etappes ) boekSleutel += "|" + e.doelId;
        if( etappes[ 0 ].vertrekGepland == 0 ) boekSleutel += "|geen-" + m_huidigeRit.id;
        BoekStart( boekSleutel );
    }

    void BusTracking::ScsWerkBij( const std::vector<ScsEtappe> &etappes )
    {
        const bool eersteKeer = m_scsEtappes.empty();
        m_scsEtappes = etappes;

        const double totaalKm = static_cast<double>( m_scsGeplandeKm );
        int aanBoord = 0;
        for( std::size_t i = 0; i < etappes.size() && i < m_huidigeRit.haltes.size(); ++i )
        {
            const ScsEtappe &e = etappes[ i ];
            StopInfo &h = m_huidigeRit.haltes[ i ];
            h.geplandVertrek = e.vertrekGepland;
            h.werkelijkVertrek = e.vertrekWerkelijk;
            h.geplandAankomst = e.aankomstGepland;
            h.werkelijkAankomst = e.aankomstWerkelijk;

            if( e.vertrekWerkelijk != 0 ) aanBoord += static_cast<int>( e.instappers );
            if( e.aankomstWerkelijk != 0 && i + 1 < etappes.size() && etappes[ i + 1 ].vertrekWerkelijk != 0 )
                aanBoord -= static_cast<int>( e.uitstappers );

            if( e.aankomstWerkelijk == 0 || h.voltooid ) continue;
            h.voltooid = true;

            const double vorigeGrens = ( i == 0 ) ? 0.0 : m_huidigeRit.haltes[ i - 1 ].geplandeAfstandKm;
            double etappeKm = h.geplandeAfstandKm - vorigeGrens;
            if( m_scsEtappesExact )
            {
                const double vorigeGemeten = ( i == 0 ) ? 0.0
                                             : ( m_scsGemetenPositie >= 0.0 ? m_scsGemetenPositie : vorigeGrens );
                const double hier = totaalKm - m_navAfstandKm;
                if( !eersteKeer && m_navAfstandKm >= 0.0 && totaalKm > 1.0
                    && hier > vorigeGemeten && hier < totaalKm + 1.0 )
                {
                    etappeKm = hier - vorigeGemeten;
                    m_scsGemetenPositie = hier;
                }
                else
                {
                    m_scsGemetenPositie = h.geplandeAfstandKm;
                }
            }
            else if( !eersteKeer && m_navAfstandKm >= 0.0 && totaalKm > 1.0 )
            {
                const double hier = totaalKm - m_navAfstandKm;
                const double volgendeGrens = ( i + 1 < m_huidigeRit.haltes.size() ) ? totaalKm : totaalKm + 1.0;
                if( hier > vorigeGrens && hier < volgendeGrens )
                {
                    etappeKm = hier - vorigeGrens;
                    m_scsGemetenPositie = hier;
                    const double oudRest = totaalKm - h.geplandeAfstandKm;
                    const double nieuwRest = totaalKm - hier;
                    for( std::size_t j = i + 1; j + 1 < m_huidigeRit.haltes.size(); ++j )
                    {
                        const double oud = m_huidigeRit.haltes[ j ].geplandeAfstandKm - h.geplandeAfstandKm;
                        m_huidigeRit.haltes[ j ].geplandeAfstandKm =
                            hier + ( oudRest > 0.0 ? oud / oudRest * nieuwRest : 0.0 );
                    }
                    if( i + 1 < m_huidigeRit.haltes.size() ) h.geplandeAfstandKm = hier;
                }
            }
            if( etappeKm < 0.0 ) etappeKm = 0.0;
            h.afgelegdeAfstandKm = etappeKm;
            m_huidigeRit.afgelegdeAfstandKm += etappeKm;
            m_liveKmSindsLaatsteHalte = 0.0;

            Logboek::Schrijf( "bus", "SCS stop COMPLETED: " + h.naam + " (" + h.cityIdentifier + ")"
                + " planned=" + std::to_string( e.aankomstGepland )
                + " actual=" + std::to_string( e.aankomstWerkelijk )
                + " difference=" + std::to_string( static_cast<long long>( e.aankomstWerkelijk )
                                                   - static_cast<long long>( e.aankomstGepland ) ) + " min"
                + " leg=" + std::to_string( etappeKm ) + " km"
                + " elapsed=" + std::to_string( VerstrekenMinutenEcht() ) + " min real" );
        }
        if( aanBoord < 0 ) aanBoord = 0;
        m_huidigeRit.passagiers = static_cast<std::uint32_t>( aanBoord );
        if( m_scsTevredenheidKanaal >= 0.0 ) m_huidigeRit.tevredenheid = m_scsTevredenheidKanaal;
    }

    void BusTracking::OpScsBusEvent( const scs_telemetry_gameplay_event_t *info )
    {
        if( info == nullptr || info->id == nullptr || info->attributes == nullptr ) return;
        if( !m_actief || !m_scsRit ) return;

        const std::string eventId = info->id;
        const bool voltooid = eventId == "bus_job.completed";
        const bool geannuleerd = eventId == "bus_job.cancelled";
        if( !voltooid && !geannuleerd ) return;

        m_huidigeRit.eindTijdIso = NuAlsIso();
        m_huidigeRit.economyEindTijd = m_scsSpelTijd;
        m_huidigeRit.passagiers = 0;

        if( voltooid )
        {
            m_huidigeRit.status = TripStatus::Voltooid;
            for( const scs_named_value_t *attr = info->attributes; attr->name != nullptr; ++attr )
            {
                const char *n = attr->name;
                if( std::strcmp( n, "revenue" ) == 0 )
                {
                    m_huidigeRit.inkomen = ScsGetal( attr );
                    m_huidigeRit.geschatUitbetaling = m_huidigeRit.inkomen;
                }
                else if( std::strcmp( n, "earned.xp" ) == 0 )
                    m_huidigeRit.verdiendeXp = static_cast<std::int32_t>( ScsGetal( attr ) );
                else if( std::strcmp( n, "passenger.satisfaction" ) == 0 )
                {
                    double v = ScsKomma( attr );
                    if( v < 0.0 ) v = 0.0;
                    if( v > 1.0 ) v = 1.0;
                    m_huidigeRit.tevredenheid = v;
                }
                else if( std::strcmp( n, "distance.km" ) == 0 )
                {
                    const double km = ScsKomma( attr );
                    if( km > 0.0 ) m_huidigeRit.afgelegdeAfstandKm = km;
                }
            }
            if( !m_huidigeRit.haltes.empty() )
            {
                const StopInfo &laatste = m_huidigeRit.haltes.back();
                if( laatste.werkelijkAankomst != 0 && laatste.geplandAankomst != 0 )
                    m_huidigeRit.opTijd = laatste.werkelijkAankomst <= laatste.geplandAankomst;
            }
            Logboek::Schrijf( "bus", "SCS ride COMPLETED -- revenue=" + std::to_string( m_huidigeRit.inkomen )
                + " xp=" + std::to_string( m_huidigeRit.verdiendeXp )
                + " satisfaction=" + std::to_string( m_huidigeRit.tevredenheid )
                + " driven=" + std::to_string( m_huidigeRit.afgelegdeAfstandKm ) + " km"
                + " carried=" + std::to_string( m_huidigeRit.passagiersVervoerd )
                + " duration=" + std::to_string( VerstrekenMinutenEcht() ) + " min real" );
        }
        else
        {
            m_huidigeRit.status = TripStatus::Geannuleerd;
            m_huidigeRit.annuleringsReden = "in het spel geannuleerd";
            m_huidigeRit.tevredenheid = -1.0;
            for( const scs_named_value_t *attr = info->attributes; attr->name != nullptr; ++attr )
            {
                if( std::strcmp( attr->name, "cancel.penalty" ) == 0 )
                    m_huidigeRit.annuleerBoete = ScsGetal( attr );
            }
            Logboek::Schrijf( "bus", "SCS ride CANCELLED -- penalty=" + std::to_string( m_huidigeRit.annuleerBoete ) );
        }

        BoekSluit();
        m_logger.RegisterVoltooideRit( m_huidigeRit );
        ScsWisRit();
    }

    void BusTracking::KoppelBrandstof( FuelCosts *brandstof )
    {
        m_brandstof = brandstof;
        if( m_brandstof != nullptr && m_actief && !m_boekSleutel.empty() && !m_brandstof->BusRitActief() )
            m_brandstof->StartBusRit();
    }

    void BusTracking::ZetVoertuig( const std::string &merk, const std::string &model )
    {
        if( merk == m_voertuigMerk && model == m_voertuigModel ) return;
        if( !m_voertuigMerk.empty() ) m_boekMomenten.clear();
        m_voertuigMerk = merk;
        m_voertuigModel = model;
        m_kmStandVers = false;
    }

    void BusTracking::ZetKilometerstand( const double km, const bool vers )
    {
        m_kmStand = km;
        m_kmStandVers = vers && km > 0.0;
    }

    void BusTracking::BoekBoete( const Boete &boete )
    {
        if( !m_actief ) return;
        m_huidigeRit.boetes.push_back( boete );
        m_huidigeRit.boeteKosten += boete.bedrag;
        BoekMoment( true );
        BoekBewaar( true );
    }

    void BusTracking::BoekDoorgang( const Doorgang &doorgang )
    {
        if( !m_actief ) return;
        switch( doorgang.type )
        {
            case DoorgangType::Tol:      m_huidigeRit.tolKosten += doorgang.bedrag; break;
            case DoorgangType::Veerboot: m_huidigeRit.veerbootKosten += doorgang.bedrag; break;
            case DoorgangType::Trein:    m_huidigeRit.treinKosten += doorgang.bedrag; break;
        }
        m_huidigeRit.doorgangen.push_back( doorgang );
        BoekMoment( true );
        BoekBewaar( true );
    }

    void BusTracking::BoekStart( const std::string &sleutel )
    {
        m_boekSleutel = sleutel;
        m_hervatWacht = false;
        m_hervatWachtSec = 0.0;
        m_boekMomenten.clear();
        m_boekLaatsteTik = std::chrono::steady_clock::now();
        m_boekLaatstBewaard = m_boekLaatsteTik;
        if( m_brandstof != nullptr ) m_brandstof->StartBusRit();

        LopendeRit oud;
        if( RitBoek::Lees( RitBoek::Soort::Bus, oud ) && oud.sleutel == sleutel )
        {
            m_hervatKandidaat = oud;
            m_hervatWacht = true;
            Logboek::Schrijf( "rit", "bus ride found in the book from " + oud.bewaardIso
                                        + ", waiting for the odometer before picking it up" );
        }
        else
        {
            BoekBewaar( true );
        }
    }

    void BusTracking::BoekHervatBeslis( const bool nuBeslissen )
    {
        if( !m_hervatWacht ) return;
        if( !m_kmStandVers && !nuBeslissen ) return;
        m_hervatWacht = false;

        if( m_kmStand <= 0.0 )
        {
            Logboek::Schrijf( "rit", "bus ride NOT picked up: no odometer reading to check it against" );
            return;
        }
        const bool zelfdeVoertuig = !m_voertuigMerk.empty() && m_hervatKandidaat.voertuigMerk == m_voertuigMerk
                                    && m_hervatKandidaat.voertuigModel == m_voertuigModel;
        if( !zelfdeVoertuig )
        {
            Logboek::Schrijf( "rit", "bus ride NOT picked up: the book was written with '" + m_hervatKandidaat.voertuigMerk + " "
                                        + m_hervatKandidaat.voertuigModel + "', this is '" + m_voertuigMerk + " " + m_voertuigModel + "'" );
            return;
        }
        LopendeRit o;
        if( !RitBoek::KnipTerug( m_hervatKandidaat, m_kmStand, o ) )
        {
            Logboek::Schrijf( "rit", "bus ride NOT picked up: odometer " + std::to_string( m_kmStand )
                                        + " does not fit the book (it covers "
                                        + std::to_string( m_hervatKandidaat.momenten.empty() ? m_hervatKandidaat.kmStand
                                                                                             : m_hervatKandidaat.momenten.front().kmStand )
                                        + " to " + std::to_string( m_hervatKandidaat.kmStand ) + ")" );
            return;
        }
        if( o.kmStand < m_hervatKandidaat.kmStand )
            Logboek::Schrijf( "rit", "the game came back at an older save: odometer " + std::to_string( m_kmStand )
                                        + ", the book went to " + std::to_string( m_hervatKandidaat.kmStand )
                                        + " -- bus ride continues from the moment at " + std::to_string( o.kmStand ) );

        if( !o.id.empty() ) m_huidigeRit.id = o.id;
        if( !o.startTijdIso.empty() ) m_huidigeRit.startTijdIso = o.startTijdIso;
        m_huidigeRit.boetes.insert( m_huidigeRit.boetes.begin(), o.boetes.begin(), o.boetes.end() );
        m_huidigeRit.doorgangen.insert( m_huidigeRit.doorgangen.begin(), o.doorgangen.begin(), o.doorgangen.end() );
        m_huidigeRit.boeteKosten += o.boeteKosten;
        m_huidigeRit.tolKosten += o.tolKosten;
        m_huidigeRit.veerbootKosten += o.veerbootKosten;
        m_huidigeRit.treinKosten += o.treinKosten;
        if( m_brandstof != nullptr )
        {
            FuelCosts::RitStand vooraf;
            vooraf.liters = o.liters; vooraf.kosten = o.kosten; vooraf.km = o.km;
            m_brandstof->HervatBusRit( vooraf, o.leegVoor, o.tankbeurten );
        }
        m_boekMomenten = o.momenten;
        Logboek::Schrijf( "rit", "bus ride PICKED UP after a restart -- started=" + m_huidigeRit.startTijdIso
                                    + " carried " + std::to_string( o.liters ) + " L, "
                                    + std::to_string( o.kosten ) + " cost, fines=" + std::to_string( o.boeteKosten )
                                    + " toll=" + std::to_string( o.tolKosten ) );
    }

    void BusTracking::BoekTik( const bool gepauzeerd )
    {
        if( m_boekSleutel.empty() || !m_actief ) return;
        const auto nu = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>( nu - m_boekLaatsteTik ).count();
        m_boekLaatsteTik = nu;

        if( m_hervatWacht )
        {
            if( !gepauzeerd && dt > 0.0 ) m_hervatWachtSec += std::min( dt, 1.0 );
            BoekHervatBeslis( m_hervatWachtSec >= HERVAT_WACHT_SECONDEN );
            if( !m_hervatWacht ) BoekBewaar( true );
            return;
        }
        BoekMoment( false );
        BoekBewaar( false );
    }

    RitMoment BusTracking::BoekStandNu() const
    {
        RitMoment m;
        m.kmStand = m_kmStandVers ? m_kmStand : 0.0;
        if( m_brandstof != nullptr && m_brandstof->BusRitActief() )
        {
            const FuelCosts::RitStand s = m_brandstof->BusRitStand();
            m.liters = s.liters; m.kosten = s.kosten; m.km = s.km;
            m.tankbeurten = static_cast<std::uint32_t>( m_brandstof->TankbeurtenBusRit().size() );
        }
        m.boetes = static_cast<std::uint32_t>( m_huidigeRit.boetes.size() );
        m.doorgangen = static_cast<std::uint32_t>( m_huidigeRit.doorgangen.size() );
        return m;
    }

    void BusTracking::BoekMoment( const bool nu )
    {
        if( !m_kmStandVers ) return;
        while( !m_boekMomenten.empty() && m_boekMomenten.back().kmStand > m_kmStand + 0.5 ) m_boekMomenten.pop_back();
        if( !nu && !m_boekMomenten.empty() && m_kmStand < m_boekMomenten.back().kmStand + RitBoek::MOMENT_KM ) return;
        RitBoek::VoegMomentToe( m_boekMomenten, BoekStandNu() );
    }

    void BusTracking::BoekBewaar( const bool forceer )
    {
        if( m_boekSleutel.empty() || !m_actief ) return;
        if( m_hervatWacht ) return;
        const auto nu = std::chrono::steady_clock::now();
        if( !forceer && std::chrono::duration<double>( nu - m_boekLaatstBewaard ).count() < 60.0 ) return;
        m_boekLaatstBewaard = nu;

        LopendeRit r;
        r.sleutel = m_boekSleutel;
        r.id = m_huidigeRit.id;
        r.startTijdIso = m_huidigeRit.startTijdIso;
        r.economyStartTijd = m_huidigeRit.economyStartTijd;
        r.voertuigMerk = m_voertuigMerk;
        r.voertuigModel = m_voertuigModel;
        const RitMoment nu_ = BoekStandNu();
        r.kmStand = nu_.kmStand;
        r.liters = nu_.liters; r.kosten = nu_.kosten; r.km = nu_.km;
        r.momenten = m_boekMomenten;
        if( m_brandstof != nullptr && m_brandstof->BusRitActief() )
        {
            r.leegVoor = m_brandstof->LeegVoorBusRit();
            r.tankbeurten = m_brandstof->TankbeurtenBusRit();
        }
        r.boetes = m_huidigeRit.boetes;
        r.doorgangen = m_huidigeRit.doorgangen;
        r.boeteKosten = m_huidigeRit.boeteKosten;
        r.tolKosten = m_huidigeRit.tolKosten;
        r.veerbootKosten = m_huidigeRit.veerbootKosten;
        r.treinKosten = m_huidigeRit.treinKosten;
        r.bewaardIso = NuAlsIso();
        RitBoek::Bewaar( RitBoek::Soort::Bus, r );
    }

    void BusTracking::BoekSluit()
    {
        m_huidigeRit.voertuigMerk = m_voertuigMerk;
        m_huidigeRit.voertuigModel = m_voertuigModel;

        BoekHervatBeslis( true );

        if( m_brandstof != nullptr && m_brandstof->BusRitActief() )
        {
            const FuelCosts::RitStand s = m_brandstof->SluitBusRitAf();
            m_huidigeRit.brandstofVerbruikLiters = s.liters;
            m_huidigeRit.brandstofKostenEuro = s.kosten;
        }
        if( !m_boekSleutel.empty() ) RitBoek::Wis( RitBoek::Soort::Bus );
        m_boekSleutel.clear();
        m_hervatWacht = false;

        Logboek::Schrijf( "rit", std::string( m_huidigeRit.status == TripStatus::Geannuleerd ? "bus ride CANCELLED" : "bus ride COMPLETED" )
            + " -- revenue=" + std::to_string( m_huidigeRit.inkomen )
            + " xp=" + std::to_string( m_huidigeRit.verdiendeXp )
            + " vehicle=" + ( m_voertuigMerk.empty() ? std::string( "?" ) : m_voertuigMerk + " " + m_voertuigModel )
            + " penalty=" + std::to_string( m_huidigeRit.annuleerBoete )
            + " fines=" + std::to_string( m_huidigeRit.boeteKosten )
            + " toll=" + std::to_string( m_huidigeRit.tolKosten )
            + " litres=" + std::to_string( m_huidigeRit.brandstofVerbruikLiters )
            + " fuelCost=" + std::to_string( m_huidigeRit.brandstofKostenEuro ) );
    }

    void BusTracking::BoekLaatLos()
    {
        if( m_boekSleutel.empty() ) return;
        BoekBewaar( true );
        if( m_brandstof != nullptr && m_brandstof->BusRitActief() ) m_brandstof->SluitBusRitAf();
        m_boekSleutel.clear();
        m_hervatWacht = false;
    }

    double BusTracking::ScsVertragingMinuten() const
    {
        if( !m_actief || !m_scsRit || m_huidigeRit.haltes.empty() ) return -1e9;

        std::size_t eerstvolgende = m_huidigeRit.haltes.size();
        for( std::size_t i = 0; i < m_huidigeRit.haltes.size(); ++i )
        {
            if( !m_huidigeRit.haltes[ i ].voltooid ) { eerstvolgende = i; break; }
        }

        if( eerstvolgende >= m_huidigeRit.haltes.size() )
        {
            const StopInfo &laatste = m_huidigeRit.haltes.back();
            if( laatste.werkelijkAankomst == 0 || laatste.geplandAankomst == 0 ) return -1e9;
            return static_cast<double>( laatste.werkelijkAankomst ) - static_cast<double>( laatste.geplandAankomst );
        }

        const StopInfo &halte = m_huidigeRit.haltes[ eerstvolgende ];
        if( halte.geplandAankomst == 0 || m_scsSpelTijd == 0 ) return -1e9;

        const double restEcht = GeschatteMinutenTotHalte( eerstvolgende );
        if( restEcht < 0.0 ) return -1e9;

        double vanaf = static_cast<double>( m_scsSpelTijd );
        if( halte.werkelijkVertrek == 0 && halte.geplandVertrek != 0
            && static_cast<double>( halte.geplandVertrek ) > vanaf )
        {
            vanaf = static_cast<double>( halte.geplandVertrek );
        }
        const double verwacht = vanaf + restEcht * TijdSchaal();
        return verwacht - static_cast<double>( halte.geplandAankomst );
    }

    void BusTracking::ScsEtappesOpnieuw()
    {
        m_scsEtappesExact = false;
        m_scsGemetenPositie = -1.0;
        m_etappeBekeken.clear();
        m_etappePogingen = 0;
        m_etappeGestopt = false;
        m_etappeVanaf = ( std::numeric_limits<std::int64_t>::min )();
        m_etappeVolgendePoging = std::chrono::steady_clock::now() + std::chrono::seconds( 3 );
    }

    void BusTracking::ScsZetExacteEtappes( const std::vector<std::uint32_t> &km )
    {
        if( km.size() != m_huidigeRit.haltes.size() ) return;
        double cumulatief = 0.0;
        for( std::size_t i = 0; i < km.size(); ++i )
        {
            cumulatief += static_cast<double>( km[ i ] );
            m_huidigeRit.haltes[ i ].geplandeAfstandKm = cumulatief;
        }
        m_scsEtappesExact = true;
    }

    void BusTracking::ScsEtappesVerwerk()
    {
        const auto nu = std::chrono::steady_clock::now();

        if( m_etappeBezig )
        {
            BusSaveLezer::Resultaat r;
            std::set<BusSaveLezer::Sleutel> bekeken;
            {
                std::lock_guard<std::mutex> slot( m_etappeMutex );
                if( !m_etappeKlaar ) return;
                r = std::move( m_etappeResultaat );
                bekeken = std::move( m_etappeBekekenUit );
                m_etappeKlaar = false;
            }
            m_etappeBezig = false;
            if( m_etappeDraad.joinable() ) m_etappeDraad.join();

            if( !m_actief || !m_scsRit || m_scsEtappesExact || m_etappeSleutel != m_scsSleutel ) return;
            m_etappeBekeken = std::move( bekeken );
            if( m_etappePogingen == 1 ) m_etappeVanaf = r.nieuwste;

            if( r.gevonden && r.etappeKm.size() == m_huidigeRit.haltes.size() )
            {
                ScsZetExacteEtappes( r.etappeKm );
                std::string som;
                for( std::size_t i = 0; i < r.etappeKm.size(); ++i )
                    som += ( i == 0 ? "" : "+" ) + std::to_string( r.etappeKm[ i ] );
                Logboek::Schrijf( "bus", "SCS legs from save -- source=" + r.bron + " save=" + r.save
                    + " legs=" + som + " total=" + std::to_string( m_scsGeplandeKm ) + " km"
                    + " attempt=" + std::to_string( m_etappePogingen ) );
                return;
            }

            const int wacht = m_etappePogingen == 1 ? 7 : ( m_etappePogingen == 2 ? 20 : 60 );
            m_etappeVolgendePoging = nu + std::chrono::seconds( wacht );
            if( m_etappePogingen <= ETAPPE_BREDE_POGINGEN || r.gelezen > 0 || !r.afwijking.empty() )
            {
                Logboek::Schrijf( "bus", "SCS legs not in a save yet -- attempt=" + std::to_string( m_etappePogingen )
                    + " read=" + std::to_string( r.gelezen )
                    + ( r.afwijking.empty() ? std::string() : " mismatch=" + r.afwijking )
                    + ( r.fout.empty() ? std::string() : " lastError=" + r.fout ) );
            }
            return;
        }

        if( !m_actief || !m_scsRit || m_scsEtappesExact || m_scsLeegGezien || m_etappeGestopt ) return;
        if( m_scsEtappes.empty() || m_scsGeplandeKm == 0 ) return;
        if( nu < m_etappeVolgendePoging ) return;

        std::vector<std::string> keten;
        keten.push_back( m_scsEtappes[ 0 ].bronId );
        for( std::size_t i = 0; i < m_scsEtappes.size(); ++i )
        {
            if( i > 0 && m_scsEtappes[ i ].bronId != m_scsEtappes[ i - 1 ].doelId ) { m_etappeGestopt = true; return; }
            keten.push_back( m_scsEtappes[ i ].doelId );
        }
        for( const std::string &halte : keten )
            if( halte.empty() ) { m_etappeGestopt = true; return; }

        ++m_etappePogingen;
        const bool breed = m_etappePogingen <= ETAPPE_BREDE_POGINGEN;
        const std::size_t maxSaves = breed ? 3 : 2;
        const std::int64_t vanaf = breed ? ( std::numeric_limits<std::int64_t>::min )() : m_etappeVanaf;
        const std::uint32_t totaalKm = m_scsGeplandeKm;
        m_etappeSleutel = m_scsSleutel;
        m_etappeBezig = true;
        {
            std::lock_guard<std::mutex> slot( m_etappeMutex );
            m_etappeKlaar = false;
        }

        try
        {
            m_etappeDraad = std::thread( [ this, keten, totaalKm, maxSaves, vanaf, bekeken = m_etappeBekeken ]() mutable
            {
                BusSaveLezer::Resultaat r;
                try { r = BusSaveLezer::Zoek( BusSaveLezer::SpelMap(), keten, totaalKm, bekeken, maxSaves, vanaf ); }
                catch( ... ) { r = BusSaveLezer::Resultaat{}; r.fout = "exception while reading"; }
                std::lock_guard<std::mutex> slot( m_etappeMutex );
                m_etappeResultaat = std::move( r );
                m_etappeBekekenUit = std::move( bekeken );
                m_etappeKlaar = true;
            } );
        }
        catch( ... )
        {
            m_etappeBezig = false;
            m_etappeGestopt = true;
            Logboek::Schrijf( "bus", "SCS legs: background thread could not start, no kilometres per stop" );
        }
    }

    double BusTracking::ScsResterendeKmTotHalte( const std::size_t index ) const
    {
        if( !m_actief || !m_scsRit || index >= m_huidigeRit.haltes.size() ) return -1.0;
        if( m_huidigeRit.haltes[ index ].voltooid ) return -1.0;
        if( m_navAfstandKm <= 0.0 ) return -1.0;

        const bool laatste = ( index + 1 == m_huidigeRit.haltes.size() );
        if( !laatste && !m_scsEtappesExact ) return -1.0;

        const double erna = m_huidigeRit.haltes.back().geplandeAfstandKm
                            - m_huidigeRit.haltes[ index ].geplandeAfstandKm;
        const double rest = m_navAfstandKm - erna;
        return rest > 0.0 ? rest : 0.0;
    }
}

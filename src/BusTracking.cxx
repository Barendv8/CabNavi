#include "BusTracking.hxx"

#include "CallbackHulp.hxx"
#include "Logboek.hxx"

#include <algorithm>
#include <chrono>
#include <ctime>
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

    // BusJobCancelledEvent::GetReason() returns an enumerated type
    // (TruckersMP::BusJobCancellationReason), not text -- this turns it
    // into something readable for the log/dashboard.
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
        m_busModule = TruckersMP::BusModule::Attach( session );
        if( !m_busModule )
        {
            // Bus module not available (e.g. older client, or intent not
            // unlocked). Bus tracking then simply stays empty; cargo tracking
            // works independently.
            return;
        }

        // Mid-session start: if the plugin reloads while a job is already
        // running, pick up the current state right away (see "Initialization
        // and reloading" in the SDK docs).
        // Only pick it up if there REALLY is a job. Passing an empty BusJob
        // object and calling getters on it reads an invalid module reference
        // -- the same trap as with the player radar.
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
                m_huidigeRit.haltes[ i ].geplandeAfstandKm = cumulatief;  // from trip start, cumulative
                m_huidigeRit.geplandeAfstandKm += afstandNaarDezeHalte;

                // GetScheduledTime() is the time from the PREVIOUS stop (per the bus
                // module docs), so add it up just like the distance.
                cumulatiefTijd += static_cast<double>( stops[ i ].GetScheduledTime().value_or( 0u ) );
                m_huidigeRit.haltes[ i ].geplandeTijdMin = cumulatiefTijd;
                m_huidigeRit.haltes[ i ].instappers  = stops[ i ].GetBoardingPassengers().value_or( 0 );
                m_huidigeRit.haltes[ i ].uitstappers = stops[ i ].GetLeavingPassengers().value_or( 0 );

                // Record per stop what the game gives us. Needed to check afterwards
                // whether the estimate to FURTHER stops is right: CabNavi computes
                // that itself with this planned distance, while the next stop simply
                // uses the GPS time.
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
            // Update the number on board. GetPassengerCount() gives who is in
            // the bus NOW, not a total for the trip -- so ask again after every
            // stop, otherwise the figure sticks at the departure count.
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
                m_liveKmSindsLaatsteHalte = 0.0;  // counter reset, this leg is now officially counted

                // Record the check-off moment, with the ELAPSED time since trip
                // start. That lets a prediction from an earlier line be placed next
                // to the actual moment.
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
            m_logger.RegisterVoltooideRit( m_huidigeRit );
            m_huidigeRit = Trip{};
            m_actief = false;
        } ) );
    }

    void BusTracking::StartRecord( const TruckersMP::BusJob &job )
    {
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
        m_gladdeSchattingMin = -1.0;  // fresh trip, fresh estimate
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
        // Track pause transitions regardless of whether there is an active
        // trip (like cargo -- the simulation pauses independently of
        // whichever job you happen to have).
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

        if( !m_actief || gepauzeerd )
        {
            return;
        }

        m_huidigeRit.huidigeSnelheidKmh = snelheidKmh;

        // Update the live odometer since the last completed stop (speed x
        // elapsed time since the previous reading).
        if( m_laatsteSnelheidMeting.time_since_epoch().count() != 0 )
        {
            double verstrekenUur = std::chrono::duration<double>( nu - m_laatsteSnelheidMeting ).count() / 3600.0;
            if( verstrekenUur > 0.0 && verstrekenUur < 0.1 )  // sanity check against odd jumps
            {
                m_liveKmSindsLaatsteHalte += snelheidKmh * verstrekenUur;
            }
        }
        m_laatsteSnelheidMeting = nu;

        // Update the moving-average window of speeds.
        m_snelheidVenster.emplace_back( nu, snelheidKmh );
        while( !m_snelheidVenster.empty()
               && std::chrono::duration<double>( nu - m_snelheidVenster.front().first ).count() > VENSTER_SECONDEN )
        {
            m_snelheidVenster.pop_front();
        }

        // Every ten seconds write the PREDICTION per stop. Without this you
        // cannot check afterwards what was predicted while you were still on
        // the way -- and that is exactly the question for stops beyond the
        // next one, which CabNavi computes itself.
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
        // Only calibrate on a REAL change: the channel refreshes once per
        // economy minute, so seeing the same value several times in between
        // says nothing about the speed.
        if( minuten != m_economyTijd )
        {
            const auto nu = std::chrono::steady_clock::now();
            // On a big jump (server sync, rest) start measuring again, and
            // refresh the anchor every 15 minutes anyway -- otherwise an old jump
            // keeps having effect forever.
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
        // See the explanation at TruckTracking::TijdSchaal: fixed TruckersMP
        // scale as the base, measurement only as a correction within
        // plausible bounds.
        constexpr double STANDAARD_SCHAAL = 6.0;
        if( m_vastgezetteSchaal > 0.0 ) return m_vastgezetteSchaal;
        if( !m_schaalGestart ) return STANDAARD_SCHAAL;

        const double echteMinuten =
            std::chrono::duration<double>( std::chrono::steady_clock::now() - m_schaalEersteEcht ).count() / 60.0;
        // Below half a real minute the measurement is too coarse (the channel
        // jumps in whole minutes); then better to claim nothing.
        if( echteMinuten < 2.0 ) return STANDAARD_SCHAAL;

        const double economyMinuten =
            static_cast<double>( m_economyTijd ) - static_cast<double>( m_schaalEersteEconomy );
        if( economyMinuten <= 0.0 ) return STANDAARD_SCHAAL;

        const double gemeten = economyMinuten / echteMinuten;
        // Wide enough for TruckersMP (6) and singleplayer (~19); anything
        // outside is a time jump, not a real scale. See the explanation at
        // TruckTracking::TijdSchaal.
        if( gemeten < 3.0 || gemeten > 25.0 ) return STANDAARD_SCHAAL;
        if( echteMinuten >= 5.0 ) m_vastgezetteSchaal = gemeten;  // locked from now on
        return gemeten;
    }

    void BusTracking::ZetNavigatie( double navTijdRuw, double navAfstandKm )
    {
        m_navTijdRuw = navTijdRuw;
        m_navAfstandKm = navAfstandKm;
    }

    double BusTracking::Gladstrijken( double ruweMinuten ) const
    {
        // Same setup as the cargo trip: adjust gradually, but take over a
        // big jump immediately (new route, different stop).
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
        // Same chain as in GeschatteResterendeMinutenEcht, but only the
        // speed part. Everything in km per REAL hour.
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

        // Find the next stop not yet completed: that is our starting point.
        std::size_t eerstvolgende = m_huidigeRit.haltes.size();
        for( std::size_t i = 0; i < m_huidigeRit.haltes.size(); ++i )
        {
            if( !m_huidigeRit.haltes[ i ].voltooid ) { eerstvolgende = i; break; }
        }
        if( eerstvolgende >= m_huidigeRit.haltes.size() ) return -1.0;

        // Up to that first stop we use the existing estimate -- including the
        // GPS ETA, because that is where the game navigates to.
        const double basis = GeschatteResterendeMinutenEcht();
        if( basis < 0.0 ) return -1.0;
        if( index == eerstvolgende ) return basis;

        // Further on: convert the extra distance. That extra distance is a
        // difference between two PLANNED distances, so it is correct anyway
        // -- our own odometer plays no role there.
        const double extraKm = m_huidigeRit.haltes[ index ].geplandeAfstandKm
                                - m_huidigeRit.haltes[ eerstvolgende ].geplandeAfstandKm;
        if( extraKm <= 0.0 ) return basis;

        // Preferably the travel speed that follows from the GPS: more stable
        // than our own measurement and does not change when you stand at a
        // stop. Same source as the estimate to the next stop, so the times
        // agree with each other.
        double snelheidEcht = 0.0;
        if( m_navTijdRuw > 0.0 && m_navAfstandKm > 1.0 )
        {
            const double spelUren = m_navTijdRuw / 3600.0;
            const double kmhSpel = spelUren > 0.0 ? m_navAfstandKm / spelUren : 0.0;
            if( kmhSpel >= 3.0 && kmhSpel <= 200.0 )
            {
                snelheidEcht = kmhSpel * TijdSchaal();  // km per REAL hour
            }
        }
        if( snelheidEcht <= 0.0 ) snelheidEcht = EffectieveSnelheidEcht();

        return basis + ( extraKm / snelheidEcht ) * 60.0;
    }

    double BusTracking::GeschatteVertragingMinuten() const
    {
        if( !m_actief || m_huidigeRit.haltes.empty() ) return -1e9;

        // Deadline = start time + planned time to the LAST stop. Only that
        // one counts for the penalty.
        const double geplandTotEind = m_huidigeRit.haltes.back().geplandeTijdMin;
        if( geplandTotEind <= 0.0 ) return -1e9;  // navigation data not ready yet

        const double deadline =
            static_cast<double>( m_huidigeRit.economyStartTijd ) + geplandTotEind;
        const double economyNu = static_cast<double>( m_economyTijd );

        const double resterendeKm =
            m_huidigeRit.geplandeAfstandKm - m_huidigeRit.afgelegdeAfstandKm - m_liveKmSindsLaatsteHalte;
        if( resterendeKm <= 0.0 )
        {
            // Already at the end point: delay is purely the difference with the
            // deadline.
            return economyNu - deadline;
        }

        // EffectieveSnelheidEcht() gives km per REAL hour; dividing by the
        // time-scale conversion gives game minutes again, because the
        // deadline is in game minutes too.
        const double vEcht = EffectieveSnelheidEcht();
        if( vEcht <= 0.0 ) return -1e9;

        const double echteMinutenNodig = ( resterendeKm / vEcht ) * 60.0;
        const double spelMinutenNodig = echteMinutenNodig * TijdSchaal();
        const double verwachteAankomst = economyNu + spelMinutenNodig;

        return verwachteAankomst - deadline;
    }

    double BusTracking::GeschatteBoetePercentage() const
    {
        const double vertraging = GeschatteVertragingMinuten();
        if( vertraging <= -1e8 ) return 0.0;  // not determinable

        // First 60 minutes are free.
        const double strafbaar = vertraging - 60.0;
        if( strafbaar <= 0.0 ) return 0.0;

        // 0.333% per minute, capped at 100% (after ~300 penalised minutes).
        return std::min( 100.0, strafbaar * 0.333 );
    }

    double BusTracking::GeschatteResterendeMinutenEcht() const
    {
        if( !m_actief ) return -1.0;

        // Remaining distance to the next uncompleted stop (not to the end
        // point of the whole line -- per stop is more useful).
        double geplandTotEerstvolgende = -1.0;
        for( const StopInfo &s : m_huidigeRit.haltes )
        {
            if( !s.voltooid )
            {
                geplandTotEerstvolgende = s.geplandeAfstandKm;
                break;
            }
        }
        if( geplandTotEerstvolgende < 0.0 ) return -1.0;  // all stops already completed

        // IMPORTANT -- this is where the bus differs from the cargo trip.
        //
        // On a cargo trip the navigation points to your destination, so
        // navigation.distance and navigation.time can be used directly. On a
        // BUS LINE the navigation points to the END POINT of the line: the
        // game reported 374 km and 3h05 while the next stop was 1 km away.
        // Taking those numbers as "to the next stop" gave 31 minutes for a
        // one-minute hop.
        //
        // The distance to the next stop therefore comes from our own
        // bookkeeping. Below, the GPS is only used for the SPEED that follows
        // from it -- that is usable, because it holds for the whole route.
        // How much has been driven already? Our own sum proved unreliable: at
        // arrival in Amsterdam it was still zero with 156 km behind us. So we
        // derive it from the GPS, which does know:
        //
        //   driven = total line length - what the GPS still has to go
        //
        // The GPS distance goes to the END POINT of the line, and the planned
        // distance of the last stop is exactly that same end point -- so the
        // two belong together. Only when the GPS is silent do we fall back on
        // our own counter.
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
        // Below 300 metres you are practically on top of it; then "0 min" is
        // a more honest answer than the minute or two the GPS still holds.
        if( resterendeKm <= 0.3 ) return 0.0;

        // ---- Source 0: the travel speed that follows from the GPS ---------
        //
        // HERE THE BUS DEVIATES FROM THE CARGO TRIP, and it has to.
        //
        // On a cargo trip the navigation points to your destination, so
        // navigation.time can be taken directly as "time to arrival". On a
        // BUS LINE the navigation points to the END POINT of the line: the
        // game reported 374 km and 3h05 while the next stop was 1 km away.
        // Taking that time gave 31 minutes for a one-minute hop -- the
        // distance did not matter at all.
        //
        // What we DO take from the GPS is the RATIO distance/time. That is
        // the speed the game reckons with, and it holds for the whole route.
        // With it we convert our own distance to the next stop. More stable
        // than our own speed reading, because that ratio does not change
        // when you stand at a stop.
        if( m_navTijdRuw > 0.0 && m_navAfstandKm > 1.0 )
        {
            const double schaal = TijdSchaal();
            const double spelUren = m_navTijdRuw / 3600.0;  // assumption: seconds

            double snelheid = spelUren > 0.0 ? m_navAfstandKm / spelUren : 0.0;
            if( snelheid < 3.0 || snelheid > 200.0 )
            {
                // Impossible speed -> probably minutes after all.
                const double alsMinuten = m_navTijdRuw / 60.0;
                const double snelheidMin = alsMinuten > 0.0 ? m_navAfstandKm / alsMinuten : 0.0;
                if( snelheidMin >= 3.0 && snelheidMin <= 200.0 ) snelheid = snelheidMin;
            }

            if( snelheid >= 3.0 && snelheid <= 200.0 )
            {
                // Speed is km per GAME hour; dividing by the time scale makes real
                // minutes of it.
                return Gladstrijken( ( resterendeKm / snelheid ) * 60.0 / schaal );
            }
        }

        double gemiddeldeSnelheid = 0.0;

        // 1) Preferably: moving average of the last ~3 minutes. For the bus
        //    that is a window of speed readings; they are in km per GAME
        //    hour, so we convert to km per REAL hour -- so the unit matches
        //    the cargo trip.
        if( m_snelheidVenster.size() >= 2 )
        {
            double som = 0.0;
            for( const auto &sample : m_snelheidVenster ) som += sample.second;
            gemiddeldeSnelheid = ( som / m_snelheidVenster.size() ) * TijdSchaal();
        }

        // 2) Otherwise: average over the whole trip so far.
        if( gemiddeldeSnelheid < 1.0 )
        {
            const double verstrekenMin = VerstrekenMinutenEcht();
            const double gereden = m_huidigeRit.afgelegdeAfstandKm + m_liveKmSindsLaatsteHalte;
            if( verstrekenMin > 0.5 && gereden > 0.5 )
            {
                gemiddeldeSnelheid = gereden / ( verstrekenMin / 60.0 );
            }
        }

        // 3) Otherwise: the speedometer at this moment, converted to km per
        //    REAL hour. Mixing those two up is off by a factor of six.
        if( gemiddeldeSnelheid < 1.0 )
        {
            gemiddeldeSnelheid = m_huidigeRit.huidigeSnelheidKmh * TijdSchaal();
        }

        // Floor under the speed. Without this a bus standing still (at a
        // stop) divides by almost zero and you get nonsense -- or "unknown".
        const double BODEM_SNELHEID_ECHT = 40.0 * TijdSchaal();  // 40 km/h on the speedometer
        if( gemiddeldeSnelheid < BODEM_SNELHEID_ECHT )
        {
            if( m_gladdeSchattingMin > 0.0 )
            {
                return m_gladdeSchattingMin;  // keep the last known value
            }
            gemiddeldeSnelheid = BODEM_SNELHEID_ECHT;
        }

        return Gladstrijken( ( resterendeKm / gemiddeldeSnelheid ) * 60.0 );
    }
}

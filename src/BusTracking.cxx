#include "BusTracking.hxx"

#include "CallbackHulp.hxx"

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

    // BusJobCancelledEvent::GetReason() geeft een genummerd type terug
    // (TruckersMP::BusJobCancellationReason), geen tekst -- deze zet het om
    // naar iets leesbaars voor het logboek/dashboard.
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
            // Bus-module niet beschikbaar (bv. oudere client, of intent niet
            // ontgrendeld). Bus-tracking blijft dan simpelweg leeg;
            // vracht-tracking werkt onafhankelijk.
            return;
        }

        // Mid-sessie start: als de plugin herlaadt terwijl er al een job
        // loopt, meteen de huidige status oppikken (zie "Initialization and
        // reloading" in de SDK-docs).
        // Alleen oppakken als er ECHT een job is. Een leeg BusJob-object
        // doorgeven en daar getters op aanroepen leest een ongeldige
        // moduleverwijzing -- dezelfde valstrik als bij de spelersradar.
        if( const auto lopendeJob = m_busModule->GetJob() )
        {
            StartRecord( *lopendeJob );
        }

        m_busModule->OnJobStarted.Register( Beschermd( "OnJobStarted", [ this ]( TruckersMP::BusJobStartedEvent &e )
        {
            StartRecord( e.GetJob() );
            m_huidigeRit.geschatUitbetaling = e.GetEstimatedPayout();
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
                m_huidigeRit.haltes[ i ].geplandeAfstandKm = cumulatief; // vanaf ritstart, cumulatief
                m_huidigeRit.geplandeAfstandKm += afstandNaarDezeHalte;

                // GetScheduledTime() is de tijd vanaf de VORIGE halte (zo staat
                // het in de busmodule-docs), dus net als de afstand optellen.
                cumulatiefTijd += static_cast<double>( stops[ i ].GetScheduledTime().value_or( 0u ) );
                m_huidigeRit.haltes[ i ].geplandeTijdMin = cumulatiefTijd;
            }
        } ) );

        m_busModule->OnStopCompleted.Register( Beschermd( "OnStopCompleted", [ this ]( TruckersMP::BusStopCompletedEvent &e )
        {
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
                m_liveKmSindsLaatsteHalte = 0.0; // teller reset, deze etappe is nu officieel meegeteld
                break;
            }
        } ) );

        m_busModule->OnJobFinished.Register( Beschermd( "OnJobFinished", [ this ]( TruckersMP::BusJobFinishedEvent &e )
        {
            m_huidigeRit.status = TripStatus::Voltooid;
            m_huidigeRit.eindTijdIso = NuAlsIso();
            m_huidigeRit.economyEindTijd = m_economyTijd;
            m_huidigeRit.geschatUitbetaling = e.GetPayout();
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
        m_gladdeSchattingMin = -1.0; // verse rit, verse schatting
        m_huidigeRit.type = TripType::Bus;
        m_huidigeRit.status = TripStatus::Bezig;
        m_huidigeRit.id = "bus-" + NuAlsIso();
        m_huidigeRit.startTijdIso = NuAlsIso();
        m_huidigeRit.economyStartTijd = job.GetStartTime().value_or( 0 );

        for( const TruckersMP::BusStop &stop : job.GetStops().value_or( std::vector<TruckersMP::BusStop>{} ) )
        {
            StopInfo info;
            info.naam = stop.GetName().value_or( "" );
            info.cityIdentifier = stop.GetCityIdentifier().value_or( "" );
            m_huidigeRit.haltes.push_back( std::move( info ) );
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
        // Pauze-overgangen bijhouden, ongeacht of er een actieve rit is
        // (net als bij vracht -- de simulatie pauzeert los van welke job je
        // toevallig hebt).
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

        // Live kilometerteller sinds de laatste voltooide halte bijwerken
        // (snelheid x verstreken tijd sinds vorige meting).
        if( m_laatsteSnelheidMeting.time_since_epoch().count() != 0 )
        {
            double verstrekenUur = std::chrono::duration<double>( nu - m_laatsteSnelheidMeting ).count() / 3600.0;
            if( verstrekenUur > 0.0 && verstrekenUur < 0.1 ) // sanity check tegen rare sprongen
            {
                m_liveKmSindsLaatsteHalte += snelheidKmh * verstrekenUur;
            }
        }
        m_laatsteSnelheidMeting = nu;

        // Voortschrijdend-gemiddelde-venster van snelheden bijwerken.
        m_snelheidVenster.emplace_back( nu, snelheidKmh );
        while( !m_snelheidVenster.empty()
               && std::chrono::duration<double>( nu - m_snelheidVenster.front().first ).count() > VENSTER_SECONDEN )
        {
            m_snelheidVenster.pop_front();
        }
    }

    void BusTracking::ZetEconomyTijd( std::uint32_t minuten )
    {
        // Alleen ijken op een ECHTE verandering: het kanaal ververst eens per
        // economy-minuut, dus tussendoor dezelfde waarde meerdere keren zien
        // zegt niets over de snelheid.
        if( minuten != m_economyTijd )
        {
            const auto nu = std::chrono::steady_clock::now();
            // Bij een grote sprong (serversynchronisatie, rust) opnieuw
            // beginnen met meten, en het anker sowieso elke 15 minuten
            // verversen -- anders blijft een oude sprong eeuwig doorwerken.
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
        // Zie de uitleg bij TruckTracking::TijdSchaal: vaste TruckersMP-schaal
        // als basis, meting alleen als correctie binnen geloofwaardige grenzen.
        constexpr double STANDAARD_SCHAAL = 6.0;
        if( m_vastgezetteSchaal > 0.0 ) return m_vastgezetteSchaal;
        if( !m_schaalGestart ) return STANDAARD_SCHAAL;

        const double echteMinuten =
            std::chrono::duration<double>( std::chrono::steady_clock::now() - m_schaalEersteEcht ).count() / 60.0;
        // Onder een halve echte minuut is de meting te grof (het kanaal
        // springt met hele minuten); dan liever niets beweren.
        if( echteMinuten < 2.0 ) return STANDAARD_SCHAAL;

        const double economyMinuten =
            static_cast<double>( m_economyTijd ) - static_cast<double>( m_schaalEersteEconomy );
        if( economyMinuten <= 0.0 ) return STANDAARD_SCHAAL;

        const double gemeten = economyMinuten / echteMinuten;
        // Ruim genoeg voor TruckersMP (6) en singleplayer (~19); alles
        // daarbuiten is een tijdsprong, geen echte schaal. Zie de uitleg bij
        // TruckTracking::TijdSchaal.
        if( gemeten < 3.0 || gemeten > 25.0 ) return STANDAARD_SCHAAL;
        if( echteMinuten >= 5.0 ) m_vastgezetteSchaal = gemeten; // vanaf nu vast
        return gemeten;
    }

    void BusTracking::ZetNavigatie( double navTijdRuw, double navAfstandKm )
    {
        m_navTijdRuw = navTijdRuw;
        m_navAfstandKm = navAfstandKm;
    }

    double BusTracking::Gladstrijken( double ruweMinuten ) const
    {
        // Zelfde opzet als bij de vrachtrit: geleidelijk bijsturen, maar een
        // grote sprong meteen overnemen (nieuwe route, andere halte).
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
        // Zelfde keten als in GeschatteResterendeMinutenEcht, maar dan alleen
        // het snelheidsdeel. Alles in km per ECHT uur.
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

        // Eerstvolgende nog niet voltooide halte zoeken: die is ons vertrekpunt.
        std::size_t eerstvolgende = m_huidigeRit.haltes.size();
        for( std::size_t i = 0; i < m_huidigeRit.haltes.size(); ++i )
        {
            if( !m_huidigeRit.haltes[ i ].voltooid ) { eerstvolgende = i; break; }
        }
        if( eerstvolgende >= m_huidigeRit.haltes.size() ) return -1.0;

        // Tot die eerste halte gebruiken we de bestaande schatting -- inclusief
        // de GPS-ETA, want daar navigeert het spel naartoe.
        const double basis = GeschatteResterendeMinutenEcht();
        if( basis < 0.0 ) return -1.0;
        if( index == eerstvolgende ) return basis;

        // Verderop: de extra afstand omrekenen. Die extra afstand is een
        // verschil tussen twee GEPLANDE afstanden, dus die klopt sowieso --
        // daar speelt onze eigen kilometerteller geen rol in.
        const double extraKm = m_huidigeRit.haltes[ index ].geplandeAfstandKm
                                - m_huidigeRit.haltes[ eerstvolgende ].geplandeAfstandKm;
        if( extraKm <= 0.0 ) return basis;

        // Bij voorkeur de reissnelheid die uit de GPS volgt: die is stabieler
        // dan onze eigen meting en verandert niet als je bij een halte
        // stilstaat. Zelfde bron als de schatting tot de eerstvolgende halte,
        // zodat de tijden onderling kloppen.
        double snelheidEcht = 0.0;
        if( m_navTijdRuw > 0.0 && m_navAfstandKm > 1.0 )
        {
            const double spelUren = m_navTijdRuw / 3600.0;
            const double kmhSpel = spelUren > 0.0 ? m_navAfstandKm / spelUren : 0.0;
            if( kmhSpel >= 3.0 && kmhSpel <= 200.0 )
            {
                snelheidEcht = kmhSpel * TijdSchaal(); // km per ECHT uur
            }
        }
        if( snelheidEcht <= 0.0 ) snelheidEcht = EffectieveSnelheidEcht();

        return basis + ( extraKm / snelheidEcht ) * 60.0;
    }

    double BusTracking::GeschatteVertragingMinuten() const
    {
        if( !m_actief || m_huidigeRit.haltes.empty() ) return -1e9;

        // Deadline = starttijd + geplande tijd tot de LAATSTE halte. Alleen
        // die telt voor de boete.
        const double geplandTotEind = m_huidigeRit.haltes.back().geplandeTijdMin;
        if( geplandTotEind <= 0.0 ) return -1e9; // navigatiedata nog niet klaar

        const double deadline =
            static_cast<double>( m_huidigeRit.economyStartTijd ) + geplandTotEind;
        const double economyNu = static_cast<double>( m_economyTijd );

        const double resterendeKm =
            m_huidigeRit.geplandeAfstandKm - m_huidigeRit.afgelegdeAfstandKm - m_liveKmSindsLaatsteHalte;
        if( resterendeKm <= 0.0 )
        {
            // Al bij het eindpunt: vertraging is puur het verschil met de deadline.
            return economyNu - deadline;
        }

        // EffectieveSnelheidEcht() geeft km per ECHT uur; delen door de
        // tijdschaal-omrekening levert weer spelminuten, want de deadline
        // staat ook in spelminuten.
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
        if( vertraging <= -1e8 ) return 0.0; // niet te bepalen

        // Eerste 60 minuten zijn gratis.
        const double strafbaar = vertraging - 60.0;
        if( strafbaar <= 0.0 ) return 0.0;

        // 0,333% per minuut, afgetopt op 100% (na ~300 strafbare minuten).
        return std::min( 100.0, strafbaar * 0.333 );
    }

    double BusTracking::GeschatteResterendeMinutenEcht() const
    {
        if( !m_actief ) return -1.0;

        // Resterende afstand tot de eerstvolgende niet-voltooide halte
        // (niet tot het eindpunt van de hele lijn -- per-halte is nuttiger).
        double geplandTotEerstvolgende = -1.0;
        for( const StopInfo &s : m_huidigeRit.haltes )
        {
            if( !s.voltooid )
            {
                geplandTotEerstvolgende = s.geplandeAfstandKm;
                break;
            }
        }
        if( geplandTotEerstvolgende < 0.0 ) return -1.0; // alle haltes al voltooid

        // BELANGRIJK -- hier zit het verschil met de vrachtrit.
        //
        // Bij een vrachtrit wijst de navigatie naar je bestemming, dus daar
        // kun je navigation.distance en navigation.time rechtstreeks
        // gebruiken. Bij een BUSLIJN wijst de navigatie naar het EINDPUNT van
        // de lijn: het spel meldde 374 km en 3u05 terwijl de eerstvolgende
        // halte op 1 km lag. Die getallen als "tot de volgende halte" nemen
        // gaf 31 minuten voor een ritje van een minuut.
        //
        // De afstand tot de volgende halte komt daarom uit onze eigen
        // boekhouding. De GPS gebruiken we hieronder alleen nog voor de
        // SNELHEID die eruit volgt -- dat is wel bruikbaar, want die geldt
        // voor de hele route.
        // Hoeveel is er al gereden? Onze eigen optelsom bleek niet te
        // vertrouwen: bij aankomst in Amsterdam stond die nog op nul, terwijl
        // er 156 km op zat. Daarom leiden we het af uit de GPS, die het wel
        // weet:
        //
        //   gereden = totale lijnlengte - wat de GPS nog te gaan geeft
        //
        // De GPS-afstand gaat naar het EINDPUNT van de lijn, en de geplande
        // afstand van de laatste halte is precies datzelfde eindpunt -- dus
        // die twee horen bij elkaar. Alleen als de GPS zwijgt vallen we terug
        // op onze eigen teller.
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
        // Onder de 300 meter sta je er praktisch bovenop; dan is "0 min" een
        // eerlijker antwoord dan een minuut of twee die de GPS nog overhoudt.
        if( resterendeKm <= 0.3 ) return 0.0;

        // ---- Bron 0: de reissnelheid die uit de GPS volgt ---------------
        //
        // HIER WIJKT DE BUS AF VAN DE VRACHTRIT, en dat moet ook.
        //
        // Bij een vrachtrit wijst de navigatie naar je bestemming, dus daar
        // kun je navigation.time rechtstreeks als "tijd tot aankomst" nemen.
        // Bij een BUSLIJN wijst de navigatie naar het EINDPUNT van de lijn:
        // het spel meldde 374 km en 3u05 terwijl de eerstvolgende halte op
        // 1 km lag. Die tijd overnemen gaf 31 minuten voor een ritje van een
        // minuut -- de afstand deed dan helemaal niet mee.
        //
        // Wat we WEL uit de GPS halen is de VERHOUDING afstand/tijd. Dat is
        // de snelheid waarmee het spel rekent, en die geldt voor de hele
        // route. Daarmee rekenen we onze eigen afstand tot de volgende halte
        // om. Stabieler dan onze eigen snelheidsmeting, want die verhouding
        // verandert niet als je bij een halte stilstaat.
        if( m_navTijdRuw > 0.0 && m_navAfstandKm > 1.0 )
        {
            const double schaal = TijdSchaal();
            const double spelUren = m_navTijdRuw / 3600.0; // aanname: seconden

            double snelheid = spelUren > 0.0 ? m_navAfstandKm / spelUren : 0.0;
            if( snelheid < 3.0 || snelheid > 200.0 )
            {
                // Onmogelijke snelheid -> waarschijnlijk toch minuten.
                const double alsMinuten = m_navTijdRuw / 60.0;
                const double snelheidMin = alsMinuten > 0.0 ? m_navAfstandKm / alsMinuten : 0.0;
                if( snelheidMin >= 3.0 && snelheidMin <= 200.0 ) snelheid = snelheidMin;
            }

            if( snelheid >= 3.0 && snelheid <= 200.0 )
            {
                // Snelheid is km per SPELuur; delen door de tijdschaal maakt
                // er echte minuten van.
                return Gladstrijken( ( resterendeKm / snelheid ) * 60.0 / schaal );
            }
        }

        double gemiddeldeSnelheid = 0.0;

        // 1) Bij voorkeur: voortschrijdend gemiddelde van de laatste ~3
        //    minuten. Bij de bus is dat een venster van snelheidsmetingen;
        //    die staan in km per SPELuur, dus we rekenen ze om naar km per
        //    ECHT uur -- zodat de eenheid gelijk is aan die van de vrachtrit.
        if( m_snelheidVenster.size() >= 2 )
        {
            double som = 0.0;
            for( const auto &sample : m_snelheidVenster ) som += sample.second;
            gemiddeldeSnelheid = ( som / m_snelheidVenster.size() ) * TijdSchaal();
        }

        // 2) Anders: gemiddelde over de hele rit tot nu toe.
        if( gemiddeldeSnelheid < 1.0 )
        {
            const double verstrekenMin = VerstrekenMinutenEcht();
            const double gereden = m_huidigeRit.afgelegdeAfstandKm + m_liveKmSindsLaatsteHalte;
            if( verstrekenMin > 0.5 && gereden > 0.5 )
            {
                gemiddeldeSnelheid = gereden / ( verstrekenMin / 60.0 );
            }
        }

        // 3) Anders: de snelheidsmeter van dit moment, omgerekend naar km per
        //    ECHT uur. Die twee door elkaar halen scheelt een factor zes.
        if( gemiddeldeSnelheid < 1.0 )
        {
            gemiddeldeSnelheid = m_huidigeRit.huidigeSnelheidKmh * TijdSchaal();
        }

        // Bodem onder de snelheid. Zonder dit deelt een stilstaande bus (bij
        // een halte) door bijna nul en krijg je onzin -- of "onbekend".
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
}

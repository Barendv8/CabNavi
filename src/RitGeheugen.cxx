#include "RitGeheugen.hxx"

#include <chrono>
#include <cmath>

namespace Ritten
{
    namespace
    {
        long long Nu()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch() ).count();
        }

        double Steady()
        {
            return std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
        }
    }

    void RitGeheugen::Reset()
    {
        m_top = 0.0;
        m_route.clear();
        m_laatstePuntTijd = 0;
        m_laatstePuntBekend = false;
        m_overtredingen.clear();
        m_inOvertreding = false;
        m_bovenLimietSinds = -1.0;
        m_teleports = 0;
    }

    void RitGeheugen::Voed( double snelheidKmh, double limietKmh, bool posBekend, double x, double z, bool ritActief )
    {
        std::lock_guard<std::mutex> lock( m_mutex );

        // A trip starts: forget the previous one. A trip ends: keep everything
        // until the next start, so the completion callback can read it.
        if( ritActief && !m_ritWasActief ) Reset();
        m_ritWasActief = ritActief;
        if( !ritActief ) return;

        const long long nu = Nu();
        const double st = Steady();

        // --- top speed ---
        if( snelheidKmh > m_top ) m_top = snelheidKmh;

        // --- route, one point per interval; teleport check on the way ---
        if( posBekend && ( m_route.empty() || nu - m_laatstePuntTijd >= static_cast<long long>( ROUTE_INTERVAL_SEC ) ) )
        {
            if( m_laatstePuntBekend )
            {
                const double dx = x - m_laatsteX, dz = z - m_laatsteZ;
                const double afstandM = std::sqrt( dx * dx + dz * dz );
                const double dt = static_cast<double>( nu - m_laatstePuntTijd );
                if( afstandM > TELEPORT_MIN_M && dt > 0.0 )
                {
                    // What speed would this jump need? Beyond any truck: a
                    // teleport (F7 to service, a save reload, a TMP /fix).
                    const double impliedKmh = ( afstandM / dt ) * 3.6;
                    if( impliedKmh > TELEPORT_MAX_KMH ) ++m_teleports;
                }
            }
            m_route.push_back( { nu, x, z } );
            m_laatstePuntTijd = nu;
            m_laatsteX = x; m_laatsteZ = z; m_laatstePuntBekend = true;
        }

        // --- speeding: above limit + margin for at least MIN_SEC ---
        const bool limietBekend = limietKmh > 0.0;
        const bool erover = limietBekend && snelheidKmh > limietKmh + OVERTREDING_MARGE;
        if( erover )
        {
            if( m_bovenLimietSinds < 0.0 ) m_bovenLimietSinds = st;
            if( !m_inOvertreding && st - m_bovenLimietSinds >= OVERTREDING_MIN_SEC )
            {
                m_inOvertreding = true;
                m_overtredingStart = nu;
                m_overtredingMax = snelheidKmh;
                m_overtredingLimiet = limietKmh;
                m_overtredingX = posBekend ? x : 0.0;
                m_overtredingZ = posBekend ? z : 0.0;
            }
            if( m_inOvertreding && snelheidKmh > m_overtredingMax ) m_overtredingMax = snelheidKmh;
        }
        else
        {
            m_bovenLimietSinds = -1.0;
            if( m_inOvertreding )
            {
                Overtreding o;
                o.maxKmh = m_overtredingMax; o.limietKmh = m_overtredingLimiet;
                o.start = m_overtredingStart; o.eind = nu;
                o.x = m_overtredingX; o.z = m_overtredingZ;
                m_overtredingen.push_back( o );
                m_inOvertreding = false;
            }
        }
    }

    double RitGeheugen::TopSnelheidKmh() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_top;
    }

    std::vector<RitGeheugen::Punt> RitGeheugen::Route() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_route;
    }

    std::vector<RitGeheugen::Overtreding> RitGeheugen::Overtredingen() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        std::vector<Overtreding> uit = m_overtredingen;
        // A speeding stretch still open when the trip closes counts too.
        if( m_inOvertreding )
        {
            Overtreding o;
            o.maxKmh = m_overtredingMax; o.limietKmh = m_overtredingLimiet;
            o.start = m_overtredingStart; o.eind = Nu();
            o.x = m_overtredingX; o.z = m_overtredingZ;
            uit.push_back( o );
        }
        return uit;
    }

    int RitGeheugen::Teleports() const
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        return m_teleports;
    }
}

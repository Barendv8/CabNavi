#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

namespace Ritten
{
    class RitGeheugen
    {
    public:
        struct Punt
        {
            long long tijd = 0;
            double x = 0.0, z = 0.0;
        };

        struct Overtreding
        {
            double maxKmh = 0.0;
            double limietKmh = 0.0;
            long long start = 0, eind = 0;
            double x = 0.0, z = 0.0;
        };

        void Voed( double snelheidKmh, double limietKmh, bool posBekend, double x, double z, bool ritActief );

        double TopSnelheidKmh() const;
        std::vector<Punt> Route() const;
        std::vector<Overtreding> Overtredingen() const;
        int Teleports() const;

        static constexpr double ROUTE_INTERVAL_SEC   = 5.0;
        static constexpr double OVERTREDING_MARGE    = 5.0;
        static constexpr double OVERTREDING_MIN_SEC  = 3.0;
        static constexpr double TELEPORT_MIN_M       = 800.0;
        static constexpr double TELEPORT_MAX_KMH     = 300.0;

    private:
        void Reset();

        mutable std::mutex m_mutex;
        bool m_ritWasActief = false;

        double m_top = 0.0;
        std::vector<Punt> m_route;
        long long m_laatstePuntTijd = 0;
        bool m_laatstePuntBekend = false;
        double m_laatsteX = 0.0, m_laatsteZ = 0.0;

        std::vector<Overtreding> m_overtredingen;
        bool m_inOvertreding = false;
        long long m_overtredingStart = 0;
        double m_overtredingMax = 0.0, m_overtredingLimiet = 0.0, m_overtredingX = 0.0, m_overtredingZ = 0.0;
        double m_bovenLimietSinds = -1.0;

        int m_teleports = 0;
    };
}

#pragma once
// RitGeheugen.hxx
//
// The trip's memory. CabNavi knows every frame how fast you drive and where
// you are, but until now it showed that and forgot it. A company hub wants
// four things remembered until the trip closes:
//
//   * the highest speed of the trip,
//   * the route as a list of points (one every five seconds),
//   * speeding: stretches above the posted limit, with how fast and where,
//   * teleports: jumps in position that no speed can explain.
//
// This class measures NOTHING itself. It is fed the values that already
// exist -- LiveSnelheidKmh(), the speed limit from VoertuigStatus, the own
// position the radar keeps -- once per frame from Plugin.cxx, and it keeps
// them in memory for the duration of one trip. Empty at trip start, read at
// trip end, empty again. Nothing is written to disk.

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
            long long tijd = 0;   // real time, unix seconds
            double x = 0.0, z = 0.0;
        };

        struct Overtreding
        {
            double maxKmh = 0.0;
            double limietKmh = 0.0;
            long long start = 0, eind = 0;   // unix seconds
            double x = 0.0, z = 0.0;         // where it began
        };

        // Called every frame. `ritActief` rising edge resets the memory;
        // position may be unknown (posBekend false) in menus and loading.
        void Voed( double snelheidKmh, double limietKmh, bool posBekend, double x, double z, bool ritActief );

        // Snapshots for the trip that just closed (or is running).
        double TopSnelheidKmh() const;
        std::vector<Punt> Route() const;
        std::vector<Overtreding> Overtredingen() const;
        int Teleports() const;

        // Rules, in one place so the numbers can be read.
        static constexpr double ROUTE_INTERVAL_SEC   = 5.0;    // one point per 5 s: what hubs expect, ~1400 points on a 2 h trip
        static constexpr double OVERTREDING_MARGE    = 5.0;    // km/h above the limit before it counts
        static constexpr double OVERTREDING_MIN_SEC  = 3.0;    // must last this long: a dip over the line is not speeding
        static constexpr double TELEPORT_MIN_M       = 800.0;  // a jump this far between two points ...
        static constexpr double TELEPORT_MAX_KMH     = 300.0;  // ... at an implied speed no truck reaches, is a teleport

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
        double m_bovenLimietSinds = -1.0;   // seconds (steady clock) since first frame over the line

        int m_teleports = 0;
    };
}

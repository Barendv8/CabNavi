#pragma once

#include <TruckersMP/TruckersMP.hxx>

#include <cstdint>
#include <map>
#include <string>
#include <chrono>
#include <vector>

namespace Ritten
{
    struct SpelerRecord
    {
        std::int32_t spelerId = 0;
        std::uint64_t accountId = 0;
        std::uint64_t steamId = 0;
        std::string gebruikersnaam;

        std::chrono::steady_clock::time_point laatsteUpdate{};
        std::string tagTekst;
        float tagKleurR = 1.f, tagKleurG = 1.f, tagKleurB = 1.f;

        bool isPatron = false;
        bool isModerator = false;
        bool isTeam = false;
        bool isManager = false;

        float afstandMeter = 0.f;
        std::uint16_t pingMs = 0;

        bool positieBekend = false;
        float peilingGraden = 0.f;
        float koersVerschilGraden = 0.f;

        bool heeftAanhanger = false;
        std::string aanhangerType;

        float aanhangerLengteM = -1.0f;
    };

    class PlayersNearby
    {
    public:
        explicit PlayersNearby( TruckersMP::Session &session );

        std::uint64_t EigenAccountId() const;

        bool EigenPositie( double &x, double &z ) const;

        std::vector<SpelerRecord> GeefSpelers() const;

        void VerversPosities();

        std::chrono::steady_clock::time_point m_laatsteVerversing{};
        double m_eigenX = 0.0, m_eigenZ = 0.0;
        std::chrono::steady_clock::time_point m_eigenMoment{};
        bool m_eigenBekend = false;

    private:
        void VerversRecord( const TruckersMP::Player &speler );

        void VerversPositie( const TruckersMP::Player &speler, SpelerRecord &r );

        TruckersMP::Session &m_session;
        std::map<std::int32_t, SpelerRecord> m_spelers;
    };
}

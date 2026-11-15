#pragma once

#include "PlayersNearby.hxx"

#include <chrono>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

namespace Ritten
{
    struct IncidentFrame
    {
        std::string tijdLabel;
        std::vector<SpelerRecord> spelers;
    };

    class IncidentRecorder
    {
    public:
        IncidentRecorder();

        void Tick( const std::vector<SpelerRecord> &spelers );

        void MeldSchade( const std::string &vermoedelijkeSpelerId );

        bool HeeftIncident() const { return !m_bevrorenIncident.empty(); }
        int AantalFrames() const { return static_cast<int>( m_bevrorenIncident.size() ); }

        int IncidentTeller() const { return m_incidentTeller; }
        const IncidentFrame *GeefFrame( int index ) const;
        std::string VermoedelijkeSpelerId() const { return m_vermoedelijkeSpelerId; }
        void WisIncident() { m_bevrorenIncident.clear(); }

        void ZetBufferMinuten( int minuten );
        int BufferMinuten() const { return m_bufferMinuten; }

    private:
        void LaadInstellingen();
        void SlaInstellingenOp() const;
        static std::filesystem::path InstellingenPad();

        struct RingFrame
        {
            std::chrono::steady_clock::time_point tijdstip;
            std::vector<SpelerRecord> spelers;
        };
        std::deque<RingFrame> m_buffer;
        std::vector<IncidentFrame> m_bevrorenIncident;
        std::string m_vermoedelijkeSpelerId;
        int m_incidentTeller = 0;
        std::chrono::steady_clock::time_point m_laatsteTick;
        int m_bufferMinuten = 4;
    };
}

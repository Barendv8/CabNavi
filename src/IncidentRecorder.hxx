#pragma once
// IncidentRecorder.hxx
//
// Continuously keeps a ring buffer of the last X minutes of player
// snapshots (distance/name/IDs, one snapshot every ~1 second). As soon as
// TruckTracking reports a sudden damage increase, the buffer is "frozen"
// into an incident you can scrub back through with a timeline slider in
// the overlay.
//
// NOTE: this uses ONLY the confirmed distance data (like the Players tab),
// no guessed position/compass fields -- see the note in PlayersNearby.hxx
// about why we show no real direction.

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
        std::string tijdLabel;  // e.g. "-3:42"
        std::vector<SpelerRecord> spelers;
    };

    class IncidentRecorder
    {
    public:
        IncidentRecorder();

        // To be called every frame (internally limited to ~1x/second, so no
        // problem to do this from Overlay::Teken()).
        void Tick( const std::vector<SpelerRecord> &spelers );

        // Called by TruckTracking on a sudden damage increase.
        void MeldSchade( const std::string &vermoedelijkeSpelerId );

        bool HeeftIncident() const { return !m_bevrorenIncident.empty(); }
        int AantalFrames() const { return static_cast<int>( m_bevrorenIncident.size() ); }

        // Increments on every newly frozen incident. The overlay uses this
        // to see there is a NEW recording and then jump to the last moment
        // -- that is the impact itself.
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
        std::vector<IncidentFrame> m_bevrorenIncident;  // empty = no incident recorded
        std::string m_vermoedelijkeSpelerId;
        int m_incidentTeller = 0;
        std::chrono::steady_clock::time_point m_laatsteTick;
        int m_bufferMinuten = 4;  // adjustable 2-6, see Settings tab
    };
}

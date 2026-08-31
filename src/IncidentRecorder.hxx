#pragma once
// IncidentRecorder.hxx
//
// Houdt continu een "ringbuffer" bij van de laatste X minuten aan spelers-
// snapshots (afstand/naam/ID's, elke ~1 seconde een momentopname). Zodra
// TruckTracking een plotselinge schadetoename meldt, wordt de buffer op dat
// moment "bevroren" tot een incident dat je met een tijdlijn-schuif kan
// terugspoelen in de overlay.
//
// LET OP: dit gebruikt UITSLUITEND de al-bevestigde afstandsdata (net als
// de Spelers-tab), geen gegokte positie-/kompasvelden -- zie de opmerking
// in PlayersNearby.hxx over waarom we geen echte richting tonen.

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
        std::string tijdLabel; // bv. "-3:42"
        std::vector<SpelerRecord> spelers;
    };

    class IncidentRecorder
    {
    public:
        IncidentRecorder();

        // Elke frame aan te roepen (intern zelf beperkt tot ~1x/seconde,
        // dus geen probleem om dit vanuit Overlay::Teken() te doen).
        void Tick( const std::vector<SpelerRecord> &spelers );

        // Door TruckTracking aangeroepen bij een plotselinge schadetoename.
        void MeldSchade( const std::string &vermoedelijkeSpelerId );

        bool HeeftIncident() const { return !m_bevrorenIncident.empty(); }
        int AantalFrames() const { return static_cast<int>( m_bevrorenIncident.size() ); }
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
        std::vector<IncidentFrame> m_bevrorenIncident; // leeg = geen incident vastgelegd
        std::string m_vermoedelijkeSpelerId;
        std::chrono::steady_clock::time_point m_laatsteTick;
        int m_bufferMinuten = 4; // instelbaar 2-6, zie Instellingen-tab
    };
}

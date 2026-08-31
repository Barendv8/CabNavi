#include "IncidentRecorder.hxx"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>

using json = nlohmann::json;

namespace Ritten
{
    std::filesystem::path IncidentRecorder::InstellingenPad()
    {
        std::filesystem::path basis;
        if( const char *appdata = std::getenv( "APPDATA" ) ) basis = appdata;
        basis /= "CabNavi";
        std::error_code ec;
        std::filesystem::create_directories( basis, ec );
        return basis / "incident_instellingen.json";
    }

    IncidentRecorder::IncidentRecorder()
    {
        LaadInstellingen();
        m_laatsteTick = std::chrono::steady_clock::now();
    }

    void IncidentRecorder::LaadInstellingen()
    {
        std::ifstream in( InstellingenPad() );
        if( !in ) return;
        try
        {
            json j; in >> j;
            m_bufferMinuten = j.value( "buffer_minuten", m_bufferMinuten );
        }
        catch( ... ) { }
    }

    void IncidentRecorder::SlaInstellingenOp() const
    {
        std::ofstream uit( InstellingenPad() );
        if( !uit ) return;
        json j;
        j[ "buffer_minuten" ] = m_bufferMinuten;
        uit << j.dump( 2 );
    }

    void IncidentRecorder::ZetBufferMinuten( int minuten )
    {
        if( minuten < 2 ) minuten = 2;
        if( minuten > 6 ) minuten = 6;
        m_bufferMinuten = minuten;
        SlaInstellingenOp();
    }

    void IncidentRecorder::Tick( const std::vector<SpelerRecord> &spelers )
    {
        auto nu = std::chrono::steady_clock::now();
        if( std::chrono::duration<double>( nu - m_laatsteTick ).count() < 1.0 )
        {
            return; // max 1x per seconde een momentopname bewaren
        }
        m_laatsteTick = nu;

        m_buffer.push_back( RingFrame{ nu, spelers } );

        double bufferSeconden = m_bufferMinuten * 60.0;
        while( !m_buffer.empty()
               && std::chrono::duration<double>( nu - m_buffer.front().tijdstip ).count() > bufferSeconden )
        {
            m_buffer.pop_front();
        }
    }

    void IncidentRecorder::MeldSchade( const std::string &vermoedelijkeSpelerId )
    {
        if( m_buffer.empty() ) return;

        // Bevries de huidige buffer-inhoud als "het incident" -- overschrijft
        // een eventueel vorig incident (je wil het meest recente zien).
        m_bevrorenIncident.clear();
        auto nu = std::chrono::steady_clock::now();
        for( const RingFrame &f : m_buffer )
        {
            double secondenGeleden = std::chrono::duration<double>( nu - f.tijdstip ).count();
            int minuten = static_cast<int>( secondenGeleden ) / 60;
            int seconden = static_cast<int>( secondenGeleden ) % 60;
            char buf[ 16 ];
            snprintf( buf, sizeof( buf ), "-%d:%02d", minuten, seconden );

            IncidentFrame frame;
            frame.tijdLabel = buf;
            frame.spelers = f.spelers;
            m_bevrorenIncident.push_back( std::move( frame ) );
        }
        m_vermoedelijkeSpelerId = vermoedelijkeSpelerId;
    }

    const IncidentFrame *IncidentRecorder::GeefFrame( int index ) const
    {
        if( index < 0 || index >= static_cast<int>( m_bevrorenIncident.size() ) ) return nullptr;
        return &m_bevrorenIncident[ index ];
    }
}

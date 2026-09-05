#include "PlayersNearby.hxx"

#include "CallbackHulp.hxx"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Ritten
{
    std::uint64_t PlayersNearby::EigenAccountId() const
    {
        return m_session.Account().GetAccountID().value_or( 0 );
    }

    PlayersNearby::PlayersNearby( TruckersMP::Session &session )
        : m_session( session )
    {
        // Snapshot at startup/reload (see Player module docs: meant as a
        // one-time seed, not to be called every frame).
        for( const TruckersMP::Player &speler : m_session.Player().GetAllPlayers().value_or( std::vector<TruckersMP::Player>{} ) )
        {
            VerversRecord( speler );
        }

        m_session.Player().OnStreamIn.Register( Beschermd( "OnStreamIn", [ this ]( TruckersMP::PlayerStreamInEvent &e )
        {
            VerversRecord( e.GetPlayer() );
        } ) );

        m_session.Player().OnStreamOut.Register( Beschermd( "OnStreamOut", [ this ]( TruckersMP::PlayerStreamOutEvent &e )
        {
            if( const std::optional<TruckersMP::Int32> id = e.GetPlayer().GetPlayerID() )
            {
                m_spelers.erase( *id );
            }
        } ) );

        // OnUpdate fires at network rate -- pure data management, no SDK
        // calls back and no logic here (see the docs: "Treat the handler as
        // a data tap").
        m_session.Player().OnUpdate.Register( Beschermd( "OnUpdate", [ this ]( TruckersMP::PlayerUpdateEvent &e )
        {
            const TruckersMP::Player &speler = e.GetPlayer();
            if( const std::optional<TruckersMP::Int32> id = speler.GetPlayerID() )
            {
                auto it = m_spelers.find( *id );
                if( it != m_spelers.end() )
                {
                    it->second.afstandMeter = speler.GetDistanceFromLocalPlayer().value_or( it->second.afstandMeter );
                    it->second.pingMs = speler.GetNetworkLatency().value_or( it->second.pingMs );
                    it->second.laatsteUpdate = std::chrono::steady_clock::now();
                }
            }
        } ) );
    }

    void PlayersNearby::VerversRecord( const TruckersMP::Player &speler )
    {
        const std::optional<TruckersMP::Int32> id = speler.GetPlayerID();
        if( !id ) return;

        SpelerRecord r;
        r.spelerId = *id;
        r.accountId = speler.GetAccountID().value_or( 0 );
        r.steamId = speler.GetSteamID().value_or( 0 );
        r.gebruikersnaam = speler.GetUsername().value_or( "Onbekend" );
        r.tagTekst = speler.GetTagText().value_or( "" );

        // NB: in your SDK version these getters return std::optional<Bool>
        // (not a bare Bool, as the module docs suggest) -- hence .value_or(false).
        r.isPatron = speler.IsPatron().value_or( false );
        r.isModerator = speler.IsGameModerator().value_or( false );
        r.isTeam = speler.IsTeamMember().value_or( false );
        r.isManager = speler.IsManager().value_or( false );

        r.afstandMeter = speler.GetDistanceFromLocalPlayer().value_or( 0.f );
        r.pingMs = speler.GetNetworkLatency().value_or( 0 );

        // Position, heading and trailer status. This makes SDK calls and so
        // belongs here (stream-in / seed), NOT in OnUpdate -- that fires at
        // network rate and per the docs must remain a pure data tap.
        VerversPositie( speler, r );
        // On stream-in the player is alive by definition: mark as updated
        // right away, otherwise he is invisible for a fraction until the
        // first OnUpdate.
        r.laatsteUpdate = std::chrono::steady_clock::now();

        m_spelers[ *id ] = std::move( r );
    }

    namespace
    {
        // Yaw (compass heading) from a quaternion, in degrees 0..360.
        // Standard formula for rotation about the vertical axis; we only use
        // yaw because a radar is flat -- pitch and roll do not matter.
        double YawGraden( const TruckersMP::Quaternion &q )
        {
            const double siny = 2.0 * ( (double)q.w * (double)q.y + (double)q.x * (double)q.z );
            const double cosy = 1.0 - 2.0 * ( (double)q.y * (double)q.y + (double)q.z * (double)q.z );
            double graden = std::atan2( siny, cosy ) * 180.0 / 3.14159265358979323846;
            if( graden < 0.0 ) graden += 360.0;
            return graden;
        }

        // Wrap an angle back into the range 0..360.
        double NormaliseerGraden( double g )
        {
            while( g < 0.0 ) g += 360.0;
            while( g >= 360.0 ) g -= 360.0;
            return g;
        }
    }

    void PlayersNearby::VerversPositie( const TruckersMP::Player &speler, SpelerRecord &r )
    {
        r.positieBekend = false;

        // NEVER construct an empty handle and call methods on it.
        //
        // This used to be `.value_or( TruckersMP::Vehicle{} )`. Looks
        // harmless, but such a default-constructed handle has no valid module
        // reference; calling a getter on it reads an invalid pointer and
        // crashes the plugin. Every optional is now checked before use.
        r.heeftAanhanger = false;

        r.aanhangerLengteM = -1.0f;
        if( const auto aanhanger = speler.GetTrailer() )
        {
            r.heeftAanhanger = aanhanger->Exists().value_or( false );

            // Length from the bounding box. It is in vehicle space, so the
            // longest of the three axes IS the length -- which axis that is
            // depends on their coordinate system, and we do not need to know.
            if( r.heeftAanhanger )
            {
                if( const auto doos = aanhanger->GetBoundingBox() )
                {
                    const float dx = std::abs( doos->max.x - doos->min.x );
                    const float dy = std::abs( doos->max.y - doos->min.y );
                    const float dz = std::abs( doos->max.z - doos->min.z );
                    r.aanhangerLengteM = std::max( dx, std::max( dy, dz ) );
                }
            }
        }

        const auto lokaal = m_session.Player().GetLocalPlayer();
        if( !lokaal ) return;

        const auto mijnVoertuig = lokaal->GetVehicle();
        if( !mijnVoertuig ) return;

        const auto zijnVoertuig = speler.GetVehicle();
        if( !zijnVoertuig ) return;

        const std::optional<TruckersMP::Placement> mij = mijnVoertuig->GetPlacement();
        const std::optional<TruckersMP::Placement> hem = zijnVoertuig->GetPlacement();

        if( !mij || !hem ) return;  // no vehicle in the world -> nothing to show

        // Difference vector in the horizontal plane. Y is height and does
        // not count: a truck on a viaduct above you is simply "ahead" on the
        // radar, not further away.
        const double dx = hem->position.x - mij->position.x;
        const double dz = hem->position.z - mij->position.z;
        if( dx == 0.0 && dz == 0.0 ) return;  // exactly on top of each other; angle meaningless

        // Absolute direction to that player, in the same frame as the yaw.
        double richtingNaarHem = std::atan2( dx, dz ) * 180.0 / 3.14159265358979323846;

        const double mijnKoers = YawGraden( mij->rotation );
        const double zijnKoers = YawGraden( hem->rotation );

        // Bearing relative to where YOU are looking: 0 = straight ahead.
        r.peilingGraden = static_cast<float>( NormaliseerGraden( richtingNaarHem - mijnKoers ) );
        // Heading difference: around 0 = same direction, around 180 = oncoming.
        r.koersVerschilGraden = static_cast<float>( NormaliseerGraden( zijnKoers - mijnKoers ) );
        r.positieBekend = true;
    }

    void PlayersNearby::VerversPosities()
    {
        // BRAKE. This loop makes a handful of SDK calls per player, and the
        // caller hangs on the frame event -- sixty times a second times fifty
        // players is needlessly much. Four times a second is plenty for a
        // radar and for the incident recorder, which itself only keeps a
        // snapshot once per second.
        const auto nu = std::chrono::steady_clock::now();
        if( std::chrono::duration<double>( nu - m_laatsteVerversing ).count() < 0.25 )
        {
            return;
        }
        m_laatsteVerversing = nu;

        // Own position first, independent of whether anyone else is nearby.
        // Same SDK path as the radar; nothing new is asked of the game.
        m_eigenBekend = false;
        if( const auto lokaal = m_session.Player().GetLocalPlayer() )
        {
            if( const auto voertuig = lokaal->GetVehicle() )
            {
                if( const std::optional<TruckersMP::Placement> pl = voertuig->GetPlacement() )
                {
                    m_eigenX = pl->position.x; m_eigenZ = pl->position.z;
                    m_eigenMoment = nu; m_eigenBekend = true;
                }
            }
        }

        // Handles are fetched fresh each time via the player ID; we never
        // store them (see "Do not store handles" in the docs). A player who
        // just left simply returns nothing and keeps his last known record
        // with positieBekend = false.
        for( auto &[ id, record ] : m_spelers )
        {
            // Only players TruckersMP itself has updated in the last two
            // seconds. A player being torn down gets no more OnUpdate, but
            // GetPlayerByID may still briefly return a handle whose getters read
            // dead memory. See the crash of 03-09: TruckersMP calls us, we call
            // the SDK, and it fails inside core_ets2mp.dll.
            if( std::chrono::duration<double>( nu - record.laatsteUpdate ).count() > 2.0 )
            {
                record.positieBekend = false;
                continue;
            }
            const std::optional<TruckersMP::Player> speler = m_session.Player().GetPlayerByID( id );
            if( !speler )
            {
                record.positieBekend = false;
                continue;
            }
            // Distance and ping normally arrive via OnUpdate, but right after a
            // plugin reload that has not happened yet -- taking them here costs
            // nothing and avoids a frame of zeros.
            record.afstandMeter = speler->GetDistanceFromLocalPlayer().value_or( record.afstandMeter );
            VerversPositie( *speler, record );
        }
    }

    std::vector<SpelerRecord> PlayersNearby::GeefSpelers() const
    {
        std::vector<SpelerRecord> lijst;
        lijst.reserve( m_spelers.size() );
        for( const auto &[ id, r ] : m_spelers ) lijst.push_back( r );
        std::sort( lijst.begin(), lijst.end(),
                   []( const SpelerRecord &a, const SpelerRecord &b ) { return a.afstandMeter < b.afstandMeter; } );
        return lijst;
    }

    bool PlayersNearby::EigenPositie( double &x, double &z ) const
    {
        if( !m_eigenBekend ) return false;
        if( std::chrono::duration<double>( std::chrono::steady_clock::now() - m_eigenMoment ).count() > 2.0 ) return false;
        x = m_eigenX; z = m_eigenZ;
        return true;
    }
}

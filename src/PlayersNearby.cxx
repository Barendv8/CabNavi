#include "PlayersNearby.hxx"

#include "CallbackHulp.hxx"

#include <algorithm>
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
        // Snapshot bij opstarten/herladen (zie Player-moduledocs: bedoeld
        // als eenmalige seed, niet om elke frame aan te roepen).
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

        // OnUpdate vuurt op netwerksnelheid -- puur databeheer, geen SDK-
        // aanroepen terug en geen logica hier (zie de docs: "Treat the
        // handler as a data tap").
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

        // NB: deze getters komen in jouw SDK-versie terug als std::optional<Bool>
        // (niet als kale Bool, zoals de moduledocs suggereren) -- vandaar .value_or(false).
        r.isPatron = speler.IsPatron().value_or( false );
        r.isModerator = speler.IsGameModerator().value_or( false );
        r.isTeam = speler.IsTeamMember().value_or( false );
        r.isManager = speler.IsManager().value_or( false );

        r.afstandMeter = speler.GetDistanceFromLocalPlayer().value_or( 0.f );
        r.pingMs = speler.GetNetworkLatency().value_or( 0 );

        // Positie, koers en aanhangerstatus. Dit doet SDK-aanroepen en hoort
        // daarom hier (stream-in / seed) thuis, NIET in OnUpdate -- die vuurt
        // op netwerksnelheid en moet volgens de docs een pure datatap blijven.
        VerversPositie( speler, r );

        m_spelers[ *id ] = std::move( r );
    }

    namespace
    {
        // Yaw (kompaskoers) uit een quaternion, in graden 0..360.
        // Standaardformule voor rotatie om de verticale as; we gebruiken
        // alleen yaw omdat een radar plat is -- pitch en roll doen niet mee.
        double YawGraden( const TruckersMP::Quaternion &q )
        {
            const double siny = 2.0 * ( (double)q.w * (double)q.y + (double)q.x * (double)q.z );
            const double cosy = 1.0 - 2.0 * ( (double)q.y * (double)q.y + (double)q.z * (double)q.z );
            double graden = std::atan2( siny, cosy ) * 180.0 / 3.14159265358979323846;
            if( graden < 0.0 ) graden += 360.0;
            return graden;
        }

        // Zet een hoek terug in het bereik 0..360.
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

        // NOOIT een leeg handle construeren en daar methodes op aanroepen.
        //
        // Hier stond `.value_or( TruckersMP::Vehicle{} )`. Dat ziet er
        // onschuldig uit, maar zo'n standaard-geconstrueerd handle heeft geen
        // geldige moduleverwijzing; er een getter op aanroepen leest een
        // ongeldige pointer en klapt de plugin eruit. Elke optional wordt nu
        // eerst gecontroleerd voordat we hem gebruiken.
        r.heeftAanhanger = false;

        r.aanhangerLengteM = -1.0f;
        if( const auto aanhanger = speler.GetTrailer() )
        {
            r.heeftAanhanger = aanhanger->Exists().value_or( false );

            // Lengte uit de bounding box. Die is in voertuigruimte, dus de
            // langste van de drie assen IS de lengte -- welke as dat is
            // hangt af van hun assenstelsel, en dat hoeven we zo niet te
            // weten.
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

        if( !mij || !hem ) return; // geen voertuig in de wereld -> niets te tonen

        // Verschilvector in het horizontale vlak. Y is hoogte en telt niet
        // mee: een truck op een viaduct boven je is op de radar gewoon
        // "voor je", niet verder weg.
        const double dx = hem->position.x - mij->position.x;
        const double dz = hem->position.z - mij->position.z;
        if( dx == 0.0 && dz == 0.0 ) return; // exact op elkaar; hoek zinloos

        // Absolute richting naar die speler, in hetzelfde stelsel als de yaw.
        double richtingNaarHem = std::atan2( dx, dz ) * 180.0 / 3.14159265358979323846;

        const double mijnKoers = YawGraden( mij->rotation );
        const double zijnKoers = YawGraden( hem->rotation );

        // Peiling relatief aan waar JIJ naar kijkt: 0 = recht vooruit.
        r.peilingGraden = static_cast<float>( NormaliseerGraden( richtingNaarHem - mijnKoers ) );
        // Koersverschil: rond 0 = zelfde kant op, rond 180 = tegenligger.
        r.koersVerschilGraden = static_cast<float>( NormaliseerGraden( zijnKoers - mijnKoers ) );
        r.positieBekend = true;
    }

    void PlayersNearby::VerversPosities()
    {
        // Handles worden per keer opnieuw opgehaald via het speler-ID; we
        // bewaren ze nooit (zie "Do not store handles" in de docs). Een
        // speler die net weg is, geeft gewoon niets terug en houdt dan zijn
        // laatst bekende record met positieBekend = false.
        for( auto &[ id, record ] : m_spelers )
        {
            const std::optional<TruckersMP::Player> speler = m_session.Player().GetPlayerByID( id );
            if( !speler )
            {
                record.positieBekend = false;
                continue;
            }
            // Afstand en ping komen normaal via OnUpdate binnen, maar bij een
            // net herladen plugin is dat nog niet gebeurd -- meenemen kost hier
            // niets en voorkomt een frame met nullen.
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
}

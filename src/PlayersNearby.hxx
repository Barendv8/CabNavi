#pragma once
// PlayersNearby.hxx
//
// Keeps a live list of players within streaming range, via
// TruckersMP::Player (see the Player module docs). Important to know:
//
//   The SDK gives us per player: name, tag, vehicle/trailer, distance,
//   ping and rights (patron/moderator/team/manager). The SDK does NOT give
//   us the cargo, source/destination or income of someone else's job --
//   that is private job data known only to that player. So "what they
//   carry" is shown as "loaded / empty" (based on whether a trailer is
//   attached), not as an exact cargo -- that would be invented.
//
// OnUpdate fires at network rate (see the docs: "many times per second").
// There we only update values in an existing PlayerRecord, no
// allocations/lookups; the actual list is rebuilt periodically (every
// frame, not every update) by the overlay.

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
        std::uint64_t accountId = 0;  // TruckersMP ID, for truckersmp.com/user/<id>
        std::uint64_t steamId = 0;  // Steam64 ID, for steamcommunity.com/profiles/<id>
        std::string gebruikersnaam;

        // When did this player last receive an OnUpdate from TruckersMP?
        // That event only comes for players they themselves consider alive.
        // VerversPosities only touches players that had an update recently
        // -- so we never query someone who is being torn down at that moment.
        std::chrono::steady_clock::time_point laatsteUpdate{};
        std::string tagTekst;
        float tagKleurR = 1.f, tagKleurG = 1.f, tagKleurB = 1.f;

        bool isPatron = false;
        bool isModerator = false;
        bool isTeam = false;
        bool isManager = false;

        float afstandMeter = 0.f;
        std::uint16_t pingMs = 0;

        // --- Real position (SDK 1.0+, Vehicles-and-Trailers module) -----
        // `peilingGraden` is the direction in which this player IS LOCATED,
        // seen from you: 0 = straight ahead, 90 = right, 180 = behind,
        // 270 = left. So not that player's own heading.
        // `koersVerschilGraden` is the heading: the difference between his
        // course and yours, so you can tell oncoming traffic (around 180)
        // from fellow travellers (around 0).
        // Both only valid when `positieBekend` is true -- without a vehicle
        // in the world the SDK returns nothing, and then the radar must not
        // put him at an invented spot.
        bool positieBekend = false;
        float peilingGraden = 0.f;
        float koersVerschilGraden = 0.f;

        bool heeftAanhanger = false;
        std::string aanhangerType;

        // Trailer length in metres, from Trailer::GetBoundingBox()
        // (SDK 1.1.0). Shows whether someone pulls a normal semi-trailer or
        // a double combination -- that matters a lot when you start
        // overtaking. -1 = unknown.
        float aanhangerLengteM = -1.0f;
    };

    // RADAR DISPLAY: since the Vehicles-and-Trailers module,
    // `Vehicle::GetPlacement()` gives the REAL position and rotation in
    // world coordinates. So the radar shows an actual bearing, no longer
    // an evenly distributed angle.
    //
    // This used to say the SDK only gave a distance and no direction;
    // that was true for the older documentation, but no longer. What is
    // there:
    //
    //   - Your own vehicle: Player().GetLocalPlayer() -> GetVehicle().
    //   - Every other player: speler.GetVehicle() -> GetPlacement().
    //   - Position is Double3 (world metres), rotation a Quaternion.
    //
    // The bearing is computed in the horizontal plane (X/Z); Y is height
    // and does not matter for a radar. A vehicle's heading comes from the
    // yaw of the quaternion.
    //
    // Important: without a vehicle in the world (just loaded, in a menu)
    // the SDK returns nothing. Then `positieBekend` stays false and the
    // overlay must NOT draw that player on the radar -- better to miss a
    // player briefly than to put him at an invented spot.

    class PlayersNearby
    {
    public:
        explicit PlayersNearby( TruckersMP::Session &session );

        // Your own TruckersMP ID. Comes from the SDK, so no request needed.
        // 0 = not available (e.g. not logged in yet).
        std::uint64_t EigenAccountId() const;

        // Own world position (X/Z), refreshed with the radar loop. False if the
        // vehicle is not in the world or the last fix is older than two seconds.
        // Used for refuelling: nearest city -> country -> price.
        bool EigenPositie( double &x, double &z ) const;

        // Thread-safe snapshot for the overlay, sorted by distance (nearest first).
        std::vector<SpelerRecord> GeefSpelers() const;

        // Fetches the current position/heading for every known player.
        //
        // MUST be called from a frame event (so on the game thread), not
        // from our own thread -- SDK getters outside the game thread simply
        // return nothing.
        //
        // Why not in OnUpdate: that event fires at network rate and, per the
        // docs, must remain a pure data tap without SDK calls. Positions are
        // "state", and state should be polled -- exactly what this function
        // does, once per frame.
        void VerversPosities();

        // When did we last run that loop? See the brake in VerversPosities.
        std::chrono::steady_clock::time_point m_laatsteVerversing{};
        double m_eigenX = 0.0, m_eigenZ = 0.0;
        std::chrono::steady_clock::time_point m_eigenMoment{};
        bool m_eigenBekend = false;

    private:
        void VerversRecord( const TruckersMP::Player &speler );

        // Fills bearing, heading difference and trailer status into `r`,
        // based on this player's vehicle and the local player's. Does
        // nothing if either vehicle is not in the world.
        void VerversPositie( const TruckersMP::Player &speler, SpelerRecord &r );

        TruckersMP::Session &m_session;
        std::map<std::int32_t, SpelerRecord> m_spelers;
    };
}

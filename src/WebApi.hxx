#pragma once
// WebApi.hxx
//
// Fetches data from the public TruckersMP Web API
// (https://api.truckersmp.com/v2). No key or login needed.
//
// Two rules this sticks to:
//
//  1. NEVER on the game thread. Network traffic takes hundreds of
//     milliseconds; that would make the game stutter. Everything happens on
//     its own thread, like the Discord webhook. The overlay only reads a
//     copy of the last answer.
//
//  2. Take it easy. This is community data, not telemetry. Server status
//     once a minute and events once a quarter hour is plenty. Asking more
//     often gains nothing and burdens their servers needlessly.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Ritten
{
    // One server as the API returns it.
    struct ServerInfo
    {
        std::string naam;  // "Simulation 1"
        std::string spel;  // "ETS2" or "ATS"
        int spelers = 0;
        int maxSpelers = 0;
        int wachtrij = 0;
        bool online = false;
        bool collisions = false;  // collisions on or off
        bool snelheidsbegrenzer = false;
    };

    // One upcoming event.
    struct EvenementInfo
    {
        int id = 0;  // TruckersMP event number, for ticking
        std::string naam;
        std::string vertrek;  // departure city
        std::string aankomst;  // arrival city
        std::string startTijd;  // as the API gives it (UTC)
        std::string spel;
        std::string server;
    };

    // Data of one VTC, as /v2/vtc/{id} returns it.
    struct VtcInfo
    {
        bool geldig = false;
        std::string naam;
        std::string tag;
        std::string slogan;
        std::string taal;
        std::string werving;  // "Open" or "Close"
        int leden = 0;
        bool geverifieerd = false;
    };

    // One news item of a VTC.
    struct VtcNieuwsInfo
    {
        std::string titel;
        std::string datum;  // published_at, as the API gives it
        std::string auteur;
        bool vastgezet = false;
    };

    class WebApi
    {
    public:
        WebApi();
        ~WebApi();

        // On/off. Default OFF: network traffic should be something you choose
        // yourself, not something a plugin does quietly.
        void ZetIngeschakeld( bool aan );
        bool Ingeschakeld() const { return m_aan.load(); }

        // VTC integration. Switched on separately, with its own number: that
        // is in the address of your VTC page (truckersmp.com/vtc/<number>).
        // 0 = no VTC set; then nothing is fetched either.
        void ZetVtc( bool aan, int vtcId );

        // Your own TruckersMP ID (from the SDK). Needed to fetch which events
        // YOU signed up for -- without it we only know what your VTC does.
        void ZetEigenAccount( std::uint64_t accountId );


        bool VtcIngeschakeld() const { return m_vtcAan.load(); }
        int VtcId() const { return m_vtcId.load(); }

        // --- VTC per player ------------------------------------------------
        // The VTC NUMBER is the only truly unique attribute: a tag is text
        // anyone can type, a number is given to a company once by TruckersMP.
        // Looking it up costs one request per player, so that goes through a
        // queue at an easy pace, and the answer is remembered -- someone's
        // company changes at most once a month.
        //
        // Enqueueing happens from the overlay for the players in view; already
        // known or already enqueued players are skipped. Priority: nearby
        // first. You see those first on the radar, so you do not want them at
        // the back of the queue.
        void MeldSpelerAan( std::uint64_t accountId, bool voorrang = false );

        // -1 = not looked up yet, 0 = not in a VTC.
        int SpelerVtcId( std::uint64_t accountId ) const;

        // Is this player a patron according to the Web API? -1 = not looked
        // up yet. MEASURED 30-08: the SDK flag IsPatron stays false, even for
        // someone who demonstrably is a patron. The API does say it, and that
        // question is already in the same answer as the VTC number -- so this
        // costs no extra request.
        int SpelerIsPatron( std::uint64_t accountId ) const;

        // Patron according to the WEB API. The SDK has a flag for this too,
        // but it proved unreliable: a player with an active Master Trucker
        // contribution came out as NOT patron (measured 30-08). The answer
        // comes from the same lookup as the VTC number, so this costs no
        // extra request.
        // -1 = not looked up yet, 0 = no patron, 1 = patron.
        int SpelerPatron( std::uint64_t accountId ) const;

        // For the display: how many players have been looked up, and how
        // many are still queued. Without this you can only guess whether it
        // works.
        void OpzoekStand( int &opgezochtUit, int &inRijUit ) const;

        // Copies of the last answer. Safe from the game thread.
        std::vector<ServerInfo> Servers() const;
        std::vector<EvenementInfo> Evenementen() const;
        VtcInfo Vtc() const;
        std::vector<EvenementInfo> VtcEvenementen() const;

        // Convoys YOU or your VTC signed up for. Only from these does the
        // reminder on the Live tab come: a notice about a convoy you have
        // nothing to do with is just noise.
        std::vector<EvenementInfo> AangemeldeEvenementen() const;

        // Only what your VTC signed up for, apart from your own sign-ups --
        // for the list on the VTC tab.
        std::vector<EvenementInfo> VtcAangemeld() const;
        std::vector<VtcNieuwsInfo> VtcNieuws() const;

        // For the display: when last fetched, and whether it succeeded.
        std::string Status() const;
        std::string VtcStatus() const;

    private:
        void StartDraadIndienNodig();
        void WerkLus();
        bool HaalOp( const std::string &pad, std::string &antwoordUit );
        void VerwerkServers( const std::string &json );
        void VerwerkEvenementen( const std::string &json );
        void VerwerkVtc( const std::string &json );
        void VerwerkVtcEvenementen( const std::string &json );
        void VerwerkAangemeld( const std::string &json, bool viaVtc );
        void VerwerkVtcNieuws( const std::string &json );
        void VerwerkSpelerVtc( std::uint64_t accountId, const std::string &json );
        static std::filesystem::path CacheBestand();
        void LaadSpelerCache();
        void SlaSpelerCacheOp();

        std::atomic<bool> m_aan{ false };
        std::atomic<bool> m_vtcAan{ false };
        std::atomic<int> m_vtcId{ 0 };
        std::atomic<bool> m_vtcNuOphalen{ false };  // set by ZetVtc, so a change takes effect immediately
        std::atomic<bool> m_stoppen{ false };
        std::thread m_thread;

        mutable std::mutex m_slot;
        std::vector<ServerInfo> m_servers;
        std::vector<EvenementInfo> m_evenementen;
        VtcInfo m_vtc;
        std::vector<EvenementInfo> m_vtcEvenementen;
        std::vector<EvenementInfo> m_aangemeldVtc;  // your VTC
        std::atomic<std::uint64_t> m_eigenAccount{ 0 };
        std::vector<VtcNieuwsInfo> m_vtcNieuws;
        // What we know per player, and what still needs looking up. The
        // cache also stays when a player leaves view: meet him again later
        // and nothing needs asking again.
        // What we get per player from one lookup.
        struct SpelerGegevens
        {
            int vtcId = 0;
            bool patron = false;
        };
        std::map<std::uint64_t, SpelerGegevens> m_spelerVtc;
        std::deque<std::uint64_t> m_wachtrij;

        // Who is being looked up RIGHT NOW. Without this such a player is
        // briefly in no-man's land -- no longer queued, not yet cached -- and
        // the overlay simply enqueues him again. Measured 30-08: every
        // successful lookup appeared twice in the log.
        std::set<std::uint64_t> m_bezig;

        // Back off after a failure. The API has a limit that is documented
        // nowhere; at five per second 68% of requests failed (measured
        // 30-08). If it goes wrong, stop completely for a while instead of
        // pushing harder.
        int m_rustSeconden = 0;
        bool m_cacheGewijzigd = false;

        std::string m_status = "nog niet opgehaald";
        std::string m_vtcStatus = "nog niet opgehaald";
    };
}

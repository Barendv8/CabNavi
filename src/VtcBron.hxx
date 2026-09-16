#pragma once
// VtcBron.hxx
//
// One question, four answers: "what does my company's system know?"
//
// A VTC source fills the VTC tab from wherever the driver's company lives:
//   Bot          -- the CabNavi Discord bot (receivers/discord-bot). Jobs
//                   handed out in Discord show up here; accepting them goes
//                   back. Uses the trip-webhook address and key, nothing extra.
//   Trucky       -- e.truckyapp.com. Mostly open endpoints: the driver's own
//                   Steam ID (from the TruckersMP SDK) is enough to find their
//                   Trucky account and company. No key needed.
//   Horizon      -- hdispatch.eu, read only, needs the company's public
//                   pk_ key (5-boost tier).
//   TruckersHub  -- api.truckershub.in, needs the company token. That token
//                   can also add and remove drivers, so the settings say so.
//   VTLog        -- api.vtlog.net. The driver's Steam ID finds the account
//                   and company on open endpoints; an optional VTC key (10
//                   requests/minute) adds members and who is driving now.
//
// Every source produces the same VtcOverzicht, so the tab draws one thing.
// Polling runs on its own thread, once a minute, at most a handful of
// requests per poll; the game thread only ever copies the last result.
// Nothing here writes anywhere but %APPDATA%\CabNavi\vtc_bron.json.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Ritten
{
    class VtcWebhook;

    enum class VtcBronType
    {
        Geen        = 0,
        Bot         = 1,
        Trucky      = 2,
        Horizon     = 3,
        TruckersHub = 4,
        Vtlog       = 5,
    };

    struct BronConvooi
    {
        std::string titel;
        std::string start;      // as the source gives it
        std::string vertrek;
        std::string aankomst;
        std::string server;
    };

    struct BronNieuws
    {
        std::string titel;
        std::string datum;
    };

    struct Opdracht
    {
        std::string id;         // source's id, echoed back on accept
        std::string vertrek;
        std::string aankomst;
        std::string lading;
        std::string deadline;
        std::string notitie;
        std::string status;     // "open" / "accepted"
        bool accepteerbaar = false;   // only the bot lets you accept from the cab
    };

    struct VtcOverzicht
    {
        bool geldig = false;
        VtcBronType bron = VtcBronType::Geen;
        std::string bronNaam;           // "Trucky", "Horizon Dispatch", ...
        std::string status;             // last error or "" when fine

        std::string bedrijf;
        std::string tag;
        int leden = 0;
        double totaalKm = 0.0;          // company total, when the source has it

        std::string mijnNaam;           // how the source knows me
        std::string mijnRol;
        double mijnKm = 0.0;
        int mijnRitten = 0;

        std::vector<std::string> online;        // driver names currently live
        std::vector<BronConvooi> convooien;
        std::vector<BronNieuws> nieuws;
        std::vector<Opdracht> opdrachten;
    };

    class VtcBron
    {
    public:
        explicit VtcBron( VtcWebhook &webhook );
        ~VtcBron();

        // Game-thread safe snapshot.
        VtcOverzicht Overzicht() const;

        // Settings (persisted in vtc_bron.json).
        VtcBronType Type() const;
        std::string Sleutel() const;
        void ZetType( VtcBronType t );
        void ZetSleutel( const std::string &sleutel );

        // The SDK knows who we are; Plugin.cxx passes it on once connected.
        void ZetEigenSteamId( std::uint64_t steamId );

        // Bot only: tell the company we take this job. Enqueued, sent on the
        // worker thread, the overview refreshes right after.
        void AccepteerOpdracht( const std::string &id );

        // Ask for a refresh sooner than the next minute (settings changed).
        void VerversNu();

        static const char *Naam( VtcBronType t );

    private:
        void Laad();
        void Bewaar() const;
        void WorkerLoop();
        void Ververs();

        VtcOverzicht HaalBot();
        VtcOverzicht HaalTrucky();
        VtcOverzicht HaalHorizon();
        VtcOverzicht HaalTruckersHub();
        VtcOverzicht HaalVtlog();

        VtcWebhook &m_webhook;

        mutable std::mutex m_mutex;
        VtcOverzicht m_overzicht;
        VtcBronType m_type = VtcBronType::Geen;
        std::string m_sleutel;
        std::uint64_t m_steamId = 0;
        std::vector<std::string> m_teAccepteren;

        // Trucky remembers what it learned so a poll is two requests, not four.
        int m_truckyUserId = 0;
        int m_truckyCompanyId = 0;
        int m_vtlogVtcId = 0;

        std::thread m_thread;
        std::condition_variable m_wekker;
        std::atomic<bool> m_stop{ false };
        std::atomic<bool> m_nu{ false };
    };
}

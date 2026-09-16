#pragma once
// VtcWebhook.hxx
//
// The universal VTC bridge: every finished or cancelled trip is POSTed as
// one JSON document to an endpoint the USER fills in -- their own VTC
// portal, a PHP file on their company site, whatever they run. CabNavi
// itself talks to nobody; the user decides where their data goes, which is
// the same promise the Discord webhook makes.
//
// The document follows the open trip format described in
// docs/TRIP_FORMAT.md: a stable, versioned schema that any system may
// implement, so a company is never locked to one tracker. Amounts stay in
// the game's own currency (named in the payload), distances in km, fuel in
// litres -- data is metric and explicit; DISPLAY units are the overlay's
// business, not the wire's.
//
// Same shape as DiscordWebhook on purpose: own settings file
// (%APPDATA%\CabNavi\vtc_webhook.json, so components never overwrite each
// other), own background worker with a queue -- the game thread only
// enqueues (see Threading in the SDK docs: callbacks must not do I/O). The
// optional key is sent as "Authorization: Bearer <key>" and an
// "X-CabNavi-Key" header, and is treated like the Discord URL: never
// logged.

#include "TripTypes.hxx"
#include "HubFormaat.hxx"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace Ritten
{
    class VtcWebhook
    {
    public:
        VtcWebhook();
        ~VtcWebhook();

        // A snapshot of the own tachograph at the moment the trip closed;
        // the one field no other tracker can offer. Filled by the caller
        // (Plugin.cxx has the tracker at hand), consumed by the payload.
        struct TachoMoment
        {
            bool geldig = false;         // false: leave the block out entirely
            double rijMinutenSindsRust = 0.0;
            bool inRust = false;
        };

        // Game-thread safe: enqueue and return. Does nothing when off or
        // when no endpoint is set.
        // Priced fuel and the empty run that led to this job: both come from
        // FuelCosts at the moment the trip closes.
        struct RitContext
        {
            double prijsPerLiter = 0.0;      // 0 = unknown
            int prijsBron = -1;              // 0 last refuel of this truck, 1 price here, 2 manual
            double leegKm = 0.0, leegLiters = 0.0, leegKosten = 0.0;
            HubFormaat::Context hub;         // only read when the envelope format is chosen
        };
        void StuurRit( const Trip &trip, const TachoMoment &tacho, const RitContext &ctx );
        void StuurRit( const Trip &trip, const TachoMoment &tacho ) { StuurRit( trip, tacho, RitContext{} ); }

        // A hand-made ping so the user can see the connection work before
        // any trip finishes (button in the settings, like Discord's).
        void StuurTestbericht();

        void ZetEndpointUrl( const std::string &url );
        void ZetSleutel( const std::string &sleutel );
        void ZetFormaat( int formaat );
        void ZetLivePositie( bool aan );
        bool LivePositie() const;

        // Fed every frame by Plugin.cxx; sent by the worker every 10 s when
        // livePositie is on and the trip webhook is enabled.
        struct LiveStand
        {
            bool bekend = false;
            double x = 0.0, z = 0.0, snelheidKmh = 0.0;
            bool opRit = false;
            std::string van, naar, server;
        };
        void ZetLive( const LiveStand &stand );
        void ZetHubGeheim( const std::string &geheim );
        int Formaat() const;
        std::string HubGeheim() const;
        void ZetIngeschakeld( bool ingeschakeld );
        std::string EndpointUrl() const;
        std::string Sleutel() const;
        bool IsIngeschakeld() const;

        // Payload builder, public so a test can hold the JSON against
        // docs/TRIP_FORMAT.md without any network in between.
        std::string BouwRitJson( const Trip &trip, const TachoMoment &tacho, const RitContext &ctx ) const;

    private:
        struct Instellingen
        {
            std::string endpointUrl;
            std::string sleutel;
            bool ingeschakeld = false;
            // 0 = the CabNavi trip format (docs/TRIP_FORMAT.md), 1 = the
            // envelope format some hubs expect (HubFormaat.hxx), signed with
            // hubGeheim in the header those hubs look for.
            int formaat = 0;
            std::string hubGeheim;
            // Live position to the company's own system, every 10 s while
            // driving. OFF by default: this is where you are, right now.
            bool livePositie = false;
        };

        void LaadInstellingen();
        void SlaInstellingenOp() const;
        static std::filesystem::path InstellingenPad();

        void Verstuur( const std::string &jsonBody );
        void WorkerLoop();
        void VerstuurBericht( const std::string &url, const std::string &sleutel,
                               const std::string &jsonBody,
                               const std::string &extraHeaderNaam = std::string(),
                               const std::string &extraHeaderWaarde = std::string() ) const;

        Instellingen m_instellingen;
        mutable std::mutex m_mutex;

        struct WerkItem
        {
            std::string url;
            std::string sleutel;
            std::string jsonBody;
            std::string extraHeaderNaam, extraHeaderWaarde;   // envelope format: the signature
        };
        std::deque<WerkItem> m_wachtrij;
        LiveStand m_live;                 // guarded by m_mutex
        std::chrono::steady_clock::time_point m_laatsteLive{};
        std::mutex m_queueMutex;
        std::condition_variable m_queueCv;
        std::thread m_worker;
        std::atomic<bool> m_stoppen{ false };
    };
}

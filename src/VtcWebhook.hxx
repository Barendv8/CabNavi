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

#include <atomic>
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
        void StuurRit( const Trip &trip, const TachoMoment &tacho );

        // A hand-made ping so the user can see the connection work before
        // any trip finishes (button in the settings, like Discord's).
        void StuurTestbericht();

        void ZetEndpointUrl( const std::string &url );
        void ZetSleutel( const std::string &sleutel );
        void ZetIngeschakeld( bool ingeschakeld );
        std::string EndpointUrl() const;
        std::string Sleutel() const;
        bool IsIngeschakeld() const;

        // Payload builder, public so a test can hold the JSON against
        // docs/TRIP_FORMAT.md without any network in between.
        std::string BouwRitJson( const Trip &trip, const TachoMoment &tacho ) const;

    private:
        struct Instellingen
        {
            std::string endpointUrl;
            std::string sleutel;
            bool ingeschakeld = false;
        };

        void LaadInstellingen();
        void SlaInstellingenOp() const;
        static std::filesystem::path InstellingenPad();

        void Verstuur( const std::string &jsonBody );
        void WorkerLoop();
        void VerstuurBericht( const std::string &url, const std::string &sleutel,
                               const std::string &jsonBody ) const;

        Instellingen m_instellingen;
        mutable std::mutex m_mutex;

        struct WerkItem
        {
            std::string url;
            std::string sleutel;
            std::string jsonBody;
        };
        std::deque<WerkItem> m_wachtrij;
        std::mutex m_queueMutex;
        std::condition_variable m_queueCv;
        std::thread m_worker;
        std::atomic<bool> m_stoppen{ false };
    };
}

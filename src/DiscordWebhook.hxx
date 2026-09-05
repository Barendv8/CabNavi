#pragma once
// DiscordWebhook.hxx
//
// Sends a tidy notification to a Discord webhook when a trip is completed
// or cancelled. Uses WinHTTP (built into Windows, no extra dependency) for
// the POST request.
//
// Like TripLogger: the actual network call happens on its own background
// thread with a queue, never on the game thread (see Threading in the SDK
// docs -- callbacks must not do I/O).
//
// The webhook URL and the on/off state are stored in
// %APPDATA%\CabNavi\discord.json (separate from FuelCosts'
// instellingen.json, so two independent components never overwrite each
// other's settings file).

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
    class DiscordWebhook
    {
    public:
        DiscordWebhook();
        ~DiscordWebhook();

        // Safe to call from the game thread: puts the message on the send
        // queue and returns immediately. Does nothing if no (valid) webhook
        // URL is set or if it is switched off.
        void StuurRitVoltooid( const Trip &trip );

        // Send a test message manually (e.g. via a button in the overlay).
        // This one also goes onto the background queue.
        void StuurTestbericht();

        // Settings, saved to disk immediately on change.
        void ZetWebhookUrl( const std::string &url );
        void ZetIngeschakeld( bool ingeschakeld );
        std::string WebhookUrl() const;
        bool IsIngeschakeld() const;

    private:
        struct Instellingen
        {
            std::string webhookUrl;
            bool ingeschakeld = false;
        };

        void LaadInstellingen();
        void SlaInstellingenOp() const;
        static std::filesystem::path InstellingenPad();

        void WorkerLoop();
        void VerstuurBericht( const std::string &url, const std::string &jsonBody ) const;
        std::string BouwEmbedJson( const Trip &trip ) const;

        struct WerkItem
        {
            std::string url;
            std::string jsonBody;
        };

        mutable std::mutex m_mutex;
        Instellingen m_instellingen;

        std::mutex m_queueMutex;
        std::condition_variable m_queueCv;
        std::deque<WerkItem> m_wachtrij;

        std::atomic<bool> m_stoppen{ false };
        std::thread m_worker;
    };
}

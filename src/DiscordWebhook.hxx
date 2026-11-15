#pragma once

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

        void StuurRitVoltooid( const Trip &trip, double leegKm = 0.0, double leegKosten = 0.0 );

        void StuurTestbericht();

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
        std::string BouwEmbedJson( const Trip &trip, double leegKm, double leegKosten ) const;

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

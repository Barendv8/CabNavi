#pragma once
// DiscordWebhook.hxx
//
// Stuurt een nette melding naar een Discord-webhook zodra een rit is
// afgerond of geannuleerd. Gebruikt WinHTTP (ingebouwd in Windows, geen
// extra afhankelijkheid nodig) om de POST-request te doen.
//
// Net als TripLogger: de daadwerkelijke netwerk-aanroep gebeurt op een
// eigen achtergrond-thread met een wachtrij, nooit op de game-thread (zie
// Threading in de SDK-docs -- callbacks mogen geen I/O doen).
//
// De webhook-URL en of het aan/uit staat worden bewaard in
// %APPDATA%\CabNavi\discord.json (los van instellingen.json van
// FuelCosts, om te voorkomen dat twee onafhankelijke onderdelen elkaars
// instellingenbestand overschrijven).

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

        // Veilig aan te roepen vanaf de game-thread: zet het bericht op de
        // verstuur-queue en keert direct terug. Doet niets als er geen
        // (geldige) webhook-URL is ingesteld of als het uit staat.
        void StuurRitVoltooid( const Trip &trip );

        // Handmatig een testbericht sturen (bv. via een knop in de
        // overlay). Ook deze komt op de achtergrond-queue terecht.
        void StuurTestbericht();

        // Instellingen, direct opgeslagen naar disk bij wijziging.
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

#pragma once

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

        struct TachoMoment
        {
            bool geldig = false;
            double rijMinutenSindsRust = 0.0;
            bool inRust = false;
        };

        struct RitContext
        {
            double prijsPerLiter = 0.0;
            int prijsBron = -1;
            double leegKm = 0.0, leegLiters = 0.0, leegKosten = 0.0;
            HubFormaat::Context hub;
        };
        void StuurRit( const Trip &trip, const TachoMoment &tacho, const RitContext &ctx );
        void StuurRit( const Trip &trip, const TachoMoment &tacho ) { StuurRit( trip, tacho, RitContext{} ); }

        void StuurTestbericht();

        void ZetEndpointUrl( const std::string &url );
        void ZetSleutel( const std::string &sleutel );
        void ZetFormaat( int formaat );
        void ZetLivePositie( bool aan );
        bool LivePositie() const;

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

        std::string BouwRitJson( const Trip &trip, const TachoMoment &tacho, const RitContext &ctx ) const;

    private:
        struct Instellingen
        {
            std::string endpointUrl;
            std::string sleutel;
            bool ingeschakeld = false;
            int formaat = 0;
            std::string hubGeheim;
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
            std::string extraHeaderNaam, extraHeaderWaarde;
        };
        std::deque<WerkItem> m_wachtrij;
        LiveStand m_live;
        std::chrono::steady_clock::time_point m_laatsteLive{};
        std::mutex m_queueMutex;
        std::condition_variable m_queueCv;
        std::thread m_worker;
        std::atomic<bool> m_stoppen{ false };
    };
}

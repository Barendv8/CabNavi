#pragma once

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
        std::string start;
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
        std::string id;
        std::string vertrek;
        std::string aankomst;
        std::string lading;
        std::string deadline;
        std::string notitie;
        std::string status;
        bool accepteerbaar = false;
    };

    struct VtcOverzicht
    {
        bool geldig = false;
        VtcBronType bron = VtcBronType::Geen;
        std::string bronNaam;
        std::string status;

        std::string bedrijf;
        std::string tag;
        int leden = 0;
        double totaalKm = 0.0;

        std::string mijnNaam;
        std::string mijnRol;
        double mijnKm = 0.0;
        int mijnRitten = 0;

        std::vector<std::string> online;
        std::vector<BronConvooi> convooien;
        std::vector<BronNieuws> nieuws;
        std::vector<Opdracht> opdrachten;
    };

    class VtcBron
    {
    public:
        explicit VtcBron( VtcWebhook &webhook );
        ~VtcBron();

        VtcOverzicht Overzicht() const;

        VtcBronType Type() const;
        std::string Sleutel() const;
        void ZetType( VtcBronType t );
        void ZetSleutel( const std::string &sleutel );

        void ZetEigenSteamId( std::uint64_t steamId );

        void AccepteerOpdracht( const std::string &id );

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

        int m_truckyUserId = 0;
        int m_truckyCompanyId = 0;
        int m_vtlogVtcId = 0;

        std::thread m_thread;
        std::condition_variable m_wekker;
        std::atomic<bool> m_stop{ false };
        std::atomic<bool> m_nu{ false };
    };
}

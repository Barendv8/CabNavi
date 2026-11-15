#pragma once

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
    struct ServerInfo
    {
        std::string naam;
        std::string spel;
        int spelers = 0;
        int maxSpelers = 0;
        int wachtrij = 0;
        bool online = false;
        bool collisions = false;
        bool snelheidsbegrenzer = false;
    };

    struct EvenementInfo
    {
        int id = 0;
        std::string naam;
        std::string vertrek;
        std::string aankomst;
        std::string startTijd;
        std::string spel;
        std::string server;
    };

    struct VtcInfo
    {
        bool geldig = false;
        std::string naam;
        std::string tag;
        std::string slogan;
        std::string taal;
        std::string werving;
        int leden = 0;
        bool geverifieerd = false;
    };

    struct VtcNieuwsInfo
    {
        std::string titel;
        std::string datum;
        std::string auteur;
        bool vastgezet = false;
    };

    class WebApi
    {
    public:
        WebApi();
        ~WebApi();

        void ZetIngeschakeld( bool aan );
        bool Ingeschakeld() const { return m_aan.load(); }

        void ZetVtc( bool aan, int vtcId );

        void ZetEigenAccount( std::uint64_t accountId );


        bool VtcIngeschakeld() const { return m_vtcAan.load(); }
        int VtcId() const { return m_vtcId.load(); }

        void MeldSpelerAan( std::uint64_t accountId, bool voorrang = false );

        int SpelerVtcId( std::uint64_t accountId ) const;

        int SpelerIsPatron( std::uint64_t accountId ) const;

        int SpelerPatron( std::uint64_t accountId ) const;

        void OpzoekStand( int &opgezochtUit, int &inRijUit ) const;

        std::vector<ServerInfo> Servers() const;
        std::vector<EvenementInfo> Evenementen() const;
        VtcInfo Vtc() const;
        std::vector<EvenementInfo> VtcEvenementen() const;

        std::vector<EvenementInfo> AangemeldeEvenementen() const;

        std::vector<EvenementInfo> VtcAangemeld() const;
        std::vector<VtcNieuwsInfo> VtcNieuws() const;

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
        std::atomic<bool> m_vtcNuOphalen{ false };
        std::atomic<bool> m_stoppen{ false };
        std::thread m_thread;

        mutable std::mutex m_slot;
        std::vector<ServerInfo> m_servers;
        std::vector<EvenementInfo> m_evenementen;
        VtcInfo m_vtc;
        std::vector<EvenementInfo> m_vtcEvenementen;
        std::vector<EvenementInfo> m_aangemeldVtc;
        std::atomic<std::uint64_t> m_eigenAccount{ 0 };
        std::vector<VtcNieuwsInfo> m_vtcNieuws;
        struct SpelerGegevens
        {
            int vtcId = 0;
            bool patron = false;
        };
        std::map<std::uint64_t, SpelerGegevens> m_spelerVtc;
        std::deque<std::uint64_t> m_wachtrij;

        std::set<std::uint64_t> m_bezig;

        int m_rustSeconden = 0;
        bool m_cacheGewijzigd = false;

        std::string m_status = "nog niet opgehaald";
        std::string m_vtcStatus = "nog niet opgehaald";
    };
}

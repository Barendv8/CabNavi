#pragma once
// WebApi.hxx
//
// Haalt gegevens op bij de publieke TruckersMP Web API
// (https://api.truckersmp.com/v2). Geen sleutel of inlog nodig.
//
// Twee regels waar dit zich aan houdt:
//
//  1. NOOIT op de spelthread. Netwerkverkeer duurt honderden milliseconden;
//     dat zou het spel laten haperen. Alles gebeurt op een eigen thread, net
//     als bij de Discord-webhook. De overlay leest alleen een kopie van het
//     laatste antwoord.
//
//  2. Rustig aan. Dit is community-data, geen telemetrie. Serverstatus eens
//     per minuut en evenementen eens per kwartier is ruim voldoende. Vaker
//     vragen levert niets op en belast hun servers onnodig.

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
    // Eén server zoals de API hem teruggeeft.
    struct ServerInfo
    {
        std::string naam;          // "Simulation 1"
        std::string spel;          // "ETS2" of "ATS"
        int spelers = 0;
        int maxSpelers = 0;
        int wachtrij = 0;
        bool online = false;
        bool collisions = false;   // botsingen aan of uit
        bool snelheidsbegrenzer = false;
    };

    // Eén aankomend evenement.
    struct EvenementInfo
    {
        int id = 0;                // TruckersMP-evenementnummer, om aan te vinken
        std::string naam;
        std::string vertrek;       // stad van vertrek
        std::string aankomst;      // stad van aankomst
        std::string startTijd;     // zoals de API hem geeft (UTC)
        std::string spel;
        std::string server;
    };

    // Gegevens van één VTC, zoals /v2/vtc/{id} ze teruggeeft.
    struct VtcInfo
    {
        bool geldig = false;
        std::string naam;
        std::string tag;
        std::string slogan;
        std::string taal;
        std::string werving;       // "Open" of "Close"
        int leden = 0;
        bool geverifieerd = false;
    };

    // Eén nieuwsbericht van een VTC.
    struct VtcNieuwsInfo
    {
        std::string titel;
        std::string datum;         // published_at, zoals de API hem geeft
        std::string auteur;
        bool vastgezet = false;
    };

    class WebApi
    {
    public:
        WebApi();
        ~WebApi();

        // Aan/uit. Staat standaard UIT: netwerkverkeer hoort iets te zijn
        // waar je zelf voor kiest, niet iets wat een plugin stiekem doet.
        void ZetIngeschakeld( bool aan );
        bool Ingeschakeld() const { return m_aan.load(); }

        // VTC-integratie. Apart aan te zetten, met een eigen nummer: dat
        // staat in het adres van je VTC-pagina (truckersmp.com/vtc/<nummer>).
        // 0 = geen VTC ingesteld; dan wordt er ook niets opgehaald.
        void ZetVtc( bool aan, int vtcId );

        // Je eigen TruckersMP-ID (uit de SDK). Nodig om op te halen voor
        // welke evenementen JIJ je hebt aangemeld -- zonder dit weten we
        // alleen wat je VTC doet.
        void ZetEigenAccount( std::uint64_t accountId );


        bool VtcIngeschakeld() const { return m_vtcAan.load(); }
        int VtcId() const { return m_vtcId.load(); }

        // --- VTC per speler ------------------------------------------------
        // Het VTC-NUMMER is het enige echt unieke kenmerk: een tag is tekst
        // die iedereen kan intypen, een nummer krijgt een bedrijf één keer
        // van TruckersMP. Opvragen kost wel één verzoek per speler, dus dat
        // gaat via een wachtrij op een rustig tempo, en het antwoord wordt
        // onthouden -- iemands bedrijf verandert hooguit eens per maand.
        //
        // Aanmelden gebeurt vanuit de overlay voor de spelers die in beeld
        // zijn; al bekende of al aangemelde spelers worden overgeslagen.
        // voorrang: dichtbij eerst. Die zie je toch als eerste op de radar,
        // dus die wil je niet achteraan in de rij hebben staan.
        void MeldSpelerAan( std::uint64_t accountId, bool voorrang = false );

        // -1 = nog niet opgezocht, 0 = zit niet bij een VTC.
        int SpelerVtcId( std::uint64_t accountId ) const;

        // Is deze speler patron volgens de Web API? -1 = nog niet opgezocht.
        // GEMETEN 30-08: de SDK-vlag IsPatron blijft op false staan, ook bij
        // iemand die aantoonbaar patron is. De API zegt het wel, en die
        // vraag zit al in hetzelfde antwoord als het VTC-nummer -- dus dit
        // kost geen extra verzoek.
        int SpelerIsPatron( std::uint64_t accountId ) const;

        // Patron volgens de WEB API. De SDK heeft hier ook een vlag voor,
        // maar die bleek onbetrouwbaar: een speler met een actieve Master
        // Trucker-bijdrage kwam er als NIET-patron uit (gemeten 30-08).
        // Het antwoord komt uit dezelfde opvraging als het VTC-nummer, dus
        // dit kost geen extra verzoek.
        // -1 = nog niet opgezocht, 0 = geen patron, 1 = wel.
        int SpelerPatron( std::uint64_t accountId ) const;

        // Voor de weergave: hoeveel spelers zijn er al opgezocht, en hoeveel
        // staan er nog in de rij. Zonder dit moet je maar raden of het werkt.
        void OpzoekStand( int &opgezochtUit, int &inRijUit ) const;

        // Kopieën van het laatste antwoord. Veilig vanaf de spelthread.
        std::vector<ServerInfo> Servers() const;
        std::vector<EvenementInfo> Evenementen() const;
        VtcInfo Vtc() const;
        std::vector<EvenementInfo> VtcEvenementen() const;

        // Convooien waarvoor JIJ of je VTC zich heeft aangemeld. Alleen
        // hieruit komt de herinnering op de Live-tab: een melding over een
        // convooi waar je niks mee te maken hebt is alleen maar ruis.
        std::vector<EvenementInfo> AangemeldeEvenementen() const;

        // Alleen waar je VTC zich voor heeft aangemeld, los van jouw eigen
        // aanmeldingen -- voor de lijst op het VTC-tabblad.
        std::vector<EvenementInfo> VtcAangemeld() const;
        std::vector<VtcNieuwsInfo> VtcNieuws() const;

        // Voor de weergave: wanneer voor het laatst opgehaald, en of het lukte.
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
        std::atomic<bool> m_vtcNuOphalen{ false }; // gezet door ZetVtc, zodat een wijziging meteen effect heeft
        std::atomic<bool> m_stoppen{ false };
        std::thread m_thread;

        mutable std::mutex m_slot;
        std::vector<ServerInfo> m_servers;
        std::vector<EvenementInfo> m_evenementen;
        VtcInfo m_vtc;
        std::vector<EvenementInfo> m_vtcEvenementen;
        std::vector<EvenementInfo> m_aangemeldVtc;     // je VTC
        std::atomic<std::uint64_t> m_eigenAccount{ 0 };
        std::vector<VtcNieuwsInfo> m_vtcNieuws;
        // Wat we per speler weten, en wat er nog opgezocht moet worden.
        // De cache blijft ook staan als een speler uit beeld verdwijnt: kom
        // je hem later weer tegen, dan hoef je niets opnieuw te vragen.
        // Wat we per speler uit één opvraging halen.
        struct SpelerGegevens
        {
            int vtcId = 0;
            bool patron = false;
        };
        std::map<std::uint64_t, SpelerGegevens> m_spelerVtc;
        std::deque<std::uint64_t> m_wachtrij;

        // Wie er op DIT MOMENT wordt opgezocht. Zonder dit zit zo iemand
        // even in niemandsland -- niet meer in de rij, nog niet in de cache
        // -- en meldt de overlay hem gewoon opnieuw aan. Gemeten 30-08:
        // elke geslaagde opzoeking stond twee keer in het logboek.
        std::set<std::uint64_t> m_bezig;

        // Terugschakelen na een mislukking. De API heeft een limiet die
        // nergens gedocumenteerd staat; op vijf per seconde mislukte 68%
        // van de verzoeken (gemeten 30-08). Loopt het mis, dan even
        // helemaal stoppen in plaats van harder duwen.
        int m_rustSeconden = 0;
        bool m_cacheGewijzigd = false;

        std::string m_status = "nog niet opgehaald";
        std::string m_vtcStatus = "nog niet opgehaald";
    };
}

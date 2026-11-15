#pragma once

#include "BusTracking.hxx"
#include "DiscordWebhook.hxx"
#include "VtcWebhook.hxx"
#include "VtcBron.hxx"
#include "FuelCosts.hxx"
#include "IncidentRecorder.hxx"
#include "WebApi.hxx"
#include "PlayersNearby.hxx"
#include "TripLogger.hxx"
#include "TruckTracking.hxx"

#include <TruckersMP/TruckersMP.hxx>

#include <chrono>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ImFont;
struct ImVec4;
struct IDXGISwapChain;

namespace Ritten
{
    class Overlay
    {
    public:
        Overlay( TripLogger &logger, BusTracking &bus, TruckTracking &vracht,
                 PlayersNearby &spelers, FuelCosts &brandstof, DiscordWebhook &discord,
                 VtcWebhook &vtc, VtcBron &vtcBron, IncidentRecorder &incident );
        ~Overlay();

        bool InitDirectX11( ID3D11Device *device, void *vensterHandle );
        void Shutdown();

        void Teken();

        void OpMuisBeweging( int x, int y );
        void OpMuisKnop( int knop, bool ingedrukt );
        void OpMuisWiel( float delta );
        void OpToets( unsigned int virtualKeyCode, bool ingedrukt );
        void OpKarakter( unsigned int codepoint );
        bool WilMuis() const;
        bool WilToetsenbord() const;

        void SchakelZichtbaarheid() { m_zichtbaar = !m_zichtbaar; }
        bool IsZichtbaar() const { return m_zichtbaar; }

    private:
        void TekenLiveTab();
        void TekenBoordcomputerTab();

        void TekenTachoStrip();
        void TekenSpelersTab();
        void TekenGeschiedenisTab();
        void TekenStatistiekenTab();
        void TekenInstellingenTab();
        void TekenVtcTab();
        void TekenVtcInstellingenTab();

        void SectieStart( const char *naam, ImVec4 kleur, float vasteHoogte = 0.0f );

        float SectieHoogte( int tekstRegels, int velden, int spaties = 0 ) const;
        void SectieEind();

        float VeldBreedte() const;
        void TekenIncidentTab();

        void TekenReportScherm();

        void TekenSpelerContextKnop( const SpelerRecord &speler, const std::string &uniekeId );

        void TekenPassagierIcoon( float grootte, unsigned int kleur );

        void TekenRustIcoon( bool verplichtePauze, float grootte, unsigned int kleur );

        void TekenVoertuigIcoon( bool isBus, float grootte = 20.0f );

        void TekenTabIcoon( int tabIndex, float middenX, float middenY, float straal, unsigned int kleur );

        TripLogger &m_logger;
        BusTracking &m_bus;
        TruckTracking &m_vracht;
        PlayersNearby &m_spelers;
        FuelCosts &m_brandstof;
        DiscordWebhook &m_discord;
        VtcWebhook &m_vtc;
        VtcBron &m_vtcBron;

        WebApi m_webApi;

        bool m_vtcAan = false;
        int m_vtcId = 0;
        bool m_vtcTagsBijSpelers = true;
        bool m_vtcRadarMarkering = false;
        bool m_vtcConvooienTonen = true;

        bool m_vtcSpelersOpzoeken = false;

        bool m_eigenConvooien = false;
        std::vector<EvenementInfo> m_aangevinkt;
        bool IsAangevinkt( int evenementId ) const;
        void ZetAangevinkt( const EvenementInfo &e, bool aan );
        std::vector<EvenementInfo> MijnConvooien() const;
        char m_vtcIdBuffer[ 16 ] = "";
        bool m_vtcIdBufferGeladen = false;

        char m_vtcTagsBuffer[ 128 ] = "";
        bool IsEigenVtc( const SpelerRecord &s ) const;

        bool IsPatron( const SpelerRecord &s ) const;

        std::string ConvooiHerinnering() const;
        void TekenConvooiHerinnering();

        void TekenRijstijl();

        bool m_zuinigheidTonen = true;

        bool m_webApiAan = true;
        bool m_kaartDownload = true;

        int m_taal = 0;

        bool m_uitgebreidLog = false;

        bool m_klokTonen = true;
        static constexpr int HERINNERING_MINUTEN = 60;
        void LaadVtcInstellingen();
        void SlaVtcInstellingenOp();
        IncidentRecorder &m_incident;
        int m_incidentFrameIndex = 0;

        SpelerRecord m_reportPopupSpeler;
        std::string m_reportPopupSpelerId;

        bool m_reportPopupOpenen = false;

        int m_incidentTellerGezien = -1;
        std::vector<char> m_reportRedenenAangevinkt;
        char m_reportOmschrijving[ 1024 ] = "";
        char m_reportBewijsLink[ 256 ] = "";
        char m_prijsBuffer[ 16 ] = "1.65";
        int m_eenheden = 0;
        char m_webhookBuffer[ 256 ] = "";
        bool m_webhookBufferGeladen = false;
        char m_vtcUrlBuffer[ 512 ] = "";
        char m_vtcSleutelBuffer[ 256 ] = "";
        char m_bronSleutelBuffer[ 256 ] = "";
        bool m_bronBufferGeladen = false;
        char m_hubGeheimBuffer[ 256 ] = "";
        bool m_hubGeheimGeladen = false;
        bool m_vtcBuffersGeladen = false;

        float m_doorzichtigheid = 0.90f;
        float m_iconenDoorzichtigheid = 0.95f;
        float m_accentKleur[ 3 ] = { 0.83f, 0.55f, 0.16f };

        std::string m_iniPad;

        float m_minimapScrollKm = 0.0f;
        std::chrono::steady_clock::time_point m_minimapLaatsteUpdate;
        bool m_uiterlijkGeladen = false;
        void LaadUiterlijk();
        void SlaUiterlijkOp() const;
        unsigned int AccentKleurU32( float alpha = 1.0f ) const;

        ImVec4 KaartKleur() const;
        ImVec4 TintKaartKleur( const ImVec4 &tint, float sterkte ) const;

        void KopBalk( const char *tekst );

        void StatKaart( const char *label, const std::string &waarde, float breedte,
                         const char *onderschrift = nullptr, bool waarschuwing = false,
                         bool compact = false );

        float KaartHoogte( bool metOnderschrift, bool compact = false ) const;

        void SchadeBalk( const char *naam, double percentage, const unsigned int *kleurOverride = nullptr,
                          float labelBreedte = 100.0f, float breedte = 0.0f );

        void LaadLogo();
        void *m_logoTextuur = nullptr;
        int m_logoBreedte = 0;
        int m_logoHoogte = 0;

        static constexpr int AANTAL_TABS = 9;
        void *m_tabTexturen[ AANTAL_TABS ] = {};
        void LaadTabIconen();

        ImFont *m_kopFont = nullptr;

        ImFont *m_kleinFont = nullptr;

        bool m_kleinFontActief = false;

        bool m_geinitialiseerd = false;
        bool m_zichtbaar = true;
        int m_actieveTab = 0;
        void *m_vensterHandle = nullptr;
        ID3D11Device *m_device = nullptr;
        ID3D11DeviceContext *m_context = nullptr;
    };
}

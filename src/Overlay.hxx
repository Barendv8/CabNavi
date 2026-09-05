#pragma once
// Overlay.hxx
//
// Draws a modern ImGui window on top of the game: a "Live" tab with the
// active trip (bus or cargo), a "History" tab with the latest trips and a
// "Statistics" tab with totals. Initialised with the DirectX11 device of
// the Render module and drawn in OnPostRender (see the Render module docs:
// "most overlays want" OnPostRender).

#include "BusTracking.hxx"
#include "DiscordWebhook.hxx"
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
// ImVec4 is used below as return/parameter type (KaartKleur,
// TintKaartKleur). Only forward-declare, do not include imgui.h: that
// header does not belong in our public interface, and the .cxx that
// implements these functions already pulls in imgui.h itself.
struct ImVec4;
struct IDXGISwapChain;

namespace Ritten
{
    class Overlay
    {
    public:
        Overlay( TripLogger &logger, BusTracking &bus, TruckTracking &vracht,
                 PlayersNearby &spelers, FuelCosts &brandstof, DiscordWebhook &discord,
                 IncidentRecorder &incident );
        ~Overlay();

        // Call as soon as Render().GetRendererID() == DirectX11 and a device
        // handle is available.
        bool InitDirectX11( ID3D11Device *device, void *vensterHandle );
        void Shutdown();

        // Registered in Render().OnPostRender.
        void Teken();

        // Forward input from the TruckersMP Input module to ImGui. Without
        // this you draw an overlay, but it reacts to nothing: the SDK has no
        // "window hook" like Dear ImGui's Win32 backend normally uses, so
        // mouse/keyboard must be passed on by us.
        // Returns whether the overlay "claims" the input -- if so, the caller
        // must block the corresponding event for the game itself (see
        // InputMouseButtonEvent::SetBlock in the SDK docs).
        void OpMuisBeweging( int x, int y );
        void OpMuisKnop( int knop, bool ingedrukt );
        void OpMuisWiel( float delta );
        void OpToets( unsigned int virtualKeyCode, bool ingedrukt );
        void OpKarakter( unsigned int codepoint );
        bool WilMuis() const;
        bool WilToetsenbord() const;

        // Toggle key (e.g. Insert) via the Input module.
        void SchakelZichtbaarheid() { m_zichtbaar = !m_zichtbaar; }
        bool IsZichtbaar() const { return m_zichtbaar; }

    private:
        void TekenLiveTab();
        void TekenBoordcomputerTab();

        // Narrow tachograph strip for the Live tab: only header, bar and
        // time on ONE line. The full tachograph card is on the board
        // computer tab; this strip exists so you do not have to switch tabs
        // while driving to see your driving time.
        void TekenTachoStrip();
        void TekenSpelersTab();
        void TekenGeschiedenisTab();
        void TekenStatistiekenTab();
        void TekenInstellingenTab();
        void TekenVtcTab();
        void TekenVtcInstellingenTab();

        // Settings sections as cards: opens a framed box with a coloured
        // header. Close with SectieEind().
        //
        // vasteHoogte (optional, default 0): 0 means exactly as always -- the
        // card grows automatically with its content. Greater than 0 fixes
        // the height; only UITERLIJK uses that.
        void SectieStart( const char *naam, ImVec4 kleur, float vasteHoogte = 0.0f );

        // COMPUTE the height of a section instead of typing a pixel number.
        // See the warning at KaartHoogte(): "a fixed number went wrong as
        // soon as the header font turned out larger than planned -- the
        // caption fell out and ImGui put a scrollbar in." Exactly that
        // happened here too.
        //
        // You specify WHAT is in it; the sizes come from ImGui itself, so it
        // scales with the font:
        //   tekstRegels = lines of TekstGedimd / TextDisabled
        //   velden      = dropdowns, input fields, sliders, checkboxes, buttons
        //   spaties     = separate ImGui::Spacing()
        float SectieHoogte( int tekstRegels, int velden, int spaties = 0 ) const;
        void SectieEind();

        // Width for a slider or dropdown in the settings. Scales with the
        // box instead of a fixed number, so no emptiness is left on the right
        // -- but with limits, because an 800-pixel slider is no good either.
        float VeldBreedte() const;
        void TekenIncidentTab();

        // Reusable: small "..." button that opens a context menu with Steam
        // profile/TruckersMP profile/copy ID for a player.
        // The report screen. SEPARATE from the context button: that runs per
        // player, and then this screen was drawn just as often.
        void TekenReportScherm();

        void TekenSpelerContextKnop( const SpelerRecord &speler, const std::string &uniekeId );

        // Draws a small hand-drawn truck or bus icon at the current cursor
        // position (reserves layout space via Dummy, so just use
        // ImGui::SameLine() afterwards for text next to it).
        // Small passenger figure, meant to sit next to a count.
        void TekenPassagierIcoon( float grootte, unsigned int kleur );

        void TekenVoertuigIcoon( bool isBus, float grootte = 20.0f );

        // Hand-drawn sidebar icons (instead of text labels or emoji, which
        // are respectively dull and unsupported by ImGui's font). Draws
        // centred on the current cursor position within a size x size square,
        // colour is passed in.
        void TekenTabIcoon( int tabIndex, float middenX, float middenY, float straal, unsigned int kleur );

        TripLogger &m_logger;
        BusTracking &m_bus;
        TruckTracking &m_vracht;
        PlayersNearby &m_spelers;
        FuelCosts &m_brandstof;
        DiscordWebhook &m_discord;

        // Public TruckersMP Web API: server status and events. Owned by the
        // overlay itself, because only the statistics tab uses it.
        WebApi m_webApi;

        // --- VTC integration -----------------------------------------------
        // Switched on separately from the rest. The number is in the address
        // of your VTC page: truckersmp.com/vtc/<number>. Stored in vtc.json,
        // so it survives a restart.
        bool m_vtcAan = false;
        int m_vtcId = 0;
        bool m_vtcTagsBijSpelers = true;
        bool m_vtcRadarMarkering = false;
        bool m_vtcConvooienTonen = true;

        // Look up players at the Web API (for VTC number and patron). Can be
        // off entirely: the marking then still works on tags, only without
        // requests to their servers. See also the 429 we got on 30-08 --
        // their limit is documented nowhere, so we are frugal with it and
        // leave the choice to you.
        bool m_vtcSpelersOpzoeken = false;

        // Convoys ticked by yourself. The Web API cannot tell what you signed
        // up for -- /events/user/{id} returns what you CREATED, not what you
        // registered for (measured 30-08: empty answer while there were
        // sign-ups). Hence ticking yourself: costs zero requests and is
        // guaranteed to work.
        //
        // SEPARATE from the VTC: even without a company you sign up for
        // convoys, and then you simply want that reminder.
        bool m_eigenConvooien = false;
        std::vector<EvenementInfo> m_aangevinkt;
        bool IsAangevinkt( int evenementId ) const;
        void ZetAangevinkt( const EvenementInfo &e, bool aan );
        std::vector<EvenementInfo> MijnConvooien() const;
        char m_vtcIdBuffer[ 16 ] = "";
        bool m_vtcIdBufferGeladen = false;

        // Tags that are marked on the radar and in the list, comma-separated.
        // The SDK already passes the tag shown in front of someone's name in
        // the game (Player::GetTagText), and for VTC members that is usually
        // exactly their company tag -- so no API requests per player are
        // needed here. You can enter several, for example if your company
        // uses different department tags.
        char m_vtcTagsBuffer[ 128 ] = "";
        bool IsEigenVtc( const SpelerRecord &s ) const;

        // The SDK flag IsPatron stays false, even for someone who
        // demonstrably is a patron (measured 30-08). The Web API does say
        // it, and we fetch that answer anyway for the VTC number.
        bool IsPatron( const SpelerRecord &s ) const;

        // Little line about a convoy coming up soon. Only for convoys YOU or
        // your VTC signed up for -- a notice about a convoy you have nothing
        // to do with is noise. Empty = nothing to report, and then nothing is
        // drawn.
        std::string ConvooiHerinnering() const;
        void TekenConvooiHerinnering();

        // One line below the cards: are you driving this trip more or less
        // economically than your own average from the trip log? Empty if
        // there is too little to compare -- then nothing is drawn.
        void TekenRijstijl();

        // On/off for that economy line. Stored in uiterlijk.json with the
        // other display choices.
        bool m_zuinigheidTonen = true;

        // Network switches, both default ON and remembered in uiterlijk.json:
        // the TruckersMP Web API (server status, events) and the map table
        // download from the CabNavi repository.
        bool m_webApiAan = true;
        bool m_kaartDownload = true;

        // 0 = Dutch, 1 = English. Dutch is the base: texts without a
        // translation simply stay Dutch (see Taal.hxx).
        int m_taal = 0;

        // Verbose diagnostics in debug.log. Default off: those lines are
        // only needed while investigating something, and they write to disk
        // every few seconds.
        bool m_uitgebreidLog = false;

        // Show the real PC time at the top right of the header. Default on:
        // handy to see the IRL time without alt-tab, and needed if you ever
        // want to lay a screen recording next to debug.log.
        bool m_klokTonen = true;
        static constexpr int HERINNERING_MINUTEN = 60;
        void LaadVtcInstellingen();
        void SlaVtcInstellingenOp();
        IncidentRecorder &m_incident;
        int m_incidentFrameIndex = 0;  // position of the timeline slider in the replay viewer

        // For the report screen (see TekenSpelerContextKnop): which player
        // and which reasons are currently ticked.
        SpelerRecord m_reportPopupSpeler;
        std::string m_reportPopupSpelerId;

        // The report screen may only open AFTER the context popup is closed;
        // otherwise the popup belongs to the wrong window and never appears.
        // See the explanation in TekenSpelerContextKnop.
        bool m_reportPopupOpenen = false;

        // Which incident recording have we already shown? If this number
        // changes there is a new impact and the slider jumps to the end.
        int m_incidentTellerGezien = -1;
        std::vector<char> m_reportRedenenAangevinkt;
        char m_reportOmschrijving[ 1024 ] = "";
        char m_reportBewijsLink[ 256 ] = "";
        char m_prijsBuffer[ 16 ] = "1.65";
        char m_webhookBuffer[ 256 ] = "";
        bool m_webhookBufferGeladen = false;

        // Appearance: transparency and accent colour, adjustable by the user
        // on the Settings tab and stored in %APPDATA%\CabNavi\uiterlijk.json.
        float m_doorzichtigheid = 0.90f;
        float m_iconenDoorzichtigheid = 0.95f;  // separate slider for sidebar/menus/icons
        float m_accentKleur[ 3 ] = { 0.83f, 0.55f, 0.16f };  // warm amber/gold, like TMP's own interface

        // Path to imgui.ini. MUST be a member: io.IniFilename is a raw
        // pointer ImGui does not copy, so a local string would leave a
        // dangling pointer after the function ends.
        std::string m_iniPad;


        // For the "alive-feeling" scroll effect on the minimap: an
        // accumulated distance that moves with your real speed (no real
        // position, purely a visual sense of motion -- see the note in
        // PlayersNearby.hxx about why we have no real GPS).
        float m_minimapScrollKm = 0.0f;
        std::chrono::steady_clock::time_point m_minimapLaatsteUpdate;
        bool m_uiterlijkGeladen = false;
        void LaadUiterlijk();
        void SlaUiterlijkOp() const;
        unsigned int AccentKleurU32( float alpha = 1.0f ) const;

        // --- Shared building blocks for the card style ------------------
        // These used to be loose lambdas in TekenLiveTab, so new parts (like
        // the board computer) accidentally sat next to them with plain text.
        // Now as methods, so every new block gets the same visual language.

        // Section header in the accent colour, with a thin line below.
        // Dark, always-readable background for cards; see the explanation at
        // the implementation for why not white-transparent.
        ImVec4 KaartKleur() const;
        ImVec4 TintKaartKleur( const ImVec4 &tint, float sterkte ) const;

        void KopBalk( const char *tekst );

        // Card with a small label on top and the value large below.
        // `onderschrift` is optional (e.g. the unit).
        // `waarschuwing` makes the card red instead of neutral, for cases
        // like "you are speeding".
        // `compact` makes the card lower: label and caption in the small
        // font, and the value in the normal one instead of the large header
        // font. Meant for the bus tab, where the stop list needs the space
        // more. The width is left alone.
        void StatKaart( const char *label, const std::string &waarde, float breedte,
                         const char *onderschrift = nullptr, bool waarschuwing = false,
                         bool compact = false );

        // Computes how high a stat card must be based on the actual font
        // sizes. A fixed number went wrong as soon as the header font got
        // larger than planned: the caption fell out and a scrollbar appeared
        // in the card.
        float KaartHoogte( bool metOnderschrift, bool compact = false ) const;

        // Line with the name on the left, a coloured bar on the right and the
        // percentage after it. Colour runs from green via amber to red. A
        // negative value means "unknown" and shows a grey, empty bar.
        // Line with the name on the left, a coloured bar on the right and the
        // percentage after it. Colour runs from green via amber to red,
        // unless `kleurOverride` is set -- then that colour is used instead
        // of the damage thresholds (so other bars, like the tachograph, can
        // reuse the same compact style with their own colour logic). ImU32
        // is just an unsigned int (typedef from imgui.h) -- we do not want
        // that include in the header, so written here as the underlying type.
        // `breedte` = 0 means "use the rest of the line". Pass a value to put
        // bars side by side in columns; the function then computes relative
        // to the current cursor position, so a second column does not fall
        // over the first.
        void SchadeBalk( const char *naam, double percentage, const unsigned int *kleurOverride = nullptr,
                          float labelBreedte = 100.0f, float breedte = 0.0f );

        // Load the logo as a DirectX11 texture so ImGui::Image() can draw
        // it. Expects %APPDATA%\CabNavi\logo.png. Fails quietly (logo stays
        // empty, overlay just keeps working) if the file is not there -- not
        // every user necessarily has a logo.
        void LaadLogo();
        void *m_logoTextuur = nullptr;  // ID3D11ShaderResourceView*, as void* to avoid a D3D include in the header
        int m_logoBreedte = 0;
        int m_logoHoogte = 0;

        // Six colourful tab icons (Live/Players/History/Statistics/Incident/
        // Settings), hand-drawn and loaded as PNG -- ImGui cannot show colour
        // emoji, this is the way to still get that colourful "app icon" look.
        // Number of tabs in the sidebar. As a constant, so the count is in
        // ONE place -- there are four loops over it and they all used to run
        // with a loosely typed 6.
        static constexpr int AANTAL_TABS = 9;
        void *m_tabTexturen[ AANTAL_TABS ] = {};
        void LaadTabIconen();

        // Second, larger font for key figures (ETA, tachograph time, stat
        // cards) -- gives them the "big number, small label" feel of the
        // mockups, instead of everything the same size. Stored as ImFont*
        // (imgui.h is already included elsewhere in Overlay.cxx, so no void*
        // detour needed like with the texture).
        ImFont *m_kopFont = nullptr;

        // Third font: smaller than the default 19pt, for cards with many
        // short lines side by side (damage, trailer, tachograph). Without it
        // the text there got cut off as soon as the window was narrower, and
        // hardly any room was left for the bars.
        ImFont *m_kleinFont = nullptr;

        // Is the small font active right now? SchadeBalk uses this to put the
        // PERCENTAGE briefly in the normal (larger) font: the number is what
        // you look at, the label around it may stay small.
        bool m_kleinFontActief = false;

        bool m_geinitialiseerd = false;
        bool m_zichtbaar = true;
        // Order must match `bestandsnamen[]` in LaadTabIconen, the switch in
        // Teken() and the fallback icons -- four places. When Boordcomputer
        // was inserted all the numbers after it shifted.
        //   0=Live 1=Boordcomputer 2=Spelers 3=Geschiedenis
        //   4=Statistieken 5=Incident 6=Instellingen
        int m_actieveTab = 0;
        void *m_vensterHandle = nullptr;
        ID3D11Device *m_device = nullptr;
        ID3D11DeviceContext *m_context = nullptr;
    };
}

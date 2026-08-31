#pragma once
// Overlay.hxx
//
// Tekent een modern ImGui-venster bovenop het spel: een tabblad "Live" met
// de actieve rit (bus of vracht), een tabblad "Geschiedenis" met de laatste
// ritten en een tabblad "Statistieken" met totalen. Wordt geïnitialiseerd
// met de DirectX11-device van de Render-module en getekend in OnPostRender
// (zie de Render-moduledocs: "most overlays want" OnPostRender).

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
// ImVec4 wordt hieronder als retour-/parametertype gebruikt (KaartKleur,
// TintKaartKleur). Alleen aankondigen, niet imgui.h includen: die header
// hoort niet in onze publieke interface thuis, en de .cxx die deze functies
// implementeert haalt imgui.h zelf al binnen.
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

        // Aanroepen zodra Render().GetRendererID() == DirectX11 is en er een
        // device-handle beschikbaar is.
        bool InitDirectX11( ID3D11Device *device, void *vensterHandle );
        void Shutdown();

        // In Render().OnPostRender geregistreerd.
        void Teken();

        // Voer input van de TruckersMP Input-module door naar ImGui. Zonder
        // dit teken je wel een overlay, maar reageert hij nergens op: de SDK
        // heeft geen "vensterhaak" zoals Dear ImGui's Win32-backend normaal
        // gebruikt, dus muis/toetsenbord moeten we zelf doorgeven.
        // Retourneert of de overlay de input "opeist" -- zo ja, moet de
        // aanroeper het bijbehorende event blokkeren voor het spel zelf
        // (zie InputMouseButtonEvent::SetBlock in de SDK-docs).
        void OpMuisBeweging( int x, int y );
        void OpMuisKnop( int knop, bool ingedrukt );
        void OpMuisWiel( float delta );
        void OpToets( unsigned int virtualKeyCode, bool ingedrukt );
        void OpKarakter( unsigned int codepoint );
        bool WilMuis() const;
        bool WilToetsenbord() const;

        // Toggle-toets (bv. Insert) via de Input-module.
        void SchakelZichtbaarheid() { m_zichtbaar = !m_zichtbaar; }
        bool IsZichtbaar() const { return m_zichtbaar; }

    private:
        void TekenLiveTab();
        void TekenBoordcomputerTab();

        // Smalle tachograafstrip voor de Live-tab: alleen kopje, balk en tijd
        // op EEN regel. De volledige tachograafkaart staat op het
        // boordcomputer-tabblad; deze strip is er zodat je tijdens het rijden
        // niet van tabblad hoeft te wisselen om je rijtijd te zien.
        void TekenTachoStrip();
        void TekenSpelersTab();
        void TekenGeschiedenisTab();
        void TekenStatistiekenTab();
        void TekenInstellingenTab();
        void TekenVtcTab();
        void TekenVtcInstellingenTab();

        // Instellingen-secties als kaartjes: opent een omkaderd vak met een
        // gekleurd kopje. Sluiten met SectieEind().
        //
        // vasteHoogte (optioneel, standaard 0): 0 betekent precies zoals
        // altijd -- het kaartje groeit automatisch mee met zijn inhoud.
        // Groter dan 0 legt de hoogte vast; alleen UITERLIJK gebruikt dat.
        void SectieStart( const char *naam, ImVec4 kleur, float vasteHoogte = 0.0f );

        // Hoogte van een sectie UITREKENEN in plaats van een pixelgetal
        // intypen. Zie de waarschuwing bij KaartHoogte(): "een vast getal
        // ging mis zodra het kop-font groter bleek dan gepland -- het
        // onderschrift viel eronderuit en ImGui zette er een scrollbalkje
        // in." Precies dat gebeurde hier ook.
        //
        // Je geeft op WAT erin staat; de maten komen van ImGui zelf, dus het
        // schaalt mee met het lettertype:
        //   tekstRegels = regels TekstGedimd / TextDisabled
        //   velden      = keuzelijsten, invulvelden, schuiven, vinkjes, knoppen
        //   spaties     = losse ImGui::Spacing()
        float SectieHoogte( int tekstRegels, int velden, int spaties = 0 ) const;
        void SectieEind();

        // Breedte voor een schuif of keuzemenu in de instellingen. Schaalt
        // mee met het vak in plaats van een vast getal, zodat er rechts geen
        // leegte overblijft -- maar met grenzen, want een schuif van 800
        // pixels is ook nergens goed voor.
        float VeldBreedte() const;
        void TekenIncidentTab();

        // Herbruikbaar: klein "..."-knopje dat een contextmenu opent met
        // Steam-profiel/TruckersMP-profiel/ID-kopieren voor een speler.
        void TekenSpelerContextKnop( const SpelerRecord &speler, const std::string &uniekeId );

        // Tekent een klein zelfgetekend vrachtwagen- of bus-icoontje op de
        // huidige cursorpositie (reserveert layout-ruimte via Dummy, dus
        // gewoon ImGui::SameLine() erna gebruiken voor tekst ernaast).
        void TekenVoertuigIcoon( bool isBus, float grootte = 20.0f );

        // Eigen getekende zijbalk-icoontjes (i.p.v. tekstlabels of emoji,
        // die respectievelijk saai en niet-ondersteund door ImGui's
        // lettertype zijn). Tekent gecentreerd op de huidige cursorpositie
        // binnen een grootte x grootte vierkant, kleur wordt meegegeven.
        void TekenTabIcoon( int tabIndex, float middenX, float middenY, float straal, unsigned int kleur );

        TripLogger &m_logger;
        BusTracking &m_bus;
        TruckTracking &m_vracht;
        PlayersNearby &m_spelers;
        FuelCosts &m_brandstof;
        DiscordWebhook &m_discord;

        // Publieke TruckersMP Web API: serverstatus en evenementen. Eigendom
        // van de overlay zelf, want alleen de statistieken-tab gebruikt hem.
        WebApi m_webApi;

        // --- VTC-integratie ------------------------------------------------
        // Los van de rest aan te zetten. Het nummer staat in het adres van je
        // VTC-pagina: truckersmp.com/vtc/<nummer>. Wordt bewaard in
        // vtc.json, zodat het een herstart overleeft.
        bool m_vtcAan = false;
        int m_vtcId = 0;
        bool m_vtcTagsBijSpelers = true;
        bool m_vtcRadarMarkering = false;
        bool m_vtcConvooienTonen = true;

        // Spelers opzoeken bij de Web API (voor VTC-nummer en patron). Kan
        // helemaal uit: de markering werkt dan nog steeds op tags, alleen
        // zonder verzoeken naar hun servers. Zie ook de 429 die we op
        // 30-08 kregen -- hun limiet staat nergens gedocumenteerd, dus we
        // gaan er zuinig mee om en laten de keuze bij jou.
        bool m_vtcSpelersOpzoeken = false;

        // Zelf aangevinkte convooien. De Web API kan niet vertellen waar jij
        // je voor hebt aangemeld -- /events/user/{id} geeft wat je zelf hebt
        // AANGEMAAKT, niet waar je je voor opgaf (gemeten 30-08: leeg
        // antwoord terwijl er wel aanmeldingen waren). Vandaar zelf
        // aanvinken: kost nul verzoeken en werkt gegarandeerd.
        //
        // Staat LOS van de VTC: ook zonder bedrijf meld je je voor convooien
        // aan, en dan wil je die herinnering gewoon zien.
        bool m_eigenConvooien = false;
        std::vector<EvenementInfo> m_aangevinkt;
        bool IsAangevinkt( int evenementId ) const;
        void ZetAangevinkt( const EvenementInfo &e, bool aan );
        std::vector<EvenementInfo> MijnConvooien() const;
        char m_vtcIdBuffer[ 16 ] = "";
        bool m_vtcIdBufferGeladen = false;

        // Tags die op de radar en in de lijst gemarkeerd worden, gescheiden
        // door komma's. De SDK geeft de tag die in het spel voor iemands naam
        // staat al mee (Player::GetTagText), en dat is bij VTC-leden meestal
        // precies hun bedrijfstag -- dus hier zijn geen API-verzoeken per
        // speler voor nodig. Je kunt er meerdere invullen, bijvoorbeeld als
        // je bedrijf verschillende afdelingstags gebruikt.
        char m_vtcTagsBuffer[ 128 ] = "";
        bool IsEigenVtc( const SpelerRecord &s ) const;

        // De SDK-vlag IsPatron blijft op false staan, ook bij iemand die
        // aantoonbaar patron is (gemeten 30-08). De Web API zegt het wel,
        // en dat antwoord halen we toch al op voor het VTC-nummer.
        bool IsPatron( const SpelerRecord &s ) const;

        // Regeltje over een convooi dat er zo aan komt. Alleen voor
        // convooien waar JIJ of je VTC zich voor heeft aangemeld -- een
        // melding over een convooi waar je niks mee te maken hebt is ruis.
        // Leeg = niets te melden, en dan wordt er ook niets getekend.
        std::string ConvooiHerinnering() const;
        void TekenConvooiHerinnering();

        // Eén regel onder de kaartjes: rijd je deze rit zuiniger of
        // onzuiniger dan je eigen gemiddelde uit het rittenlogboek? Leeg als
        // er te weinig te vergelijken valt -- dan wordt er niets getekend.
        void TekenZuinigheid();

        // Aan/uit voor die zuinigheidsregel. Wordt bewaard in uiterlijk.json
        // bij de andere weergavekeuzes.
        bool m_zuinigheidTonen = true;

        // 0 = Nederlands, 1 = Engels. Nederlands is de basis: teksten zonder
        // vertaling blijven gewoon Nederlands staan (zie Taal.hxx).
        int m_taal = 0;

        // Uitgebreide diagnose in debug.log. Standaard uit: die regels zijn
        // alleen nodig als er iets uitgezocht moet worden, en ze schrijven
        // elke paar seconden naar schijf.
        bool m_uitgebreidLog = false;
        static constexpr int HERINNERING_MINUTEN = 60;
        void LaadVtcInstellingen();
        void SlaVtcInstellingenOp();
        IncidentRecorder &m_incident;
        int m_incidentFrameIndex = 0; // positie van de tijdlijn-schuif in de replay-viewer

        // Voor het report-scherm (zie TekenSpelerContextKnop): welke speler
        // en welke redenen er momenteel aangevinkt staan.
        SpelerRecord m_reportPopupSpeler;
        std::string m_reportPopupSpelerId;
        std::vector<char> m_reportRedenenAangevinkt;
        char m_reportOmschrijving[ 1024 ] = "";
        char m_reportBewijsLink[ 256 ] = "";
        char m_prijsBuffer[ 16 ] = "1.65";
        char m_webhookBuffer[ 256 ] = "";
        bool m_webhookBufferGeladen = false;

        // Uiterlijk: doorzichtigheid en accentkleur, door de gebruiker zelf
        // instelbaar op de Instellingen-tab en bewaard in
        // %APPDATA%\CabNavi\uiterlijk.json.
        float m_doorzichtigheid = 0.90f;
        float m_iconenDoorzichtigheid = 0.95f; // losse schuif voor zijbalk/menu's/iconen
        float m_accentKleur[ 3 ] = { 0.83f, 0.55f, 0.16f }; // warm amber/goud, zoals TMP's eigen interface

        // Pad naar imgui.ini. MOET een lid zijn: io.IniFilename is een kale
        // pointer die ImGui niet kopieert, dus een lokale string zou na
        // afloop van de functie een bungelende pointer achterlaten.
        std::string m_iniPad;


        // Voor het "levend voelende" scroll-effect op de minimap: een
        // opgebouwde afstand die meebeweegt met je echte snelheid (geen
        // echte positie, puur een visueel gevoel van beweging -- zie de
        // opmerking in PlayersNearby.hxx over waarom we geen echte GPS
        // hebben).
        float m_minimapScrollKm = 0.0f;
        std::chrono::steady_clock::time_point m_minimapLaatsteUpdate;
        bool m_uiterlijkGeladen = false;
        void LaadUiterlijk();
        void SlaUiterlijkOp() const;
        unsigned int AccentKleurU32( float alpha = 1.0f ) const;

        // --- Gedeelde bouwstenen voor de kaartenstijl ------------------
        // Deze stonden eerst als losse lambda's in TekenLiveTab, waardoor
        // nieuwe onderdelen (zoals de boordcomputer) er per ongeluk naast
        // gingen zitten met platte tekst. Nu als methodes, zodat elk nieuw
        // blok dezelfde vormtaal krijgt.

        // Sectiekop in de accentkleur, met een dun lijntje eronder.
        // Donkere, altijd-leesbare achtergrond voor kaartjes; zie de
        // uitleg bij de implementatie waarom niet wit-transparant.
        ImVec4 KaartKleur() const;
        ImVec4 TintKaartKleur( const ImVec4 &tint, float sterkte ) const;

        void KopBalk( const char *tekst );

        // Kaartje met klein label boven en de waarde groot eronder.
        // `onderschrift` is optioneel (bv. de eenheid).
        // `waarschuwing` maakt de kaart rood in plaats van neutraal, voor
        // gevallen als "je rijdt te hard".
        // `compact` maakt de kaart lager: label en onderschrift in het kleine
        // lettertype, en de waarde in het normale in plaats van het grote
        // kopfont. Bedoeld voor de bus-tab, waar de haltelijst de ruimte
        // harder nodig heeft. De breedte blijft ongemoeid.
        void StatKaart( const char *label, const std::string &waarde, float breedte,
                         const char *onderschrift = nullptr, bool waarschuwing = false,
                         bool compact = false );

        // Berekent hoe hoog een stat-kaartje moet zijn op basis van de
        // werkelijke lettergroottes. Een vast getal ging mis zodra het
        // kop-lettertype groter werd dan gepland: het onderschrift viel er
        // dan onderuit en er kwam een scrollbalkje in het kaartje.
        float KaartHoogte( bool metOnderschrift, bool compact = false ) const;

        // Regel met naam links, een gekleurd balkje rechts en het percentage
        // erachter. Kleur loopt van groen via amber naar rood. Een negatieve
        // waarde betekent "onbekend" en toont een grijs, leeg balkje.
        // Regel met naam links, een gekleurd balkje rechts en het percentage
        // erachter. Kleur loopt van groen via amber naar rood, tenzij
        // `kleurOverride` gezet is -- dan wordt die kleur gebruikt in
        // plaats van de schade-drempels (zodat andere balken, zoals de
        // tachograaf, dezelfde compacte stijl kunnen hergebruiken met hun
        // eigen kleurlogica). ImU32 is gewoon een unsigned int (typedef uit
        // imgui.h) -- die include willen we niet in de header, dus hier
        // geschreven als het onderliggende type.
        // `breedte` = 0 betekent "gebruik de rest van de regel". Geef een
        // waarde mee om balken naast elkaar in kolommen te zetten; de functie
        // rekent dan relatief vanaf de huidige cursorpositie, zodat een
        // tweede kolom niet over de eerste heen valt.
        void SchadeBalk( const char *naam, double percentage, const unsigned int *kleurOverride = nullptr,
                          float labelBreedte = 100.0f, float breedte = 0.0f );

        // Logo laden als DirectX11-textuur zodat ImGui::Image() 'm kan
        // tekenen. Verwacht %APPDATA%\CabNavi\logo.png. Faalt
        // stil (logo blijft leeg, overlay werkt gewoon door) als het
        // bestand er niet is -- niet elke gebruiker heeft per se een logo.
        void LaadLogo();
        void *m_logoTextuur = nullptr; // ID3D11ShaderResourceView*, als void* om geen D3D-include in de header nodig te hebben
        int m_logoBreedte = 0;
        int m_logoHoogte = 0;

        // Zes kleurrijke tab-icoontjes (Live/Spelers/Geschiedenis/
        // Statistieken/Incident/Instellingen), zelfgetekend en als PNG
        // geladen -- ImGui kan geen kleuremoji tonen, dit is de manier om
        // toch die kleurrijke "app-icoon"-look te krijgen.
        // Aantal tabbladen in de zijbalk. Als constante, zodat het aantal
        // maar op EEN plek staat -- er zijn vier lussen die eroverheen lopen
        // en die liepen eerder allemaal met een los ingetypte 6.
        static constexpr int AANTAL_TABS = 9;
        void *m_tabTexturen[ AANTAL_TABS ] = {};
        void LaadTabIconen();

        // Tweede, groter lettertype voor kerncijfers (ETA, tachograaf-tijd,
        // statistiek-kaartjes) -- geeft die het "grote getal, klein
        // label"-gevoel van de mockups, i.p.v. alles even groot. Als
        // ImFont* opgeslagen (imgui.h wordt al elders in Overlay.cxx
        // geincludeerd, dus geen void*-omweg nodig zoals bij de textuur).
        ImFont *m_kopFont = nullptr;

        // Derde lettertype: kleiner dan de standaard 19pt, voor kaarten met
        // veel korte regels naast elkaar (schade, aanhanger, tachograaf).
        // Zonder dit werd de tekst daar afgekapt zodra het venster smaller
        // stond, en bleef er nauwelijks ruimte over voor de balkjes.
        ImFont *m_kleinFont = nullptr;

        // Staat het kleine lettertype nu actief? SchadeBalk gebruikt dit om
        // het PERCENTAGE even in het gewone (grotere) font te zetten: het
        // getal is waar je naar kijkt, het label eromheen mag klein blijven.
        bool m_kleinFontActief = false;

        bool m_geinitialiseerd = false;
        bool m_zichtbaar = true;
        // Volgorde moet gelijklopen met `bestandsnamen[]` in LaadTabIconen,
        // de switch in Teken() en de terugval-iconen -- vier plekken. Bij het
        // invoegen van Boordcomputer schoven de nummers erachter allemaal op.
        //   0=Live 1=Boordcomputer 2=Spelers 3=Geschiedenis
        //   4=Statistieken 5=Incident 6=Instellingen
        int m_actieveTab = 0;
        void *m_vensterHandle = nullptr;
        ID3D11Device *m_device = nullptr;
        ID3D11DeviceContext *m_context = nullptr;
    };
}

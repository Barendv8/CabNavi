#include "Overlay.hxx"

#include "BoeteTekst.hxx"
#include "Logboek.hxx"
#include "Taal.hxx"
#include "WinHulp.hxx"

#include <windows.h>
#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

using json = nlohmann::json;

namespace
{
    // "5 min" / "1 uur 20 min" / "onbekend" (bij negatieve waarde)
    std::string FormatteerMinuten( double minuten )
    {
        if( minuten < 0.0 ) return Ritten::T( "onbekend" );

        // Naar BOVEN afronden. Bij een aftelteller is dat eerlijker: heb je
        // nog 20 seconden, dan hoort er "1 min" te staan en geen "0 min".
        // Gewoon afronden liet het laatste stukje er al af vallen.
        int totaal = static_cast<int>( std::ceil( minuten - 0.0001 ) );
        int uren = totaal / 60;
        int rest = totaal % 60;
        if( uren <= 0 ) return std::to_string( rest ) + Ritten::T( " min" );
        return std::to_string( uren ) + Ritten::T( " uur " ) + std::to_string( rest ) + Ritten::T( " min" );
    }

    // Zet "over N echte minuten" om naar een wandkloktijd, bv. "21:47".
    // Bewust de echte systeemklok en niet de speltijd: als je wilt weten of
    // je nog voor het eten klaar bent, gaat het om jouw klok, niet die in
    // het spel.
    std::string KlokTijdOver( double minutenVanafNu )
    {
        std::time_t nu = std::time( nullptr );
        std::time_t straks = nu + static_cast<std::time_t>( minutenVanafNu * 60.0 );
        std::tm tmBuf{};
    #if defined( _WIN32 )
        localtime_s( &tmBuf, &straks );
    #else
        localtime_r( &straks, &tmBuf );
    #endif
        char buf[ 16 ];
        std::snprintf( buf, sizeof( buf ), "%02d:%02d", tmBuf.tm_hour, tmBuf.tm_min );
        return buf;
    }

    // Het spel meldt de reden van een boete als korte Engelse code in het
    // "fine.offence"-attribuut. Deze lijst dekt de overtredingen die ETS2
    // kent (de laatste drie zijn toegevoegd in een latere SDK-versie).
    // Onbekende codes tonen we gewoon ruw -- beter een code die je zelf
    // kan opzoeken dan een verkeerde vertaling.
    // --- Leesbaarheid op een wisselende spelachtergrond ----------------
    //
    // Het probleem: de HUD is half doorzichtig (bewust -- je wil zien waar
    // je rijdt), maar daardoor valt lichte tekst weg tegen licht gras of
    // een betonplaat. De oplossing is NIET het paneel donkerder maken,
    // maar de letters zelf laten "loskomen": we tekenen elke regel eerst
    // in bijna-zwart met 1px verschuiving, en daar bovenop de echte kleur.
    // Zo krijg je een randje contrast dat meebeweegt met de letter,
    // ongeacht wat eronder zit. Dit is de standaardtruc voor game-HUD's.
    // Gedimd grijs voor labels en onderschriften -- vervangt
    // ImGui::TextDisabled, dat geen schaduw kan tekenen.
    // Labels, eenheden en uitleg: gewoon wit. Ze onderscheiden zich van de
    // waarden door hun grootte en door de lichtere schaduw (zie TekstS),
    // niet meer door een grijstint -- grijs zakte te ver weg in de
    // doorzichtige kaart met het spel eronder.
    const ImU32 KLEUR_GEDIMD = IM_COL32( 255, 255, 255, 255 );

    // Tekst met een donkere contour eromheen. Eerst stond hier één schaduw
    // rechtsonder, maar dan blijft de linker-/bovenkant van elke letter
    // onbeschermd: staat daar toevallig iets lichts in het spel, dan
    // "vervaagt" die kant van de letter alsnog. Een rondom-contour (vier
    // hoeken + vier zijden) lost dat op -- dezelfde truc als bij
    // ondertiteling.
    //
    // Die volle contour is alleen bedoeld voor de WAARDEN (de grote cijfers).
    // Op kleine grijze labels maakt hij de letters juist dikker en donkerder,
    // waardoor ze net zo hard opvallen als de cijfers -- precies wat een
    // label niet moet doen. Vandaar `zwareContour`.
    void TekstS( const char *tekst, ImU32 kleur = IM_COL32( 255, 255, 255, 255 ),
                  bool zwareContour = true )
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList *dl = ImGui::GetWindowDrawList();

        if( zwareContour )
        {
            const ImU32 contour = IM_COL32( 0, 0, 0, 190 );
            static const float dx[ 8 ] = { -1, 1, 0, 0, -1, -1, 1, 1 };
            static const float dy[ 8 ] = { 0, 0, -1, 1, -1, 1, -1, 1 };
            for( int i = 0; i < 8; ++i )
            {
                dl->AddText( ImVec2( p.x + dx[ i ], p.y + dy[ i ] ), contour, tekst );
            }
        }
        else
        {
            // Alleen een zachte schaduw rechtsonder: genoeg om de letters
            // van de achtergrond te scheiden, niet genoeg om ze vet te maken.
            dl->AddText( ImVec2( p.x + 1.0f, p.y + 1.0f ), IM_COL32( 0, 0, 0, 130 ), tekst );
        }

        dl->AddText( p, kleur, tekst );
        ImGui::Dummy( ImGui::CalcTextSize( tekst ) );
    }

    // Kleine, ingetogen tekst: kopjes van kaartjes, eenheden, uitleg.
    // Zelfde uitstraling als ImGui's eigen TextDisabled.
    void TekstGedimd( const char *tekst )
    {
        TekstS( tekst, KLEUR_GEDIMD, false );
    }

    void TekstSFmt( ImU32 kleur, const char *fmt, ... )
    {
        char buf[ 256 ];
        va_list args;
        va_start( args, fmt );
        vsnprintf( buf, sizeof( buf ), fmt, args );
        va_end( args );
        TekstS( buf, kleur );
    }

    // Opgemaakte variant van TekstGedimd: zelfde witte kleur, maar met de
    // lichte schaduw in plaats van de volle contour. Sinds labels en waarden
    // allebei zuiver wit zijn, is de contourzwaarte het enige verschil --
    // die moet dus expliciet gekozen worden en kan niet meer uit de kleur
    // worden afgeleid.
    void TekstGedimdFmt( const char *fmt, ... )
    {
        char buf[ 256 ];
        va_list args;
        va_start( args, fmt );
        vsnprintf( buf, sizeof( buf ), fmt, args );
        va_end( args );
        TekstS( buf, KLEUR_GEDIMD, false );
    }

    // 18083 -> "18.083" (Nederlandse duizendtalscheiding)
    std::string MetPunten( double waarde )
    {
        char ruw[ 32 ];
        snprintf( ruw, sizeof( ruw ), "%.0f", waarde );
        std::string s = ruw;
        for( int i = static_cast<int>( s.size() ) - 3; i > 0; i -= 3 )
        {
            s.insert( i, "." );
        }
        return s;
    }
}

namespace
{
    // Eigen (kleine) omzetting van Windows virtual-key codes naar ImGuiKey --
    // ImGui_ImplWin32_VirtualKeyToImGuiKey bleek niet beschikbaar in deze
    // ImGui-versie/build, dus geen afhankelijkheid daarvan. Dekt de toetsen
    // die je nodig hebt om in het brandstofprijs-/webhook-veld te typen:
    // cijfers, punt/komma, backspace, delete, pijltjes, enter, tab.
    ImGuiKey VkNaarImGuiKey( unsigned int vk )
    {
        switch( vk )
        {
            case 0x08: return ImGuiKey_Backspace;
            case 0x09: return ImGuiKey_Tab;
            case 0x0D: return ImGuiKey_Enter;
            case 0x1B: return ImGuiKey_Escape;
            case 0x20: return ImGuiKey_Space;
            case 0x2E: return ImGuiKey_Delete;
            case 0x24: return ImGuiKey_Home;
            case 0x23: return ImGuiKey_End;
            case 0x25: return ImGuiKey_LeftArrow;
            case 0x26: return ImGuiKey_UpArrow;
            case 0x27: return ImGuiKey_RightArrow;
            case 0x28: return ImGuiKey_DownArrow;
            case 0x2D: return ImGuiKey_Insert;
            case 0xBD: return ImGuiKey_Minus;      // -
            case 0xBE: return ImGuiKey_Period;      // .
            case 0xBC: return ImGuiKey_Comma;       // ,
            case 0x30: return ImGuiKey_0;
            case 0x31: return ImGuiKey_1;
            case 0x32: return ImGuiKey_2;
            case 0x33: return ImGuiKey_3;
            case 0x34: return ImGuiKey_4;
            case 0x35: return ImGuiKey_5;
            case 0x36: return ImGuiKey_6;
            case 0x37: return ImGuiKey_7;
            case 0x38: return ImGuiKey_8;
            case 0x39: return ImGuiKey_9;
            case 0x60: return ImGuiKey_Keypad0;
            case 0x61: return ImGuiKey_Keypad1;
            case 0x62: return ImGuiKey_Keypad2;
            case 0x63: return ImGuiKey_Keypad3;
            case 0x64: return ImGuiKey_Keypad4;
            case 0x65: return ImGuiKey_Keypad5;
            case 0x66: return ImGuiKey_Keypad6;
            case 0x67: return ImGuiKey_Keypad7;
            case 0x68: return ImGuiKey_Keypad8;
            case 0x69: return ImGuiKey_Keypad9;
            case 0x6E: return ImGuiKey_KeypadDecimal;

            // Letters. Nodig voor de bewerkingssneltoetsen in tekstvelden:
            // zonder A/C/V/X/Y/Z ziet ImGui Ctrl+V nooit, en kon je een
            // webhook-URL dus niet plakken -- alleen intypen.
            case 0x41: return ImGuiKey_A; // alles selecteren
            case 0x43: return ImGuiKey_C; // kopieren
            case 0x56: return ImGuiKey_V; // plakken
            case 0x58: return ImGuiKey_X; // knippen
            case 0x59: return ImGuiKey_Y; // opnieuw
            case 0x5A: return ImGuiKey_Z; // ongedaan maken

            // Modifiers: zonder deze blijft Ctrl "los" en werkt geen enkele
            // combinatie, ook al komt de letter wel binnen.
            case 0x11: return ImGuiKey_LeftCtrl;
            case 0xA2: return ImGuiKey_LeftCtrl;
            case 0xA3: return ImGuiKey_RightCtrl;
            case 0x10: return ImGuiKey_LeftShift;
            case 0xA0: return ImGuiKey_LeftShift;
            case 0xA1: return ImGuiKey_RightShift;

            default:   return ImGuiKey_None;
        }
    }
}

namespace Ritten
{
    Overlay::Overlay( TripLogger &logger, BusTracking &bus, TruckTracking &vracht,
                       PlayersNearby &spelers, FuelCosts &brandstof, DiscordWebhook &discord,
                       IncidentRecorder &incident )
        : m_logger( logger ), m_bus( bus ), m_vracht( vracht ), m_spelers( spelers ), m_brandstof( brandstof ),
          m_discord( discord ), m_incident( incident )
    {
        std::snprintf( m_prijsBuffer, sizeof( m_prijsBuffer ), "%.2f", m_brandstof.PrijsPerLiter() );
        LaadUiterlijk();
        LaadVtcInstellingen();
    }

    // %APPDATA%\CabNavi\ -- de map waar al onze instellingen staan.
    // Wordt aangemaakt als hij nog niet bestaat.
    static std::filesystem::path InstellingenMap()
    {
        std::filesystem::path basis;
        if( const char *appdata = std::getenv( "APPDATA" ) )
        {
            basis = appdata;
        }
        else
        {
            basis = std::filesystem::current_path();
        }
        basis /= "CabNavi";
        std::error_code ec;
        std::filesystem::create_directories( basis, ec );
        return basis;
    }

    static std::filesystem::path UiterlijkPad()
    {
        return InstellingenMap() / "uiterlijk.json";
    }

    void Overlay::LaadUiterlijk()
    {
        m_uiterlijkGeladen = true;
        std::ifstream in( UiterlijkPad() );
        if( !in ) return;
        try
        {
            json j; in >> j;
            m_doorzichtigheid = j.value( "doorzichtigheid", m_doorzichtigheid );
            if( m_doorzichtigheid < 0.35f ) m_doorzichtigheid = 0.35f; // ondergrens, zie Instellingen-tab
            m_iconenDoorzichtigheid = j.value( "iconen_doorzichtigheid", m_iconenDoorzichtigheid );
            m_zuinigheidTonen = j.value( "zuinigheid_tonen", true );
            m_taal = j.value( "taal", 0 );
            m_uitgebreidLog = j.value( "uitgebreid_log", false );
            Logboek::Uitgebreid() = m_uitgebreidLog;
            Taal::Kies( m_taal == 1 ? TaalKeuze::Engels : TaalKeuze::Nederlands );
            if( m_iconenDoorzichtigheid < 0.4f ) m_iconenDoorzichtigheid = 0.4f; // ondergrens, zie Instellingen-tab
            m_accentKleur[ 0 ] = j.value( "accent_r", m_accentKleur[ 0 ] );
            m_accentKleur[ 1 ] = j.value( "accent_g", m_accentKleur[ 1 ] );
            m_accentKleur[ 2 ] = j.value( "accent_b", m_accentKleur[ 2 ] );
        }
        catch( ... ) { /* corrupt bestand: default gebruiken */ }
    }

    void Overlay::SlaUiterlijkOp() const
    {
        std::ofstream uit( UiterlijkPad() );
        if( !uit ) return;
        json j;
        j[ "doorzichtigheid" ] = m_doorzichtigheid;
        j[ "iconen_doorzichtigheid" ] = m_iconenDoorzichtigheid;
        j[ "zuinigheid_tonen" ] = m_zuinigheidTonen;
        j[ "taal" ] = m_taal;
        j[ "uitgebreid_log" ] = m_uitgebreidLog;
        j[ "accent_r" ] = m_accentKleur[ 0 ];
        j[ "accent_g" ] = m_accentKleur[ 1 ];
        j[ "accent_b" ] = m_accentKleur[ 2 ];
        uit << j.dump( 2 );
    }

    unsigned int Overlay::AccentKleurU32( float alpha ) const
    {
        return IM_COL32(
            static_cast<int>( m_accentKleur[ 0 ] * 255 ), static_cast<int>( m_accentKleur[ 1 ] * 255 ),
            static_cast<int>( m_accentKleur[ 2 ] * 255 ), static_cast<int>( alpha * 255 ) );
    }

    // Achtergrondkleur voor elk kaartje in de overlay.
    //
    // WAAROM DONKER EN NIET WIT-TRANSPARANT: eerder stond hier overal
    // Kaartachtergrond: een LICHTE, doorzichtige waas -- geen donker vlak.
    // Het spel moet zichtbaar blijven door de HUD heen; de leesbaarheid
    // regelen we met de contour om de letters (zie TekstS bovenaan), niet
    // door het paneel dicht te gooien.
    //
    // Ik heb dit een ronde lang donker gemaakt omdat lichte tekst op een
    // lichte kaart boven fel gras minder contrast heeft. Technisch waar,
    // maar het maakt de HUD zwaar en dat is niet de look die we willen.
    // De letter-contour vangt dat contrastverlies voldoende op.
    ImVec4 Overlay::KaartKleur() const
    {
        return ImVec4( 1.0f, 1.0f, 1.0f, 0.08f );
    }

    // Zelfde doorzichtige opzet, maar met een kleurzweem (rood voor een
    // waarschuwing, groen voor cruise control, amber voor de tachograaf).
    // De sterkte bepaalt alleen hoe nadrukkelijk de tint is, niet hoe
    // donker de kaart wordt.
    ImVec4 Overlay::TintKaartKleur( const ImVec4 &tint, float sterkte ) const
    {
        return ImVec4( tint.x, tint.y, tint.z, 0.10f + 0.14f * sterkte );
    }

    void Overlay::KopBalk( const char *tekst )
    {
        TekstS( tekst, AccentKleurU32( 1.0f ) );
        // Dun accentlijntje onder de kop -- geeft de sectie een duidelijk
        // begin zonder een volledige ImGui::Separator, die te hard oogt.
        ImVec2 p = ImGui::GetCursorScreenPos();
        float breedte = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2( p.x, p.y + 1 ), ImVec2( p.x + breedte, p.y + 2 ), AccentKleurU32( 0.55f ) );
        ImGui::Dummy( ImVec2( 0, 6 ) );
    }

    float Overlay::KaartHoogte( bool metOnderschrift, bool compact ) const
    {
        const ImGuiStyle &st = ImGui::GetStyle();
        // Regelhoogte van het normale lettertype (label + onderschrift) en
        // van het kop-lettertype (de grote waarde). Een vast getal ging mis
        // zodra het kop-font groter bleek dan gepland: het onderschrift viel
        // er onderuit en ImGui zette er een scrollbalkje in.
        // In compacte vorm rekenen we met het KLEINE font voor de labels en
        // het normale font voor de waarde -- geen kopfont, dat is wat de
        // hoogte normaal opdrijft.
        float klein = compact && m_kleinFont
                          ? m_kleinFont->FontSize + 2.0f
                          : ImGui::GetTextLineHeightWithSpacing();
        // In compacte vorm blijft de WAARDE in het normale lettertype -- die
        // moet je in een oogopslag kunnen lezen. De hoogtewinst halen we uit
        // de regelafstand (2 px in plaats van de standaard) en uit de kleine
        // labels, niet uit het cijfer zelf.
        const float compactSpatie = 2.0f;
        float groot = compact
                          ? ImGui::GetFontSize() + compactSpatie
                          : ( m_kopFont ? m_kopFont->FontSize : ImGui::GetFontSize() ) + st.ItemSpacing.y;

        float hoogte = st.WindowPadding.y * 2.0f + klein + groot;
        if( metOnderschrift ) hoogte += klein;
        return hoogte + 6.0f; // paar pixels lucht, zodat niets tegen de rand plakt
    }

    void Overlay::StatKaart( const char *label, const std::string &waarde, float breedte,
                              const char *onderschrift, bool waarschuwing, bool compact )
    {
        ImGui::PushStyleColor( ImGuiCol_ChildBg,
            waarschuwing ? TintKaartKleur( ImVec4( 0.85f, 0.25f, 0.20f, 1.0f ), 0.5f ) : KaartKleur() );
        ImGui::BeginChild( label, ImVec2( breedte, KaartHoogte( onderschrift != nullptr, compact ) ),
                            true, ImGuiWindowFlags_NoScrollbar );

        // Krappe regelafstand in compacte vorm: daar zit de hoogtewinst,
        // zonder aan de leesbaarheid van de cijfers te komen.
        if( compact ) ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( ImGui::GetStyle().ItemSpacing.x, 2.0f ) );

        // Label en onderschrift klein in compacte vorm; dat scheelt het meest
        // in de hoogte, en juist bij een lang kopje als "SNELHEID" met de
        // limiet eronder.
        if( compact && m_kleinFont ) ImGui::PushFont( m_kleinFont );
        TekstGedimd( label );
        if( compact && m_kleinFont ) ImGui::PopFont();

        if( !compact && m_kopFont ) ImGui::PushFont( m_kopFont );
        TekstS( waarde.c_str() );
        if( !compact && m_kopFont ) ImGui::PopFont();

        if( onderschrift )
        {
            if( compact && m_kleinFont ) ImGui::PushFont( m_kleinFont );
            TekstGedimd( onderschrift );
            if( compact && m_kleinFont ) ImGui::PopFont();
        }
        if( compact ) ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void Overlay::SchadeBalk( const char *naam, double percentage, const unsigned int *kleurOverride,
                               float labelBreedte, float breedte )
    {
        // Alles wordt relatief aan de startpositie gerekend, niet absoluut
        // vanaf de linkerrand -- anders zou een tweede kolom boven op de
        // eerste landen.
        float startY = ImGui::GetCursorPosY();
        float startX = ImGui::GetCursorPosX();
        float beschikbaar = ( breedte > 0.0f ) ? breedte : ImGui::GetContentRegionAvail().x;
        TekstGedimd( naam );

        // Balk zelf tekenen in plaats van ImGui::ProgressBar: die gebruikt
        // een ondoorzichtige achtergrondkleur uit het thema, en dat zou hier
        // alsnog donkere blokken op je scherm zetten.
        // Alle maten hieronder worden uit de LETTERGROOTTE afgeleid in
        // plaats van in vaste pixels gezet. Zet je deze kaart in een kleiner
        // lettertype, dan krimpen de percentage-kolom en de balkhoogte
        // automatisch mee en gaat de vrijgekomen breedte naar de balk. Met
        // vaste pixels bleef het even lomp ogen, hoe klein je de tekst ook
        // maakte.
        // Het percentage wordt straks in het gewone (grotere) lettertype
        // getekend, dus de kolom moet daar ook op gemeten worden -- anders
        // valt "100%" net buiten de kaart.
        if( m_kleinFontActief ) ImGui::PopFont();
        float pctBreedte = ImGui::CalcTextSize( "100%" ).x + 5.0f;
        if( m_kleinFontActief ) ImGui::PushFont( m_kleinFont );
        float balkHoogte = std::max( 5.0f, ImGui::GetFontSize() * 0.44f );
        float balkBreedte = std::max( 24.0f, beschikbaar - labelBreedte - pctBreedte );

        // Plafond schaalt mee: 8x de lettergrootte is bij 13pt ruim 100px,
        // en bij een groter lettertype navenant meer.
        float balkMaximum = ImGui::GetFontSize() * 7.0f;
        if( balkBreedte > balkMaximum ) balkBreedte = balkMaximum;

        // Balk verticaal uitlijnen met het midden van de tekstregel.
        ImGui::SetCursorPosY( startY + ( ImGui::GetTextLineHeight() - balkHoogte ) * 0.5f );
        ImGui::SetCursorPosX( startX + labelBreedte );
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        // Donkere goot met lage dekking: genoeg om de balk af te bakenen,
        // niet genoeg om het beeld eronder te blokkeren.
        dl->AddRectFilled( p, ImVec2( p.x + balkBreedte, p.y + balkHoogte ), IM_COL32( 0, 0, 0, 110 ), balkHoogte * 0.5f );

        if( percentage >= 0.0 )
        {
            float fractie = static_cast<float>( percentage / 100.0 );
            if( fractie > 1.0f ) fractie = 1.0f;

            // Groen tot 10%, dan amber, boven 40% rood -- tenzij de
            // aanroeper zijn eigen kleur meegeeft (bv. de tachograaf, die
            // niet over "schade" gaat en dus andere drempels heeft).
            ImU32 kleur;
            if( kleurOverride )
            {
                kleur = *kleurOverride;
            }
            else
            {
                kleur = ( percentage < 10.0 )  ? IM_COL32( 64, 176, 138, 255 )
                        : ( percentage < 40.0 ) ? IM_COL32( 217, 164, 66, 255 )
                                                 : IM_COL32( 212, 71, 61, 255 );
            }
            float vulling = balkBreedte * fractie;
            if( vulling > 3.0f )
            {
                dl->AddRectFilled( p, ImVec2( p.x + vulling, p.y + balkHoogte ), kleur, balkHoogte * 0.5f );
            }
            ImGui::Dummy( ImVec2( balkBreedte, balkHoogte + 2.0f ) );

            ImGui::SetCursorPosY( startY );
            ImGui::SetCursorPosX( startX + labelBreedte + balkBreedte + 5.0f );
            char pctTekst[ 16 ];
            snprintf( pctTekst, sizeof( pctTekst ), "%.0f%%", percentage );
            // Het percentage in het gewone (grotere) lettertype: dat is het
            // getal waar je op leest, het label ernaast mag klein blijven.
            if( m_kleinFontActief ) ImGui::PopFont();
            TekstS( pctTekst, kleurOverride ? IM_COL32( 255, 255, 255, 255 ) : kleur );
            if( m_kleinFontActief ) ImGui::PushFont( m_kleinFont );
        }
        else
        {
            // Onbekend: lege goot, geen misleidende 0%.
            ImGui::Dummy( ImVec2( balkBreedte, balkHoogte + 2.0f ) );
            ImGui::SetCursorPosY( startY );
            ImGui::SetCursorPosX( startX + labelBreedte + balkBreedte + 5.0f );
            TekstGedimd( T( "--" ) );
        }
    }

    void Overlay::LaadLogo()
    {
        std::filesystem::path pad;
        if( const char *appdata = std::getenv( "APPDATA" ) )
        {
            pad = appdata;
        }
        pad /= "CabNavi";
        pad /= "logo.png";

        // Debug: altijd naar debug.log schrijven wat er gebeurt, zodat we
        // niet hoeven te gokken als het logo niet verschijnt.
        auto schrijfDebug = [ & ]( const std::string &regel )
        {
            std::filesystem::path debugPad;
            if( const char *appdata = std::getenv( "APPDATA" ) ) debugPad = appdata;
            debugPad /= "CabNavi";
            std::error_code ec;
            std::filesystem::create_directories( debugPad, ec );
            debugPad /= "debug.log";
            std::ofstream uit( debugPad, std::ios::app );
            if( uit ) uit << "[Logo] " << regel << "\n";
        };

        if( !std::filesystem::exists( pad ) )
        {
            schrijfDebug( "Bestand niet gevonden op: " + pad.string() );
            return;
        }
        schrijfDebug( "Bestand gevonden op: " + pad.string() );

        int breedte = 0, hoogte = 0, kanalen = 0;
        unsigned char *pixels = stbi_load( pad.string().c_str(), &breedte, &hoogte, &kanalen, 4 );
        if( pixels == nullptr )
        {
            schrijfDebug( std::string( "stbi_load mislukt: " ) + stbi_failure_reason() );
            return;
        }
        if( m_device == nullptr )
        {
            schrijfDebug( "m_device is nullptr -- device nog niet klaar" );
            stbi_image_free( pixels );
            return;
        }
        schrijfDebug( "Gedecodeerd: " + std::to_string( breedte ) + "x" + std::to_string( hoogte ) );

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = breedte;
        desc.Height = hoogte;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA subresource{};
        subresource.pSysMem = pixels;
        subresource.SysMemPitch = desc.Width * 4;

        ID3D11Texture2D *textuur = nullptr;
        HRESULT hr = m_device->CreateTexture2D( &desc, &subresource, &textuur );
        if( SUCCEEDED( hr ) && textuur != nullptr )
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            ID3D11ShaderResourceView *srv = nullptr;
            m_device->CreateShaderResourceView( textuur, &srvDesc, &srv );
            m_logoTextuur = srv;
            m_logoBreedte = breedte;
            m_logoHoogte = hoogte;
            textuur->Release();
            schrijfDebug( "Textuur + SRV succesvol aangemaakt." );
        }
        else
        {
            schrijfDebug( "CreateTexture2D mislukt, HRESULT=" + std::to_string( hr ) );
        }

        stbi_image_free( pixels );
    }

    void Overlay::LaadTabIconen()
    {
        // Herbruikt hetzelfde laad-recept als LaadLogo(), maar dan voor
        // zes losse bestanden. Verwacht ze in
        // %APPDATA%\CabNavi\icons\<naam>.png -- ontbreekt een
        // bestand, dan blijft dat icoon leeg en valt de overlay terug op
        // niets tekenen voor die badge (geen crash, geen halve overlay).
        static const char *bestandsnamen[ AANTAL_TABS ] = {
            "live.png", "boordcomputer.png", "spelers.png", "geschiedenis.png",
            "statistieken.png", "incident.png", "vtc.png", "vtc-instellingen.png",
            "instellingen.png"
        };

        std::filesystem::path iconMap;
        if( const char *appdata = std::getenv( "APPDATA" ) ) iconMap = appdata;
        iconMap /= "CabNavi";
        iconMap /= "icons";

        for( int i = 0; i < AANTAL_TABS; ++i )
        {
            std::filesystem::path pad = iconMap / bestandsnamen[ i ];
            if( !std::filesystem::exists( pad ) ) continue;
            if( m_device == nullptr ) continue;

            int breedte = 0, hoogte = 0, kanalen = 0;
            unsigned char *pixels = stbi_load( pad.string().c_str(), &breedte, &hoogte, &kanalen, 4 );
            if( pixels == nullptr ) continue;

            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = breedte;
            desc.Height = hoogte;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA subresource{};
            subresource.pSysMem = pixels;
            subresource.SysMemPitch = desc.Width * 4;

            ID3D11Texture2D *textuur = nullptr;
            if( SUCCEEDED( m_device->CreateTexture2D( &desc, &subresource, &textuur ) ) && textuur != nullptr )
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = desc.Format;
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;

                ID3D11ShaderResourceView *srv = nullptr;
                m_device->CreateShaderResourceView( textuur, &srvDesc, &srv );
                m_tabTexturen[ i ] = srv;
                textuur->Release();
            }
            stbi_image_free( pixels );
        }
    }

    Overlay::~Overlay()
    {
        Shutdown();
    }

    bool Overlay::InitDirectX11( ID3D11Device *device, void *vensterHandle )
    {
        if( m_geinitialiseerd || device == nullptr )
        {
            return false;
        }

        m_device = device;
        m_device->GetImmediateContext( &m_context );

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // ImGui bewaart venstergrootte en -positie in een "imgui.ini" en
        // laadt dat terug bij de volgende sessie -- precies wat je van een
        // HUD verwacht.
        //
        // Dit stond eerder uit omdat een per ongeluk enorm gesleept venster
        // steeds terugkwam. Die oplossing gooide het kind met het badwater
        // weg. Nu staat het weer aan, maar met twee verschillen:
        //
        //   1. Het bestand gaat naar %APPDATA%\CabNavi\ in plaats van
        //      de spelmap, zodat het bij je andere instellingen staat en
        //      een spel-update het niet opruimt.
        //   2. SetNextWindowSizeConstraints (zie Teken()) begrenst de maat
        //      op 1400x1000, en dat geldt OOK voor een teruggeladen maat.
        //      Een doorgeschoten venster wordt dus vanzelf teruggetrokken.
        //
        // Het pad moet blijven leven zolang ImGui draait: io.IniFilename is
        // een kale pointer die ImGui niet kopieert. Vandaar een lid in
        // plaats van een lokale string.
        m_iniPad = ( InstellingenMap() / "imgui.ini" ).string();
        io.IniFilename = m_iniPad.c_str();

        // Standaard gebruikt ImGui een heel klein, kaal pixel-lettertype --
        // daar zag alles "standaard debugvenster" uit. Segoe UI staat op
        // elke Windows-installatie (het is Windows' eigen systeemlettertype
        // sinds Windows Vista), dus dat laden we in plaats daarvan, op een
        // groter formaat met anti-aliasing voor een strak, modern gevoel.
        {
            ImFontConfig fontConfig;
            fontConfig.OversampleH = 3;
            fontConfig.OversampleV = 3;
            fontConfig.PixelSnapH = true;
            ImFont *font = io.Fonts->AddFontFromFileTTF(
                "C:\\Windows\\Fonts\\segoeui.ttf", 19.0f, &fontConfig );
            if( font == nullptr )
            {
                // Onwaarschijnlijk (Segoe UI zit standaard op elke Windows-
                // pc), maar voor de zekerheid: val terug op het ingebouwde
                // lettertype zodat de overlay nooit helemaal leeg blijft.
                io.Fonts->AddFontDefault();
            }

            // Tweede lettertype, groter en zwaarder (Semibold-variant),
            // voor kerncijfers zoals ETA en tachograaf-tijd -- geeft
            // visuele hierarchie i.p.v. dat alles even zwaar oogt.
            ImFontConfig kopFontConfig;
            kopFontConfig.OversampleH = 3;
            kopFontConfig.OversampleV = 3;
            kopFontConfig.PixelSnapH = true;
            m_kopFont = io.Fonts->AddFontFromFileTTF(
                "C:\\Windows\\Fonts\\seguisb.ttf", 32.0f, &kopFontConfig );
            if( m_kopFont == nullptr )
            {
                // Segoe UI Semibold niet gevonden (kan voorkomen op oudere
                // Windows-versies) -- val terug op gewone Segoe UI, gewoon
                // groter, dan mis je alleen het extra gewicht.
                m_kopFont = io.Fonts->AddFontFromFileTTF(
                    "C:\\Windows\\Fonts\\segoeui.ttf", 32.0f, &kopFontConfig );
            }

            // Derde lettertype, kleiner dan de standaard 19pt. Bedoeld voor
            // de dichtbezette kaarten (schade, aanhanger, tachograaf): daar
            // staan korte labels, balkjes en percentages naast elkaar, en op
            // 19pt paste dat niet meer zodra het venster smaller stond.
            ImFontConfig kleinConfig;
            kleinConfig.OversampleH = 3;
            kleinConfig.OversampleV = 3;
            kleinConfig.PixelSnapH = true;
            m_kleinFont = io.Fonts->AddFontFromFileTTF(
                "C:\\Windows\\Fonts\\segoeui.ttf", 16.0f, &kleinConfig );
        }

        // Modern, donker thema met afgeronde hoeken -- past bij een
        // "in-game HUD"-uitstraling in plaats van een debug-venster.
        ImGui::StyleColorsDark();
        ImGuiStyle &stijl = ImGui::GetStyle();
        stijl.WindowRounding = 10.0f;
        stijl.ChildRounding = 8.0f;
        stijl.FrameRounding = 6.0f;
        stijl.GrabRounding = 6.0f;
        stijl.WindowBorderSize = 0.0f;
        stijl.WindowPadding = ImVec2( 14, 14 );
        stijl.FramePadding = ImVec2( 8, 5 );
        stijl.ItemSpacing = ImVec2( 8, 7 );
        stijl.Colors[ ImGuiCol_WindowBg ] = ImVec4( 0.06f, 0.07f, 0.09f, 0.92f );
        stijl.Colors[ ImGuiCol_TitleBgActive ] = ImVec4( 0.15f, 0.35f, 0.60f, 1.0f );
        // Terugval voor het eerste frame; wordt daarna elke frame overschreven
        // in Teken(). Zonder deze regels staat hier ImGui's ondoorzichtige
        // bijna-zwart zolang het venster geen focus heeft.
        stijl.Colors[ ImGuiCol_TitleBg ] = ImVec4( 0.10f, 0.22f, 0.38f, 0.85f );
        stijl.Colors[ ImGuiCol_TitleBgCollapsed ] = ImVec4( 0.10f, 0.22f, 0.38f, 0.85f );
        stijl.Colors[ ImGuiCol_Header ] = ImVec4( 0.15f, 0.35f, 0.60f, 0.6f );

        // Standaard-blauw van ImGui's eigen Dark-thema eruit -- neutraal
        // grijs voor knoppen die niet actief zijn, zodat alleen de actieve
        // tab (via de losse PushStyleColor hieronder) de accentkleur krijgt.
        stijl.Colors[ ImGuiCol_Button ] = ImVec4( 0.22f, 0.22f, 0.24f, 1.0f );
        stijl.Colors[ ImGuiCol_ButtonHovered ] = ImVec4( 0.30f, 0.30f, 0.33f, 1.0f );
        stijl.Colors[ ImGuiCol_ButtonActive ] = ImVec4( 0.35f, 0.35f, 0.38f, 1.0f );
        stijl.Colors[ ImGuiCol_FrameBg ] = ImVec4( 0.16f, 0.16f, 0.18f, 1.0f );
        stijl.Colors[ ImGuiCol_FrameBgHovered ] = ImVec4( 0.22f, 0.22f, 0.24f, 1.0f );
        stijl.Colors[ ImGuiCol_FrameBgActive ] = ImVec4( 0.26f, 0.26f, 0.28f, 1.0f );
        stijl.Colors[ ImGuiCol_CheckMark ] = ImVec4( 0.85f, 0.85f, 0.85f, 1.0f );
        stijl.Colors[ ImGuiCol_SliderGrab ] = ImVec4( 0.55f, 0.55f, 0.58f, 1.0f );
        stijl.Colors[ ImGuiCol_SliderGrabActive ] = ImVec4( 0.65f, 0.65f, 0.68f, 1.0f );

        // Terugval voor de scrollbalk. Elke frame worden deze alsnog
        // overschreven op basis van de doorzichtigheid-schuif (zie Teken()),
        // maar zonder deze regels staat ImGui's ondoorzichtige standaardgrijs
        // er even, bijvoorbeeld in het allereerste frame.
        stijl.Colors[ ImGuiCol_ScrollbarBg ] = ImVec4( 0.03f, 0.03f, 0.04f, 0.20f );
        stijl.Colors[ ImGuiCol_ScrollbarGrab ] = ImVec4( 0.45f, 0.45f, 0.50f, 0.55f );
        stijl.Colors[ ImGuiCol_ScrollbarGrabHovered ] = ImVec4( 0.58f, 0.58f, 0.63f, 0.70f );
        stijl.Colors[ ImGuiCol_ScrollbarGrabActive ] = ImVec4( 0.68f, 0.68f, 0.73f, 0.80f );
        stijl.ScrollbarSize = 11.0f;   // iets smaller dan de standaard 14
        stijl.ScrollbarRounding = 6.0f;

        ImGui_ImplWin32_Init( vensterHandle );
        ImGui_ImplDX11_Init( m_device, m_context );

        m_vensterHandle = vensterHandle;
        m_geinitialiseerd = true;

        LaadLogo();
        LaadTabIconen();

        return true;
    }

    void Overlay::Shutdown()
    {
        if( !m_geinitialiseerd )
        {
            return;
        }
        if( m_logoTextuur != nullptr )
        {
            static_cast<ID3D11ShaderResourceView *>( m_logoTextuur )->Release();
            m_logoTextuur = nullptr;
        }
        for( int i = 0; i < AANTAL_TABS; ++i )
        {
            if( m_tabTexturen[ i ] != nullptr )
            {
                static_cast<ID3D11ShaderResourceView *>( m_tabTexturen[ i ] )->Release();
                m_tabTexturen[ i ] = nullptr;
            }
        }
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        if( m_context ) m_context->Release();
        m_geinitialiseerd = false;
    }

    void Overlay::OpMuisBeweging( int x, int y )
    {
        if( !m_geinitialiseerd ) return;
        ImGui::GetIO().AddMousePosEvent( static_cast<float>( x ), static_cast<float>( y ) );
    }

    void Overlay::OpMuisKnop( int knop, bool ingedrukt )
    {
        if( !m_geinitialiseerd ) return;
        ImGui::GetIO().AddMouseButtonEvent( knop, ingedrukt );
    }

    void Overlay::OpMuisWiel( float delta )
    {
        if( !m_geinitialiseerd ) return;
        ImGui::GetIO().AddMouseWheelEvent( 0.0f, delta );
    }

    void Overlay::OpToets( unsigned int virtualKeyCode, bool ingedrukt )
    {
        if( !m_geinitialiseerd ) return;
        ImGuiIO &io = ImGui::GetIO();

        // ImGui houdt de modifier-status apart bij van de gewone toetsen;
        // AddKeyEvent alleen is niet genoeg om Ctrl+V te laten werken.
        switch( virtualKeyCode )
        {
            case 0x11: case 0xA2: case 0xA3:
                io.AddKeyEvent( ImGuiMod_Ctrl, ingedrukt );
                break;
            case 0x10: case 0xA0: case 0xA1:
                io.AddKeyEvent( ImGuiMod_Shift, ingedrukt );
                break;
            case 0x12: case 0xA4: case 0xA5:
                io.AddKeyEvent( ImGuiMod_Alt, ingedrukt );
                break;
            default:
                break;
        }

        ImGuiKey key = VkNaarImGuiKey( virtualKeyCode );
        if( key != ImGuiKey_None )
        {
            io.AddKeyEvent( key, ingedrukt );
        }
    }

    void Overlay::OpKarakter( unsigned int codepoint )
    {
        if( !m_geinitialiseerd ) return;
        ImGui::GetIO().AddInputCharacter( codepoint );
    }

    bool Overlay::WilMuis() const
    {
        return m_geinitialiseerd && ImGui::GetIO().WantCaptureMouse;
    }

    bool Overlay::WilToetsenbord() const
    {
        return m_geinitialiseerd && ImGui::GetIO().WantCaptureKeyboard;
    }

    void Overlay::Teken()
    {
        if( !m_geinitialiseerd || !m_zichtbaar )
        {
            return;
        }

        ImGui_ImplDX11_NewFrame();

        // LET OP: ImGui_ImplWin32_NewFrame() bewust NIET aanroepen. Die
        // functie peilt zelf ook de muispositie/-knoppen op via Windows'
        // eigen GetCursorPos/GetKeyState, en overschrijft daarmee wat wij
        // net via OpMuisBeweging/OpMuisKnop hebben doorgegeven -- dat was
        // precies de reden dat klikken niet aankwamen terwijl onze eigen
        // debug-tellers wel gewoon opliepen. Wij voeden alle input zelf
        // (zie OpMuis*/OpToets), dus we hoeven alleen zelf de vensterграootte
        // en de tijd tussen frames bij te houden -- dat deed die functie
        // verder ook nog.
        {
            ImGuiIO &io = ImGui::GetIO();

            RECT rect{};
            if( m_vensterHandle != nullptr && GetClientRect( static_cast<HWND>( m_vensterHandle ), &rect ) )
            {
                float breedte = static_cast<float>( rect.right - rect.left );
                float hoogte = static_cast<float>( rect.bottom - rect.top );
                if( breedte > 0.0f && hoogte > 0.0f )
                {
                    io.DisplaySize = ImVec2( breedte, hoogte );
                }
            }

            static auto vorigeTijd = std::chrono::steady_clock::now();
            auto nu = std::chrono::steady_clock::now();
            float delta = std::chrono::duration<float>( nu - vorigeTijd ).count();
            io.DeltaTime = delta > 0.0f ? delta : ( 1.0f / 60.0f );
            vorigeTijd = nu;
        }

        ImGui::NewFrame();

        // Titelbalk/header-kleur elke frame op basis van de instelling
        // (i.p.v. alleen bij opstarten), zodat een kleurwijziging in
        // Instellingen meteen zichtbaar is.
        //
        // LET OP: ImGui heeft DRIE titelbalk-kleuren. Hier stond alleen
        // TitleBgActive (venster heeft focus). TitleBg (geen focus) en
        // TitleBgCollapsed (ingeklapt) bleven op ImGui's standaard staan, en
        // dat is ondoorzichtig bijna-zwart. Zodra je in het spel klikte en de
        // overlay de focus kwijtraakte, sloeg de balk dus om naar zwart --
        // precies dat "soms zwart" dat je zag.
        ImVec4 titelActief( m_accentKleur[ 0 ] * 0.5f, m_accentKleur[ 1 ] * 0.5f,
                             m_accentKleur[ 2 ] * 0.5f, m_doorzichtigheid );
        // Zonder focus dezelfde tint, alleen wat gedempter -- zo zie je nog
        // steeds welk venster actief is, zonder harde kleurwissel.
        ImVec4 titelInactief( m_accentKleur[ 0 ] * 0.32f, m_accentKleur[ 1 ] * 0.32f,
                               m_accentKleur[ 2 ] * 0.32f, m_doorzichtigheid * 0.85f );

        ImGui::PushStyleColor( ImGuiCol_TitleBgActive, titelActief );
        ImGui::PushStyleColor( ImGuiCol_TitleBg, titelInactief );
        ImGui::PushStyleColor( ImGuiCol_TitleBgCollapsed, titelInactief );
        ImGui::PushStyleColor( ImGuiCol_Header,
            ImVec4( m_accentKleur[ 0 ], m_accentKleur[ 1 ], m_accentKleur[ 2 ], 0.5f ) );

        // Invulvakjes/schuiven/checkboxen liepen niet mee met de
        // doorzichtigheidsschuif -- stonden vast op ondoorzichtig zwart,
        // wat er als "losse harde blokjes" uitzag bij een doorzichtiger
        // venster. Nu ook gekoppeld, met een ondergrens zodat ze altijd
        // nog wel duidelijk als invulveld herkenbaar blijven.
        float frameAlpha = std::max( 0.55f, m_doorzichtigheid );
        ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( 0.16f, 0.16f, 0.18f, frameAlpha ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( 0.22f, 0.22f, 0.24f, frameAlpha ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( 0.26f, 0.26f, 0.28f, frameAlpha ) );

        // De scrollbalk stond nog op ImGui's standaardkleuren: de goot is
        // half doorzichtig, maar de GRIJPER is standaard volledig
        // ondoorzichtig grijs. Dat gaf die harde donkere balk aan de
        // rechterkant, die niet meeliep met de rest van de HUD.
        // Nu ook gekoppeld aan de schuif, met een lagere ondergrens dan de
        // invulvelden -- een scrollbalk mag rustig wegvallen zolang je 'm
        // nog kunt aanwijzen.
        float scrollAlpha = std::max( 0.30f, 0.75f * m_doorzichtigheid );
        ImGui::PushStyleColor( ImGuiCol_ScrollbarBg, ImVec4( 0.03f, 0.03f, 0.04f, scrollAlpha * 0.45f ) );
        ImGui::PushStyleColor( ImGuiCol_ScrollbarGrab, ImVec4( 0.45f, 0.45f, 0.50f, scrollAlpha ) );
        ImGui::PushStyleColor( ImGuiCol_ScrollbarGrabHovered,
                                ImVec4( 0.58f, 0.58f, 0.63f, std::min( 1.0f, scrollAlpha + 0.15f ) ) );
        ImGui::PushStyleColor( ImGuiCol_ScrollbarGrabActive,
                                ImVec4( 0.68f, 0.68f, 0.73f, std::min( 1.0f, scrollAlpha + 0.25f ) ) );

        ImGui::SetNextWindowSize( ImVec2( 760, 620 ), ImGuiCond_FirstUseEver );
        ImGui::SetNextWindowSizeConstraints( ImVec2( 380, 320 ), ImVec2( 1400, 1000 ) );
        // Ondergrens van 0.62: onder die waarde begint losse tekst die NIET
        // in een kaartje staat (kopregel, "geen actieve rit", hints) weg te
        // vallen boven fel gras of een lichte lucht. De kaartjes zelf zijn
        // altijd donker (zie KaartKleur), dus de schuif regelt vooral hoeveel
        // spel je tussen de kaarten door ziet.
        ImGui::SetNextWindowBgAlpha( m_doorzichtigheid );
        if( ImGui::Begin( "CabNavi", &m_zichtbaar, ImGuiWindowFlags_NoCollapse ) )
        {
            // --- Zijbalk (links): alleen de tab-iconen (logo staat bovenin het hoofdgedeelte, zie verderop -- te smal hier voor een leesbaar sierlettertype) ---
            ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0, 0, 0, 0.15f * m_iconenDoorzichtigheid ) );
            ImGui::BeginChild( "zijbalk", ImVec2( 78, 0 ), true );

            ImGui::Spacing();

            // LET OP: de maat is AANTAL_TABS, niet een los ingetypt getal.
            // Hier stond [6] terwijl de lus hieronder tot AANTAL_TABS (7)
            // loopt. Bij het zevende icoon werd dus geheugen NAAST de tabel
            // gelezen -- dat was de crash: EXCEPTION_ACCESS_VIOLATION in
            // ucrtbase, met de letters "Live" als adres.
            //
            // Met AANTAL_TABS als maat klaagt de compiler voortaan meteen als
            // er een tabblad bijkomt en de namen niet meegroeien.
            struct TabIcoon { const char *tooltip; };
            Logboek::Spoor( "zijbalk met tabbladiconen" );
            static const TabIcoon iconen[ AANTAL_TABS ] = {
                { "Live" }, { "Boordcomputer" }, { "Spelers" }, { "Geschiedenis" },
                { "Statistieken" }, { "Incident / Replay" }, { "VTC" },
                { "VTC-instellingen" }, { "Instellingen" },
            };
            for( int i = 0; i < AANTAL_TABS; ++i )
            {
                bool actief = ( m_actieveTab == i );
                ImGui::SetCursorPosX( 6 );
                ImVec2 knopPos = ImGui::GetCursorScreenPos();

                // Onzichtbare knop puur voor klik-/hover-detectie -- de
                // zichtbare badge tekenen we zelf eronder, dat geeft een
                // steviger, herkenbaarder "app-icoon"-gevoel dan een
                // gewone rechthoekige ImGui-knop.
                ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0, 0, 0, 0 ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 1, 1, 1, 0.06f ) );
                ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 1, 1, 1, 0.1f ) );
                if( ImGui::Button( ( "##tab" + std::to_string( i ) ).c_str(), ImVec2( 66, 54 ) ) )
                {
                    m_actieveTab = i;
                }
                ImGui::PopStyleColor( 3 );

                ImDrawList *railDraw = ImGui::GetWindowDrawList();
                ImVec2 badgeMidden( knopPos.x + 33, knopPos.y + 27 );
                bool heeftPng = m_tabTexturen[ i ] != nullptr;

                // De PNG-icoontjes hebben zelf al een gekleurd afgerond
                // vierkantje als achtergrond gebakken -- dus alleen bij de
                // terugval-lijniconen tekenen we zelf nog een rond badgetje.
                if( !heeftPng )
                {
                    ImU32 badgeKleur = actief
                        ? IM_COL32( 212, 55, 47, static_cast<int>( 255 * m_iconenDoorzichtigheid ) )
                        : IM_COL32( 40, 40, 45, static_cast<int>( 220 * m_iconenDoorzichtigheid ) );
                    railDraw->AddCircleFilled( badgeMidden, 21.0f, badgeKleur, 32 );
                }
                if( actief )
                {
                    // Zachte rode gloed-rand rondom de actieve tab -- altijd,
                    // ook bovenop een PNG-icoon, als duidelijk "geselecteerd"-signaal.
                    railDraw->AddCircle( badgeMidden, 24.0f, IM_COL32( 212, 55, 47, static_cast<int>( 140 * m_iconenDoorzichtigheid ) ), 32, 2.5f );
                }

                if( heeftPng )
                {
                    // Echte kleurrijke PNG geladen -- die tekenen, niet de
                    // zelfgetekende lijn-versie.
                    float iconGrootte = 38.0f;
                    ImVec2 iconMin( badgeMidden.x - iconGrootte / 2, badgeMidden.y - iconGrootte / 2 );
                    ImVec2 iconMax( badgeMidden.x + iconGrootte / 2, badgeMidden.y + iconGrootte / 2 );
                    float alpha = m_iconenDoorzichtigheid;
                    railDraw->AddImage( m_tabTexturen[ i ], iconMin, iconMax, ImVec2( 0, 0 ), ImVec2( 1, 1 ),
                                        IM_COL32( 255, 255, 255, static_cast<int>( 255 * alpha ) ) );
                }
                else
                {
                    // Geen PNG gevonden -- terugval op de zelfgetekende
                    // lijn-iconen, overlay blijft gewoon werken.
                    ImU32 icoonKleur = actief ? IM_COL32( 255, 255, 255, 255 ) : IM_COL32( 225, 225, 230, 255 );
                    TekenTabIcoon( i, badgeMidden.x, badgeMidden.y, 12.5f, icoonKleur );
                }
                if( ImGui::IsItemHovered() )
                {
                    ImGui::SetTooltip( "%s", T( iconen[ i ].tooltip ) );
                }
                ImGui::Spacing();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();

            ImGui::SameLine();

            // --- Hoofdgedeelte (rechts): actieve tabblad-inhoud ---
            ImGui::BeginGroup();

            // Logo groot en leesbaar bovenaan het hoofdgedeelte -- hier is
            // wel genoeg breedte voor (de zijbalk was veel te smal, daar
            // werd het sierlettertype een onleesbare rode veeg).
            if( m_logoTextuur != nullptr && m_logoHoogte > 0 )
            {
                float doelHoogte = 32.0f;
                float schaal = doelHoogte / static_cast<float>( m_logoHoogte );
                ImVec2 grootte( m_logoBreedte * schaal, doelHoogte );
                ImGui::Image( m_logoTextuur, grootte, ImVec2( 0, 0 ), ImVec2( 1, 1 ),
                              ImVec4( 1, 1, 1, m_iconenDoorzichtigheid ) );
                ImGui::Spacing();
            }

            ImGui::TextDisabled( T( "Insert = verbergen | Rechtermuisklik = muis aan/uit" ) );
            ImGui::Separator();

            // Spelers in beeld aanmelden voor een VTC-opzoeking. Dit staat
            // BEWUST hier en niet op het spelers-tabblad: stond het daar,
            // dan werd er niemand opgezocht zolang je naar een ander
            // tabblad keek, en bleef de markering leeg. Kost hier niets --
            // er gaat alleen een nummer in een rij, de Web API werkt die op
            // zijn eigen thread af en slaat bekende spelers zelf over.
            // Eigen TruckersMP-ID doorgeven zodra de SDK het weet; daarmee
            // kan de Web API ophalen waar JIJ je voor hebt aangemeld. Staat
            // los van de VTC-schakelaar.
            if( m_eigenConvooien )
            {
                m_webApi.ZetEigenAccount( m_spelers.EigenAccountId() );
            }

            if( m_vtcAan && m_vtcSpelersOpzoeken )
            {
                // ALLEEN DE DICHTSTBIJZIJNDEN. GEMETEN 30-08: zonder een
                // grens waren er 875 spelers opgezocht en gaf de API een 429
                // ("te veel verzoeken"). De lijst komt al op afstand
                // gesorteerd binnen, dus de eersten zijn de dichtstbijzijnden
                // -- die krijgen ook voorrang in de wachtrij.
                int nr = 0;
                for( const auto &s : m_spelers.GeefSpelers() )
                {
                    if( nr >= 20 ) break;
                    if( s.afstandMeter > 400.0f ) break;
                    m_webApi.MeldSpelerAan( s.accountId, nr < 5 );
                    ++nr;
                }
            }

            switch( m_actieveTab )
            {
                // Spoor per tabblad. Crasht het spel, dan wijst de laatste
                // [spoor]-regel in debug.log aan waar de overlay mee bezig was.
                case 0: Logboek::Spoor( "tab Live" );          TekenLiveTab(); break;
                case 1: Logboek::Spoor( "tab Boordcomputer" ); TekenBoordcomputerTab(); break;
                case 2: Logboek::Spoor( "tab Spelers" );       TekenSpelersTab(); break;
                case 3: Logboek::Spoor( "tab Geschiedenis" );  TekenGeschiedenisTab(); break;
                case 4: Logboek::Spoor( "tab Statistieken" );  TekenStatistiekenTab(); break;
                case 5: Logboek::Spoor( "tab Incident" );      TekenIncidentTab(); break;
                case 6: Logboek::Spoor( "tab VTC" );           TekenVtcTab(); break;
                case 7: Logboek::Spoor( "tab VTC-instel" );    TekenVtcInstellingenTab(); break;
                case 8: Logboek::Spoor( "tab Instellingen" );  TekenInstellingenTab(); break;
                default: break;
            }
            ImGui::EndGroup();
        }
        ImGui::End();
        ImGui::PopStyleColor( 11 ); // 3x Titel, Header, 3x FrameBg, 4x Scrollbar (zie push hierboven)

        ImGui::Render();

        // Belangrijke stap die ontbrak: het spel heeft op dit punt (net na
        // zijn eigen rendering) waarschijnlijk nog het juiste render target
        // gebonden, maar we zetten het hier defensief opnieuw expliciet
        // vast voordat we tekenen. Zonder dit kan ImGui in het "luchtledige"
        // tekenen: de aanroep slaagt, maar er verschijnt niets op het scherm.
        ID3D11RenderTargetView *huidigRenderTarget = nullptr;
        m_context->OMGetRenderTargets( 1, &huidigRenderTarget, nullptr );
        if( huidigRenderTarget != nullptr )
        {
            m_context->OMSetRenderTargets( 1, &huidigRenderTarget, nullptr );
        }

        ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );

        if( huidigRenderTarget != nullptr )
        {
            huidigRenderTarget->Release();
        }
    }

    void Overlay::TekenTabIcoon( int tabIndex, float middenX, float middenY, float straal, ImU32 kleur )
    {
        ImDrawList *draw = ImGui::GetWindowDrawList();
        ImVec2 midden( middenX, middenY );
        float r = straal;

        switch( tabIndex )
        {
            case 0: // Live -- stuurwiel (cirkel + 3 spaken + naaf)
            {
                draw->AddCircle( midden, r, kleur, 24, 3.0f );
                for( int i = 0; i < 3; ++i )
                {
                    float hoek = i * ( 2.0f * 3.14159265f / 3.0f ) - 1.57f;
                    draw->AddLine( midden, ImVec2( midden.x + r * cosf( hoek ), midden.y + r * sinf( hoek ) ), kleur, 2.8f );
                }
                draw->AddCircleFilled( midden, r * 0.28f, kleur );
                break;
            }
            case 1: // Boordcomputer -- meterklokje (boog + wijzer + naaf)
            {
                // Halve cirkel als wijzerplaat, met een wijzer schuin omhoog.
                draw->PathArcTo( midden, r * 0.75f, 3.14159265f, 2.0f * 3.14159265f, 16 );
                draw->PathStroke( kleur, 0, 2.6f );
                draw->AddLine( midden,
                                ImVec2( midden.x + r * 0.55f * cosf( -0.9f ),
                                         midden.y + r * 0.55f * sinf( -0.9f ) ),
                                kleur, 2.6f );
                draw->AddCircleFilled( midden, 2.4f, kleur );
                break;
            }
            case 2: // Spelers -- radar/satellietschotel (concentrische bogen + stip)
            {
                for( int i = 1; i <= 3; ++i )
                {
                    draw->PathArcTo( midden, r * i / 3.0f, -2.3f, -0.8f, 16 );
                    draw->PathStroke( kleur, 0, 2.8f );
                }
                draw->AddCircleFilled( ImVec2( midden.x + r * 0.75f, midden.y + r * 0.55f ), 2.5f, kleur );
                break;
            }
            case 3: // Geschiedenis -- klok (cirkel + wijzers)
            {
                draw->AddCircle( midden, r, kleur, 24, 2.8f );
                draw->AddLine( midden, ImVec2( midden.x, midden.y - r * 0.6f ), kleur, 2.8f );
                draw->AddLine( midden, ImVec2( midden.x + r * 0.45f, midden.y + r * 0.1f ), kleur, 2.8f );
                break;
            }
            case 4: // Statistieken -- staafdiagram (3 balkjes, oplopend)
            {
                float breedteBalk = r * 0.35f;
                float basis = midden.y + r * 0.9f;
                draw->AddRectFilled( ImVec2( midden.x - breedteBalk * 2.2f, basis - r * 0.7f ),
                                      ImVec2( midden.x - breedteBalk * 0.6f, basis ), kleur, 1.0f );
                draw->AddRectFilled( ImVec2( midden.x - breedteBalk * 0.5f, basis - r * 1.2f ),
                                      ImVec2( midden.x + breedteBalk * 0.7f, basis ), kleur, 1.0f );
                draw->AddRectFilled( ImVec2( midden.x + breedteBalk * 0.8f, basis - r * 1.6f ),
                                      ImVec2( midden.x + breedteBalk * 2.0f, basis ), kleur, 1.0f );
                break;
            }
            case 5: // Incident/Replay -- filmklapper (rechthoek + schuine streep bovenaan)
            {
                float b = r * 0.9f;
                draw->AddRect( ImVec2( midden.x - b, midden.y - b * 0.5f ), ImVec2( midden.x + b, midden.y + b ), kleur, 2.0f, 0, 2.8f );
                draw->AddLine( ImVec2( midden.x - b, midden.y - b * 0.1f ), ImVec2( midden.x + b, midden.y - b * 0.1f ), kleur, 2.8f );
                draw->AddLine( ImVec2( midden.x - b * 0.5f, midden.y - b * 0.5f ), ImVec2( midden.x - b * 0.15f, midden.y - b * 0.1f ), kleur, 2.0f );
                draw->AddLine( ImVec2( midden.x + b * 0.15f, midden.y - b * 0.5f ), ImVec2( midden.x + b * 0.5f, midden.y - b * 0.1f ), kleur, 2.0f );
                break;
            }
            case 6: // VTC -- bedrijfspand (lage loods links, hoog kantoor rechts)
            {
                const float b = r * 0.95f;   // halve breedte van het geheel
                // Kantoor
                draw->AddRectFilled( ImVec2( midden.x + b * 0.05f, midden.y - r * 0.85f ),
                                      ImVec2( midden.x + b, midden.y + r * 0.9f ), kleur, 1.5f );
                // Loods
                draw->AddRectFilled( ImVec2( midden.x - b, midden.y - r * 0.15f ),
                                      ImVec2( midden.x - b * 0.05f, midden.y + r * 0.9f ), kleur, 1.5f );
                break;
            }

            case 7: // VTC-instellingen -- moersleutel (steel + open bek)
            {
                const float hoek = -0.785398f; // 45 graden, bek naar rechtsboven
                ImVec2 kop( midden.x + r * 0.45f * cosf( hoek ), midden.y + r * 0.45f * sinf( hoek ) );
                ImVec2 staart( midden.x - r * 0.9f * cosf( hoek ), midden.y - r * 0.9f * sinf( hoek ) );
                draw->AddLine( staart, kop, kleur, 3.2f );
                // Bek: een boog in plaats van een dichte cirkel, zodat het
                // een sleutel blijft en geen ring wordt.
                draw->PathArcTo( kop, r * 0.5f, hoek + 0.7f, hoek + 5.6f, 20 );
                draw->PathStroke( kleur, 0, 3.0f );
                break;
            }

            case 8: // Instellingen -- tandwiel (cirkel + 6 tandjes)
            {
                draw->AddCircle( midden, r * 0.55f, kleur, 16, 2.8f );
                for( int i = 0; i < 6; ++i ) // 6 tandjes -- niets met tabbladen te maken
                {
                    float hoek = i * ( 2.0f * 3.14159265f / 6.0f );
                    ImVec2 binnen( midden.x + r * 0.7f * cosf( hoek ), midden.y + r * 0.7f * sinf( hoek ) );
                    ImVec2 buiten( midden.x + r * 1.15f * cosf( hoek ), midden.y + r * 1.15f * sinf( hoek ) );
                    draw->AddLine( binnen, buiten, kleur, 3.0f );
                }
                break;
            }
            default: break;
        }
    }

    void Overlay::TekenVoertuigIcoon( bool isBus, float grootte )
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList *draw = ImGui::GetWindowDrawList();
        ImU32 kleurTegel = IM_COL32( 212, 55, 47, 255 ); // TMP-rood, zoals de knoppenrij in het spel
        ImU32 kleurIcoon = IM_COL32( 255, 255, 255, 255 ); // wit icoon erop, zoals TMP

        // Rood afgerond vierkant tegeltje als achtergrond.
        draw->AddRectFilled( pos, ImVec2( pos.x + grootte, pos.y + grootte ), kleurTegel, grootte * 0.22f );

        float pad = grootte * 0.2f;
        float b = grootte - pad * 2;      // breedte van het tekengebied binnen de tegel
        float x0 = pos.x + pad, y0 = pos.y + pad;

        if( isBus )
        {
            // Simpel wit bus-silhouet: lange rechthoek met 3 raampjes en 2 wielen.
            draw->AddRectFilled( ImVec2( x0, y0 + b * 0.15f ), ImVec2( x0 + b, y0 + b * 0.75f ), kleurIcoon, 1.5f );
            float raamB = ( b - 4 ) / 3.0f;
            for( int i = 0; i < 3; ++i )
            {
                float rx = x0 + 2 + i * raamB;
                draw->AddRectFilled( ImVec2( rx, y0 + b * 0.25f ), ImVec2( rx + raamB - 2, y0 + b * 0.5f ), kleurTegel );
            }
            draw->AddCircleFilled( ImVec2( x0 + b * 0.22f, y0 + b * 0.85f ), b * 0.12f, kleurIcoon );
            draw->AddCircleFilled( ImVec2( x0 + b * 0.78f, y0 + b * 0.85f ), b * 0.12f, kleurIcoon );
        }
        else
        {
            // Simpel wit vrachtwagen-silhouet: cabine + trailer + 2 wielen.
            draw->AddRectFilled( ImVec2( x0, y0 + b * 0.2f ), ImVec2( x0 + b * 0.34f, y0 + b * 0.75f ), kleurIcoon, 1.5f );
            draw->AddRectFilled( ImVec2( x0 + b * 0.32f, y0 + b * 0.4f ), ImVec2( x0 + b, y0 + b * 0.75f ), kleurIcoon, 1.5f );
            draw->AddRectFilled( ImVec2( x0 + b * 0.05f, y0 + b * 0.3f ), ImVec2( x0 + b * 0.26f, y0 + b * 0.55f ),
                                  kleurTegel );
            draw->AddCircleFilled( ImVec2( x0 + b * 0.2f, y0 + b * 0.85f ), b * 0.12f, kleurIcoon );
            draw->AddCircleFilled( ImVec2( x0 + b * 0.8f, y0 + b * 0.85f ), b * 0.12f, kleurIcoon );
        }

        ImGui::Dummy( ImVec2( grootte, grootte ) );
    }

    void Overlay::TekenLiveTab()
    {
        bool iets = false;

        // Convooi dat er zo aankomt. Verschijnt alleen als jij of je VTC
        // zich ervoor heeft aangemeld, en alleen binnen het laatste uur --
        // anders staat het er dagenlang voor niets.
        TekenConvooiHerinnering();

        if( m_vracht.HeeftActieveRit() )
        {
            iets = true;
            const Trip &t = m_vracht.HuidigeRit();

            // Route-kaart
            ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
            ImGui::BeginChild( "route_kaart", ImVec2( 0, 90 ), true, ImGuiWindowFlags_NoScrollbar );
            TekenVoertuigIcoon( false, 24.0f );
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextColored(
                ImVec4( m_accentKleur[ 0 ] + 0.15f, m_accentKleur[ 1 ] + 0.1f, m_accentKleur[ 2 ] + 0.1f, 1.0f ),
                T( "ONDERWEG" ) );
            ImGui::Text( "%s -> %s", t.bronStad.empty() ? "?" : t.bronStad.c_str(),
                         t.bestemmingStad.empty() ? "?" : t.bestemmingStad.c_str() );
            ImGui::TextDisabled( T( "%s | %.0f km gepland" ), t.lading.empty() ? "-" : t.lading.c_str(), t.geplandeAfstandKm );
            ImGui::EndGroup();
            ImGui::EndChild();
            ImGui::PopStyleColor();

            ImGui::Spacing();

            // Drie statistiek-kaartjes naast elkaar
            float breedte = ( ImGui::GetContentRegionAvail().x - 16 ) / 3.0f;
            // Limiet klein onder de snelheid, in dezelfde kaart -- je
            // vergelijkt die twee toch met elkaar. Een los vakje ernaast zou
            // alleen maar ruimte kosten.
            //
            // Zelfde meekrimpende opzet als op het boordcomputer-tabblad, zodat
            // beide schermen er hetzelfde uitzien: past "km/h - limiet 80" niet,
            // dan wordt het "limiet 80", en anders alleen "km/h". Afkappen
            // halverwege leest niet.
            {
                TruckTracking::VoertuigStatus vs = m_vracht.HuidigeVoertuigStatus();
                std::string onder = "km/h";
                if( vs.snelheidslimietKmh >= 0.0 )
                {
                    char lang[ 40 ], kort[ 24 ];
                    snprintf( lang, sizeof( lang ), "km/h - limiet %.0f", vs.snelheidslimietKmh );
                    snprintf( kort, sizeof( kort ), T( "limiet %.0f" ), vs.snelheidslimietKmh );

                    const float ruimteKaart = breedte - ImGui::GetStyle().WindowPadding.x * 2.0f;
                    if( ImGui::CalcTextSize( lang ).x <= ruimteKaart )       onder = lang;
                    else if( ImGui::CalcTextSize( kort ).x <= ruimteKaart )  onder = kort;
                }
                StatKaart( T( "SNELHEID" ), std::to_string( (int)t.huidigeSnelheidKmh ),
                            breedte, onder.c_str(), m_vracht.RijdtTeHard() );
            }
            // Leeg onderschrift in plaats van geen onderschrift: zo houden deze
            // twee dezelfde hoogte als de snelheidskaart, die wel een regel
            // eronder heeft. Zonder dit staan de drie kaartjes ongelijk.
            ImGui::SameLine();
            StatKaart( T( "BRANDSTOF" ), std::to_string( (int)t.brandstofPercentage ) + "%", breedte, "" );
            ImGui::SameLine();
            StatKaart( T( "SCHADE" ), std::to_string( (int)t.schadeChassisPercentage ) + "%", breedte, "" );

            // Rijtijd blijft op Live zichtbaar: tijdens het rijden staat je
            // muis uit, en van tabblad wisselen kost dus een handeling.
            ImGui::Spacing();
            TekenTachoStrip();

            ImGui::Spacing();

            // Resterende-tijd + brandstofkosten-kaart, in de accentkleur
            BrandstofState bs = m_brandstof.HuidigeState();
            ImGui::PushStyleColor( ImGuiCol_ChildBg,
                TintKaartKleur( ImVec4( m_accentKleur[ 0 ], m_accentKleur[ 1 ], m_accentKleur[ 2 ], 1.0f ), 0.28f ) );
            // EEN keer berekenen per frame. De schatting wordt gladgestreken,
            // en dat verschuift de waarde bij elke aanroep -- twee keer
            // aanroepen zou dus twee keer zo snel bijsturen en kon de twee
            // getallen op het scherm van elkaar laten verschillen.
            const double resterendMin = m_vracht.GeschatteResterendeMinutenEcht();

            ImGui::BeginChild( "eta_kaart", ImVec2( 0, 96 ), true, ImGuiWindowFlags_NoScrollbar );
            float helft = ImGui::GetContentRegionAvail().x / 2.0f;
            ImGui::BeginGroup();
            ImGui::TextColored( ImVec4( m_accentKleur[ 0 ] + 0.15f, m_accentKleur[ 1 ] + 0.1f, m_accentKleur[ 2 ] + 0.1f, 1.0f ),
                                 T( "RESTEREND (IRL)" ) );
            if( m_kopFont ) ImGui::PushFont( m_kopFont );
            ImGui::Text( "%s", FormatteerMinuten( resterendMin ).c_str() );
            if( m_kopFont ) ImGui::PopFont();
            ImGui::EndGroup();
            ImGui::SameLine( helft );
            ImGui::BeginGroup();
            ImGui::TextColored( ImVec4( m_accentKleur[ 0 ] + 0.15f, m_accentKleur[ 1 ] + 0.1f, m_accentKleur[ 2 ] + 0.1f, 1.0f ),
                                 T( "BRANDSTOFKOSTEN" ) );
            if( m_kopFont ) ImGui::PushFont( m_kopFont );
            ImGui::Text( T( "EUR %.2f" ), bs.kostenDezeRitEuro );
            if( m_kopFont ) ImGui::PopFont();
            ImGui::EndGroup();
            ImGui::EndChild();
            ImGui::PopStyleColor();

            ImGui::TextDisabled( T( "Onderweg: %s -- schatting o.b.v. recente snelheid, telt niet door tijdens pauze" ),
                                  FormatteerMinuten( m_vracht.VerstrekenMinutenEcht() ).c_str() );
            {
                const double resterendEcht = resterendMin;
                if( resterendEcht >= 0.0 )
                {
                    TekstGedimd( ( std::string( T( "Aankomst rond " ) ) + KlokTijdOver( resterendEcht ) +
                                    T( " (jouw klok)" ) ).c_str() );

                }
            }

            // --- Onkosten deze rit (ideeenlijst #3, #4, #5) ------------
            // Alleen tonen als er daadwerkelijk iets betaald is, anders
            // staat er een leeg kaartje dat alleen maar ruimte inneemt.
            std::int64_t onkosten = t.tolKosten + t.veerbootKosten + t.treinKosten + t.boeteKosten;
            if( onkosten > 0 )
            {
                ImGui::Spacing();
                KopBalk( T( "ONKOSTEN DEZE RIT" ) );
                // Hoogte uitrekenen uit het aantal regels dat we echt gaan
                // tekenen -- zie de uitleg bij het schade-kaartje.
                const ImGuiStyle &stK = ImGui::GetStyle();
                float regelK = ImGui::GetTextLineHeightWithSpacing();
                int regels = 0;
                if( t.tolKosten > 0 ) regels++;
                if( t.veerbootKosten > 0 ) regels++;
                if( t.treinKosten > 0 ) regels++;
                if( t.boeteKosten > 0 ) regels += 1 + static_cast<int>( t.boetes.size() );
                float onkostenHoogte = stK.WindowPadding.y * 2.0f
                                        + regels * regelK
                                        + stK.ItemSpacing.y * 2.0f + 6.0f   // Spacing + Separator + Spacing
                                        + ( m_kopFont ? m_kopFont->FontSize : ImGui::GetFontSize() ) + stK.ItemSpacing.y
                                        + regelK                            // slotregel
                                        + 4.0f;

                ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
                ImGui::BeginChild( "onkosten_kaart", ImVec2( 0, onkostenHoogte ), true,
                                    ImGuiWindowFlags_NoScrollbar );
                auto kostenRegel = []( const char *naam, std::int64_t bedrag )
                {
                    TekstGedimd( naam );
                    ImGui::SameLine( 130.0f );
                    TekstSFmt( IM_COL32( 255, 255, 255, 255 ), "%lld", (long long)bedrag );
                };
                if( t.tolKosten > 0 )      kostenRegel( T( "Tol" ), t.tolKosten );
                if( t.veerbootKosten > 0 ) kostenRegel( T( "Veerboot" ), t.veerbootKosten );
                if( t.treinKosten > 0 )    kostenRegel( T( "Trein" ), t.treinKosten );
                if( t.boeteKosten > 0 )
                {
                    TekstGedimd( T( "Boetes" ) );
                    ImGui::SameLine( 130.0f );
                    TekstSFmt( IM_COL32( 230, 115, 102, 255 ), "%lld", (long long)t.boeteKosten );
                    for( const Boete &b : t.boetes )
                    {
                        TekstGedimdFmt( "    %s", VertaalOffence( b.reden ).c_str() );
                        ImGui::SameLine( 130.0f );
                        TekstGedimdFmt( "%lld", (long long)b.bedrag );
                    }
                }
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                TekstGedimd( T( "Totaal" ) );
                ImGui::SameLine( 130.0f );
                if( m_kopFont ) ImGui::PushFont( m_kopFont );
                TekstSFmt( IM_COL32( 255, 255, 255, 255 ), "%lld", (long long)onkosten );
                if( m_kopFont ) ImGui::PopFont();
                TekstGedimd( T( "Echte in-game bedragen, gemeld door het spel zelf." ) );
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
        }

        if( m_bus.HeeftActieveRit() )
        {
            if( iets ) ImGui::Spacing(), ImGui::Separator(), ImGui::Spacing();
            iets = true;
            const Trip &t = m_bus.HuidigeRit();

            // Kopregel als smalle strook, net als de tachograafstrip: alles
            // op EEN regel in plaats van een blok van 80 pixels hoog. Scheelt
            // ruimte voor de haltelijst.
            int voltooid = 0;
            for( const StopInfo &s : t.haltes ) if( s.voltooid ) voltooid++;

            {
                const ImGuiStyle &stB = ImGui::GetStyle();
                float hoogteB = stB.WindowPadding.y * 2.0f + ImGui::GetTextLineHeightWithSpacing() + 2.0f;

                ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
                ImGui::BeginChild( "buslijn_kaart", ImVec2( 0, hoogteB ), true, ImGuiWindowFlags_NoScrollbar );
                TekenVoertuigIcoon( true, 16.0f );
                ImGui::SameLine( 0.0f, 6.0f );
                TekstSFmt( IM_COL32( 255, 255, 255, 255 ), T( "BUSLIJN ONDERWEG" ) );
                ImGui::SameLine( 0.0f, 8.0f );
                TekstGedimdFmt( T( "halte %d / %d" ), voltooid, (int)t.haltes.size() );
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }

            // Resterende-tijd-kaart (tot de eerstvolgende halte)
            double resterend = m_bus.GeschatteResterendeMinutenEcht();
            ImGui::PushStyleColor( ImGuiCol_ChildBg,
                TintKaartKleur( ImVec4( m_accentKleur[ 0 ], m_accentKleur[ 1 ], m_accentKleur[ 2 ], 1.0f ), 0.28f ) );
            // Dezelfde drie kaartjes als bij de vrachtrit en op de
            // boordcomputer -- snelheid met limiet, brandstof, schade.
            // Bij de bus staan ze wat compacter, want de haltelijst eronder
            // vraagt de meeste ruimte.
            {
                TruckTracking::VoertuigStatus vs = m_vracht.HuidigeVoertuigStatus();
                float breedte = ( ImGui::GetContentRegionAvail().x - 16 ) / 3.0f;

                std::string onder = "km/h";
                if( vs.snelheidslimietKmh >= 0.0 )
                {
                    char lang[ 40 ], kort[ 24 ];
                    snprintf( lang, sizeof( lang ), "km/h - limiet %.0f", vs.snelheidslimietKmh );
                    snprintf( kort, sizeof( kort ), "limiet %.0f", vs.snelheidslimietKmh );
                    const float ruimte = breedte - ImGui::GetStyle().WindowPadding.x * 2.0f;
                    if( ImGui::CalcTextSize( lang ).x <= ruimte )       onder = lang;
                    else if( ImGui::CalcTextSize( kort ).x <= ruimte )  onder = kort;
                }

                // Compacte vorm: zo laag mogelijk, breedte ongemoeid. Scheelt
                // ruimte voor de haltelijst eronder.
                StatKaart( T( "SNELHEID" ), std::to_string( (int)m_vracht.LiveSnelheidKmh() ),
                            breedte, onder.c_str(), m_vracht.RijdtTeHard(), true );
                ImGui::SameLine();
                StatKaart( T( "SCHADE" ), std::to_string( (int)vs.schadeChassis ) + "%", breedte, "", false, true );
                ImGui::SameLine();

                const bool cruiseAan = vs.cruiseControlKmh > 1.0;
                ImGui::PushStyleColor( ImGuiCol_ChildBg,
                    cruiseAan ? TintKaartKleur( ImVec4( 0.25f, 0.69f, 0.54f, 1.0f ), 0.30f ) : KaartKleur() );
                ImGui::BeginChild( "bus_cruise", ImVec2( breedte, KaartHoogte( true, true ) ), true,
                                    ImGuiWindowFlags_NoScrollbar );
                if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
                {
                    const float ruimte = ImGui::GetContentRegionAvail().x;
                    const char *kopje = T( "CRUISE CONTROL" );
                    if( ImGui::CalcTextSize( kopje ).x > ruimte ) kopje = T( "CRUISE" );
                    if( ImGui::CalcTextSize( kopje ).x > ruimte ) kopje = "CC";
                    TekstGedimd( kopje );
                }
                if( m_kleinFont ) ImGui::PopFont();

                ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing,
                                      ImVec2( ImGui::GetStyle().ItemSpacing.x, 2.0f ) );
                if( cruiseAan )
                {
                    TekstSFmt( IM_COL32( 115, 217, 173, 255 ), "%.0f", vs.cruiseControlKmh );
                    if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
                    TekstGedimd( T( "km/h" ) );
                    if( m_kleinFont ) ImGui::PopFont();
                }
                else
                {
                    TekstS( T( "UIT" ) );
                    if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
                    TekstGedimd( "" );
                    if( m_kleinFont ) ImGui::PopFont();
                }
                ImGui::PopStyleVar();
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }

            // Rijtijd blijft ook bij de bus zichtbaar op Live.
            ImGui::Spacing();
            TekenTachoStrip();
            ImGui::Spacing();

            // Als strook, zelfde formaat als de tachograaf eronder: kopje,
            // tijd en aankomstklok naast elkaar op een regel. Was een blok van
            // 84 pixels hoog met alles onder elkaar.
            {
                const ImGuiStyle &stE = ImGui::GetStyle();
                float hoogteE = stE.WindowPadding.y * 2.0f + ImGui::GetTextLineHeightWithSpacing() + 2.0f;

                ImGui::BeginChild( "bus_eta_kaart", ImVec2( 0, hoogteE ), true, ImGuiWindowFlags_NoScrollbar );

                // Alleen het KOPJE klein; de tijd en de klok in het normale
                // lettertype -- dat zijn de getallen waar je naar kijkt.
                if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
                TekstGedimd( T( "VOLGENDE HALTE" ) );
                if( m_kleinFont ) ImGui::PopFont();

                ImGui::SameLine( 0.0f, 8.0f );
                TekstSFmt( IM_COL32( 255, 255, 255, 255 ), "%s", FormatteerMinuten( resterend ).c_str() );
                if( resterend >= 0.0 )
                {
                    ImGui::SameLine( 0.0f, 8.0f );
                    TekstGedimd( ( "rond " + KlokTijdOver( resterend ) ).c_str() );
                }
                ImGui::EndChild();
            }
            ImGui::PopStyleColor();
            ImGui::Spacing();

            // Halte-tijdlijn met status-iconen (voltooid/huidig/nog te gaan)
            for( std::size_t i = 0; i < t.haltes.size(); ++i )
            {
                const StopInfo &s = t.haltes[ i ];
                bool isHuidig = !s.voltooid && ( i == 0 || t.haltes[ i - 1 ].voltooid );

                ImVec2 iconPos = ImGui::GetCursorScreenPos();
                ImDrawList *hd = ImGui::GetWindowDrawList();
                ImU32 icoonKleur = s.voltooid ? IM_COL32( 63, 176, 138, 255 )
                                  : isHuidig    ? AccentKleurU32( 1.0f )
                                                 : IM_COL32( 90, 90, 95, 255 );
                hd->AddCircleFilled( ImVec2( iconPos.x + 10, iconPos.y + 10 ), 9.0f, icoonKleur );
                if( isHuidig )
                {
                    hd->AddCircle( ImVec2( iconPos.x + 10, iconPos.y + 10 ), 13.0f, icoonKleur, 16, 1.5f );
                }
                // verbindingslijntje naar de volgende halte
                if( i + 1 < t.haltes.size() )
                {
                    hd->AddLine( ImVec2( iconPos.x + 10, iconPos.y + 20 ), ImVec2( iconPos.x + 10, iconPos.y + 38 ),
                                 IM_COL32( 255, 255, 255, 30 ), 2.0f );
                }

                ImGui::SetCursorPosX( ImGui::GetCursorPosX() + 26 );
                ImGui::BeginGroup();
                const char *statusTekst = s.voltooid ? T( "voltooid" ) : ( isHuidig ? T( "eerstvolgende" ) : T( "nog te gaan" ) );
                if( s.voltooid )
                    ImGui::TextDisabled( "%s", s.naam.c_str() );
                else
                    ImGui::Text( "%s", s.naam.c_str() );
                // Kilometers laten staan, en de geschatte aankomsttijd erbij.
                // Die wordt op precies dezelfde manier gerekend als de tijd
                // tot de eerstvolgende halte -- zie GeschatteMinutenTotHalte.
                const double naarHalte = m_bus.GeschatteMinutenTotHalte( i );
                if( naarHalte >= 0.0 )
                {
                    ImGui::TextDisabled( T( "%s -- %.0f km -- rond %s" ),
                                          statusTekst, s.geplandeAfstandKm,
                                          KlokTijdOver( naarHalte ).c_str() );
                }
                else
                {
                    ImGui::TextDisabled( T( "%s -- %.0f km vanaf start" ), statusTekst, s.geplandeAfstandKm );
                }
                ImGui::EndGroup();
                ImGui::Spacing();
            }
            ImGui::Spacing();
            // De SDK geeft de geschatte uitbetaling alleen mee in het
            // START-event van de rit; er is geen getter om 'm later op te
            // halen. Kwam daar 0 binnen -- bijvoorbeeld omdat de plugin
            // halverwege een rit is geladen -- dan valt er niets te
            // verversen. Dan liever eerlijk zijn dan een nul tonen die
            // eruitziet alsof je niets verdient.
            if( t.geschatUitbetaling > 0 )
            {
                ImGui::Text( T( "Geschatte uitbetaling: %lld" ), (long long)t.geschatUitbetaling );
            }
            else
            {
                TekstGedimd( T( "Geschatte uitbetaling: niet doorgegeven bij ritstart" ) );
            }
            ImGui::Text( T( "Onderweg: %s" ), FormatteerMinuten( m_bus.VerstrekenMinutenEcht() ).c_str() );

            // --- Onkosten deze rit -------------------------------------
            // Alleen tonen als er daadwerkelijk iets betaald is, net als bij
            // de vrachtrit. Anders staat er een leeg kaartje ruimte te vullen
            // die de haltelijst beter kan gebruiken.
            {
                const std::int64_t busOnkosten =
                    t.tolKosten + t.veerbootKosten + t.treinKosten + t.boeteKosten;
                if( busOnkosten > 0 )
                {
                    if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }

                    const ImGuiStyle &stO = ImGui::GetStyle();
                    float regelO = ImGui::GetTextLineHeightWithSpacing();
                    int regels = 1; // de totaalregel
                    if( t.tolKosten > 0 )      ++regels;
                    if( t.veerbootKosten > 0 ) ++regels;
                    if( t.treinKosten > 0 )    ++regels;
                    if( t.boeteKosten > 0 )    ++regels;

                    ImGui::Spacing();
                    ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
                    ImGui::BeginChild( "bus_onkosten", ImVec2( 0, stO.WindowPadding.y * 2.0f + regels * regelO + 2.0f ),
                                        true, ImGuiWindowFlags_NoScrollbar );
                    TekstGedimd( T( "ONKOSTEN DEZE RIT" ) );
                    if( t.tolKosten > 0 )      TekstGedimdFmt( T( "Tol        EUR %lld" ), (long long)t.tolKosten );
                    if( t.veerbootKosten > 0 ) TekstGedimdFmt( T( "Veerboot   EUR %lld" ), (long long)t.veerbootKosten );
                    if( t.treinKosten > 0 )    TekstGedimdFmt( T( "Trein      EUR %lld" ), (long long)t.treinKosten );
                    if( t.boeteKosten > 0 )
                    {
                        TekstSFmt( IM_COL32( 240, 140, 130, 255 ), T( "Boetes     EUR %lld" ), (long long)t.boeteKosten );
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleColor();

                    if( m_kleinFont ) { ImGui::PopFont(); m_kleinFontActief = false; }
                }
            }

            // Te-laat-waarschuwing. Alleen tonen als er iets aan de hand is:
            // rijd je op schema, dan hoort hier niets te staan.
            double vertraging = m_bus.GeschatteVertragingMinuten();
            if( vertraging > -1e8 )
            {
                double boete = m_bus.GeschatteBoetePercentage();
                if( boete > 0.0 )
                {
                    ImGui::Spacing();
                    KopBalk( T( "TE LAAT" ) );
                    ImGui::PushStyleColor( ImGuiCol_ChildBg,
                                            TintKaartKleur( ImVec4( 0.85f, 0.25f, 0.20f, 1.0f ), 0.5f ) );
                    const ImGuiStyle &stW = ImGui::GetStyle();
                    float regelW = ImGui::GetTextLineHeightWithSpacing();
                    ImGui::BeginChild( "telaat_kaart", ImVec2( 0, stW.WindowPadding.y * 2 + regelW * 3 + 4 ),
                                        true, ImGuiWindowFlags_NoScrollbar );
                    TekstSFmt( IM_COL32( 255, 160, 150, 255 ),
                                T( "%.0f min over de eindtijd" ), vertraging );
                    TekstSFmt( IM_COL32( 255, 255, 255, 255 ), T( "-%.0f%% van de uitbetaling" ), boete );
                    TekstGedimd( T( "Eerste uur vertraging is gratis; daarna 0,333% per minuut." ) );
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }
                else if( vertraging > 0.0 )
                {
                    // Wel achter op schema, maar nog binnen het gratis uur.
                    TekstGedimd( ( std::string( T( "Achter op schema, nog " ) ) +
                                    std::to_string( (int)( 60.0 - vertraging ) ) +
                                    T( " min speling voor de boete ingaat." ) ).c_str() );
                }
            }
            else
            {
                TekstGedimd( T( "Vertraging nog niet te bepalen -- wacht op navigatiedata en een stukje rijden." ) );
            }
        }

        if( !iets )
        {
            ImGui::TextDisabled( T( "Geen actieve rit. Start een vrachtjob of buslijn." ) );
        }

    }

    void Overlay::TekenSpelerContextKnop( const SpelerRecord &speler, const std::string &uniekeId )
    {
        std::string knopLabel = "...##ctx_" + uniekeId;
        std::string popupNaam = "spelermenu_" + uniekeId;

        if( ImGui::SmallButton( knopLabel.c_str() ) )
        {
            ImGui::OpenPopup( popupNaam.c_str() );
        }
        if( ImGui::BeginPopup( popupNaam.c_str() ) )
        {
            ImGui::TextDisabled( "%s", speler.gebruikersnaam.c_str() );
            ImGui::Separator();

            if( speler.steamId != 0 )
            {
                if( ImGui::MenuItem( T( "Open Steam-profiel" ) ) )
                {
                    OpenUrlInBrowser( "https://steamcommunity.com/profiles/" + std::to_string( speler.steamId ) );
                }
            }
            else
            {
                ImGui::TextDisabled( T( "Steam-profiel onbekend" ) );
            }

            if( speler.accountId != 0 )
            {
                if( ImGui::MenuItem( T( "Open TruckersMP-profiel" ) ) )
                {
                    OpenUrlInBrowser( "https://truckersmp.com/user/" + std::to_string( speler.accountId ) );
                }
                if( ImGui::MenuItem( T( "Kopieer TruckersMP-ID" ) ) )
                {
                    KopieerNaarKlembord( std::to_string( speler.accountId ) );
                }
                ImGui::Separator();
                if( ImGui::MenuItem( T( "Rapporteer speler..." ) ) )
                {
                    m_reportPopupSpeler = speler;
                    m_reportPopupSpelerId = uniekeId;
                    m_reportRedenenAangevinkt.assign( 11, false );
                    m_reportOmschrijving[ 0 ] = '\0';
                    m_reportBewijsLink[ 0 ] = '\0';
                    ImGui::OpenPopup( "report_scherm" );
                }
            }
            else
            {
                ImGui::TextDisabled( T( "TruckersMP-ID onbekend" ) );
            }

            ImGui::EndPopup();
        }

        // --- Report-scherm: zo dicht mogelijk nagebouwd op TMP's eigen
        // in-game "Report User"-scherm (echte paragraafnummers, echte
        // ID-gegevens). We kunnen het report zelf niet versturen -- dat
        // kan alleen via TMP's eigen in-game scherm of hun website, de SDK
        // geeft geen enkele Report-functie door aan plugins. Wat wij doen:
        // alles keurig voorbereiden en op je klembord zetten, en de
        // website-reportpagina openen, zodat jij het alleen nog hoeft te
        // plakken en te verzenden.
        ImGui::SetNextWindowSize( ImVec2( 440, 0 ), ImGuiCond_Appearing );
        if( ImGui::BeginPopup( "report_scherm" ) )
        {
            const SpelerRecord &rs = m_reportPopupSpeler;

            ImGui::TextColored( ImVec4( 0.9f, 0.35f, 0.3f, 1.0f ), "REPORT USER" );
            ImGui::Separator();
            ImGui::Text( T( "Nickname: %s" ), rs.gebruikersnaam.c_str() );
            ImGui::Text( "ID: %d", rs.spelerId );
            ImGui::Text( T( "SteamID64: %s" ), rs.steamId != 0 ? std::to_string( rs.steamId ).c_str() : T( "onbekend" ) );
            ImGui::Text( T( "TruckersMP ID: %s" ), rs.accountId != 0 ? std::to_string( rs.accountId ).c_str() : T( "onbekend" ) );
            ImGui::Spacing();
            ImGui::Separator();

            // Exacte TMP-regelcategorieen zoals in hun eigen in-game scherm.
            static const char *redenLabels[ 11 ] = {
                "Other",
                "\xC2\xA7""1.3 - Spamming or Abuse",
                "\xC2\xA7""1.5 - Inappropriate use of language",
                "\xC2\xA7""1.6 - Impersonation of any kind",
                "\xC2\xA7""1.7 - Inappropriate comment",
                "\xC2\xA7""2.1 - Hacking/Bug/Feature Abusing",
                "\xC2\xA7""2.2 - Collisions",
                "\xC2\xA7""2.3 - Blocking",
                "\xC2\xA7""2.4 - Incorrect Way/Inappropriate Overtaking",
                "\xC2\xA7""2.5 - Reckless Driving",
                "\xC2\xA7""3 - Save Editing",
            };
            if( (int)m_reportRedenenAangevinkt.size() != 11 ) m_reportRedenenAangevinkt.assign( 11, false );
            for( int i = 0; i < 11; ++i )
            {
                bool waarde = m_reportRedenenAangevinkt[ i ] != 0;
                if( ImGui::Checkbox( redenLabels[ i ], &waarde ) )
                {
                    m_reportRedenenAangevinkt[ i ] = waarde;
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text( T( "Omschrijving" ) );
            ImGui::InputTextMultiline( "##omschrijving", m_reportOmschrijving, sizeof( m_reportOmschrijving ),
                                        ImVec2( -1, 70 ) );
            ImGui::Spacing();
            ImGui::Text( T( "Bewijs-link (video, verplicht bij de meeste categorieen)" ) );
            ImGui::SetNextItemWidth( -1 );
            ImGui::InputText( "##bewijslink", m_reportBewijsLink, sizeof( m_reportBewijsLink ) );

            ImGui::Spacing();
            ImGui::TextColored( ImVec4( 0.9f, 0.35f, 0.3f, 1.0f ),
                "%s", T( "De plugin kan dit NIET automatisch versturen (de SDK geeft" ) );
            ImGui::TextColored( ImVec4( 0.9f, 0.35f, 0.3f, 1.0f ),
                "%s", T( "geen Report-functie door). Onderstaande knop bereidt alles" ) );
            ImGui::TextColored( ImVec4( 0.9f, 0.35f, 0.3f, 1.0f ),
                "%s", T( "voor op je klembord en opent de website -- verzenden doe jij zelf." ) );
            ImGui::Spacing();

            if( ImGui::Button( T( "Voorbereiden + report-pagina openen" ), ImVec2( -1, 32 ) ) )
            {
                // Echte, huidige tijd erbij -- handig omdat TMP bij video-
                // bewijs vaak om tijdstippen vraagt.
                auto nu = std::chrono::system_clock::now();
                std::time_t nuT = std::chrono::system_clock::to_time_t( nu );
                std::tm tmBuf{};
#if defined( _WIN32 )
                localtime_s( &tmBuf, &nuT );
#else
                localtime_r( &nuT, &tmBuf );
#endif
                char tijdBuf[ 32 ];
                std::strftime( tijdBuf, sizeof( tijdBuf ), "%d-%m-%Y %H:%M", &tmBuf );

                std::string redenTekst;
                bool eersteReden = true;
                for( int i = 0; i < 11; ++i )
                {
                    if( m_reportRedenenAangevinkt[ i ] )
                    {
                        if( !eersteReden ) redenTekst += ", ";
                        redenTekst += redenLabels[ i ];
                        eersteReden = false;
                    }
                }
                if( eersteReden ) redenTekst = T( "(nog geen reden aangevinkt)" );

                std::string tekst =
                    "=== TruckersMP report -- voorbereid door CabNavi ===\n"
                    "Tijdstip: " + std::string( tijdBuf ) + "\n"
                    "Nickname: " + rs.gebruikersnaam + "\n"
                    "ID: " + std::to_string( rs.spelerId ) + "\n"
                    "SteamID64: " + ( rs.steamId != 0 ? std::to_string( rs.steamId ) : "onbekend" ) + "\n"
                    "TruckersMP ID: " + ( rs.accountId != 0 ? std::to_string( rs.accountId ) : "onbekend" ) + "\n" +
                    T( "Reden(en): " ) + redenTekst + "\n"
                    "Bewijs-link: " + ( m_reportBewijsLink[ 0 ] != '\0' ? m_reportBewijsLink : T( "(nog niet ingevuld)" ) ) + "\n"
                    "Omschrijving:\n" + m_reportOmschrijving;

                KopieerNaarKlembord( tekst );
                OpenUrlInBrowser( "https://truckersmp.com/reports/" );
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void Overlay::TekenTachoStrip()
    {
        if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }

        const ImGuiStyle &st = ImGui::GetStyle();
        float hoogte = st.WindowPadding.y * 2.0f + ImGui::GetTextLineHeightWithSpacing() + 2.0f;

        ImGui::PushStyleColor( ImGuiCol_ChildBg, TintKaartKleur( ImVec4( 0.9f, 0.65f, 0.2f, 1.0f ), 0.22f ) );
        ImGui::BeginChild( "live_tacho_strip", ImVec2( 0, hoogte ), true, ImGuiWindowFlags_NoScrollbar );

        TekstGedimd( T( "TACHOGRAAF" ) );
        ImGui::SameLine( 0.0f, 8.0f );

        // Zelfde bronkeuze als op het boordcomputer-tabblad: liefst de klok
        // van het spel, anders onze eigen teller.
        const double totRust = m_vracht.MinutenTotRustSpel();
        const double periode = m_vracht.VolledigeRustperiodeMinuten();
        const bool spelData = totRust >= 0.0 && periode > 1.0;
        const bool inRust = m_vracht.TachograafInRust();
        const double rijtijd = m_vracht.TachograafRijtijdMinuten();

        // Wat speelt het eerst: de verplichte pauze of de lange rust? Op Live
        // is maar plek voor EEN balk, dus tonen we degene die het dichtst bij
        // is. Meestal is dat de pauze (4u30) en niet de rust (10 uur).
        const double totPauze = m_vracht.MinutenTotVerplichtePauze();

        // Er was hier een "pauze bezig"-modus die aansloeg zodra je stilstond
        // en dan naar negen uur aftelde. Dat is een restant van toen stilstaan
        // nog als rust telde. Nu telt alleen een echte rustactie, dus die tak
        // is weg -- hij liet bij een tankbeurt ineens een heel ander getal
        // zien dan de P-teller in het spel.

        // Rust is alleen "urgenter" als het spel die klok geeft en hij lager staat.
        const bool toonRust = spelData && totRust >= 0.0 && totRust < totPauze;

        float fractie;
        bool bijnaOp;
        if( toonRust )
        {
            fractie = static_cast<float>( std::min( 1.0, std::max( 0.0, 1.0 - totRust / periode ) ) );
            bijnaOp = totRust <= 45.0;
        }
        else
        {
            fractie = static_cast<float>( std::min( 1.0, std::max( 0.0,
                        1.0 - totPauze / m_vracht.RijPeriodeSpelMinuten() ) ) );
            bijnaOp = totPauze <= TruckTracking::WAARSCHUW_SPELMINUTEN;
        }
        if( inRust ) fractie = 0.0f;

        const ImU32 balkKleur = inRust   ? IM_COL32( 102, 204, 128, 220 )
                                : bijnaOp ? IM_COL32( 212, 71, 61, 190 )
                                           : IM_COL32( 217, 164, 66, 220 );

        // Tekst rechts eerst opmeten, zodat de balk de rest krijgt.
        // Tekst rechts: waar de balk over gaat.
        std::string tekst;
        if( inRust )
        {
            tekst = T( "rust" );
        }
        else if( toonRust )
        {
            tekst = FormatteerMinuten( totRust );
        }
        else
        {
            tekst = totPauze <= 0.0 ? std::string( "P nu" )
                                     : "P " + FormatteerMinuten( totPauze );
        }
        const float tekstBreedte = ImGui::CalcTextSize( tekst.c_str() ).x + 8.0f;

        const float balkH = std::max( 5.0f, ImGui::GetFontSize() * 0.44f );
        const float balkBreedte = std::max( 30.0f, ImGui::GetContentRegionAvail().x - tekstBreedte );

        ImVec2 p = ImGui::GetCursorScreenPos();
        p.y += ( ImGui::GetTextLineHeight() - balkH ) * 0.5f;
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled( p, ImVec2( p.x + balkBreedte, p.y + balkH ), IM_COL32( 0, 0, 0, 110 ), balkH * 0.5f );
        if( fractie > 0.01f )
        {
            dl->AddRectFilled( p, ImVec2( p.x + balkBreedte * fractie, p.y + balkH ), balkKleur, balkH * 0.5f );
        }
        ImGui::Dummy( ImVec2( balkBreedte, balkH ) );
        ImGui::SameLine( 0.0f, 8.0f );
        TekstSFmt( inRust ? IM_COL32( 115, 217, 173, 255 ) : IM_COL32( 255, 255, 255, 255 ),
                    "%s", tekst.c_str() );

        ImGui::EndChild();
        ImGui::PopStyleColor();

        if( m_kleinFont ) { ImGui::PopFont(); m_kleinFontActief = false; }
    }

    void Overlay::TekenBoordcomputerTab()
    {
        // Dit tabblad is bewust ONAFHANKELIJK van een actieve rit: bereik,
        // kilometerstand, schade en tachograaf zijn net zo goed nuttig als je
        // leeg rondrijdt. Daarom staat het niet meer op Live.
        //
        // Wat hieronder staat is LETTERLIJK het blok dat eerst onderaan de
        // Live-tab stond -- alleen verplaatst, geen letter aan veranderd.
        // --- Boordcomputer (ideeenlijst #1,2,6,7,8,9,10) ---------------
        // Zelfde vormtaal als de rest van de HUD: kaartjes met een klein
        // label erboven en de waarde groot eronder, schade als gekleurde
        // balkjes. Los van of er een job actief is -- bereik, kilometer-
        // stand en schade zijn ook relevant als je leeg rondrijdt.
        {
            TruckTracking::VoertuigStatus vs = m_vracht.HuidigeVoertuigStatus();

            auto getal = []( double waarde, const char *eenheid, int decimalen = 0 ) -> std::string
            {
                if( waarde < 0.0 ) return "--";
                char buf[ 64 ];
                snprintf( buf, sizeof( buf ), "%.*f%s", decimalen, waarde, eenheid );
                return buf;
            };

            ImGui::Spacing();
            KopBalk( T( "BOORDCOMPUTER" ) );
            TekenConvooiHerinnering();

            // Snelheid en brandstof in hetzelfde formaat als op Live, zodat
            // dit tabblad ook bruikbaar is als je leeg rondrijdt en er geen
            // rit loopt. Het SCHADE-kaartje laten we hier weg: de uitgebreide
            // schadebalken staan onderaan dit scherm al.
            {
                // Rij 1: snelheid / brandstof / cruise control.
                // Drie even brede kaartjes, zelfde maat als de rij eronder.
                // Cruise control stond eerst alleen op een halve regel, wat
                // een scheef gat opleverde -- zo vult de rij netjes.
                float derdeB = ( ImGui::GetContentRegionAvail().x - 16 ) / 3.0f;

                // Onderschrift met de limiet erbij, maar alleen zolang het past.
                // In een smal venster valt eerst "km/h" weg en daarna de hele
                // limiet -- afkappen halverwege leest niet.
                std::string onder = "km/h";
                if( vs.snelheidslimietKmh >= 0.0 )
                {
                    char lang[ 40 ], kort[ 24 ];
                    snprintf( lang, sizeof( lang ), "km/h - limiet %.0f", vs.snelheidslimietKmh );
                    snprintf( kort, sizeof( kort ), "limiet %.0f", vs.snelheidslimietKmh );

                    const float ruimteKaart = derdeB - ImGui::GetStyle().WindowPadding.x * 2.0f;
                    if( ImGui::CalcTextSize( lang ).x <= ruimteKaart )       onder = lang;
                    else if( ImGui::CalcTextSize( kort ).x <= ruimteKaart )  onder = kort;
                    // anders blijft het gewoon "km/h"
                }
                StatKaart( T( "SNELHEID" ), std::to_string( (int)m_vracht.LiveSnelheidKmh() ),
                            derdeB, onder.c_str(), m_vracht.RijdtTeHard() );
                ImGui::SameLine();
                // Leeg onderschrift in plaats van geen onderschrift: zo houdt
                // dit kaartje dezelfde hoogte als de twee ernaast, die wel een
                // regel eronder hebben.
                StatKaart( T( "BRANDSTOF" ), getal( m_vracht.HuidigeRit().brandstofPercentage, "%" ),
                            derdeB, "" );
                ImGui::SameLine();

                // Cruise aan: waarde in het groen, net als voorheen. Uit: "UIT".
                const bool cruiseAan = vs.cruiseControlKmh > 1.0;
                ImGui::PushStyleColor( ImGuiCol_ChildBg,
                    cruiseAan ? TintKaartKleur( ImVec4( 0.25f, 0.69f, 0.54f, 1.0f ), 0.30f ) : KaartKleur() );
                ImGui::BeginChild( "bc_cruise", ImVec2( derdeB, KaartHoogte( true ) ), true,
                                    ImGuiWindowFlags_NoScrollbar );

                // "CRUISE CONTROL" past niet in een smal kaartje. In plaats van
                // het te laten afkappen korten we het zelf in -- dat leest beter
                // dan "CRUISE CONTRO". De ruimte die de kaart werkelijk heeft
                // bepaalt de keuze, dus dit klopt bij elke vensterbreedte.
                const float ruimte = ImGui::GetContentRegionAvail().x;
                const char *kopje = T( "CRUISE CONTROL" );
                if( ImGui::CalcTextSize( kopje ).x > ruimte )      kopje = T( "CRUISE" );
                if( ImGui::CalcTextSize( kopje ).x > ruimte )      kopje = "CC";
                TekstGedimd( kopje );
                if( m_kopFont ) ImGui::PushFont( m_kopFont );
                if( cruiseAan )
                {
                    TekstSFmt( IM_COL32( 115, 217, 173, 255 ), "%.0f", vs.cruiseControlKmh );
                    if( m_kopFont ) ImGui::PopFont();
                    TekstGedimd( T( "km/h" ) );
                }
                else
                {
                    TekstGedimd( T( "UIT" ) );
                    if( m_kopFont ) ImGui::PopFont();
                    TekstGedimd( "" );
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }

            // Rij 2: bereik / verbruik / kilometerstand -- zelfde maat als rij 1.
            float breedte = ( ImGui::GetContentRegionAvail().x - 16 ) / 3.0f;
            // 0 betekent hier "het spel heeft nog niets zinnigs gestuurd",
            // niet echt nul: een bereik van 0 km of een verbruik van 0,0
            // l/100km bestaat niet terwijl je rijdt. Daarom in beide
            // gevallen "--" in plaats van een cijfer dat betrouwbaar oogt
            // maar het niet is.
            StatKaart( T( "BEREIK" ),
                        vs.bereikKm > 0.0 ? MetPunten( vs.bereikKm ) : "--", breedte, T( "km te gaan" ) );
            ImGui::SameLine();
            // VERBRUIK: bij lage snelheid l/uur (daar zegt een verbruik per
            // afstand niets), daarboven km/l -- dezelfde eenheid als het
            // dashboard in de truck, zodat je 1-op-1 kunt vergelijken.
            // Intern rekenen we in l/100km omdat dat recht evenredig is met
            // de brandstofstroom; hier draaien we het om. StatKaart zelf
            // (opmaak/grootte/lettertype) blijft ongemoeid.
            auto naarKmPerLiter = []( double literPer100Km ) -> double
            {
                if( literPer100Km <= 0.0 ) return -1.0;
                double kmpl = 100.0 / literPer100Km;
                if( kmpl > 99.9 ) kmpl = 99.9; // past anders niet in het vakje
                return kmpl;
            };

            std::string verbruikWaarde = "--";
            std::string verbruikOnder;
            if( vs.staatStil && vs.verbruikLiterPerUur >= 0.0 )
            {
                verbruikWaarde = getal( vs.verbruikLiterPerUur, "", 1 );
                verbruikOnder = vs.echtStil ? T( "l/uur - stationair" ) : T( "l/uur" );
                // Ladder: past het lange onderschrift niet in een smal
                // venster, dan een kortere vorm i.p.v. laten afkappen.
                const float ruimte = breedte - ImGui::GetStyle().WindowPadding.x * 2.0f;
                if( ImGui::CalcTextSize( verbruikOnder.c_str() ).x > ruimte ) verbruikOnder = T( "l/uur stat." );
                if( ImGui::CalcTextSize( verbruikOnder.c_str() ).x > ruimte ) verbruikOnder = T( "l/uur" );
            }
            else
            {
                const double nuKmpl = naarKmPerLiter( vs.verbruikNuLiterPer100Km );
                const double gemKmpl = naarKmPerLiter( vs.verbruikGemiddeldLiterPer100Km );
                if( nuKmpl > 0.0 )
                {
                    verbruikWaarde = getal( nuKmpl, "", 1 );
                }
                if( gemKmpl > 0.0 )
                {
                    const std::string gem = getal( gemKmpl, "", 1 );
                    verbruikOnder = "km/l - gem " + gem;
                    const float ruimte = breedte - ImGui::GetStyle().WindowPadding.x * 2.0f;
                    if( ImGui::CalcTextSize( verbruikOnder.c_str() ).x > ruimte ) verbruikOnder = "gem " + gem;
                    if( ImGui::CalcTextSize( verbruikOnder.c_str() ).x > ruimte ) verbruikOnder = "km/l";
                }
                else
                {
                    verbruikOnder = "km/l";
                }
            }
            StatKaart( T( "VERBRUIK" ), verbruikWaarde, breedte, verbruikOnder.c_str() );
            ImGui::SameLine();
            StatKaart( T( "KM-STAND" ),
                        vs.kilometerstandKm >= 0.0 ? MetPunten( vs.kilometerstandKm ) : "--",
                        breedte, T( "km totaal" ) );

            // --- Tankbeurten deze rit ---------------------------------
            // Het spel meldt niet DAT je getankt hebt; we herkennen het aan
            // een sprong omhoog in het brandstofniveau. De kosten zijn met
            // JOUW ingestelde literprijs, want de pompprijs geeft de SDK
            // niet door -- vandaar die schuif in de instellingen.
            {
                const auto tankbeurten = m_brandstof.TankbeurtenDezeRit();
                if( !tankbeurten.empty() )
                {
                    if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }

                    const ImGuiStyle &stT = ImGui::GetStyle();
                    const float regelT = ImGui::GetTextLineHeight() + 3.0f;
                    const int toon = (int)std::min<std::size_t>( tankbeurten.size(), 3 );

                    ImGui::Spacing();
                    ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
                    ImGui::BeginChild( "bc_tankbeurten",
                                        ImVec2( 0, stT.WindowPadding.y * 2.0f + ( toon + 1 ) * regelT + 4.0f ),
                                        true, ImGuiWindowFlags_NoScrollbar );
                    TekstGedimd( T( "GETANKT DEZE RIT" ) );

                    int n = 0;
                    for( const auto &t : tankbeurten )
                    {
                        if( ++n > 3 ) break;
                        const float breed = ImGui::GetContentRegionAvail().x;

                        char links[ 48 ];
                        snprintf( links, sizeof( links ), "+%.0f l", t.liters );
                        TekstS( links );

                        std::string rechtsTekst = std::string( T( "EUR " ) ) + getal( t.kostenEuro, "", 2 );
                        if( t.kmStand > 0.0 ) rechtsTekst += T( "   bij " ) + MetPunten( t.kmStand ) + T( " km" );
                        const float rb = ImGui::CalcTextSize( rechtsTekst.c_str() ).x;
                        ImGui::SameLine( 0.0f, 0.0f );
                        ImGui::SetCursorPosX( ImGui::GetCursorPosX() +
                                               std::max( 8.0f, breed - rb - ImGui::GetCursorPosX() + stT.WindowPadding.x ) );
                        TekstGedimd( rechtsTekst.c_str() );
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleColor();

                    if( m_kleinFont ) { ImGui::PopFont(); m_kleinFontActief = false; }
                }
            }

            ImGui::Spacing();

            // Rij 3: smalle tachograaf links (breedte van BEREIK hierboven),
            // daarnaast EEN vak met schade links en aanhanger rechts.
            //
            // Deze hele rij gebruikt het KLEINE lettertype. Op 19pt werd de
            // tekst hier afgekapt zodra het venster smaller stond ("7" in
            // plaats van "7%") en bleef er nauwelijks breedte over voor de
            // balkjes. Kleiner zetten lost beide op: de labels passen weer
            // en de balken krijgen de vrijgekomen ruimte.
            if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }

            const ImGuiStyle &st = ImGui::GetStyle();
            float regel = ImGui::GetTextLineHeight() + 2.0f; // 2.0 = de krappe ItemSpacing hieronder
            float kopRegel = ImGui::GetTextLineHeightWithSpacing();
            float totaalVoorRij = ImGui::GetContentRegionAvail().x;

            // Vijf schadebalken bepalen de hoogte. Staan schade en aanhanger
            // onder elkaar (smal venster), dan komen er een kopregel en twee
            // balken bij; dat schatten we hier alvast in.
            float schadeKaartBreedte = ( totaalVoorRij - 16.0f ) * 2.0f / 3.0f;
            float proefKolom = ( schadeKaartBreedte - 24.0f ) / 2.0f;
            bool verwachtNaastElkaar = proefKolom >= 150.0f;
            int balkRegels = verwachtNaastElkaar ? 5 : 8; // 5 + kop + trailer + lading
            // De tachograafkaart heeft nu twee balken (rust en verplichte
            // pauze) plus drie tekstregels. Zonder deze vergelijking wint de
            // schadekaart altijd en werd de pauzeregel eronder afgekapt.
            float schadeNodig = kopRegel + balkRegels * regel;
            float tachoNodig = kopRegel + ( regel + 3.0f ) + 3 * regel; // 1 balk i.p.v. 2
            float rijHoogte = st.WindowPadding.y * 2.0f + std::max( schadeNodig, tachoNodig ) + 4.0f;

            // Hoe breed is er eigenlijk plek? Bij een smal venster past de
            // tachograaf NIET meer naast schade+aanhanger: dan houdt elke
            // kolom zo'n 100px over en wordt alles afgekapt. In dat geval
            // krijgt de tachograaf een eigen regel over de volle breedte, en
            // staat schade+aanhanger daaronder. Zo blijft het leesbaar hoe
            // smal je het venster ook trekt.
            float totaal = totaalVoorRij;

            // Tachograaf staat ALTIJD links, even breed als een kaart uit de
            // bovenste rij. Bij een smal venster geeft de schadekaart zelf
            // ruimte terug door zijn twee kolommen onder elkaar te zetten
            // (zie hieronder) -- dat leest beter dan de tachograaf verplaatsen.
            const bool tachoErnaast = true;
            float derde = ( totaal - 16.0f ) / 3.0f;
            float tachoHoogte = rijHoogte;

            // --- Tachograaf ---
            ImGui::PushStyleColor( ImGuiCol_ChildBg, TintKaartKleur( ImVec4( 0.9f, 0.65f, 0.2f, 1.0f ), 0.22f ) );
            ImGui::BeginChild( "bc_tacho", ImVec2( derde, tachoHoogte ), true, ImGuiWindowFlags_NoScrollbar );
            TekstGedimd( T( "TACHOGRAAF" ) );

            // EEN balk, over de volle kaartbreedte: de verplichte pauze.
            //
            // Hier stond er nog een tweede boven, gevoed door
            // "game.next.rest.stop". Dat kanaal bestaat sinds 1.60 niet meer
            // (gemeten: alle registratiepogingen worden geweigerd), dus die
            // balk stond er altijd leeg bij.
            {
                const bool inRust = m_vracht.TachograafInRust();

                float balkH = std::max( 5.0f, ImGui::GetFontSize() * 0.44f );
                float breed = ImGui::GetContentRegionAvail().x;
                ImDrawList *dl = ImGui::GetWindowDrawList();

                // De verplichte pauze (P-icoon in het spel). ETS2 1.60 splitste
                // vermoeidheid in twee dingen -- de lange rust en deze pauze --
                // maar de telemetrie geeft geen van beide door, dus dit is onze
                // eigen teller, gelijklopend met de serverklok.
                {
                    const double totPauze = m_vracht.MinutenTotVerplichtePauze();
                    // Altijd de P-teller, ook bij stilstand -- die tikt in het
                    // spel immers gewoon door. Hier stond een "pauze bezig"-tak
                    // die bij stilstand naar negen uur aftelde; dat gaf een
                    // heel ander getal dan je Route Advisor.
                    const float pf = static_cast<float>( std::min( 1.0, std::max( 0.0,
                            1.0 - totPauze / m_vracht.RijPeriodeSpelMinuten() ) ) );
                    // Amber vanaf twee uur van tevoren, net als het spel.
                    const ImU32 pk = totPauze <= 0.0 ? IM_COL32( 212, 71, 61, 190 )
                                     : totPauze <= TruckTracking::WAARSCHUW_SPELMINUTEN
                                           ? IM_COL32( 217, 164, 66, 220 )
                                           : IM_COL32( 130, 170, 210, 200 );

                    ImVec2 pp = ImGui::GetCursorScreenPos();
                    dl->AddRectFilled( pp, ImVec2( pp.x + breed, pp.y + balkH ), IM_COL32( 0, 0, 0, 110 ), balkH * 0.5f );
                    if( pf > 0.01f )
                    {
                        dl->AddRectFilled( pp, ImVec2( pp.x + breed * pf, pp.y + balkH ), pk, balkH * 0.5f );
                    }
                    ImGui::Dummy( ImVec2( breed, balkH + 3.0f ) );

                    if( totPauze <= 0.0 )
                    {
                        TekstSFmt( IM_COL32( 240, 140, 130, 255 ), T( "pauze verplicht (9 uur rust)" ) );
                    }
                    else
                    {
                        TekstGedimd( ( std::string( "P " ) + FormatteerMinuten( totPauze ) + T( " tot pauze" ) ).c_str() );
                    }

                    // --- Eigen tachograaf (stand 2 en 3) -------------------
                    // Twee losse regels: pauze en dagrijtijd. Die kun je
                    // allebei overtreden, dus allebei zichtbaar. In stand 1
                    // geeft MinutenTotPauzeEigen() -1 en blijft dit weg.
                    const double totPauzeEigen = m_vracht.MinutenTotPauzeEigen();
                    if( totPauzeEigen > -0.5 )
                    {
                        const double dagOver = m_vracht.MinutenDagrijtijdOver();
                        const auto inst = m_vracht.HuidigeTachoInstelling();

                        auto eigenBalk = [ & ]( const char *label, double over, double totaal )
                        {
                            const float f = static_cast<float>(
                                std::min( 1.0, std::max( 0.0, 1.0 - over / std::max( 1.0, totaal ) ) ) );
                            const ImU32 k = over <= 0.0    ? IM_COL32( 212, 71, 61, 190 )
                                            : over <= 30.0 ? IM_COL32( 217, 164, 66, 220 )
                                                            : IM_COL32( 130, 170, 210, 200 );

                            ImVec2 bp = ImGui::GetCursorScreenPos();
                            dl->AddRectFilled( bp, ImVec2( bp.x + breed, bp.y + balkH ),
                                                IM_COL32( 0, 0, 0, 110 ), balkH * 0.5f );
                            if( f > 0.01f )
                            {
                                dl->AddRectFilled( bp, ImVec2( bp.x + breed * f, bp.y + balkH ), k, balkH * 0.5f );
                            }
                            ImGui::Dummy( ImVec2( breed, balkH + 3.0f ) );

                            if( over <= 0.0 )
                            {
                                TekstSFmt( IM_COL32( 240, 140, 130, 255 ), "%s: nu", label );
                            }
                            else
                            {
                                TekstGedimd( ( std::string( label ) + ": " +
                                                FormatteerMinuten( over ) ).c_str() );
                            }
                        };

                        eigenBalk( T( "pauze" ), totPauzeEigen, inst.maxAaneengeslotenRijden );
                        if( dagOver > -0.5 ) eigenBalk( T( "dag" ), dagOver, inst.maxDagRijden );
                    }
                }

                if( inRust )
                {
                    TekstSFmt( IM_COL32( 115, 217, 173, 255 ), T( "Aan het rusten" ) );
                }
                else
                {
                    // Hoe lang je al onderweg bent sinds je laatste rust, in
                    // het gewone lettertype -- dat is het cijfer waar je op
                    // leest.
                    if( m_kleinFont ) ImGui::PopFont();
                    TekstSFmt( IM_COL32( 255, 255, 255, 255 ), "%s",
                                FormatteerMinuten( m_vracht.TachograafRijtijdMinuten() ).c_str() );
                    if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
                    TekstGedimd( T( "gereden" ) );
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();

            if( tachoErnaast ) ImGui::SameLine();

            // --- Een vak met schade LINKS en aanhanger RECHTS ---
            ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
            ImGui::BeginChild( "bc_schade", ImVec2( 0, rijHoogte ), true, ImGuiWindowFlags_NoScrollbar );
            {
                float binnen = ImGui::GetContentRegionAvail().x;
                float kolom = ( binnen - 8.0f ) / 2.0f;

                // Past er nog een leesbare balk in zo'n kolom? Zo niet, dan
                // zetten we schade en aanhanger ONDER elkaar in plaats van
                // naast elkaar. Dat is de plek waar we ruimte teruggeven bij
                // een smal venster -- de tachograaf blijft gewoon links staan.
                float minKolom = ImGui::CalcTextSize( "Chassis" ).x + 6.0f
                                  + ImGui::CalcTextSize( "100%" ).x + 5.0f + 40.0f;
                bool naastElkaar = kolom >= minKolom;
                if( !naastElkaar ) kolom = binnen;

                // Label opgemeten in plaats van geschat: geen lucht tussen
                // woord en balk, en het schaalt mee met het lettertype.
                float lbl = 0.0f;
                for( const char *naam : { T( "Chassis" ), T( "Cabine" ), T( "Wielen" ), T( "Trailer" ), T( "Gewicht" ) } )
                {
                    lbl = std::max( lbl, ImGui::CalcTextSize( naam ).x );
                }
                lbl += 6.0f;

                ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing,
                                      ImVec2( ImGui::GetStyle().ItemSpacing.x, 2.0f ) );

                ImGui::BeginChild( "kol_schade", ImVec2( kolom, naastElkaar ? 0.0f : kopRegel + 5 * regel ),
                                    false, ImGuiWindowFlags_NoScrollbar );
                TekstGedimd( T( "SCHADE" ) );
                SchadeBalk( T( "Chassis" ), vs.schadeChassis, nullptr, lbl );
                SchadeBalk( T( "Motor" ), vs.schadeMotor, nullptr, lbl );
                SchadeBalk( T( "Bak" ), vs.schadeBak, nullptr, lbl );
                SchadeBalk( T( "Cabine" ), vs.schadeCabine, nullptr, lbl );
                SchadeBalk( T( "Wielen" ), vs.schadeWielen, nullptr, lbl );
                ImGui::EndChild();

                if( naastElkaar ) ImGui::SameLine( 0.0f, 8.0f );

                ImGui::BeginChild( "kol_aanhanger", ImVec2( 0, 0 ), false, ImGuiWindowFlags_NoScrollbar );
                TekstGedimd( T( "AANHANGER" ) );
                if( vs.heeftAanhanger )
                {
                    SchadeBalk( T( "Trailer" ), vs.aanhangerSchade, nullptr, lbl );
                    SchadeBalk( T( "Lading" ), vs.ladingSchade, nullptr, lbl );
                    if( vs.ladingGewichtKg > 0.0 )
                    {
                        TekstGedimd( T( "Gewicht" ) );
                        ImGui::SameLine( lbl );
                        TekstSFmt( IM_COL32( 255, 255, 255, 255 ), "%.1f t", vs.ladingGewichtKg / 1000.0 );
                    }
                    // Deze uitleg alleen tonen als hij echt past; anders zag
                    // je "Alleen ladingsc..." halverwege afgekapt staan.
                    if( ImGui::CalcTextSize( T( "Alleen lading telt" ) ).x < kolom )
                    {
                        TekstGedimd( T( "Alleen lading telt" ) );
                    }
                }
                else
                {
                    TekstGedimd( T( "Niet gekoppeld" ) );
                }
                ImGui::EndChild();

                ImGui::PopStyleVar();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();

            // Helemaal onderaan, over de volle breedte: hoe zuinig rijd je
            // deze rit vergeleken met je eigen gemiddelde. Eén regel, zelfde
            // vorm als de convooi-herinnering. Is er niets te vergelijken,
            // dan komt er ook niets -- en schuift er dus niets op.
            TekenZuinigheid();

            if( m_kleinFont ) { ImGui::PopFont(); m_kleinFontActief = false; }
        }
    }

    void Overlay::TekenSpelersTab()
    {
        // Posities/koersen bijwerken voordat we de lijst ophalen. Dit draait
        // binnen het frame-event en dus op de game-thread -- de enige plek
        // waar SDK-getters antwoord geven.
        Logboek::Spoor( "spelers: posities verversen" );
        m_spelers.VerversPosities();
        std::vector<SpelerRecord> spelers = m_spelers.GeefSpelers();

        // Diagnose: hoeveel spelers dragen welke vlag? Zonder dit is niet te
        // zeggen of "geen patron in beeld" betekent dat ze er niet zijn, of
        // dat de SDK die vlag niet meegeeft. Hooguit eens per 10 seconden.
        {
            static std::chrono::steady_clock::time_point laatst;
            const auto nu = std::chrono::steady_clock::now();
            if( std::chrono::duration<double>( nu - laatst ).count() > 10.0 )
            {
                laatst = nu;
                int team = 0, mod = 0, patron = 0, metTag = 0, gekleurd = 0;
                for( const auto &s : spelers )
                {
                    if( s.isTeam || s.isManager ) ++team;
                    if( s.isModerator ) ++mod;
                    if( s.isPatron ) ++patron;
                    if( !s.tagTekst.empty() ) ++metTag;
                    if( s.tagKleurR < 0.95f || s.tagKleurG < 0.95f || s.tagKleurB < 0.95f ) ++gekleurd;
                }
                char regel[ 160 ];
                std::snprintf( regel, sizeof( regel ),
                                "spelers=%d team=%d mod=%d patron=%d met_tag=%d gekleurd=%d",
                                static_cast<int>( spelers.size() ), team, mod, patron, metTag,
                                gekleurd );
                Logboek::Schrijf( "vlaggen", regel );

                // Een paar voorbeelden erbij: naam, de patron-vlag en de
                // tagkleur naast elkaar. Een gekleurde tag is in TruckersMP
                // een Patreon-voordeel, dus als die kleur er WEL is en de
                // vlag niet, dan geeft de SDK die vlag gewoon niet mee.
                int getoond = 0;
                for( const auto &s : spelers )
                {
                    const bool heeftKleur = ( s.tagKleurR < 0.95f || s.tagKleurG < 0.95f ||
                                               s.tagKleurB < 0.95f );
                    if( !heeftKleur && !s.isPatron ) continue;
                    char v[ 200 ];
                    std::snprintf( v, sizeof( v ), "  %s | tag='%s' patron=%d kleur=%.2f,%.2f,%.2f",
                                    s.gebruikersnaam.c_str(), s.tagTekst.c_str(),
                                    s.isPatron ? 1 : 0,
                                    s.tagKleurR, s.tagKleurG, s.tagKleurB );
                    Logboek::Schrijf( "vlaggen", v );
                    if( ++getoond >= 5 ) break;
                }
            }
        }


        // Scroll-offset bijwerken op basis van je echte snelheid -- geeft
        // de minimap een "levend, meebewegend" gevoel zonder een echte
        // GPS-positie te verzinnen die de SDK niet geeft.
        {
            auto nu = std::chrono::steady_clock::now();
            if( m_minimapLaatsteUpdate.time_since_epoch().count() != 0 )
            {
                double verstrekenUur = std::chrono::duration<double>( nu - m_minimapLaatsteUpdate ).count() / 3600.0;
                if( verstrekenUur > 0.0 && verstrekenUur < 0.05 )
                {
                    m_minimapScrollKm += static_cast<float>( m_vracht.LiveSnelheidKmh() * verstrekenUur );
                }
            }
            m_minimapLaatsteUpdate = nu;
        }
        float scrollPixels = fmodf( m_minimapScrollKm * 40.0f, 1000.0f ); // 40px per "km" gevoel, geen echte schaal

        // --- Minimap: rechthoekig kaartje met kruispunt, GEEN ronde radar ---
        const float mapGrootte = 200.0f;
        ImVec2 startPos = ImGui::GetCursorScreenPos();
        ImVec2 midden = ImVec2( startPos.x + mapGrootte / 2, startPos.y + mapGrootte / 2 );
        ImDrawList *draw = ImGui::GetWindowDrawList();

        // Terreinachtergrond
        draw->AddRectFilled( startPos, ImVec2( startPos.x + mapGrootte, startPos.y + mapGrootte ),
                              IM_COL32( 12, 14, 12, 235 ), 10.0f );

        // Fijn stratenpatroon (dunne lijnen onder een paar hoeken), schuift
        // mee met je snelheid voor een "meebewegend" gevoel.
        auto tekenStratenlaag = [ & ]( float hoekGraden, float afstandTussen, ImU32 kleur )
        {
            float hoekRad = hoekGraden * 3.14159265f / 180.0f;
            float dx = cosf( hoekRad ), dy = sinf( hoekRad );
            float diag = mapGrootte * 1.5f;
            float verschuiving = fmodf( scrollPixels, afstandTussen );
            for( float offset = -diag; offset < diag; offset += afstandTussen )
            {
                float o = offset + verschuiving;
                ImVec2 p1( midden.x + o * -dy - diag * dx, midden.y + o * dx - diag * dy );
                ImVec2 p2( midden.x + o * -dy + diag * dx, midden.y + o * dx + diag * dy );
                draw->AddLine( p1, p2, kleur, 1.0f );
            }
        };
        draw->PushClipRect( startPos, ImVec2( startPos.x + mapGrootte, startPos.y + mapGrootte ), true );
        tekenStratenlaag( 4.0f, 20.0f, IM_COL32( 255, 255, 255, 18 ) );
        tekenStratenlaag( 94.0f, 24.0f, IM_COL32( 255, 255, 255, 18 ) );
        tekenStratenlaag( 48.0f, 34.0f, IM_COL32( 255, 255, 255, 10 ) );

        // Twee dikkere "hoofdwegen" die door het midden kruisen
        draw->AddLine( ImVec2( startPos.x, midden.y - 6 ), ImVec2( startPos.x + mapGrootte, midden.y + 6 ),
                        IM_COL32( 255, 255, 255, 55 ), 4.0f );
        draw->AddLine( ImVec2( midden.x - 8, startPos.y ), ImVec2( midden.x + 8, startPos.y + mapGrootte ),
                        IM_COL32( 255, 255, 255, 55 ), 4.0f );

        // Zelf (amber stip in het midden)
        draw->AddCircleFilled( midden, 5.5f, AccentKleurU32( 1.0f ) );
        draw->AddCircle( midden, 5.5f, IM_COL32( 0, 0, 0, 150 ), 12, 1.5f );

        // Spelers -- straal is de afstand, hoek is de ECHTE peiling uit
        // Vehicle::GetPlacement(). Boven op de kaart = recht vooruit.
        const float bereikMeter = 800.0f;
        float maxStraal = mapGrootte / 2 - 10;

        // Afstandsringen met een labeltje erbij. Zonder schaal weet je niet
        // of die stip nu op 50 of op 700 meter zit.
        if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
        for( int m : { 200, 400, 600 } )
        {
            const float r = ( m / bereikMeter ) * maxStraal;
            draw->AddCircle( midden, r, IM_COL32( 255, 255, 255, 26 ), 48, 1.0f );

            char label[ 12 ];
            snprintf( label, sizeof( label ), "%dm", m );
            draw->AddText( ImVec2( midden.x + 3.0f, midden.y - r - ImGui::GetFontSize() ),
                            IM_COL32( 255, 255, 255, 70 ), label );
        }
        if( m_kleinFont ) ImGui::PopFont();

        // Hoeveel stippen tekenen we? Bij vijftig spelers wordt het midden
        // een kluwen. We tekenen de DICHTSTBIJZIJNDE 50 -- de lijst is al op
        // afstand gesorteerd, dus dat is gewoon de eerste vijftig. Wat
        // daarbuiten valt is toch te ver weg om iets mee te doen.
        const int MAX_STIPPEN = 50;

        int zonderPositie = 0;
        int getekend = 0;
        for( const SpelerRecord &s : spelers )
        {
            if( !s.positieBekend )
            {
                // Geen voertuig in de wereld: bewust NIET tekenen, in plaats
                // van op een verzonnen hoek zetten. Wordt onder de kaart geteld.
                ++zonderPositie;
                continue;
            }
            if( getekend >= MAX_STIPPEN ) break;
            ++getekend;

            float straal = std::min( s.afstandMeter / bereikMeter, 1.0f ) * maxStraal;
            // Peiling is 0 = vooruit, met de klok mee. Op het scherm is
            // "vooruit" omhoog (negatieve Y), vandaar de -90 graden draai.
            float hoek = ( s.peilingGraden - 90.0f ) * 3.14159265f / 180.0f;
            ImVec2 punt( midden.x + straal * cosf( hoek ), midden.y + straal * sinf( hoek ) );

            // Eigen VTC gaat VOOR de rol-kleuren: een collega wil je als
            // eerste herkennen. Staat de schakelaar uit, dan verandert er
            // niets aan de bestaande kleuren.
            const bool eigenVtc = m_vtcRadarMarkering && IsEigenVtc( s );

            ImU32 kleur = eigenVtc                  ? IM_COL32( 91, 141, 239, 255 )
                        : s.isTeam || s.isManager ? IM_COL32( 232, 80, 70, 255 )
                        : s.isModerator            ? IM_COL32( 232, 80, 70, 255 )
                        : IsPatron( s )             ? IM_COL32( 220, 100, 220, 255 )
                                                     : IM_COL32( 63, 176, 138, 255 );

            // Dichtbij is groter en feller dan ver weg. Zo springt wie er
            // vlak naast je rijdt eruit, ook als het druk is.
            const float nabij = 1.0f - std::min( s.afstandMeter / bereikMeter, 1.0f );
            const float punthoogte = 2.6f + nabij * 2.4f;
            const int alpha = 130 + static_cast<int>( nabij * 125.0f );
            const ImU32 stipKleur = ( kleur & 0x00FFFFFF ) | ( static_cast<ImU32>( alpha ) << 24 );

            // Streepje in zijn rijrichting: zo zie je of iemand jouw kant op
            // rijdt of je tegemoet komt. Een LANGE combinatie krijgt een
            // langer en dikker streepje -- dan zie je op de kaart al dat daar
            // iets groots rijdt, zonder de lijst erbij te pakken.
            const bool lang = s.aanhangerLengteM > 16.5f;
            float koersRad = ( s.koersVerschilGraden - 90.0f ) * 3.14159265f / 180.0f;
            const float streep = punthoogte + ( lang ? 8.0f : 3.5f );
            draw->AddLine( punt,
                            ImVec2( punt.x + streep * cosf( koersRad ),
                                     punt.y + streep * sinf( koersRad ) ),
                            stipKleur, lang ? 2.6f : 1.6f );

            draw->AddCircleFilled( punt, punthoogte, stipKleur );
            draw->AddCircle( punt, punthoogte, IM_COL32( 0, 0, 0, 120 ), 10, 1.0f );
        }
        draw->PopClipRect();
        draw->AddRect( startPos, ImVec2( startPos.x + mapGrootte, startPos.y + mapGrootte ),
                        AccentKleurU32( 0.5f ), 10.0f, 0, 1.5f );

        ImGui::Dummy( ImVec2( mapGrootte, mapGrootte ) );
        if( zonderPositie > 0 )
        {
            TekstGedimd( ( std::to_string( zonderPositie ) +
                            T( " speler(s) zonder positie -- niet op de kaart getekend." ) ).c_str() );
        }
        TekstGedimd( T( "Boven is recht vooruit. Peiling en rijrichting komen uit de"
            " voertuigposities van de SDK. Het wegenpatroon is decoratief:"
            " het volgt je snelheid, maar is geen echte kaart." ) );

        ImGui::Spacing();

        // --- Kop met telling en legenda ----------------------------------
        {
            int metPositie = 0;
            for( const SpelerRecord &s : spelers ) if( s.positieBekend ) ++metPositie;

            TekstSFmt( IM_COL32( 255, 255, 255, 255 ), T( "%d spelers in bereik" ), (int)spelers.size() );
            ImGui::SameLine( 0.0f, 10.0f );
            TekstGedimdFmt( T( "(%d op de kaart)" ), metPositie );

            // Collega's erbij, in dezelfde blauwe kleur als hun markering.
            // Alleen tonen als er ook echt iemand is -- een regel met "0
            // collega's" voegt niets toe (zie de afspraak dat onderdelen
            // zonder nieuws zich verbergen).
            if( m_vtcRadarMarkering && m_vtcId > 0 )
            {
                int collega = 0;
                for( const SpelerRecord &s : spelers ) if( IsEigenVtc( s ) ) ++collega;
                if( collega > 0 )
                {
                    ImGui::SameLine( 0.0f, 10.0f );
                    if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
                    TekstSFmt( IM_COL32( 91, 141, 239, 255 ),
                                collega == 1 ? T( "%d collega" ) : T( "%d collega's" ), collega );
                    if( m_kleinFont ) ImGui::PopFont();
                }
            }

            // Legenda: welke kleur hoort bij welke rol. Zonder dit moet je
            // raden waarom iemand oranje of blauw is.
            if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
            struct Legenda { ImU32 kleur; const char *naam; };
            // LET OP: de maat en de lus lopen via dezelfde constante. Een
            // los ingetypt getal hier was precies de crash van augustus.
            static const Legenda legenda[] = {
                { IM_COL32( 232, 80, 70, 255 ),  "team/mod" },
                { IM_COL32( 220, 100, 220, 255 ), "patron" },
                { IM_COL32( 63, 176, 138, 255 ),  "speler" },
                { IM_COL32( 91, 141, 239, 255 ),  "eigen VTC" },
            };
            constexpr int AANTAL_LEGENDA = static_cast<int>( sizeof( legenda ) / sizeof( legenda[ 0 ] ) );
            for( int i = 0; i < AANTAL_LEGENDA; ++i )
            {
                // De VTC-kleur staat als laatste in de lijst; die overslaan
                // als je de markering niet gebruikt.
                if( i == AANTAL_LEGENDA - 1 && !m_vtcRadarMarkering ) continue;

                if( i > 0 ) ImGui::SameLine( 0.0f, 10.0f );
                ImVec2 p = ImGui::GetCursorScreenPos();
                float r = ImGui::GetFontSize() * 0.25f;
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2( p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f ), r, legenda[ i ].kleur );
                ImGui::Dummy( ImVec2( r * 2.0f + 3.0f, ImGui::GetTextLineHeight() ) );
                ImGui::SameLine( 0.0f, 3.0f );
                TekstGedimd( T( legenda[ i ].naam ) );
            }
            if( m_kleinFont ) ImGui::PopFont();
        }

        ImGui::Spacing();

        // --- Spelerslijst als compacte regels -----------------------------
        //
        // Was een kaartje van 70 pixels per speler. Met vijftig spelers in
        // beeld is dat 3500 pixels in een venster van 220 -- je scrollde je
        // suf. Nu een regel per speler, in het kleine lettertype, met een
        // gekleurd streepje voor de rol en de afstand rechts uitgelijnd.
        {
            if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }

            const float regelHoogte = ImGui::GetTextLineHeight() + 6.0f;
            const float lijstHoogte = ImGui::GetContentRegionAvail().y - 4.0f;

            ImGui::BeginChild( "spelerslijst", ImVec2( 0, lijstHoogte > 80.0f ? lijstHoogte : 200.0f ), false );

            ImDrawList *ld = ImGui::GetWindowDrawList();
            int nummer = 0;
            for( const SpelerRecord &s : spelers )
            {
                ++nummer;

                const ImU32 rolKleur = ( m_vtcRadarMarkering && IsEigenVtc( s ) )
                                                                  ? IM_COL32( 91, 141, 239, 255 )
                                       : s.isTeam || s.isManager ? IM_COL32( 232, 80, 70, 255 )
                                       : s.isModerator          ? IM_COL32( 232, 80, 70, 255 )
                                       : IsPatron( s )           ? IM_COL32( 220, 100, 220, 255 )
                                                                  : IM_COL32( 63, 176, 138, 255 );

                const ImVec2 rijStart = ImGui::GetCursorScreenPos();
                const float breedte = ImGui::GetContentRegionAvail().x;

                // Om de andere regel een subtiele band: houdt lange lijsten
                // leesbaar zonder lijnen te tekenen.
                if( nummer % 2 == 0 )
                {
                    ld->AddRectFilled( rijStart, ImVec2( rijStart.x + breedte, rijStart.y + regelHoogte ),
                                        IM_COL32( 255, 255, 255, 10 ), 3.0f );
                }
                // Rolkleur als streepje links.
                ld->AddRectFilled( ImVec2( rijStart.x, rijStart.y + 2.0f ),
                                    ImVec2( rijStart.x + 3.0f, rijStart.y + regelHoogte - 2.0f ),
                                    rolKleur, 1.5f );

                ImGui::SetCursorPosX( ImGui::GetCursorPosX() + 8.0f );
                TekstGedimdFmt( "%2d", nummer );
                ImGui::SameLine( 0.0f, 6.0f );

                // Naam die niet past: dezelfde ladder als bij "CRUISE CONTROL"
                // -- eerst de volle vorm, dan een kortere, en pas als laatste
                // redmiddel afkappen. Bij spelersnamen is er een nuttige
                // tussenstap, want die zitten vaak vol met tags en versiering:
                //
                //   "[WEEDA] Barend V8 | NL"  ->  "Barend V8"
                //
                // Zo verlies je de naam zelf niet, alleen het decor eromheen.
                const float ruimteNaam = breedte - 150.0f;
                std::string naam = s.gebruikersnaam;

                if( ImGui::CalcTextSize( naam.c_str() ).x > ruimteNaam )
                {
                    // Stap 1: alles tussen haakjes of blokhaken eruit, plus
                    // wat er achter een scheidingsteken staat.
                    std::string kort;
                    int diepte = 0;
                    for( char c : s.gebruikersnaam )
                    {
                        if( c == '[' || c == '(' || c == '{' ) { ++diepte; continue; }
                        if( c == ']' || c == ')' || c == '}' ) { if( diepte > 0 ) --diepte; continue; }
                        if( diepte == 0 )
                        {
                            if( c == '|' ) break; // alles na een pijpje is versiering
                            kort += c;
                        }
                    }
                    // Spaties aan de randen weg.
                    while( !kort.empty() && kort.front() == ' ' ) kort.erase( kort.begin() );
                    while( !kort.empty() && kort.back() == ' ' ) kort.pop_back();

                    if( !kort.empty() && ImGui::CalcTextSize( kort.c_str() ).x <= ruimteNaam )
                    {
                        naam = kort;
                    }
                    else
                    {
                        // Stap 2: past nog niet -- afkappen met een punt.
                        if( !kort.empty() ) naam = kort;
                        while( naam.size() > 3 && ImGui::CalcTextSize( naam.c_str() ).x > ruimteNaam )
                        {
                            naam.erase( naam.size() - 2 );
                            naam.back() = '.';
                        }
                    }
                }
                TekstS( naam.c_str() );

                // Merkjes achter de naam. STAF eerst -- dat is het enige dat
                // je gedrag zou moeten veranderen. Daarna de combinatie:
                // een oplegger is zo'n 13 tot 16 meter, alles daarboven is
                // een dubbele of een lange combinatie, en dat is precies wat
                // je wilt weten voordat je gaat inhalen.
                if( s.isTeam || s.isManager || s.isModerator )
                {
                    ImGui::SameLine( 0.0f, 5.0f );
                    TekstS( s.isModerator ? T( "MOD" ) : T( "TEAM" ),
                             s.isModerator ? IM_COL32( 232, 80, 70, 255 )
                                           : IM_COL32( 232, 80, 70, 255 ) );
                }
                if( IsPatron( s ) )
                {
                    ImGui::SameLine( 0.0f, 5.0f );
                    TekstS( T( "PATRON" ), IM_COL32( 220, 100, 220, 255 ) );
                }
                if( m_vtcRadarMarkering && IsEigenVtc( s ) )
                {
                    ImGui::SameLine( 0.0f, 5.0f );
                    TekstS( T( "VTC" ), IM_COL32( 91, 141, 239, 255 ) );
                    if( ImGui::IsItemHovered() )
                    {
                        const VtcInfo eigen = m_webApi.Vtc();
                        ImGui::SetTooltip( "%s", eigen.naam.empty() ? T( "Jouw VTC" ) : eigen.naam.c_str() );
                    }
                }
                if( s.aanhangerLengteM > 16.5f )
                {
                    ImGui::SameLine( 0.0f, 5.0f );
                    TekstS( T( "LANG" ), IM_COL32( 235, 170, 90, 255 ) );
                    if( ImGui::IsItemHovered() )
                    {
                        ImGui::SetTooltip( T( "Lange combinatie: %.0f m aanhanger" ), s.aanhangerLengteM );
                    }
                }

                // De volledige naam bij aanwijzen, zodat je nooit iets mist.
                if( naam != s.gebruikersnaam && ImGui::IsItemHovered() )
                {
                    ImGui::SetTooltip( "%s", s.gebruikersnaam.c_str() );
                }

                // Afstand en ping rechts uitgelijnd, zodat de kolommen kloppen.
                char rechts[ 48 ];
                snprintf( rechts, sizeof( rechts ), "%4.0f m   %3u ms", s.afstandMeter, (unsigned)s.pingMs );
                const float rechtsBreedte = ImGui::CalcTextSize( rechts ).x;
                ImGui::SameLine( 0.0f, 0.0f );
                ImGui::SetCursorPosX( ImGui::GetCursorPosX() +
                                       std::max( 6.0f, breedte - 40.0f - rechtsBreedte -
                                                  ( ImGui::GetCursorPosX() - 8.0f ) ) );
                TekstGedimd( rechts );

                // Menuknop helemaal rechts.
                ImGui::SameLine( breedte - 22.0f );
                TekenSpelerContextKnop( s, std::to_string( s.spelerId ) );
            }
            ImGui::EndChild();

            if( m_kleinFont ) { ImGui::PopFont(); m_kleinFontActief = false; }
        }
    }

    // Kleine hulpfunctie: gekleurd "badge"-labeltje zoals in de mockups
    // (bv. UITERLIJK, BRANDSTOFPRIJS, DISCORD), i.p.v. kale platte tekst.
    // TekenSectieBadge stond hier: een knop-achtige badge per sectie. Nu
    // vervangen door SectieStart/SectieEind, die elke groep in een eigen
    // kaartje zetten met een gekleurd streepje voor de kop.

    float Overlay::SectieHoogte( int tekstRegels, int velden, int spaties ) const
    {
        const ImGuiStyle &st = ImGui::GetStyle();
        const float tekst = ImGui::GetTextLineHeight();  // hoogte van één regel
        const float veld  = ImGui::GetFrameHeight();     // keuzelijst, invulveld, vinkje

        // Elk element kost zijn eigen hoogte PLUS de tussenruimte die ImGui
        // er onder zet. Die tussenruimte vergeten was mijn fout: bij de
        // tachograaf scheelde dat 28 pixels, en dan krijg je een scrollbalk.
        const float tussen = st.ItemSpacing.y;

        float hoogte = st.WindowPadding.y * 2.0f;   // boven en onder
        hoogte += tekst + tussen;                    // de gekleurde kop
        hoogte += tussen;                            // de Spacing() erna (zie SectieStart)
        hoogte += tekstRegels * ( tekst + tussen );
        hoogte += velden * ( veld + tussen );
        hoogte += spaties * tussen;
        return hoogte;
    }

    void Overlay::SectieStart( const char *naam, ImVec4 kleur, float vasteHoogte )
    {
        // Iets meer lucht tussen de secties dan binnen een sectie -- dat is
        // wat het scherm leesbaar maakt zonder lijnen te trekken.
        ImGui::Spacing();
        ImGui::Spacing();

        // Breedte begrenzen. Bij een breed venster liep een sectie helemaal
        // door tot de rand, terwijl de schuiven erin maar een stuk van die
        // ruimte vulden -- dat oogt leeg. Met een plafond blijft de
        // verhouding tussen vak en inhoud kloppen.
        float sectieBreedte = ImGui::GetContentRegionAvail().x;
        if( sectieBreedte > 520.0f ) sectieBreedte = 520.0f;

        ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 12, 10 ) );

        // Standaard precies zoals altijd: hoogte 0 + AlwaysAutoResize, zodat
        // het kaartje zich om zijn eigen inhoud sluit. Alleen als er een
        // vasteHoogte is meegegeven wijken we daarvan af -- dan geen
        // AlwaysAutoResize, want die twee bijten elkaar.
        if( vasteHoogte > 0.0f )
        {
            ImGui::BeginChild( naam, ImVec2( sectieBreedte, vasteHoogte ), true,
                                ImGuiWindowFlags_NoScrollbar );
        }
        else
        {
            ImGui::BeginChild( naam, ImVec2( sectieBreedte, 0 ), true,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar );
        }

        // Gekleurd streepje links van de kop: rustiger dan een knop-achtige
        // badge, en het geeft elke sectie een eigen kleur om op te herkennen.
        const ImVec2 kp = ImGui::GetCursorScreenPos();
        const float kh = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2( kp.x - 5.0f, kp.y + 1.0f ), ImVec2( kp.x - 2.0f, kp.y + kh - 1.0f ),
            ImGui::GetColorU32( kleur ), 1.5f );

        ImGui::TextColored( kleur, "%s", T( naam ) );
        ImGui::Spacing();
    }

    float Overlay::VeldBreedte() const
    {
        // Ongeveer tweederde van het vak: genoeg om de leegte weg te nemen,
        // en er blijft ruimte over voor het label dat ImGui rechts zet.
        const float beschikbaar = ImGui::GetContentRegionAvail().x;
        float breedte = beschikbaar * 0.62f;
        if( breedte < 150.0f ) breedte = 150.0f;
        if( breedte > 340.0f ) breedte = 340.0f;
        return breedte;
    }

    void Overlay::SectieEind()
    {
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    void Overlay::TekenInstellingenTab()
    {
        // Als enige sectie een vaste hoogte: automatisch meegroeien leverde
        // hier een veel te hoog, half leeg vak op. 150px is bevestigd goed
        // (zie screenshot 30-08). Wil je 'm anders, pas dit getal aan.
        SectieStart( "UITERLIJK", ImVec4( m_accentKleur[ 0 ] + 0.2f, m_accentKleur[ 1 ] + 0.15f,
                                            m_accentKleur[ 2 ] + 0.1f, 1.0f ),
                      SectieHoogte( /*tekst*/ 1, /*velden*/ 4 ) );
        // Taalkeuze. Nederlands is de basis; teksten zonder vertaling
        // blijven Nederlands, dus er kan nooit iets leeg blijven.
        {
            static const char *talen[] = { "Nederlands", "Engels" };
            ImGui::SetNextItemWidth( VeldBreedte() );
            if( ImGui::BeginCombo( T( "Taal" ), T( talen[ m_taal == 1 ? 1 : 0 ] ) ) )
            {
                for( int i = 0; i < 2; ++i )
                {
                    if( ImGui::Selectable( T( talen[ i ] ), m_taal == i ) )
                    {
                        m_taal = i;
                        Taal::Kies( i == 1 ? TaalKeuze::Engels : TaalKeuze::Nederlands );
                        SlaUiterlijkOp();
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::SetNextItemWidth( VeldBreedte() );
        if( ImGui::SliderFloat( T( "Doorzichtigheid (achtergrond)" ), &m_doorzichtigheid, 0.35f, 1.0f, "%.2f" ) )
        {
            SlaUiterlijkOp();
        }
        ImGui::SetNextItemWidth( VeldBreedte() );
        if( ImGui::SliderFloat( T( "Doorzichtigheid (zijbalk/logo/menu's)" ), &m_iconenDoorzichtigheid, 0.4f, 1.0f, "%.2f" ) )
        {
            SlaUiterlijkOp();
        }
        if( ImGui::ColorEdit3( T( "Accentkleur" ), m_accentKleur ) )
        {
            SlaUiterlijkOp();
        }
        TekstGedimd( T( "Klik op het kleurvakje voor de volledige kleurenkiezer." ) );
        SectieEind();

        // De tachograaf en de tijdschaal stonden per ongeluk BINNEN de
        // uiterlijk-sectie -- tussen het kopje en de schuiven in. Nu een
        // eigen sectie, waar ze thuishoren.
        // Vaste hoogte, maar WELKE hangt af van de gekozen werkwijze: bij
        // "het spel volgen" staan er alleen twee keuzelijsten, bij de andere
        // twee komen er vier schuiven bij. Eén vast getal zou het ene geval
        // afknippen of het andere half leeg laten -- vandaar dat we de stand
        // hier alvast opvragen.
        {
            const TruckTracking::TachoInstelling voorHoogte = m_vracht.HuidigeTachoInstelling();
            const bool alleenKiezen =
                ( voorHoogte.stand == TruckTracking::TachoStand::SpelVolgen );
            SectieStart( "TACHOGRAAF", ImVec4( 0.90f, 0.72f, 0.35f, 1.0f ),
                          alleenKiezen ? SectieHoogte( 2, 2, 2 )
                                        : SectieHoogte( 4, 6, 2 ) );
        }
        // --- Tachograaf ---------------------------------------------------
        {
    
            TruckTracking::TachoInstelling inst = m_vracht.HuidigeTachoInstelling();
            const int huidig = static_cast<int>( inst.stand );

            static const char *standNamen[ 3 ] = {
                "Het spel volgen (11 uur)",
                "Eigen regels",
                "Rijtijdenwet (ATW)"
            };

            ImGui::SetNextItemWidth( VeldBreedte() );
            if( ImGui::BeginCombo( T( "Werkwijze" ), T( standNamen[ huidig ] ) ) )
            {
                for( int i = 0; i < 3; ++i )
                {
                    if( ImGui::Selectable( T( standNamen[ i ] ), huidig == i ) )
                    {
                        inst.stand = static_cast<TruckTracking::TachoStand>( i );

                        // De ATW-stand is dezelfde machinerie als "eigen
                        // regels", maar met de wettelijke waarden er alvast
                        // in. Zo hoef je ze niet op te zoeken.
                        if( inst.stand == TruckTracking::TachoStand::ATW )
                        {
                            inst.maxAaneengeslotenRijden = 4.5 * 60.0;
                            inst.pauzeDuur = 45.0;
                            inst.maxDagRijden = 9.0 * 60.0;
                            inst.dagRust = 11.0 * 60.0;
                        }
                        m_vracht.ZetTachoInstelling( inst );
                    }
                }
                ImGui::EndCombo();
            }

            if( inst.stand == TruckTracking::TachoStand::SpelVolgen )
            {
                TekstGedimd( T( "Loopt gelijk met de P-teller in je Route Advisor." ) );
            }
            else
            {
                // Alleen bij "eigen regels" mag je zelf schuiven; in de
                // ATW-stand staan de waarden vast, anders is het geen
                // voorinstelling meer.
                const bool bewerkbaar = ( inst.stand == TruckTracking::TachoStand::EigenRegels );
                if( !bewerkbaar )
                {
                    TekstGedimd( T( "Vaste waarden. Kies \"Eigen regels\" om ze aan te passen." ) );
                }

                bool gewijzigd = false;
                auto uurSchuif = [ & ]( const char *label, double &waardeMin,
                                         float vanUur, float totUur )
                {
                    float uren = static_cast<float>( waardeMin / 60.0 );
                    ImGui::SetNextItemWidth( VeldBreedte() );
                    if( ImGui::SliderFloat( label, &uren, vanUur, totUur, T( "%.2f uur" ) ) && bewerkbaar )
                    {
                        waardeMin = uren * 60.0;
                        gewijzigd = true;
                    }
                };

                uurSchuif( T( "Aaneengesloten rijden" ), inst.maxAaneengeslotenRijden, 1.0f, 11.0f );
                uurSchuif( T( "Pauzeduur" ), inst.pauzeDuur, 0.25f, 3.0f );
                uurSchuif( T( "Rijtijd per dag" ), inst.maxDagRijden, 2.0f, 15.0f );
                uurSchuif( T( "Dagelijkse rust" ), inst.dagRust, 4.0f, 14.0f );

                if( gewijzigd ) m_vracht.ZetTachoInstelling( inst );

                TekstGedimd( T( "Dit is JOUW tachograaf, niet die van het spel."
                              " ETS2 blijft zijn eigen elf uur hanteren." ) );
            }
            ImGui::Spacing();
        }

        // Noodknop voor de tijdschaal. Normaal meet de plugin die zelf en
        // zet hem na vijf minuten vast; dit is voor als je 'm wilt overrulen.
        {
            static const char *keuzes[] = { "Automatisch meten", "TruckersMP (6)", "Singleplayer (19)" };
            double huidig = m_vracht.HandmatigeSchaal();
            int index = huidig <= 0.0 ? 0 : ( huidig > 12.0 ? 2 : 1 );

            ImGui::SetNextItemWidth( VeldBreedte() );
            if( ImGui::BeginCombo( T( "Tijdschaal" ), T( keuzes[ index ] ) ) )
            {
                for( int i = 0; i < 3; ++i )
                {
                    if( ImGui::Selectable( T( keuzes[ i ] ), index == i ) )
                    {
                        m_vracht.ZetHandmatigeSchaal( i == 0 ? 0.0 : ( i == 1 ? 6.0 : 19.0 ) );
                    }
                }
                ImGui::EndCombo();
            }
            TekstGedimd( T( "Bepaalt hoeveel spelminuten er in een echte minuut gaan."
                          " Laat op automatisch staan tenzij de aankomsttijd er structureel naast zit." ) );
            ImGui::Spacing();
        }

        SectieEind();

        SectieStart( "BRANDSTOFPRIJS", ImVec4( 0.90f, 0.35f, 0.32f, 1.0f ),
                      SectieHoogte( 5, 3, 4 ) );

        // Landkeuze. Het spel geeft je huidige land NIET door -- zes
        // kanaalnamen geprobeerd, alle zes geweigerd (staat in debug.log).
        // Daarom kies je het zelf; bij een grensovergang is dat twee klikken,
        // en dan klopt het bedrag wel.
        {
            const std::vector<std::string> landen = m_brandstof.BekendeLanden();
            const std::string huidig = m_brandstof.HuidigLand();
            const std::string tonen = huidig.empty() ? std::string( T( "Eigen prijs hieronder" ) ) : huidig;

            ImGui::SetNextItemWidth( VeldBreedte() );
            if( ImGui::BeginCombo( T( "Land" ), tonen.c_str() ) )
            {
                if( ImGui::Selectable( T( "Eigen prijs hieronder" ), huidig.empty() ) )
                {
                    m_brandstof.ZetHuidigLand( "" );
                }
                for( const std::string &land : landen )
                {
                    if( ImGui::Selectable( land.c_str(), land == huidig ) )
                    {
                        m_brandstof.ZetHuidigLand( land );
                    }
                }
                ImGui::EndCombo();
            }

            if( !huidig.empty() )
            {
                TekstGedimdFmt( T( "Prijs uit brandstofprijzen.json: EUR %.2f per liter" ),
                                 m_brandstof.PrijsVoorLand( huidig ) );
            }
        }

        ImGui::Spacing();
        TekstGedimd( T( "Wordt gebruikt om je verbruik om te rekenen naar kosten." ) );
        ImGui::Spacing();
        ImGui::SetNextItemWidth( VeldBreedte() );
        if( ImGui::InputText( T( "EUR per liter" ), m_prijsBuffer, sizeof( m_prijsBuffer ),
                               ImGuiInputTextFlags_CharsDecimal ) )
        {
            double waarde = atof( m_prijsBuffer );
            if( waarde > 0.0 )
            {
                m_brandstof.ZetPrijsPerLiter( waarde );
            }
        }
        ImGui::Spacing();
        ImGui::TextDisabled( T( "Wijziging wordt direct opgeslagen in" ) );
        TekstGedimd( T( "Direct opgeslagen in instellingen.json" ) );

        // Hoort bij het verbruik, dus staat hier en niet bij UITERLIJK.
        ImGui::Spacing();
        if( ImGui::Checkbox( T( "Zuinigheidsregel tonen" ), &m_zuinigheidTonen ) )
        {
            SlaUiterlijkOp();
        }
        TekstGedimd( T( "Rit t.o.v. je gemiddelde" ) );
        SectieEind();

        SectieStart( "DISCORD", ImVec4( 0.45f, 0.62f, 0.96f, 1.0f ),
                      SectieHoogte( 2, 4, 3 ) );
        ImGui::Spacing();
        ImGui::TextDisabled( T( "Stuurt een bericht naar een Discord-webhook zodra een rit is afgerond of geannuleerd." ) );
        ImGui::Spacing();

        if( !m_webhookBufferGeladen )
        {
            std::string huidig = m_discord.WebhookUrl();
            std::snprintf( m_webhookBuffer, sizeof( m_webhookBuffer ), "%s", huidig.c_str() );
            m_webhookBufferGeladen = true;
        }

        bool ingeschakeld = m_discord.IsIngeschakeld();
        if( ImGui::Checkbox( T( "Meldingen aan" ), &ingeschakeld ) )
        {
            m_discord.ZetIngeschakeld( ingeschakeld );
        }

        // Invoerveld met een plakknop ernaast. Ctrl+V werkt (de letters en
        // Ctrl worden doorgegeven aan ImGui, zie OpToets), maar zo'n knop is
        // zekerder: een webhook-URL typ je liever niet over.
        float knopBreedte = ImGui::CalcTextSize( T( "Plakken" ) ).x + 20.0f;
        ImGui::SetNextItemWidth( -( knopBreedte + 8.0f ) );
        if( ImGui::InputText( "##webhookurl", m_webhookBuffer, sizeof( m_webhookBuffer ) ) )
        {
            m_discord.ZetWebhookUrl( m_webhookBuffer );
        }
        ImGui::SameLine();
        if( ImGui::Button( T( "Plakken" ) ) )
        {
            const std::string uitKlembord = LeesVanKlembord();
            if( !uitKlembord.empty() )
            {
                std::snprintf( m_webhookBuffer, sizeof( m_webhookBuffer ), "%s", uitKlembord.c_str() );
                m_discord.ZetWebhookUrl( m_webhookBuffer );
            }
        }
        ImGui::TextDisabled( T( "Webhook-URL (Discord: kanaal bewerken > Integraties > Webhooks)" ) );

        ImGui::Spacing();
        if( ImGui::Button( T( "Testbericht sturen" ) ) )
        {
            m_discord.StuurTestbericht();
        }
        SectieEind();

        SectieStart( "INCIDENT-RECORDER", ImVec4( 0.93f, 0.50f, 0.45f, 1.0f ),
                      SectieHoogte( 2, 2, 2 ) );
        ImGui::Spacing();
        ImGui::TextDisabled( T( "Hoeveel minuten spelersdata continu bewaard blijft (bevriest bij schade)." ) );
        static int bufferMinuten = m_incident.BufferMinuten();
        ImGui::SetNextItemWidth( VeldBreedte() );
        if( ImGui::SliderInt( T( "Buffer-lengte (min)" ), &bufferMinuten, 2, 6 ) )
        {
            m_incident.ZetBufferMinuten( bufferMinuten );
        }

        // Diagnose hoort hier: het is hetzelfde soort "meer vastleggen om
        // achteraf te kunnen kijken". Standaard uit, want deze regels
        // schrijven elke paar seconden naar schijf.
        ImGui::Spacing();
        if( ImGui::Checkbox( T( "Uitgebreid logboek" ), &m_uitgebreidLog ) )
        {
            Logboek::Uitgebreid() = m_uitgebreidLog;
            SlaUiterlijkOp();
        }
        TekstGedimd( T( "Alleen aanzetten bij het uitzoeken van een probleem" ) );
        SectieEind();

        // Wat lucht onderaan, zodat de laatste sectie niet tegen de rand
        // plakt bij het doorscrollen.
        ImGui::Spacing();
        ImGui::Spacing();
    }

    void Overlay::TekenGeschiedenisTab()
    {
        std::vector<Trip> recent = m_logger.GeefRecenteRitten( 50 );
        if( recent.empty() )
        {
            ImGui::TextDisabled( T( "Nog geen ritten gelogd." ) );
            return;
        }

        // Filterknoppen bovenaan (Alles / Vracht / Bus)
        static int filter = 0; // 0=alles 1=vracht 2=bus
        auto filterKnop = [ & ]( const char *label, int waarde )
        {
            bool actief = ( filter == waarde );
            if( actief ) ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( m_accentKleur[ 0 ], m_accentKleur[ 1 ], m_accentKleur[ 2 ], 1.0f ) );
            if( ImGui::Button( label ) ) filter = waarde;
            if( actief ) ImGui::PopStyleColor();
            ImGui::SameLine();
        };
        filterKnop( T( "Alles" ), 0 );
        filterKnop( T( "Vracht" ), 1 );
        filterKnop( T( "Bus" ), 2 );
        ImGui::NewLine();
        ImGui::Spacing();

        ImGui::BeginChild( "geschiedenis_lijst", ImVec2( 0, 320 ), false );
        for( auto it = recent.rbegin(); it != recent.rend(); ++it )
        {
            const Trip &t = *it;
            bool isBus = t.type == TripType::Bus;
            if( filter == 1 && isBus ) continue;
            if( filter == 2 && !isBus ) continue;

            ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
            ImGui::BeginChild( ( "rit_" + t.id ).c_str(), ImVec2( 0, 78 ), true, ImGuiWindowFlags_NoScrollbar );

            ImVec2 kaartPos = ImGui::GetCursorScreenPos();
            ImDrawList *kd = ImGui::GetWindowDrawList();
            ImU32 statusKleur = t.status == TripStatus::Voltooid ? IM_COL32( 63, 176, 138, 255 ) : IM_COL32( 226, 85, 74, 255 );
            kd->AddRectFilled( kaartPos, ImVec2( kaartPos.x + 3, kaartPos.y + 44 ), statusKleur, 2.0f );

            ImGui::SetCursorPosX( 14 );
            TekenVoertuigIcoon( isBus, 22.0f );
            ImGui::SameLine();
            ImGui::BeginGroup();
            if( isBus )
                ImGui::Text( T( "Buslijn -- %d haltes" ), (int)t.haltes.size() );
            else
                ImGui::Text( "%s -> %s", t.bronStad.c_str(), t.bestemmingStad.c_str() );
            ImGui::TextDisabled( "%.0f km -- %s", t.afgelegdeAfstandKm,
                                  t.status == TripStatus::Voltooid ? T( "Voltooid" ) : T( "Geannuleerd" ) );
            ImGui::EndGroup();

            ImGui::SameLine( ImGui::GetWindowWidth() - 110 );
            ImGui::BeginGroup();
            long long opbrengst = t.inkomen != 0 ? t.inkomen : t.geschatUitbetaling;
            ImGui::Text( "%lld", opbrengst );
            if( t.brandstofKostenEuro > 0.0 )
                ImGui::TextDisabled( T( "-EUR %.2f" ), t.brandstofKostenEuro );
            ImGui::EndGroup();

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
    }

    void Overlay::TekenStatistiekenTab()
    {
        Totals totalen = m_logger.GeefTotalen();
        double netto = (double)totalen.totaalInkomen - totalen.totaalBrandstofKostenEuro;

        // Responsieve breedte (net als de 3 kaartjes op de Live-tab) --
        // schaalt mee als je het venster smaller/breder maakt, i.p.v. een
        // vaste 150px die vast bleef staan.
        float breedte = ( ImGui::GetContentRegionAvail().x - 16 ) / 3.0f;

        auto statKaart = [ & ]( const char *label, const std::string &waarde, bool accent = false )
        {
            ImVec4 achtergrond = accent
                ? TintKaartKleur( ImVec4( m_accentKleur[ 0 ], m_accentKleur[ 1 ], m_accentKleur[ 2 ], 1.0f ), 0.28f )
                : KaartKleur();
            ImGui::PushStyleColor( ImGuiCol_ChildBg, achtergrond );
            ImGui::BeginChild( label, ImVec2( breedte, 96 ), true, ImGuiWindowFlags_NoScrollbar );
            ImGui::TextDisabled( "%s", label );
            if( m_kopFont ) ImGui::PushFont( m_kopFont );
            ImGui::Text( "%s", waarde.c_str() );
            if( m_kopFont ) ImGui::PopFont();
            ImGui::EndChild();
            ImGui::PopStyleColor();
        };

        statKaart( T( "VRACHTRITTEN" ), std::to_string( totalen.aantalVrachtRitten ) );
        ImGui::SameLine();
        statKaart( T( "BUSLIJNRITTEN" ), std::to_string( totalen.aantalBusRitten ) );
        ImGui::SameLine();
        statKaart( T( "AFSTAND" ), std::to_string( (int)totalen.totaalAfstandKm ) + " km" );

        char buf[ 32 ];
        statKaart( T( "VERDIEND" ), std::to_string( totalen.totaalInkomen ) );
        ImGui::SameLine();
        snprintf( buf, sizeof( buf ), "EUR %.2f", totalen.totaalBrandstofKostenEuro );
        statKaart( T( "BRANDSTOF" ), buf );
        ImGui::SameLine();
        snprintf( buf, sizeof( buf ), "EUR %.2f", netto );
        statKaart( T( "NETTO" ), buf, true );

        // --- Brandstof deze sessie ---------------------------------------
        {
            const int aantal = m_brandstof.AantalTankbeurten();
            if( aantal > 0 )
            {
                const double liters = m_brandstof.TotaalGetanktLiters();
                const double kosten = liters * m_brandstof.PrijsPerLiter();

                ImGui::Spacing();
                if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }
                TekstGedimdFmt( T( "Getankt sinds opstarten: %dx, %.0f liter, ongeveer EUR %.0f" ),
                                 aantal, liters, kosten );
                TekstGedimdFmt( T( "Gerekend met EUR %.2f per liter (zelf ingesteld -- het spel geeft"
                                 " de pompprijs niet door)." ), m_brandstof.PrijsPerLiter() );
                if( m_kleinFont ) { ImGui::PopFont(); m_kleinFontActief = false; }
            }
        }

        // De drie regels over trips.jsonl en dashboard.html stonden hier
        // eerder. Weggehaald: dat is opstart-informatie die je een keer leest
        // en daarna elke sessie in de weg staat. Het staat in de README.

        // --- TruckersMP Web API ------------------------------------------
        // Serverstatus en aankomende evenementen, opgehaald bij
        // api.truckersmp.com. Staat standaard UIT: netwerkverkeer hoort iets
        // te zijn waar je zelf voor kiest.
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        KopBalk( T( "TRUCKERSMP LIVE" ) );

        {
            // Vinkje in het kleine lettertype, en zonder uitleg eronder: dat
            // scheelt twee regels die de lijst eronder beter kan gebruiken.
            // Wat het doet staat in de README.
            if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }

            bool aan = m_webApi.Ingeschakeld();
            if( ImGui::Checkbox( T( "Servergegevens en evenementen ophalen" ), &aan ) )
            {
                m_webApi.ZetIngeschakeld( aan );
            }

            // --- Jouw planning ---
            // BEWUST boven de schakelaar hierboven: dit hangt aan je eigen
            // convooi-aanmeldingen, niet aan de serverstatus. Anders zou je
            // je planning kwijt zijn zodra je die lijsten uit zet.
            // Alleen waar JIJ of je VTC zich voor heeft aangemeld, en alleen
            // de komende maand. Anders is het geen planning maar een lijst.
            {
                const auto planning = MijnConvooien();
                if( !planning.empty() )
                {
                    // Grens op een maand vooruit, in dezelfde tekstvorm als
                    // de API zijn tijden geeft -- zo is vergelijken genoeg.
                    std::string grens;
                    {
                        const std::time_t over = std::time( nullptr ) + 31 * 24 * 3600;
                        std::tm tmBuf{};
                    #if defined( _WIN32 )
                        gmtime_s( &tmBuf, &over );
                    #else
                        gmtime_r( &over, &tmBuf );
                    #endif
                        char buf[ 32 ];
                        std::snprintf( buf, sizeof( buf ), "%04d-%02d-%02d 23:59:59",
                                        tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday );
                        grens = buf;
                    }

                    std::vector<EvenementInfo> binnenMaand;
                    for( const auto &e : planning )
                    {
                        if( !e.startTijd.empty() && e.startTijd > grens ) break; // al gesorteerd
                        binnenMaand.push_back( e );
                        if( binnenMaand.size() >= 6 ) break;
                    }

                    if( !binnenMaand.empty() )
                    {
                        const ImGuiStyle &stP = ImGui::GetStyle();
                        const float regelP = ImGui::GetTextLineHeight() + 4.0f;
                        const int aantalP = (int)binnenMaand.size();

                        ImGui::Spacing();
                        ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
                        ImGui::BeginChild( "vtc_planning",
                                            ImVec2( 0, stP.WindowPadding.y * 2.0f
                                                        + ( aantalP + 1 ) * regelP + 4.0f ),
                                            true, ImGuiWindowFlags_NoScrollbar );
                        TekstGedimd( T( "JOUW PLANNING" ) );
                        for( const auto &e : binnenMaand )
                        {
                            // Eén regel per convooi: datum en tijd voorop,
                            // dan de naam. Compacter dan twee regels, en je
                            // leest een planning toch op datum.
                            std::string regel = e.startTijd.size() >= 16
                                                    ? e.startTijd.substr( 5, 11 ) // MM-DD HH:MM
                                                    : e.startTijd;
                            regel += "  " + e.naam;
                            TekstS( regel.c_str() );
                            if( ImGui::IsItemHovered() )
                            {
                                std::string tip = e.naam;
                                if( !e.vertrek.empty() )
                                {
                                    tip += "\n" + e.vertrek;
                                    if( !e.aankomst.empty() ) tip += " -> " + e.aankomst;
                                }
                                if( !e.server.empty() ) tip += "\n" + e.server;
                                ImGui::SetTooltip( "%s", tip.c_str() );
                            }
                        }
                        ImGui::EndChild();
                        ImGui::PopStyleColor();
                    }
                }
            }


            if( !aan )
            {
                if( m_kleinFont ) { ImGui::PopFont(); m_kleinFontActief = false; }
                return; // uit: verder niets te tonen
            }

            // --- Servers ---
            const std::vector<ServerInfo> servers = m_webApi.Servers();
            ImGui::Spacing();
            if( servers.empty() )
            {
                TekstGedimd( ( std::string( T( "Servers: " ) ) + m_webApi.Status() ).c_str() );
            }
            else
            {
                const ImGuiStyle &stW = ImGui::GetStyle();
                const float regel = ImGui::GetTextLineHeight() + 4.0f;

                int getoond = 0;
                for( const ServerInfo &s : servers ) if( s.spel == "ETS2" ) ++getoond;
                if( getoond > 10 ) getoond = 10;

                ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
                ImGui::BeginChild( "api_servers",
                                    ImVec2( 0, stW.WindowPadding.y * 2.0f + ( getoond + 1 ) * regel + 4.0f ),
                                    true, ImGuiWindowFlags_NoScrollbar );
                TekstGedimd( T( "SERVERS (ETS2)" ) );

                int rij = 0;
                for( const ServerInfo &s : servers )
                {
                    // Alleen ETS2 -- ATS-servers zeggen je hier niets.
                    if( s.spel != "ETS2" ) continue;
                    if( ++rij > 10 ) break;

                    const float breed = ImGui::GetContentRegionAvail().x;
                    TekstS( s.naam.c_str(), s.online ? IM_COL32( 255, 255, 255, 255 )
                                                      : IM_COL32( 190, 130, 125, 255 ) );

                    // Als std::string opbouwen, NIET met snprintf en een
                    // tijdelijke .c_str(). Zo'n tijdelijke tekst wordt
                    // opgeruimd voordat snprintf hem leest -- dat is precies
                    // het soort losse pointer dat de overlay eerder liet
                    // crashen.
                    std::string rechtsTekst = std::to_string( s.spelers ) + T( " spelers" );
                    if( s.wachtrij > 0 ) rechtsTekst += T( "  wachtrij " ) + std::to_string( s.wachtrij );
                    rechtsTekst += s.collisions ? T( "  botsingen" ) : T( "  geen botsingen" );
                    if( s.snelheidsbegrenzer ) rechtsTekst += T( "  limiter" );
                    const char *rechts = rechtsTekst.c_str();

                    const float rb = ImGui::CalcTextSize( rechts ).x;
                    ImGui::SameLine( 0.0f, 0.0f );
                    ImGui::SetCursorPosX( ImGui::GetCursorPosX() +
                                           std::max( 8.0f, breed - rb - ImGui::GetCursorPosX() + stW.WindowPadding.x ) );
                    TekstGedimd( rechts );
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }

            // --- Evenementen ---
            const std::vector<EvenementInfo> evenementen = m_webApi.Evenementen();
            ImGui::Spacing();
            if( !evenementen.empty() )
            {
                const ImGuiStyle &stE = ImGui::GetStyle();
                const float regelE = ImGui::GetTextLineHeight() + 4.0f;
                const int aantal = (int)std::min<std::size_t>( evenementen.size(), 5 );

                ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
                // Met vinkjes erbij is elke regel iets hoger dan de tekst
                // alleen; anders valt de laatste net buiten het vak.
                const float extraE = m_eigenConvooien ? ( aantal * 6.0f ) : 0.0f;
                ImGui::BeginChild( "api_evenementen",
                                    ImVec2( 0, stE.WindowPadding.y * 2.0f
                                                + ( aantal * 2 + 1 ) * regelE + 4.0f + extraE ),
                                    true, ImGuiWindowFlags_NoScrollbar );
                TekstGedimd( T( "AANKOMENDE EVENEMENTEN" ) );

                int n = 0;
                for( const EvenementInfo &e : evenementen )
                {
                    if( ++n > 5 ) break;

                    // Vinkje om zelf aan te geven dat je hier heen gaat.
                    // Alleen zichtbaar als je dat aan hebt staan, anders
                    // kost het onnodig ruimte.
                    if( m_eigenConvooien && e.id != 0 )
                    {
                        bool aan = IsAangevinkt( e.id );
                        ImGui::PushID( e.id );
                        if( ImGui::Checkbox( "##ganaar", &aan ) ) ZetAangevinkt( e, aan );
                        ImGui::PopID();
                        if( ImGui::IsItemHovered() ) ImGui::SetTooltip( T( "Ik ga hier heen" ) );
                        ImGui::SameLine();
                    }
                    TekstS( e.naam.c_str() );

                    std::string onder = e.startTijd;
                    if( !e.vertrek.empty() )
                    {
                        onder += "  " + e.vertrek;
                        if( !e.aankomst.empty() ) onder += " -> " + e.aankomst;
                    }
                    if( !e.server.empty() ) onder += "  (" + e.server + ")";
                    TekstGedimd( onder.c_str() );
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }

            if( m_kleinFont ) { ImGui::PopFont(); m_kleinFontActief = false; }
        }

    }

    void Overlay::TekenIncidentTab()
    {
        if( !m_incident.HeeftIncident() )
        {
            ImGui::TextDisabled( T( "Nog geen incident vastgelegd." ) );
            ImGui::TextDisabled( T( "Zodra je schade oploopt (bv. een botsing), bevriest de plugin" ) );
            ImGui::TextDisabled( T( "automatisch de laatste paar minuten aan spelersdata hierin." ) );
            return;
        }

        ImGui::TextColored( ImVec4( 0.9f, 0.4f, 0.35f, 1.0f ), "%s", T( "INCIDENT VASTGELEGD" ) );
        ImGui::TextDisabled( T( "Vermoedelijk betrokken: %s" ), m_incident.VermoedelijkeSpelerId().c_str() );
        ImGui::TextDisabled( T( "(dichtstbijzijnde speler op het moment van schade -- geen garantie dat dit de dader is)" ) );
        ImGui::Spacing();

        int aantalFrames = m_incident.AantalFrames();
        if( m_incidentFrameIndex >= aantalFrames ) m_incidentFrameIndex = aantalFrames - 1;
        if( m_incidentFrameIndex < 0 ) m_incidentFrameIndex = 0;

        const IncidentFrame *frame = m_incident.GeefFrame( m_incidentFrameIndex );
        if( frame == nullptr )
        {
            ImGui::TextDisabled( T( "Geen data voor dit moment." ) );
            return;
        }

        // Tijdlijn-schuif
        ImGui::SetNextItemWidth( -1 );
        ImGui::SliderInt( "##tijdlijn", &m_incidentFrameIndex, 0, std::max( 0, aantalFrames - 1 ), frame->tijdLabel.c_str() );
        ImGui::Spacing();

        // Minimap voor dit moment (zelfde stijl als Spelers-tab: straal
        // klopt, hoek is indicatief -- zie de opmerking in PlayersNearby.hxx)
        const float mapGrootte = 180.0f;
        ImVec2 startPos = ImGui::GetCursorScreenPos();
        ImVec2 midden = ImVec2( startPos.x + mapGrootte / 2, startPos.y + mapGrootte / 2 );
        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled( startPos, ImVec2( startPos.x + mapGrootte, startPos.y + mapGrootte ),
                              IM_COL32( 12, 14, 12, 235 ), 10.0f );
        draw->AddCircleFilled( midden, 5.0f, AccentKleurU32( 1.0f ) );
        draw->AddCircle( midden, 5.0f, IM_COL32( 0, 0, 0, 150 ), 12, 1.5f );

        const float bereikMeter = 800.0f;
        float maxStraal = mapGrootte / 2 - 10;
        for( std::size_t i = 0; i < frame->spelers.size(); ++i )
        {
            const SpelerRecord &s = frame->spelers[ i ];
            float straal = std::min( s.afstandMeter / bereikMeter, 1.0f ) * maxStraal;
            float hoek = static_cast<float>( i ) * ( 2.0f * 3.14159265f / std::max<std::size_t>( frame->spelers.size(), 1 ) );
            ImVec2 punt( midden.x + straal * cosf( hoek ), midden.y + straal * sinf( hoek ) );
            draw->AddCircleFilled( punt, 4.0f, IM_COL32( 226, 85, 74, 255 ) );
        }
        draw->AddRect( startPos, ImVec2( startPos.x + mapGrootte, startPos.y + mapGrootte ),
                        AccentKleurU32( 0.5f ), 10.0f, 0, 1.5f );
        ImGui::Dummy( ImVec2( mapGrootte, mapGrootte ) );

        ImGui::Spacing();
        ImGui::Text( T( "Spelers op dit moment (%d):" ), (int)frame->spelers.size() );
        ImGui::BeginChild( "incident_spelers", ImVec2( 0, 140 ), false );
        for( const SpelerRecord &s : frame->spelers )
        {
            ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
            ImGui::BeginChild( ( "inc_" + std::to_string( s.spelerId ) + "_" + std::to_string( m_incidentFrameIndex ) ).c_str(),
                                ImVec2( 0, 70 ), true, ImGuiWindowFlags_NoScrollbar );
            ImGui::Text( "%s", s.gebruikersnaam.c_str() );
            ImGui::SameLine( ImGui::GetWindowWidth() - 90 );
            ImGui::Text( "%.0f m", s.afstandMeter );
            ImGui::SameLine( ImGui::GetWindowWidth() - 30 );
            TekenSpelerContextKnop( s, "incident_" + std::to_string( s.spelerId ) );
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
    }

    // --- VTC-instellingen bewaren ------------------------------------------
    // Eigen bestand naast de andere: vtc.json. Zo blijft instellingen.json
    // van de brandstofprijs, en kun je de VTC-kant weggooien zonder de rest
    // kwijt te raken.
    namespace
    {
        std::filesystem::path VtcBestand()
        {
            std::filesystem::path pad;
            if( const char *appdata = std::getenv( "APPDATA" ) ) pad = appdata;
            else pad = std::filesystem::current_path();
            pad /= "CabNavi";
            std::error_code ec;
            std::filesystem::create_directories( pad, ec );
            return pad / "vtc.json";
        }
    }

    void Overlay::LaadVtcInstellingen()
    {
        try
        {
            std::ifstream in( VtcBestand() );
            if( !in ) return;
            json j;
            in >> j;
            m_vtcAan = j.value( "aan", false );
            m_vtcId = j.value( "vtc_id", 0 );
            m_vtcTagsBijSpelers = j.value( "tags_bij_spelers", true );
            m_vtcRadarMarkering = j.value( "radar_markering", false );
            m_vtcConvooienTonen = j.value( "convooien_tonen", true );
            m_vtcSpelersOpzoeken = j.value( "spelers_opzoeken", false );
            m_eigenConvooien = j.value( "eigen_convooien", false );

            // Aangevinkte convooien terughalen. Compleet bewaard, niet
            // alleen het nummer: zo blijft je planning kloppen ook als de
            // API-lijst dat evenement niet meer teruggeeft.
            m_aangevinkt.clear();
            if( j.contains( "aangevinkt" ) && j[ "aangevinkt" ].is_array() )
            {
                for( const auto &item : j[ "aangevinkt" ] )
                {
                    EvenementInfo e;
                    e.id = item.value( "id", 0 );
                    e.naam = item.value( "naam", std::string() );
                    e.startTijd = item.value( "start", std::string() );
                    e.vertrek = item.value( "vertrek", std::string() );
                    e.aankomst = item.value( "aankomst", std::string() );
                    e.server = item.value( "server", std::string() );
                    if( e.id != 0 ) m_aangevinkt.push_back( std::move( e ) );
                }
            }
            const std::string tags = j.value( "tags", std::string() );
            std::snprintf( m_vtcTagsBuffer, sizeof( m_vtcTagsBuffer ), "%s", tags.c_str() );
            m_webApi.ZetVtc( m_vtcAan, m_vtcId );
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "gebeurt", std::string( "vtc.json niet te lezen: " ) + ex.what() );
        }
    }

    void Overlay::SlaVtcInstellingenOp()
    {
        try
        {
            json j;
            j[ "aan" ] = m_vtcAan;
            j[ "vtc_id" ] = m_vtcId;
            j[ "tags_bij_spelers" ] = m_vtcTagsBijSpelers;
            j[ "radar_markering" ] = m_vtcRadarMarkering;
            j[ "convooien_tonen" ] = m_vtcConvooienTonen;
            j[ "eigen_convooien" ] = m_eigenConvooien;
            {
                json lijst = json::array();
                for( const auto &e : m_aangevinkt )
                {
                    json x;
                    x[ "id" ] = e.id;
                    x[ "naam" ] = e.naam;
                    x[ "start" ] = e.startTijd;
                    x[ "vertrek" ] = e.vertrek;
                    x[ "aankomst" ] = e.aankomst;
                    x[ "server" ] = e.server;
                    lijst.push_back( x );
                }
                j[ "aangevinkt" ] = lijst;
            }
            j[ "spelers_opzoeken" ] = m_vtcSpelersOpzoeken;
            j[ "eigen_convooien" ] = m_eigenConvooien;
            {
                json lijst = json::array();
                for( const auto &e : m_aangevinkt )
                {
                    json x;
                    x[ "id" ] = e.id;
                    x[ "naam" ] = e.naam;
                    x[ "start" ] = e.startTijd;
                    x[ "vertrek" ] = e.vertrek;
                    x[ "aankomst" ] = e.aankomst;
                    x[ "server" ] = e.server;
                    lijst.push_back( x );
                }
                j[ "aangevinkt" ] = lijst;
            }
            j[ "tags" ] = std::string( m_vtcTagsBuffer );
            std::ofstream uit( VtcBestand() );
            if( uit ) uit << j.dump( 2 );
        }
        catch( const std::exception &ex )
        {
            Logboek::Schrijf( "gebeurt", std::string( "vtc.json niet te schrijven: " ) + ex.what() );
        }
    }

    // Hoort deze speler bij jouw VTC? We vergelijken met de tags die je in
    // het VTC-instellingen-tabblad hebt ingevuld, gescheiden door komma's.
    //
    // Waarom op de TAG en niet via de Web API: de SDK geeft de tag die in
    // het spel voor iemands naam staat al mee, en bij VTC-leden is dat
    // vrijwel altijd hun bedrijfstag. Per speler de API bevragen zou bij
    // vijftig spelers in beeld vijftig verzoeken betekenen -- dat wil je
    // niet, en dit werkt bovendien meteen zonder internet.
    //
    // Er wordt ook naar de gebruikersnaam gekeken, want niet iedereen zet
    // zijn tag in het tagveld; sommigen typen "[WDA] Jojo" als naam.
    bool Overlay::IsAangevinkt( int evenementId ) const
    {
        for( const auto &e : m_aangevinkt ) if( e.id == evenementId ) return true;
        return false;
    }

    void Overlay::ZetAangevinkt( const EvenementInfo &e, bool aan )
    {
        for( auto it = m_aangevinkt.begin(); it != m_aangevinkt.end(); ++it )
        {
            if( it->id == e.id )
            {
                if( !aan ) m_aangevinkt.erase( it );
                SlaVtcInstellingenOp();
                return;
            }
        }
        if( aan ) m_aangevinkt.push_back( e );
        SlaVtcInstellingenOp();
    }

    // Alles waar je heen gaat: zelf aangevinkt plus waar je VTC aan meedoet.
    // Op tijd gesorteerd, en wat al geweest is valt eruit.
    std::vector<EvenementInfo> Overlay::MijnConvooien() const
    {
        std::vector<EvenementInfo> alles;
        if( m_eigenConvooien ) alles = m_aangevinkt;

        if( m_vtcAan )
        {
            for( const auto &v : m_webApi.AangemeldeEvenementen() )
            {
                bool alGezien = false;
                for( const auto &a : alles ) if( a.id == v.id ) { alGezien = true; break; }
                if( !alGezien ) alles.push_back( v );
            }
        }

        // Voorbij? Weglaten. Zelfde tekstvergelijking als elders: met het
        // jaar vooraan is dat meteen een datumvergelijking.
        std::string nuUtc;
        {
            const std::time_t nu = std::time( nullptr );
            std::tm tmBuf{};
        #if defined( _WIN32 )
            gmtime_s( &tmBuf, &nu );
        #else
            gmtime_r( &nu, &tmBuf );
        #endif
            char buf[ 32 ];
            std::snprintf( buf, sizeof( buf ), "%04d-%02d-%02d %02d:%02d:%02d",
                            tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
                            tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec );
            nuUtc = buf;
        }

        std::vector<EvenementInfo> nog;
        for( auto &e : alles )
        {
            if( !e.startTijd.empty() && e.startTijd < nuUtc ) continue;
            nog.push_back( e );
        }
        std::sort( nog.begin(), nog.end(),
                    []( const EvenementInfo &a, const EvenementInfo &b )
                    { return a.startTijd < b.startTijd; } );
        return nog;
    }

    std::string Overlay::ConvooiHerinnering() const
    {
        // Eén van de twee is genoeg: je eigen aanmeldingen of die van je VTC.
        if( !m_vtcAan && !m_eigenConvooien ) return {};

        // Nu, in dezelfde vorm als de API zijn tijden geeft.
        const std::time_t nu = std::time( nullptr );
        std::tm nuTm{};
    #if defined( _WIN32 )
        gmtime_s( &nuTm, &nu );
    #else
        gmtime_r( &nu, &nuTm );
    #endif

        for( const auto &e : MijnConvooien() )
        {
            if( e.startTijd.size() < 19 ) continue;

            std::tm t{};
            t.tm_year = std::atoi( e.startTijd.substr( 0, 4 ).c_str() ) - 1900;
            t.tm_mon  = std::atoi( e.startTijd.substr( 5, 2 ).c_str() ) - 1;
            t.tm_mday = std::atoi( e.startTijd.substr( 8, 2 ).c_str() );
            t.tm_hour = std::atoi( e.startTijd.substr( 11, 2 ).c_str() );
            t.tm_min  = std::atoi( e.startTijd.substr( 14, 2 ).c_str() );
            t.tm_sec  = std::atoi( e.startTijd.substr( 17, 2 ).c_str() );

        #if defined( _WIN32 )
            const std::time_t start = _mkgmtime( &t );
        #else
            const std::time_t start = timegm( &t );
        #endif
            if( start == static_cast<std::time_t>( -1 ) ) continue;

            const double minuten = std::difftime( start, nu ) / 60.0;
            if( minuten < 0.0 || minuten > HERINNERING_MINUTEN ) continue;

            std::string regel = std::string( T( "Convooi over " ) ) + std::to_string( (int)minuten ) + T( " min" );
            if( !e.vertrek.empty() ) regel += " -- " + e.vertrek;
            return regel;
        }
        return {};
    }

    void Overlay::TekenConvooiHerinnering()
    {
        const std::string regel = ConvooiHerinnering();
        if( regel.empty() ) return; // niets te melden, dus ook geen lege balk

        if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
        TekstS( regel.c_str(), IM_COL32( 91, 141, 239, 255 ) );
        if( m_kleinFont ) ImGui::PopFont();
    }

    void Overlay::TekenZuinigheid()
    {
        if( !m_zuinigheidTonen ) return;

        // Gemiddelde uit je eigen logboek. Alleen ritten waar zowel liters
        // als kilometers bekend zijn tellen mee -- zie TripLogger.
        const Totals tot = m_logger.GeefTotalen();
        if( tot.gemetenKm < 50.0 || tot.gemetenLiters <= 0.0 ) return; // te weinig om iets te zeggen

        const double gemiddeldPer100 = tot.gemetenLiters / tot.gemetenKm * 100.0;
        if( gemiddeldPer100 <= 0.0 ) return;

        // En deze rit. Dat cijfer is er pas na een stukje rijden.
        const TruckTracking::VoertuigStatus vs = m_vracht.HuidigeVoertuigStatus();
        const double ditPer100 = vs.verbruikGemiddeldLiterPer100Km;
        if( ditPer100 <= 0.0 ) return;

        // In km/l, dezelfde eenheid als het kaartje en je dashboard.
        const double gemKmpl = 100.0 / gemiddeldPer100;
        const double ditKmpl = 100.0 / ditPer100;

        // Minder liters per kilometer is BETER, dus een lager per100 is winst.
        const double procent = ( gemiddeldPer100 - ditPer100 ) / gemiddeldPer100 * 100.0;

        char regel[ 160 ];
        ImU32 kleur;
        if( procent > 3.0 )
        {
            std::snprintf( regel, sizeof( regel ),
                            T( "Deze rit %.0f%% zuiniger dan je gemiddelde (%.1f tegen %.1f km/l)" ),
                            procent, ditKmpl, gemKmpl );
            kleur = IM_COL32( 63, 176, 138, 255 );
        }
        else if( procent < -3.0 )
        {
            std::snprintf( regel, sizeof( regel ),
                            T( "Deze rit %.0f%% onzuiniger dan je gemiddelde (%.1f tegen %.1f km/l)" ),
                            -procent, ditKmpl, gemKmpl );
            kleur = IM_COL32( 226, 85, 74, 255 );
        }
        else
        {
            // Klein verschil hoeft niet op te vallen.
            std::snprintf( regel, sizeof( regel ),
                            T( "Deze rit gelijk aan je gemiddelde (%.1f km/l)" ), gemKmpl );
            kleur = IM_COL32( 154, 157, 162, 255 );
        }

        if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
        TekstS( regel, kleur );
        if( m_kleinFont ) ImGui::PopFont();
    }

    bool Overlay::IsPatron( const SpelerRecord &s ) const
    {
        // Eerst de API, want die klopt aantoonbaar. Nog niet opgezocht of
        // VTC-integratie uit? Dan de SDK-vlag, dat is beter dan niets.
        if( s.accountId != 0 )
        {
            const int viaApi = m_webApi.SpelerIsPatron( s.accountId );
            if( viaApi >= 0 ) return ( viaApi == 1 );
        }
        return s.isPatron;
    }

    bool Overlay::IsEigenVtc( const SpelerRecord &s ) const
    {
        // 1) Het VTC-NUMMER is de enige harde bron. Een tag is tekst die
        //    iedereen kan intypen; een nummer krijgt een bedrijf één keer van
        //    TruckersMP. Is deze speler al opgezocht, dan is dit het antwoord
        //    en kijken we verder nergens naar.
        if( m_vtcId > 0 && s.accountId != 0 )
        {
            const int gevonden = m_webApi.SpelerVtcId( s.accountId );
            if( gevonden >= 0 ) return ( gevonden == m_vtcId );
        }

        // 2) Nog niet opgezocht (of geen VTC ingesteld): terugvallen op de
        //    tag. Zo zie je meteen iets, en zodra de opzoeking binnen is
        //    wordt het vanzelf preciezer.
        if( m_vtcTagsBuffer[ 0 ] == '\0' ) return false;

        auto naarKleineLetters = []( std::string t )
        {
            for( char &c : t ) c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
            return t;
        };
        const std::string tag = naarKleineLetters( s.tagTekst );
        const std::string naam = naarKleineLetters( s.gebruikersnaam );

        // Lijst met komma's uit elkaar halen, spaties eromheen negeren.
        std::string huidig;
        const std::string alles = naarKleineLetters( m_vtcTagsBuffer );
        for( size_t i = 0; i <= alles.size(); ++i )
        {
            if( i < alles.size() && alles[ i ] != ',' )
            {
                huidig += alles[ i ];
                continue;
            }
            while( !huidig.empty() && huidig.front() == ' ' ) huidig.erase( huidig.begin() );
            while( !huidig.empty() && huidig.back() == ' ' ) huidig.pop_back();

            if( !huidig.empty() )
            {
                if( !tag.empty() && tag.find( huidig ) != std::string::npos ) return true;
                if( naam.find( huidig ) != std::string::npos ) return true;
            }
            huidig.clear();
        }
        return false;
    }

    void Overlay::TekenVtcTab()
    {
        if( !m_vtcAan || m_vtcId <= 0 )
        {
            ImGui::Spacing();
            KopBalk( T( "VTC" ) );
            TekstGedimd( T( "Nog niet ingesteld -- zie het tabblad hiernaast." ) );
            return;
        }

        const VtcInfo vtc = m_webApi.Vtc();

        ImGui::Spacing();
        KopBalk( T( "MIJN VTC" ) );

        if( !vtc.geldig )
        {
            TekstGedimd( m_webApi.VtcStatus().c_str() );
            return;
        }

        // Kopregel: naam en ledenaantal, zelfde stat-kaartjes als overal.
        {
            const float halfB = ( ImGui::GetContentRegionAvail().x - 8 ) / 2.0f;
            std::string onder = vtc.tag.empty() ? std::string() : ( std::string( T( "tag " ) ) + vtc.tag );
            StatKaart( T( "BEDRIJF" ), vtc.naam, halfB, onder.c_str() );
            ImGui::SameLine();
            StatKaart( T( "LEDEN" ), std::to_string( vtc.leden ), halfB, T( "chauffeurs" ) );
        }

        if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }

        // Berekende hoogte per blok in plaats van automatisch meegroeien.
        // Dat laatste levert hier -- net als bij UITERLIJK -- een veel te
        // hoog, half leeg vak op. Kop + één regel per item is precies genoeg.
        const ImGuiStyle &stV = ImGui::GetStyle();
        const float regelV = ImGui::GetTextLineHeight() + 4.0f;

        auto lijstBlok = [ & ]( const char *id, const char *kop,
                                 const std::vector<EvenementInfo> &items, int maxItems )
        {
            if( items.empty() ) return; // niets te melden, dan ook geen leeg vak

            const int n = (int)std::min<std::size_t>( items.size(), (std::size_t)maxItems );
            ImGui::Spacing();
            ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
            ImGui::BeginChild( id,
                                ImVec2( 0, stV.WindowPadding.y * 2.0f + ( n + 1 ) * regelV + 4.0f ),
                                true, ImGuiWindowFlags_NoScrollbar );
            TekstGedimd( kop );
            for( int i = 0; i < n; ++i )
            {
                const EvenementInfo &e = items[ i ];
                // Datum voorop, dan de naam -- één regel, want een lijst
                // lees je op datum. De rest komt bij aanwijzen.
                std::string regel = e.startTijd.size() >= 16 ? e.startTijd.substr( 5, 11 )
                                                              : e.startTijd;
                regel += "  " + e.naam;
                TekstS( regel.c_str() );
                if( ImGui::IsItemHovered() )
                {
                    std::string tip = e.naam;
                    if( !e.vertrek.empty() )
                    {
                        tip += "\n" + e.vertrek;
                        if( !e.aankomst.empty() ) tip += " -> " + e.aankomst;
                    }
                    if( !e.server.empty() ) tip += "\n" + e.server;
                    ImGui::SetTooltip( "%s", tip.c_str() );
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        };

        if( m_vtcConvooienTonen )
        {
            // Zelf georganiseerd, en waar het bedrijf aan meedoet. Dat zijn
            // twee verschillende dingen: de meeste VTC's rijden vaker mee dan
            // dat ze zelf iets opzetten.
            lijstBlok( "vtc_eigen", T( "EIGEN CONVOOIEN" ), m_webApi.VtcEvenementen(), 4 );
            lijstBlok( "vtc_mee", T( "MELDT ZICH AAN VOOR" ), m_webApi.VtcAangemeld(), 4 );
        }

        // Nieuws heeft een eigen vorm (titel + datum), dus die blijft apart.
        const auto nieuwsberichten = m_webApi.VtcNieuws();
        if( !nieuwsberichten.empty() )
        {
            const int n = (int)std::min<std::size_t>( nieuwsberichten.size(), 3 );
            ImGui::Spacing();
            ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
            ImGui::BeginChild( "vtc_nieuws",
                                ImVec2( 0, stV.WindowPadding.y * 2.0f + ( n * 2 + 1 ) * regelV + 4.0f ),
                                true, ImGuiWindowFlags_NoScrollbar );
            TekstGedimd( T( "NIEUWS" ) );
            for( int i = 0; i < n; ++i )
            {
                TekstS( nieuwsberichten[ i ].titel.c_str() );
                TekstGedimd( nieuwsberichten[ i ].datum.c_str() );
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        if( m_kleinFont ) { ImGui::PopFont(); m_kleinFontActief = false; }
    }

    void Overlay::TekenVtcInstellingenTab()
    {
        // Vaste hoogte, net als bij UITERLIJK: automatisch meegroeien gaf
        // hier een veel te hoog, half leeg vak (gezien 30-08).
        // Eerst je persoonlijke aanmeldingen: die werken zonder VTC, dus die
        // horen niet achter de VTC-instellingen weggestopt te zitten.
        SectieStart( "MIJN CONVOOIEN", ImVec4( 0.36f, 0.61f, 0.94f, 1.0f ),
                      SectieHoogte( 2, 1 ) );
        if( ImGui::Checkbox( T( "Zelf aanvinken" ), &m_eigenConvooien ) )
        {
            SlaVtcInstellingenOp();
        }
        TekstGedimd( T( "Vinkjes bij Statistieken" ) );
        TekstGedimdFmt( T( "Nu aangevinkt: %d" ), (int)m_aangevinkt.size() );
        SectieEind();

        SectieStart( "VERBINDING", ImVec4( 0.36f, 0.61f, 0.94f, 1.0f ),
                      SectieHoogte( 2, 2, 1 ) );

        if( ImGui::Checkbox( T( "VTC-gegevens ophalen" ), &m_vtcAan ) )
        {
            m_webApi.ZetVtc( m_vtcAan, m_vtcId );
            SlaVtcInstellingenOp();
        }

        if( !m_vtcIdBufferGeladen )
        {
            std::snprintf( m_vtcIdBuffer, sizeof( m_vtcIdBuffer ), "%d", m_vtcId );
            m_vtcIdBufferGeladen = true;
        }

        ImGui::SetNextItemWidth( VeldBreedte() );
        if( ImGui::InputText( T( "VTC-nummer" ), m_vtcIdBuffer, sizeof( m_vtcIdBuffer ),
                               ImGuiInputTextFlags_CharsDecimal ) )
        {
            m_vtcId = std::atoi( m_vtcIdBuffer );
            m_webApi.ZetVtc( m_vtcAan, m_vtcId );
            SlaVtcInstellingenOp();
        }
        TekstGedimd( T( "truckersmp.com/vtc/<nummer>" ) );

        ImGui::Spacing();
        TekstGedimd( m_webApi.VtcStatus().c_str() );
        SectieEind();

        SectieStart( "WEERGAVE", ImVec4( 0.36f, 0.61f, 0.94f, 1.0f ),
                      SectieHoogte( 0, 2 ) );
        if( ImGui::Checkbox( T( "Tag bij spelers tonen" ), &m_vtcTagsBijSpelers ) ) SlaVtcInstellingenOp();
        if( ImGui::Checkbox( T( "Convooien tonen" ), &m_vtcConvooienTonen ) ) SlaVtcInstellingenOp();
        SectieEind();

        SectieStart( "MARKEREN", ImVec4( 0.36f, 0.61f, 0.94f, 1.0f ),
                      SectieHoogte( 4, 4, 1 ) );
        if( ImGui::Checkbox( T( "Eigen VTC markeren" ), &m_vtcRadarMarkering ) ) SlaVtcInstellingenOp();

        if( ImGui::Checkbox( T( "Spelers opzoeken" ), &m_vtcSpelersOpzoeken ) )
        {
            SlaVtcInstellingenOp();
        }
        TekstGedimd( T( "Uit = alleen tags" ) );

        ImGui::SetNextItemWidth( VeldBreedte() );
        if( ImGui::InputText( T( "Tags" ), m_vtcTagsBuffer, sizeof( m_vtcTagsBuffer ) ) )
        {
            SlaVtcInstellingenOp();
        }
        TekstGedimd( T( "Meerdere met komma's" ) );
        TekstGedimd( T( "Tot het nummer bekend is" ) );

        // Zichtbaar maken dat het opzoeken loopt. Zonder dit moet je raden
        // of er iets gebeurt.
        {
            int opgezocht = 0, inRij = 0;
            m_webApi.OpzoekStand( opgezocht, inRij );
            char regel[ 96 ];
            std::snprintf( regel, sizeof( regel ), T( "Opgezocht: %d  |  in de rij: %d" ),
                            opgezocht, inRij );
            TekstGedimd( regel );
        }

        // Snelknop: de tag van je eigen VTC invullen zodra die is opgehaald.
        // Scheelt overtypen, en je kunt hem daarna nog aanpassen.
        const VtcInfo eigen = m_webApi.Vtc();
        if( eigen.geldig && !eigen.tag.empty() )
        {
            ImGui::Spacing();
            const std::string knop = std::string( T( "Invullen: " ) ) + eigen.tag;
            if( ImGui::Button( knop.c_str() ) )
            {
                std::snprintf( m_vtcTagsBuffer, sizeof( m_vtcTagsBuffer ), "%s", eigen.tag.c_str() );
                SlaVtcInstellingenOp();
            }
        }
        SectieEind();
    }
}

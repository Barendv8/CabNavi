#include "Overlay.hxx"
#include "Kaartdata.hxx"

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
    // The real PC time as text. With metMs the milliseconds are included
    // -- only useful if you want to lay a screen recording next to
    // debug.log, because a gear change takes less than a second.
    std::string HuidigeKlokTekst( bool metMs )
    {
        const auto nu = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t( nu );
        std::tm tmBuf{};
    #if defined( _WIN32 )
        localtime_s( &tmBuf, &t );
    #else
        localtime_r( &t, &tmBuf );
    #endif

        char buf[ 32 ];
        if( metMs )
        {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                nu.time_since_epoch() ).count() % 1000;
            std::snprintf( buf, sizeof( buf ), "%02d:%02d:%02d.%03d",
                           tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec,
                           static_cast<int>( ms ) );
        }
        else
        {
            std::snprintf( buf, sizeof( buf ), "%02d:%02d:%02d",
                           tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec );
        }
        return buf;
    }

    // "5 min" / "1 uur 20 min" / "onbekend" (for a negative value)
    std::string FormatteerMinuten( double minuten )
    {
        if( minuten < 0.0 ) return Ritten::T( "onbekend" );

        // Round UP. For a countdown that is more honest: with 20 seconds left
        // it should say "1 min" and not "0 min". Plain rounding already
        // dropped the last bit.
        int totaal = static_cast<int>( std::ceil( minuten - 0.0001 ) );
        int uren = totaal / 60;
        int rest = totaal % 60;
        if( uren <= 0 ) return std::to_string( rest ) + Ritten::T( " min" );
        return std::to_string( uren ) + Ritten::T( " uur " ) + std::to_string( rest ) + Ritten::T( " min" );
    }

    // Turns "in N real minutes" into a wall-clock time, e.g. "21:47".
    // Deliberately the real system clock and not game time: if you want
    // to know whether you are done before dinner, it is about your clock,
    // not the one in the game.
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

    // The game reports the reason for a fine as a short English code in
    // the "fine.offence" attribute. This list covers the offences ETS2
    // knows (the last three were added in a later SDK version). Unknown
    // codes are simply shown raw -- better a code you can look up
    // yourself than a wrong translation.
    // --- Readability on a changing game background --------------------
    //
    // The problem: the HUD is semi-transparent (deliberately -- you want
    // to see where you drive), but light text then disappears against
    // light grass or a concrete slab. The solution is NOT to make the
    // panel darker, but to let the letters themselves "stand off": we
    // draw every line first in near-black with a 1px offset, and the real
    // colour on top. That gives a rim of contrast that moves with the
    // letter, whatever is underneath. This is the standard trick for game
    // HUDs.
    // Dimmed grey for labels and captions -- replaces ImGui::TextDisabled,
    // which cannot draw a shadow.
    // Labels, units and explanation: plain white. They differ from the
    // values by size and by the lighter shadow (see TekstS), no longer by
    // a grey tint -- grey sank too far into the transparent card with the
    // game underneath.
    const ImU32 KLEUR_GEDIMD = IM_COL32( 255, 255, 255, 255 );

    // Text with a dark contour around it. This used to be one shadow at
    // the bottom right, but then the left/top side of every letter stays
    // unprotected: if something light happens to be there in the game,
    // that side of the letter still "fades". An all-round contour (four
    // corners + four sides) solves that -- the same trick as subtitles.
    //
    // That full contour is only meant for the VALUES (the big figures). On
    // small grey labels it makes the letters thicker and darker, so they
    // stand out as much as the figures -- exactly what a label must not
    // do. Hence `zwareContour`.
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
            // Only a soft shadow at the bottom right: enough to separate the
            // letters from the background, not enough to make them bold.
            dl->AddText( ImVec2( p.x + 1.0f, p.y + 1.0f ), IM_COL32( 0, 0, 0, 130 ), tekst );
        }

        dl->AddText( p, kleur, tekst );
        ImGui::Dummy( ImGui::CalcTextSize( tekst ) );
    }

    // Small, understated text: card headers, units, explanation. Same
    // look as ImGui's own TextDisabled.
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

    // Formatted variant of TekstGedimd: same white colour, but with the
    // light shadow instead of the full contour. Since labels and values
    // are both pure white, the contour weight is the only difference -- so
    // it must be chosen explicitly and can no longer be derived from the
    // colour.
    void TekstGedimdFmt( const char *fmt, ... )
    {
        char buf[ 256 ];
        va_list args;
        va_start( args, fmt );
        vsnprintf( buf, sizeof( buf ), fmt, args );
        va_end( args );
        TekstS( buf, KLEUR_GEDIMD, false );
    }

    // 18083 -> "18.083" (Dutch thousands separator)
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
    // Own (small) mapping of Windows virtual-key codes to ImGuiKey --
    // ImGui_ImplWin32_VirtualKeyToImGuiKey turned out unavailable in this
    // ImGui version/build, so no dependency on it. Covers the keys you
    // need to type in the fuel price / webhook field: digits, dot/comma,
    // backspace, delete, arrows, enter, tab.
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
            case 0xBD: return ImGuiKey_Minus;  // -
            case 0xBE: return ImGuiKey_Period;  // .
            case 0xBC: return ImGuiKey_Comma;  // ,
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

            // Letters. Needed for the editing shortcuts in text fields: without
            // A/C/V/X/Y/Z ImGui never sees Ctrl+V, so you could not paste a
            // webhook URL -- only type it.
            case 0x41: return ImGuiKey_A;  // select all
            case 0x43: return ImGuiKey_C;  // copy
            case 0x56: return ImGuiKey_V;  // paste
            case 0x58: return ImGuiKey_X;  // cut
            case 0x59: return ImGuiKey_Y;  // redo
            case 0x5A: return ImGuiKey_Z;  // undo

            // Modifiers: without these Ctrl stays "loose" and no combination
            // works, even though the letter does arrive.
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
    static std::filesystem::path InstellingenMap();   // defined below, needed by the constructor

    Overlay::Overlay( TripLogger &logger, BusTracking &bus, TruckTracking &vracht,
                       PlayersNearby &spelers, FuelCosts &brandstof, DiscordWebhook &discord,
                       IncidentRecorder &incident )
        : m_logger( logger ), m_bus( bus ), m_vracht( vracht ), m_spelers( spelers ), m_brandstof( brandstof ),
          m_discord( discord ), m_incident( incident )
    {
        std::snprintf( m_prijsBuffer, sizeof( m_prijsBuffer ), "%.2f", m_brandstof.PrijsPerLiter() );
        LaadUiterlijk();
        LaadVtcInstellingen();

        // Both network switches default ON and are remembered in uiterlijk.json
        // (LaadUiterlijk read them, or left the defaults for a fresh install).
        m_webApi.ZetIngeschakeld( m_webApiAan );

        // Map table: a table downloaded earlier is in AppData; load it first
        // (newest wins), then look for a newer one if the switch is on.
        {
            std::string fout;
            const auto cache = InstellingenMap() / "kaartdata.json";
            std::error_code ec;
            if( std::filesystem::exists( cache, ec ) && !Kaartdata::LaadBestand( cache, fout ) )
                Logboek::Schrijf( "start", "map table cache: " + fout );
            Logboek::Schrijf( "start", "map table " + Kaartdata::Versie() + " (" + Kaartdata::Bron() + ", " + std::to_string( Kaartdata::AantalSteden() ) + " cities)" );
            if( m_kaartDownload ) Kaartdata::StartUpdate( InstellingenMap() );
        }
    }

    // %APPDATA%\CabNavi\ -- the folder where all our settings live.
    // Created if it does not exist yet.
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
            if( m_doorzichtigheid < 0.35f ) m_doorzichtigheid = 0.35f;  // floor, see Settings tab
            m_iconenDoorzichtigheid = j.value( "iconen_doorzichtigheid", m_iconenDoorzichtigheid );
            m_zuinigheidTonen = j.value( "zuinigheid_tonen", true );
            m_taal = j.value( "taal", 0 );
            m_klokTonen = j.value( "klok_tonen", true );
            m_uitgebreidLog = j.value( "uitgebreid_log", false );
            Logboek::Uitgebreid() = m_uitgebreidLog;
            m_webApiAan = j.value( "web_api_aan", true );
            m_kaartDownload = j.value( "kaartdata_download", true );
            TruckTracking::VerbruikLogInterval() =
                std::clamp( j.value( "log_interval_sec", 3.0 ), 0.5, 10.0 );
            Taal::Kies( m_taal == 1 ? TaalKeuze::Engels : TaalKeuze::Nederlands );
            if( m_iconenDoorzichtigheid < 0.4f ) m_iconenDoorzichtigheid = 0.4f;  // floor, see Settings tab
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
        j[ "klok_tonen" ] = m_klokTonen;
        j[ "uitgebreid_log" ] = m_uitgebreidLog;
        j[ "web_api_aan" ] = m_webApiAan;
        j[ "kaartdata_download" ] = m_kaartDownload;
        j[ "log_interval_sec" ] = TruckTracking::VerbruikLogInterval();
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

    // Background colour for every card in the overlay.
    //
    // WHY DARK AND NOT WHITE-TRANSPARENT: this used to be Kaartachtergrond
    // everywhere: a LIGHT, transparent haze -- not a dark panel. The game
    // must stay visible through the HUD; readability is handled by the
    // contour around the letters (see TekstS at the top), not by closing
    // the panel off.
    //
    // I made this dark for one round because light text on a light card
    // above bright grass has less contrast. Technically true, but it makes
    // the HUD heavy and that is not the look we want. The letter contour
    // compensates for that loss of contrast well enough.
    ImVec4 Overlay::KaartKleur() const
    {
        return ImVec4( 1.0f, 1.0f, 1.0f, 0.08f );
    }

    // Same transparent setup, but with a colour tint (red for a warning,
    // green for cruise control, amber for the tachograph). The strength
    // only determines how pronounced the tint is, not how dark the card
    // gets.
    ImVec4 Overlay::TintKaartKleur( const ImVec4 &tint, float sterkte ) const
    {
        return ImVec4( tint.x, tint.y, tint.z, 0.10f + 0.14f * sterkte );
    }

    void Overlay::KopBalk( const char *tekst )
    {
        TekstS( tekst, AccentKleurU32( 1.0f ) );
        // Thin accent line below the header -- gives the section a clear
        // start without a full ImGui::Separator, which looks too harsh.
        ImVec2 p = ImGui::GetCursorScreenPos();
        float breedte = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2( p.x, p.y + 1 ), ImVec2( p.x + breedte, p.y + 2 ), AccentKleurU32( 0.55f ) );
        ImGui::Dummy( ImVec2( 0, 6 ) );
    }

    float Overlay::KaartHoogte( bool metOnderschrift, bool compact ) const
    {
        const ImGuiStyle &st = ImGui::GetStyle();
        // Line height of the normal font (label + caption) and of the header
        // font (the big value). A fixed number went wrong as soon as the
        // header font turned out larger than planned: the caption fell out and
        // ImGui put a scrollbar in.
        // In compact form we reckon with the SMALL font for the labels and the
        // normal font for the value -- no header font, that is what normally
        // drives the height up.
        float klein = compact && m_kleinFont
                          ? m_kleinFont->FontSize + 2.0f
                          : ImGui::GetTextLineHeightWithSpacing();
        // In compact form the VALUE stays in the normal font -- you must be
        // able to read it at a glance. The height gain comes from the line
        // spacing (2 px instead of the default) and from the small labels, not
        // from the figure itself.
        const float compactSpatie = 2.0f;
        float groot = compact
                          ? ImGui::GetFontSize() + compactSpatie
                          : ( m_kopFont ? m_kopFont->FontSize : ImGui::GetFontSize() ) + st.ItemSpacing.y;

        float hoogte = st.WindowPadding.y * 2.0f + klein + groot;
        if( metOnderschrift ) hoogte += klein;
        return hoogte + 6.0f;  // a few pixels of air, so nothing sticks to the edge
    }

    void Overlay::StatKaart( const char *label, const std::string &waarde, float breedte,
                              const char *onderschrift, bool waarschuwing, bool compact )
    {
        ImGui::PushStyleColor( ImGuiCol_ChildBg,
            waarschuwing ? TintKaartKleur( ImVec4( 0.85f, 0.25f, 0.20f, 1.0f ), 0.5f ) : KaartKleur() );
        ImGui::BeginChild( label, ImVec2( breedte, KaartHoogte( onderschrift != nullptr, compact ) ),
                            true, ImGuiWindowFlags_NoScrollbar );

        // Tight line spacing in compact form: that is where the height gain
        // is, without touching the readability of the figures.
        if( compact ) ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( ImGui::GetStyle().ItemSpacing.x, 2.0f ) );

        // Label and caption small in compact form; that saves the most height,
        // especially with a long header like "SNELHEID" with the limit below.
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
        // Everything is computed relative to the start position, not absolute
        // from the left edge -- otherwise a second column would land on top of
        // the first.
        float startY = ImGui::GetCursorPosY();
        float startX = ImGui::GetCursorPosX();
        float beschikbaar = ( breedte > 0.0f ) ? breedte : ImGui::GetContentRegionAvail().x;
        TekstGedimd( naam );

        // Draw the bar ourselves instead of ImGui::ProgressBar: that uses an
        // opaque background colour from the theme, which would still put dark
        // blocks on your screen here.
        // All sizes below are derived from the FONT SIZE instead of set in
        // fixed pixels. Put this card in a smaller font and the percentage
        // column and bar height shrink automatically, and the freed width goes
        // to the bar. With fixed pixels it kept looking clumsy no matter how
        // small you made the text.
        // The percentage is drawn in the normal (larger) font later, so the
        // column must be measured for that too -- otherwise "100%" falls just
        // outside the card.
        if( m_kleinFontActief ) ImGui::PopFont();
        float pctBreedte = ImGui::CalcTextSize( "100%" ).x + 5.0f;
        if( m_kleinFontActief ) ImGui::PushFont( m_kleinFont );
        float balkHoogte = std::max( 5.0f, ImGui::GetFontSize() * 0.44f );
        float balkBreedte = std::max( 24.0f, beschikbaar - labelBreedte - pctBreedte );

        // Ceiling scales along: 8x the font size is a good 100px at 13pt, and
        // proportionally more at a larger font.
        float balkMaximum = ImGui::GetFontSize() * 7.0f;
        if( balkBreedte > balkMaximum ) balkBreedte = balkMaximum;

        // Align the bar vertically with the middle of the text line.
        ImGui::SetCursorPosY( startY + ( ImGui::GetTextLineHeight() - balkHoogte ) * 0.5f );
        ImGui::SetCursorPosX( startX + labelBreedte );
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        // Dark gutter with low opacity: enough to delineate the bar, not
        // enough to block the image underneath.
        dl->AddRectFilled( p, ImVec2( p.x + balkBreedte, p.y + balkHoogte ), IM_COL32( 0, 0, 0, 110 ), balkHoogte * 0.5f );

        if( percentage >= 0.0 )
        {
            float fractie = static_cast<float>( percentage / 100.0 );
            if( fractie > 1.0f ) fractie = 1.0f;

            // Green up to 10%, then amber, above 40% red -- unless the caller
            // passes its own colour (e.g. the tachograph, which is not about
            // "damage" and so has other thresholds).
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
            // The percentage in the normal (larger) font: that is the number you
            // read, the label next to it may stay small.
            if( m_kleinFontActief ) ImGui::PopFont();
            TekstS( pctTekst, kleurOverride ? IM_COL32( 255, 255, 255, 255 ) : kleur );
            if( m_kleinFontActief ) ImGui::PushFont( m_kleinFont );
        }
        else
        {
            // Unknown: empty gutter, no misleading 0%.
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

        // Debug: always write to debug.log what happens, so we do not have to
        // guess when the logo does not appear.
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
            schrijfDebug( "file not found at: " + Logboek::KortPad( pad ) );
            return;
        }
        schrijfDebug( "Bestand gevonden op: " + Logboek::KortPad( pad ) );

        int breedte = 0, hoogte = 0, kanalen = 0;
        unsigned char *pixels = stbi_load( pad.string().c_str(), &breedte, &hoogte, &kanalen, 4 );
        if( pixels == nullptr )
        {
            schrijfDebug( std::string( "stbi_load mislukt: " ) + stbi_failure_reason() );
            return;
        }
        if( m_device == nullptr )
        {
            schrijfDebug( "m_device is nullptr -- device not ready yet" );
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
        // Reuses the same loading recipe as LaadLogo(), but for six separate
        // files. Expects them in %APPDATA%\CabNavi\icons\<naam>.png -- if a
        // file is missing, that icon stays empty and the overlay falls back on
        // drawing nothing for that badge (no crash, no half overlay).
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
        Kaartdata::StopUpdate();   // never leave the download thread running past shutdown
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

        // ImGui stores window size and position in an "imgui.ini" and loads
        // it back in the next session -- exactly what you expect from a HUD.
        //
        // This was off earlier because an accidentally huge dragged window
        // kept coming back. That fix threw the baby out with the bathwater.
        // Now it is on again, but with two differences:
        //
        //   1. The file goes to %APPDATA%\CabNavi\ instead of the game
        //      folder, so it sits with your other settings and a game update
        //      does not clean it up.
        //   2. SetNextWindowSizeConstraints (see Teken()) limits the size to
        //      1400x1000, and that ALSO applies to a reloaded size. An
        //      overshot window is pulled back by itself.
        //
        // The path must stay alive as long as ImGui runs: io.IniFilename is a
        // raw pointer ImGui does not copy. Hence a member instead of a local
        // string.
        m_iniPad = ( InstellingenMap() / "imgui.ini" ).string();
        io.IniFilename = m_iniPad.c_str();

        // By default ImGui uses a very small, bare pixel font -- everything
        // looked like a "standard debug window". Segoe UI is on every Windows
        // installation (it is Windows' own system font since Vista), so we
        // load that instead, at a larger size with anti-aliasing for a clean,
        // modern feel.
        {
            ImFontConfig fontConfig;
            fontConfig.OversampleH = 3;
            fontConfig.OversampleV = 3;
            fontConfig.PixelSnapH = true;
            ImFont *font = io.Fonts->AddFontFromFileTTF(
                "C:\\Windows\\Fonts\\segoeui.ttf", 19.0f, &fontConfig );
            if( font == nullptr )
            {
                // Unlikely (Segoe UI is standard on every Windows PC), but just in
                // case: fall back on the built-in font so the overlay is never
                // completely empty.
                io.Fonts->AddFontDefault();
            }

            // Second font, larger and heavier (Semibold variant), for key figures
            // like ETA and tachograph time -- gives visual hierarchy instead of
            // everything looking equally heavy.
            ImFontConfig kopFontConfig;
            kopFontConfig.OversampleH = 3;
            kopFontConfig.OversampleV = 3;
            kopFontConfig.PixelSnapH = true;
            m_kopFont = io.Fonts->AddFontFromFileTTF(
                "C:\\Windows\\Fonts\\seguisb.ttf", 32.0f, &kopFontConfig );
            if( m_kopFont == nullptr )
            {
                // Segoe UI Semibold not found (can happen on older Windows versions)
                // -- fall back on plain Segoe UI, just larger, then you only miss the
                // extra weight.
                m_kopFont = io.Fonts->AddFontFromFileTTF(
                    "C:\\Windows\\Fonts\\segoeui.ttf", 32.0f, &kopFontConfig );
            }

            // Third font, smaller than the default 19pt. Meant for the densely
            // packed cards (damage, trailer, tachograph): short labels, bars and
            // percentages sit side by side there, and at 19pt that no longer fit
            // once the window was narrower.
            ImFontConfig kleinConfig;
            kleinConfig.OversampleH = 3;
            kleinConfig.OversampleV = 3;
            kleinConfig.PixelSnapH = true;
            m_kleinFont = io.Fonts->AddFontFromFileTTF(
                "C:\\Windows\\Fonts\\segoeui.ttf", 16.0f, &kleinConfig );
        }

        // Modern, dark theme with rounded corners -- fits an "in-game HUD"
        // look instead of a debug window.
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
        // Fallback for the first frame; overwritten every frame afterwards in
        // Teken(). Without these lines ImGui's opaque near-black sits here
        // while the window has no focus.
        stijl.Colors[ ImGuiCol_TitleBg ] = ImVec4( 0.10f, 0.22f, 0.38f, 0.85f );
        stijl.Colors[ ImGuiCol_TitleBgCollapsed ] = ImVec4( 0.10f, 0.22f, 0.38f, 0.85f );
        stijl.Colors[ ImGuiCol_Header ] = ImVec4( 0.15f, 0.35f, 0.60f, 0.6f );

        // ImGui's own Dark theme default blue out -- neutral grey for buttons
        // that are not active, so only the active tab (via the separate
        // PushStyleColor below) gets the accent colour.
        stijl.Colors[ ImGuiCol_Button ] = ImVec4( 0.22f, 0.22f, 0.24f, 1.0f );
        stijl.Colors[ ImGuiCol_ButtonHovered ] = ImVec4( 0.30f, 0.30f, 0.33f, 1.0f );
        stijl.Colors[ ImGuiCol_ButtonActive ] = ImVec4( 0.35f, 0.35f, 0.38f, 1.0f );
        stijl.Colors[ ImGuiCol_FrameBg ] = ImVec4( 0.16f, 0.16f, 0.18f, 1.0f );
        stijl.Colors[ ImGuiCol_FrameBgHovered ] = ImVec4( 0.22f, 0.22f, 0.24f, 1.0f );
        stijl.Colors[ ImGuiCol_FrameBgActive ] = ImVec4( 0.26f, 0.26f, 0.28f, 1.0f );
        stijl.Colors[ ImGuiCol_CheckMark ] = ImVec4( 0.85f, 0.85f, 0.85f, 1.0f );
        stijl.Colors[ ImGuiCol_SliderGrab ] = ImVec4( 0.55f, 0.55f, 0.58f, 1.0f );
        stijl.Colors[ ImGuiCol_SliderGrabActive ] = ImVec4( 0.65f, 0.65f, 0.68f, 1.0f );

        // Fallback for the scrollbar. Every frame these are overwritten anyway
        // based on the transparency slider (see Teken()), but without these
        // lines ImGui's opaque default grey briefly shows, for example in the
        // very first frame.
        stijl.Colors[ ImGuiCol_ScrollbarBg ] = ImVec4( 0.03f, 0.03f, 0.04f, 0.20f );
        stijl.Colors[ ImGuiCol_ScrollbarGrab ] = ImVec4( 0.45f, 0.45f, 0.50f, 0.55f );
        stijl.Colors[ ImGuiCol_ScrollbarGrabHovered ] = ImVec4( 0.58f, 0.58f, 0.63f, 0.70f );
        stijl.Colors[ ImGuiCol_ScrollbarGrabActive ] = ImVec4( 0.68f, 0.68f, 0.73f, 0.80f );
        stijl.ScrollbarSize = 11.0f;  // slightly narrower than the default 14
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

        // ImGui tracks the modifier state separately from the ordinary keys;
        // AddKeyEvent alone is not enough to make Ctrl+V work.
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

        // NOTE: deliberately do NOT call ImGui_ImplWin32_NewFrame(). That
        // function also polls the mouse position/buttons itself via Windows'
        // own GetCursorPos/GetKeyState, and thereby overwrites what we just
        // passed via OpMuisBeweging/OpMuisKnop -- that was exactly why clicks
        // did not arrive while our own debug counters did increase. We feed
        // all input ourselves (see OpMuis*/OpToets), so we only need to track
        // the window size and the time between frames ourselves -- that
        // function did that too.
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

        // Title bar/header colour every frame based on the setting (instead
        // of only at startup), so a colour change in Settings is visible
        // immediately.
        //
        // NOTE: ImGui has THREE title bar colours. Only TitleBgActive (window
        // has focus) was here. TitleBg (no focus) and TitleBgCollapsed
        // (collapsed) stayed at ImGui's default, and that is opaque
        // near-black. As soon as you clicked in the game and the overlay lost
        // focus, the bar flipped to black -- exactly that "sometimes black"
        // you saw.
        ImVec4 titelActief( m_accentKleur[ 0 ] * 0.5f, m_accentKleur[ 1 ] * 0.5f,
                             m_accentKleur[ 2 ] * 0.5f, m_doorzichtigheid );
        // Without focus the same tint, only somewhat muted -- so you still
        // see which window is active, without a harsh colour switch.
        ImVec4 titelInactief( m_accentKleur[ 0 ] * 0.32f, m_accentKleur[ 1 ] * 0.32f,
                               m_accentKleur[ 2 ] * 0.32f, m_doorzichtigheid * 0.85f );

        ImGui::PushStyleColor( ImGuiCol_TitleBgActive, titelActief );
        ImGui::PushStyleColor( ImGuiCol_TitleBg, titelInactief );
        ImGui::PushStyleColor( ImGuiCol_TitleBgCollapsed, titelInactief );
        ImGui::PushStyleColor( ImGuiCol_Header,
            ImVec4( m_accentKleur[ 0 ], m_accentKleur[ 1 ], m_accentKleur[ 2 ], 0.5f ) );

        // Input boxes/sliders/checkboxes did not follow the transparency
        // slider -- they were stuck at opaque black, which looked like "loose
        // hard blocks" on a more transparent window. Now coupled too, with a
        // floor so they always remain clearly recognisable as input fields.
        float frameAlpha = std::max( 0.55f, m_doorzichtigheid );
        ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( 0.16f, 0.16f, 0.18f, frameAlpha ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( 0.22f, 0.22f, 0.24f, frameAlpha ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( 0.26f, 0.26f, 0.28f, frameAlpha ) );

        // The scrollbar still had ImGui's default colours: the gutter is
        // semi-transparent, but the GRAB is fully opaque grey by default.
        // That gave that hard dark bar on the right that did not follow the
        // rest of the HUD.
        // Now also coupled to the slider, with a lower floor than the input
        // fields -- a scrollbar may fade as long as you can still point at it.
        float scrollAlpha = std::max( 0.30f, 0.75f * m_doorzichtigheid );
        ImGui::PushStyleColor( ImGuiCol_ScrollbarBg, ImVec4( 0.03f, 0.03f, 0.04f, scrollAlpha * 0.45f ) );
        ImGui::PushStyleColor( ImGuiCol_ScrollbarGrab, ImVec4( 0.45f, 0.45f, 0.50f, scrollAlpha ) );
        ImGui::PushStyleColor( ImGuiCol_ScrollbarGrabHovered,
                                ImVec4( 0.58f, 0.58f, 0.63f, std::min( 1.0f, scrollAlpha + 0.15f ) ) );
        ImGui::PushStyleColor( ImGuiCol_ScrollbarGrabActive,
                                ImVec4( 0.68f, 0.68f, 0.73f, std::min( 1.0f, scrollAlpha + 0.25f ) ) );

        ImGui::SetNextWindowSize( ImVec2( 760, 620 ), ImGuiCond_FirstUseEver );
        ImGui::SetNextWindowSizeConstraints( ImVec2( 380, 320 ), ImVec2( 1400, 1000 ) );
        // Floor of 0.62: below that value loose text that is NOT in a card
        // (header line, "geen actieve rit", hints) starts fading above bright
        // grass or a light sky. The cards themselves are always dark (see
        // KaartKleur), so the slider mainly controls how much game you see
        // between the cards.
        ImGui::SetNextWindowBgAlpha( m_doorzichtigheid );
        if( ImGui::Begin( "CabNavi", &m_zichtbaar, ImGuiWindowFlags_NoCollapse ) )
        {
            // --- Sidebar (left): only the tab icons (the logo is at the top of the main area, see further down -- too narrow here for a readable display font) ---
            ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0, 0, 0, 0.15f * m_iconenDoorzichtigheid ) );
            ImGui::BeginChild( "zijbalk", ImVec2( 78, 0 ), true );

            ImGui::Spacing();

            // NOTE: the size is AANTAL_TABS, not a loosely typed number. This
            // used to be [6] while the loop below runs to AANTAL_TABS (7). At the
            // seventh icon memory NEXT TO the table was read -- that was the
            // crash: EXCEPTION_ACCESS_VIOLATION in ucrtbase, with the letters
            // "Live" as the address.
            //
            // With AANTAL_TABS as the size the compiler complains right away if a
            // tab is added and the names do not grow with it.
            struct TabIcoon { const char *tooltip; };
            Logboek::Spoor( "sidebar with tab icons" );
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

                // Invisible button purely for click/hover detection -- we draw the
                // visible badge ourselves underneath, which gives a sturdier, more
                // recognisable "app icon" feel than a plain rectangular ImGui button.
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

                // The PNG icons already have a coloured rounded square baked in as
                // background -- so only for the fallback line icons do we still draw a
                // round badge ourselves.
                if( !heeftPng )
                {
                    ImU32 badgeKleur = actief
                        ? IM_COL32( 212, 55, 47, static_cast<int>( 255 * m_iconenDoorzichtigheid ) )
                        : IM_COL32( 40, 40, 45, static_cast<int>( 220 * m_iconenDoorzichtigheid ) );
                    railDraw->AddCircleFilled( badgeMidden, 21.0f, badgeKleur, 32 );
                }
                if( actief )
                {
                    // Soft red glow border around the active tab -- always, also on top
                    // of a PNG icon, as a clear "selected" signal.
                    railDraw->AddCircle( badgeMidden, 24.0f, IM_COL32( 212, 55, 47, static_cast<int>( 140 * m_iconenDoorzichtigheid ) ), 32, 2.5f );
                }

                if( heeftPng )
                {
                    // Real colourful PNG loaded -- draw that, not the hand-drawn line
                    // version.
                    float iconGrootte = 38.0f;
                    ImVec2 iconMin( badgeMidden.x - iconGrootte / 2, badgeMidden.y - iconGrootte / 2 );
                    ImVec2 iconMax( badgeMidden.x + iconGrootte / 2, badgeMidden.y + iconGrootte / 2 );
                    float alpha = m_iconenDoorzichtigheid;
                    railDraw->AddImage( m_tabTexturen[ i ], iconMin, iconMax, ImVec2( 0, 0 ), ImVec2( 1, 1 ),
                                        IM_COL32( 255, 255, 255, static_cast<int>( 255 * alpha ) ) );
                }
                else
                {
                    // No PNG found -- fall back on the hand-drawn line icons, overlay
                    // just keeps working.
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

            // --- Main area (right): active tab content ---
            ImGui::BeginGroup();

            // Logo large and readable at the top of the main area -- there is
            // enough width here (the sidebar was far too narrow, the display font
            // became an unreadable red smear there).
            // Measure the width of this area BEFORE anything is on the line. This
            // block is inside a BeginGroup next to the sidebar, and SameLine(
            // offset ) adds the group offset on top.
            const float kopBreedte = ImGui::GetContentRegionAvail().x;

            auto tekenKlok = [ & ]( bool opDezelfdeRegel )
            {
                if( !m_klokTonen ) return;
                const std::string klok = HuidigeKlokTekst( m_uitgebreidLog );
                const float rechts = kopBreedte - ImGui::CalcTextSize( klok.c_str() ).x;
                if( rechts <= 0.0f ) return;
                if( opDezelfdeRegel ) ImGui::SameLine( rechts );
                else                  ImGui::SetCursorPosX( ImGui::GetCursorPosX() + rechts );
                ImGui::TextDisabled( "%s", klok.c_str() );
            };

            // Clock on the LOGO LINE, not at the instruction line. In a narrow
            // window that is wider than the spot where the clock goes, and then
            // the two overlapped.
            if( m_logoTextuur != nullptr && m_logoHoogte > 0 )
            {
                float doelHoogte = 32.0f;
                float schaal = doelHoogte / static_cast<float>( m_logoHoogte );
                ImVec2 grootte( m_logoBreedte * schaal, doelHoogte );
                ImGui::Image( m_logoTextuur, grootte, ImVec2( 0, 0 ), ImVec2( 1, 1 ),
                              ImVec4( 1, 1, 1, m_iconenDoorzichtigheid ) );
                tekenKlok( true );
                ImGui::Spacing();
            }
            else
            {
                // No logo: then the clock gets its own line at the top.
                tekenKlok( false );
            }

            ImGui::TextDisabled( T( "Insert = verbergen | Rechts = muis" ) );

            ImGui::Separator();

            // Enqueue players in view for a VTC lookup. This is DELIBERATELY here
            // and not on the players tab: there, nobody was looked up while you
            // looked at another tab, and the marking stayed empty. Costs nothing
            // here -- only a number goes into a queue, the Web API processes it on
            // its own thread and skips known players itself.
            // Pass our own TruckersMP ID as soon as the SDK knows it; with that the
            // Web API can fetch what YOU signed up for. Independent of the VTC
            // switch.
            if( m_eigenConvooien )
            {
                m_webApi.ZetEigenAccount( m_spelers.EigenAccountId() );
            }

            if( m_vtcAan && m_vtcSpelersOpzoeken )
            {
                // ONLY THE NEAREST. MEASURED 30-08: without a limit 875 players were
                // looked up and the API returned a 429 ("too many requests"). The
                // list already arrives sorted by distance, so the first are the
                // nearest -- they also get priority in the queue.
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
                // Trail per tab. If the game crashes, the last [spoor] line in
                // debug.log points to what the overlay was doing.
                case 0: Logboek::Spoor( "tab Live" );          TekenLiveTab(); break;
                case 1: Logboek::Spoor( "tab Dashboard" ); TekenBoordcomputerTab(); break;
                case 2: Logboek::Spoor( "tab Players" );       TekenSpelersTab(); break;
                case 3: Logboek::Spoor( "tab History" );  TekenGeschiedenisTab(); break;
                case 4: Logboek::Spoor( "tab Statistics" );  TekenStatistiekenTab(); break;
                case 5: Logboek::Spoor( "tab Incident" );      TekenIncidentTab(); break;
                case 6: Logboek::Spoor( "tab VTC" );           TekenVtcTab(); break;
                case 7: Logboek::Spoor( "tab VTC settings" );    TekenVtcInstellingenTab(); break;
                case 8: Logboek::Spoor( "tab Settings" );  TekenInstellingenTab(); break;
                default: break;
            }
            ImGui::EndGroup();
        }
        ImGui::End();
        ImGui::PopStyleColor( 11 );  // 3x Title, Header, 3x FrameBg, 4x Scrollbar (see push above)

        ImGui::Render();

        // Important step that was missing: at this point (right after its own
        // rendering) the game probably still has the right render target
        // bound, but we defensively bind it explicitly again before drawing.
        // Without this ImGui can draw into "thin air": the call succeeds, but
        // nothing appears on screen.
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
            case 0:  // Live -- steering wheel (circle + 3 spokes + hub)
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
            case 1:  // Board computer -- gauge (arc + needle + hub)
            {
                // Half circle as a dial, with a needle pointing diagonally up.
                draw->PathArcTo( midden, r * 0.75f, 3.14159265f, 2.0f * 3.14159265f, 16 );
                draw->PathStroke( kleur, 0, 2.6f );
                draw->AddLine( midden,
                                ImVec2( midden.x + r * 0.55f * cosf( -0.9f ),
                                         midden.y + r * 0.55f * sinf( -0.9f ) ),
                                kleur, 2.6f );
                draw->AddCircleFilled( midden, 2.4f, kleur );
                break;
            }
            case 2:  // Players -- radar/satellite dish (concentric arcs + dot)
            {
                for( int i = 1; i <= 3; ++i )
                {
                    draw->PathArcTo( midden, r * i / 3.0f, -2.3f, -0.8f, 16 );
                    draw->PathStroke( kleur, 0, 2.8f );
                }
                draw->AddCircleFilled( ImVec2( midden.x + r * 0.75f, midden.y + r * 0.55f ), 2.5f, kleur );
                break;
            }
            case 3:  // History -- clock (circle + hands)
            {
                draw->AddCircle( midden, r, kleur, 24, 2.8f );
                draw->AddLine( midden, ImVec2( midden.x, midden.y - r * 0.6f ), kleur, 2.8f );
                draw->AddLine( midden, ImVec2( midden.x + r * 0.45f, midden.y + r * 0.1f ), kleur, 2.8f );
                break;
            }
            case 4:  // Statistics -- bar chart (3 bars, rising)
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
            case 5:  // Incident/Replay -- clapperboard (rectangle + slanted stripe on top)
            {
                float b = r * 0.9f;
                draw->AddRect( ImVec2( midden.x - b, midden.y - b * 0.5f ), ImVec2( midden.x + b, midden.y + b ), kleur, 2.0f, 0, 2.8f );
                draw->AddLine( ImVec2( midden.x - b, midden.y - b * 0.1f ), ImVec2( midden.x + b, midden.y - b * 0.1f ), kleur, 2.8f );
                draw->AddLine( ImVec2( midden.x - b * 0.5f, midden.y - b * 0.5f ), ImVec2( midden.x - b * 0.15f, midden.y - b * 0.1f ), kleur, 2.0f );
                draw->AddLine( ImVec2( midden.x + b * 0.15f, midden.y - b * 0.5f ), ImVec2( midden.x + b * 0.5f, midden.y - b * 0.1f ), kleur, 2.0f );
                break;
            }
            case 6:  // VTC -- company building (low warehouse left, tall office right)
            {
                const float b = r * 0.95f;  // half the width of the whole
                // Office
                draw->AddRectFilled( ImVec2( midden.x + b * 0.05f, midden.y - r * 0.85f ),
                                      ImVec2( midden.x + b, midden.y + r * 0.9f ), kleur, 1.5f );
                // Warehouse
                draw->AddRectFilled( ImVec2( midden.x - b, midden.y - r * 0.15f ),
                                      ImVec2( midden.x - b * 0.05f, midden.y + r * 0.9f ), kleur, 1.5f );
                break;
            }

            case 7:  // VTC settings -- wrench (handle + open jaw)
            {
                const float hoek = -0.785398f;  // 45 degrees, jaw to the top right
                ImVec2 kop( midden.x + r * 0.45f * cosf( hoek ), midden.y + r * 0.45f * sinf( hoek ) );
                ImVec2 staart( midden.x - r * 0.9f * cosf( hoek ), midden.y - r * 0.9f * sinf( hoek ) );
                draw->AddLine( staart, kop, kleur, 3.2f );
                // Jaw: an arc instead of a closed circle, so it stays a wrench and
                // does not become a ring.
                draw->PathArcTo( kop, r * 0.5f, hoek + 0.7f, hoek + 5.6f, 20 );
                draw->PathStroke( kleur, 0, 3.0f );
                break;
            }

            case 8:  // Settings -- gear (circle + 6 teeth)
            {
                draw->AddCircle( midden, r * 0.55f, kleur, 16, 2.8f );
                for( int i = 0; i < 6; ++i )  // 6 teeth -- nothing to do with tabs
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

    void Overlay::TekenPassagierIcoon( float grootte, unsigned int kleur )
    {
        // Small figure: a dot for the head, a rounded block for the torso. NO
        // tile around it like the vehicle icon -- this sits between text and
        // must look as calm as a letter.
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList *draw = ImGui::GetWindowDrawList();

        // Centre vertically with the text line it sits next to.
        const float regel = ImGui::GetTextLineHeight();
        const float y0 = pos.y + ( regel - grootte ) * 0.5f;
        const float x0 = pos.x;

        const float hoofd = grootte * 0.30f;
        draw->AddCircleFilled( ImVec2( x0 + grootte * 0.5f, y0 + hoofd ), hoofd, kleur, 10 );

        // Torso: slightly narrower than full width, with a rounded top.
        draw->AddRectFilled( ImVec2( x0 + grootte * 0.15f, y0 + grootte * 0.55f ),
                              ImVec2( x0 + grootte * 0.85f, y0 + grootte ),
                              kleur, grootte * 0.25f );

        // Reserve space so what follows does not fall over the figure.
        ImGui::Dummy( ImVec2( grootte, regel ) );
    }

    void Overlay::TekenVoertuigIcoon( bool isBus, float grootte )
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList *draw = ImGui::GetWindowDrawList();
        ImU32 kleurTegel = IM_COL32( 212, 55, 47, 255 );  // TMP red, like the button row in the game
        ImU32 kleurIcoon = IM_COL32( 255, 255, 255, 255 );  // white icon on it, like TMP

        // Red rounded square tile as background.
        draw->AddRectFilled( pos, ImVec2( pos.x + grootte, pos.y + grootte ), kleurTegel, grootte * 0.22f );

        float pad = grootte * 0.2f;
        float b = grootte - pad * 2;  // width of the drawing area inside the tile
        float x0 = pos.x + pad, y0 = pos.y + pad;

        if( isBus )
        {
            // Simple white bus silhouette: long rectangle with 3 windows and 2 wheels.
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
            // Simple white truck silhouette: cab + trailer + 2 wheels.
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

        // Convoy coming up soon. Only appears if you or your VTC signed up for
        // it, and only within the last hour -- otherwise it sits there for
        // days for nothing.
        TekenConvooiHerinnering();

        if( m_vracht.HeeftActieveRit() )
        {
            iets = true;
            const Trip &t = m_vracht.HuidigeRit();

            // Route card
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

            // Three stat cards side by side
            float breedte = ( ImGui::GetContentRegionAvail().x - 16 ) / 3.0f;
            // Limit small below the speed, in the same card -- you compare those
            // two with each other anyway. A separate box next to it would only
            // cost space.
            //
            // Same shrinking setup as on the board computer tab, so both screens
            // look the same: if "km/h - limiet 80" does not fit, it becomes
            // "limiet 80", and otherwise just "km/h". Cutting off halfway does
            // not read.
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
            // Empty caption instead of no caption: that way these two keep the
            // same height as the speed card, which does have a line below.
            // Without this the three cards are uneven.
            ImGui::SameLine();
            StatKaart( T( "BRANDSTOF" ), std::to_string( (int)t.brandstofPercentage ) + "%", breedte, "" );
            ImGui::SameLine();
            StatKaart( T( "SCHADE" ), std::to_string( (int)t.schadeChassisPercentage ) + "%", breedte, "" );

            // Driving time stays visible on Live: while driving your mouse is
            // off, so switching tabs costs an action.
            ImGui::Spacing();
            TekenTachoStrip();

            ImGui::Spacing();

            // Remaining-time + fuel cost card, in the accent colour
            BrandstofState bs = m_brandstof.HuidigeState();
            ImGui::PushStyleColor( ImGuiCol_ChildBg,
                TintKaartKleur( ImVec4( m_accentKleur[ 0 ], m_accentKleur[ 1 ], m_accentKleur[ 2 ], 1.0f ), 0.28f ) );
            // Compute ONCE per frame. The estimate is smoothed, and that shifts
            // the value on every call -- calling twice would adjust twice as fast
            // and could make the two numbers on screen differ from each other.
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

            ImGui::TextDisabled( T( "Onderweg: %s -- pauze telt niet mee" ),
                                  FormatteerMinuten( m_vracht.VerstrekenMinutenEcht() ).c_str() );
            {
                const double resterendEcht = resterendMin;
                if( resterendEcht >= 0.0 )
                {
                    TekstGedimd( ( std::string( T( "Aankomst rond " ) ) + KlokTijdOver( resterendEcht ) +
                                    T( " (jouw klok)" ) ).c_str() );

                }
            }
            // Very last line: driving style. Added at the end, so nothing above
            // shifts.
            TekenRijstijl();

            // --- Expenses this trip (idea list #3, #4, #5) ---------------
            // Only show if something was actually paid, otherwise an empty card
            // sits there taking up space.
            std::int64_t onkosten = t.tolKosten + t.veerbootKosten + t.treinKosten + t.boeteKosten;
            if( onkosten > 0 )
            {
                ImGui::Spacing();
                KopBalk( T( "ONKOSTEN DEZE RIT" ) );
                // Compute the height from the number of lines we are really going to
                // draw -- see the explanation at the damage card.
                const ImGuiStyle &stK = ImGui::GetStyle();
                float regelK = ImGui::GetTextLineHeightWithSpacing();
                int regels = 0;
                if( t.tolKosten > 0 ) regels++;
                if( t.veerbootKosten > 0 ) regels++;
                if( t.treinKosten > 0 ) regels++;
                if( t.boeteKosten > 0 ) regels += 1 + static_cast<int>( t.boetes.size() );
                float onkostenHoogte = stK.WindowPadding.y * 2.0f
                                        + regels * regelK
                                        + stK.ItemSpacing.y * 2.0f + 6.0f  // Spacing + Separator + Spacing
                                        + ( m_kopFont ? m_kopFont->FontSize : ImGui::GetFontSize() ) + stK.ItemSpacing.y
                                        + regelK  // closing line
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
                TekstGedimd( T( "Bedragen uit het spel zelf" ) );
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
        }

        if( m_bus.HeeftActieveRit() )
        {
            if( iets ) ImGui::Spacing(), ImGui::Separator(), ImGui::Spacing();
            iets = true;
            const Trip &t = m_bus.HuidigeRit();

            // Header as a narrow strip, like the tachograph strip: everything on
            // ONE line instead of a block 80 pixels high. Saves space for the stop
            // list.
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

                // Passengers on the RIGHT of the same line, small and dimmed -- same
                // approach as the clock in the header. Right-aligned to the card
                // width, so nothing shifts in what is already there. If it does not
                // fit, it simply stays away.
                if( t.passagiers > 0 )
                {
                    if( m_kleinFont ) ImGui::PushFont( m_kleinFont );

                    char pas[ 16 ];
                    snprintf( pas, sizeof( pas ), "%u", (unsigned)t.passagiers );

                    // Width of figure + gap + number, so the whole ends up on the right
                    // and nothing falls over the text on the left.
                    const float icoonGrootte = ImGui::GetTextLineHeight() * 0.85f;
                    const float breedteTotaal = icoonGrootte + 4.0f
                                                + ImGui::CalcTextSize( pas ).x;
                    const float rechtsPas = ImGui::GetWindowWidth()
                                            - ImGui::GetStyle().WindowPadding.x - breedteTotaal;
                    if( rechtsPas > ImGui::GetCursorPosX() + 8.0f )
                    {
                        ImGui::SameLine( rechtsPas );
                        TekenPassagierIcoon( icoonGrootte, IM_COL32( 190, 190, 195, 255 ) );
                        ImGui::SameLine( 0.0f, 4.0f );
                        TekstGedimd( pas );
                    }
                    if( m_kleinFont ) ImGui::PopFont();
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }

            // Remaining-time card (to the next stop)
            double resterend = m_bus.GeschatteResterendeMinutenEcht();
            ImGui::PushStyleColor( ImGuiCol_ChildBg,
                TintKaartKleur( ImVec4( m_accentKleur[ 0 ], m_accentKleur[ 1 ], m_accentKleur[ 2 ], 1.0f ), 0.28f ) );
            // The same three cards as on the cargo trip and the board computer --
            // speed with limit, fuel, damage. On the bus they are somewhat more
            // compact, because the stop list below needs the most space.
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

                // Compact form: as low as possible, width untouched. Saves space for
                // the stop list below.
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

            // Driving time stays visible on Live for the bus too.
            ImGui::Spacing();
            TekenTachoStrip();
            ImGui::Spacing();

            // As a strip, same format as the tachograph below: header, time and
            // arrival clock side by side on one line. Used to be a block 84 pixels
            // high with everything stacked.
            {
                const ImGuiStyle &stE = ImGui::GetStyle();
                float hoogteE = stE.WindowPadding.y * 2.0f + ImGui::GetTextLineHeightWithSpacing() + 2.0f;

                ImGui::BeginChild( "bus_eta_kaart", ImVec2( 0, hoogteE ), true, ImGuiWindowFlags_NoScrollbar );

                // Only the HEADER small; the time and the clock in the normal font --
                // those are the numbers you look at.
                // Header in the accent colour, like "RESTEREND (IRL)" on the cargo
                // live. Only the COLOUR; font, height and width of this box stay
                // exactly as they were.
                if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
                TekstS( T( "VOLGENDE HALTE" ), AccentKleurU32(), false );
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

            // Stop timeline with status icons (completed/current/to go)
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
                // connecting line to the next stop
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
                // Keep the kilometres, and add the estimated arrival time. That is
                // computed exactly the same way as the time to the next stop -- see
                // GeschatteMinutenTotHalte.
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

                // Boarders and alighters, small after the status line. Come straight
                // from the SDK, so nothing to compute. Only show what really happens:
                // at the last stop nobody boards, so only a minus is shown.
                if( s.instappers > 0 || s.uitstappers > 0 )
                {
                    ImGui::SameLine( 0.0f, 10.0f );
                    if( m_kleinFont ) ImGui::PushFont( m_kleinFont );

                    // Figure first, then the counts. Green added, red removed -- the same
                    // colours as "voltooid" and "geannuleerd" elsewhere, so it reads
                    // immediately without needing a word.
                    TekenPassagierIcoon( ImGui::GetTextLineHeight() * 0.85f,
                                          IM_COL32( 190, 190, 195, 255 ) );

                    if( s.instappers > 0 )
                    {
                        ImGui::SameLine( 0.0f, 4.0f );
                        TekstSFmt( IM_COL32( 63, 176, 138, 255 ), "+%d", s.instappers );
                    }
                    if( s.uitstappers > 0 )
                    {
                        ImGui::SameLine( 0.0f, 6.0f );
                        TekstSFmt( IM_COL32( 226, 85, 74, 255 ), "-%d", s.uitstappers );
                    }

                    if( m_kleinFont ) ImGui::PopFont();
                }

                ImGui::EndGroup();
                ImGui::Spacing();
            }
            ImGui::Spacing();
            // The SDK only passes the estimated payout in the START event of the
            // trip; there is no getter to fetch it later. If 0 arrived there --
            // for example because the plugin was loaded mid-trip -- there is
            // nothing to refresh. Then better to be honest than to show a zero
            // that looks as if you earn nothing.
            if( t.geschatUitbetaling > 0 )
            {
                ImGui::Text( T( "Geschatte uitbetaling: %lld" ), (long long)t.geschatUitbetaling );
            }
            else
            {
                TekstGedimd( T( "Uitbetaling niet doorgegeven" ) );
            }
            ImGui::Text( T( "Onderweg: %s" ), FormatteerMinuten( m_bus.VerstrekenMinutenEcht() ).c_str() );

            // --- Expenses this trip ------------------------------------
            // Only show if something was actually paid, like on the cargo trip.
            // Otherwise an empty card sits there filling space the stop list can
            // use better.
            {
                const std::int64_t busOnkosten =
                    t.tolKosten + t.veerbootKosten + t.treinKosten + t.boeteKosten;
                if( busOnkosten > 0 )
                {
                    if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }

                    const ImGuiStyle &stO = ImGui::GetStyle();
                    float regelO = ImGui::GetTextLineHeightWithSpacing();
                    int regels = 1;  // the total line
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

            // Late warning. Only show when something is going on: if you are on
            // schedule, nothing belongs here.
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
                    TekstGedimd( T( "Eerste uur gratis, daarna 0,333%/min" ) );
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }
                else if( vertraging > 0.0 )
                {
                    // Behind schedule, but still within the free hour.
                    TekstGedimd( ( std::string( T( "Achter op schema, nog " ) ) +
                                    std::to_string( (int)( 60.0 - vertraging ) ) +
                                    T( " min speling voor de boete ingaat." ) ).c_str() );
                }
            }
            else
            {
                TekstGedimd( T( "Wacht op navigatiedata" ) );
            }
            // Very last line of the bus: driving style. Added at the end, so
            // nothing above shifts.
            TekenRijstijl();
        }

        if( !iets )
        {
            ImGui::TextDisabled( T( "Geen actieve rit" ) );
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
                    // Do NOT call OpenPopup here. ImGui ties a popup to the window where
                    // you open it; here that is the context popup, while BeginPopup is
                    // further down in the player card. Two different windows, two
                    // different IDs -- they never found each other and nothing happened.
                    // Moreover a MenuItem closes the context popup, which immediately
                    // drags the new screen down with it. Hence a flag and opening AFTER
                    // EndPopup.
                    m_reportPopupOpenen = true;
                }
            }
            else
            {
                ImGui::TextDisabled( T( "TruckersMP-ID onbekend" ) );
            }

            ImGui::EndPopup();
        }

        // The report screen itself is NOT here. This function runs per
        // player, so with forty players that same screen was drawn forty
        // times. It now lives in TekenReportScherm(), which every tab calls
        // once after the list.
    }

    void Overlay::TekenReportScherm()
    {
        // Opening happens here, not in the context popup: ImGui ties a popup
        // to the window in which you open it, and that must be the same window
        // as where BeginPopup is.
        if( m_reportPopupOpenen )
        {
            m_reportPopupOpenen = false;
            ImGui::OpenPopup( "report_scherm" );
        }

        // --- Report screen: modelled as closely as possible on TMP's own
        // in-game "Report User" screen (real paragraph numbers, real ID
        // data). We cannot send the report itself -- that is only possible
        // via TMP's own in-game screen or their website, the SDK passes no
        // Report function to plugins. What we do: prepare everything neatly
        // and put it on your clipboard, and open the website report page, so
        // you only have to paste and send.
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

            // Exact TMP rule categories as in their own in-game screen.
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
                // Real, current time included -- handy because TMP often asks for
                // timestamps with video evidence.
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
                    "SteamID64: " + ( rs.steamId != 0 ? std::to_string( rs.steamId ) : "unknown" ) + "\n"
                    "TruckersMP ID: " + ( rs.accountId != 0 ? std::to_string( rs.accountId ) : "unknown" ) + "\n" +
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

        // Same source choice as on the board computer tab: preferably the
        // game's clock, otherwise our own counter.
        const double totRust = m_vracht.MinutenTotRustSpel();
        const double periode = m_vracht.VolledigeRustperiodeMinuten();
        const bool spelData = totRust >= 0.0 && periode > 1.0;
        const bool inRust = m_vracht.TachograafInRust();

        // Which comes first: the mandatory break or the long rest? On Live
        // there is room for only ONE bar, so we show the one that is nearest.
        // Usually that is the break (4h30) and not the rest (10 hours).
        const double totPauze = m_vracht.MinutenTotVerplichtePauze();

        // There was a "break in progress" mode here that kicked in as soon as
        // you stood still and then counted down to nine hours. That is a
        // remnant from when standing still still counted as rest. Now only a
        // real rest action counts, so that branch is gone -- at a refuelling
        // stop it suddenly showed a completely different number than the P
        // counter in the game.

        // Rest is only "more urgent" if the game gives that clock and it is lower.
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

        // Measure the text on the right first, so the bar gets the rest.
        // Text on the right: what the bar is about.
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
        // This tab is deliberately INDEPENDENT of an active trip: range,
        // odometer, damage and tachograph are just as useful when you drive
        // around empty. That is why it is no longer on Live.
        //
        // What follows is LITERALLY the block that used to be at the bottom of
        // the Live tab -- only moved, not a letter changed.
        // --- Board computer (idea list #1,2,6,7,8,9,10) ----------------
        // Same visual language as the rest of the HUD: cards with a small
        // label above and the value large below, damage as coloured bars.
        // Independent of whether a job is active -- range, odometer and damage
        // are also relevant when you drive around empty.
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

            // Speed and fuel in the same format as on Live, so this tab is also
            // usable when you drive around empty and no trip is running. The
            // DAMAGE card is left out here: the detailed damage bars are already
            // at the bottom of this screen.
            {
                // Row 1: speed / fuel / cruise control.
                // Three equally wide cards, same size as the row below. Cruise
                // control used to be only on half a line, which left a lopsided gap --
                // this fills the row neatly.
                float derdeB = ( ImGui::GetContentRegionAvail().x - 16 ) / 3.0f;

                // Caption with the limit, but only as long as it fits. In a narrow
                // window first "km/h" drops and then the whole limit -- cutting off
                // halfway does not read.
                std::string onder = "km/h";
                if( vs.snelheidslimietKmh >= 0.0 )
                {
                    char lang[ 40 ], kort[ 24 ];
                    snprintf( lang, sizeof( lang ), "km/h - limiet %.0f", vs.snelheidslimietKmh );
                    snprintf( kort, sizeof( kort ), "limiet %.0f", vs.snelheidslimietKmh );

                    const float ruimteKaart = derdeB - ImGui::GetStyle().WindowPadding.x * 2.0f;
                    if( ImGui::CalcTextSize( lang ).x <= ruimteKaart )       onder = lang;
                    else if( ImGui::CalcTextSize( kort ).x <= ruimteKaart )  onder = kort;
                    // otherwise it simply stays "km/h"
                }
                StatKaart( T( "SNELHEID" ), std::to_string( (int)m_vracht.LiveSnelheidKmh() ),
                            derdeB, onder.c_str(), m_vracht.RijdtTeHard() );
                ImGui::SameLine();
                // Empty caption instead of no caption: that way this card keeps the
                // same height as the two next to it, which do have a line below.
                StatKaart( T( "BRANDSTOF" ), getal( m_vracht.HuidigeRit().brandstofPercentage, "%" ),
                            derdeB, "" );
                ImGui::SameLine();

                // Cruise on: value in green, as before. Off: "UIT".
                const bool cruiseAan = vs.cruiseControlKmh > 1.0;
                ImGui::PushStyleColor( ImGuiCol_ChildBg,
                    cruiseAan ? TintKaartKleur( ImVec4( 0.25f, 0.69f, 0.54f, 1.0f ), 0.30f ) : KaartKleur() );
                ImGui::BeginChild( "bc_cruise", ImVec2( derdeB, KaartHoogte( true ) ), true,
                                    ImGuiWindowFlags_NoScrollbar );

                // "CRUISE CONTROL" does not fit in a narrow card. Instead of letting
                // it get cut off we shorten it ourselves -- that reads better than
                // "CRUISE CONTRO". The space the card really has decides, so this is
                // right at every window width.
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

            // Row 2: range / consumption / odometer -- same size as row 1.
            float breedte = ( ImGui::GetContentRegionAvail().x - 16 ) / 3.0f;
            // 0 here means "the game has not sent anything meaningful yet", not
            // really zero: a range of 0 km or a consumption of 0.0 l/100km does
            // not exist while you drive. So in both cases "--" instead of a figure
            // that looks reliable but is not.
            StatKaart( T( "BEREIK" ),
                        vs.bereikKm > 0.0 ? MetPunten( vs.bereikKm ) : "--", breedte, T( "km te gaan" ) );
            ImGui::SameLine();
            // CONSUMPTION: at low speed l/h (consumption per distance says nothing
            // there), above that km/l -- the same unit as the dashboard in the
            // truck, so you can compare 1-to-1. Internally we reckon in l/100km
            // because that is directly proportional to the fuel flow; here we
            // invert it. StatKaart itself (layout/size/font) is left alone.
            auto naarKmPerLiter = []( double literPer100Km ) -> double
            {
                if( literPer100Km <= 0.0 ) return -1.0;
                double kmpl = 100.0 / literPer100Km;
                if( kmpl > 99.9 ) kmpl = 99.9;  // otherwise does not fit in the box
                return kmpl;
            };

            std::string verbruikWaarde = "--";
            std::string verbruikOnder;
            if( vs.staatStil && vs.verbruikLiterPerUur >= 0.0 )
            {
                verbruikWaarde = getal( vs.verbruikLiterPerUur, "", 1 );
                verbruikOnder = vs.echtStil ? T( "l/uur - stationair" ) : T( "l/uur" );
                // Ladder: if the long caption does not fit in a narrow window, a
                // shorter form instead of cutting off.
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

            // --- Refuelling stops this trip ---------------------------
            // The game does not report THAT you refuelled; we recognise it by a
            // jump up in the fuel level. The cost uses YOUR set litre price,
            // because the SDK does not pass the pump price -- hence that slider in
            // the settings.
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
                        snprintf( links, sizeof( links ), "+%.0f l%s", t.liters, t.garage ? T( " garage" ) : "" );
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

            // Row 3: narrow tachograph on the left (width of BEREIK above), next
            // to it ONE box with damage on the left and trailer on the right.
            //
            // This whole row uses the SMALL font. At 19pt the text here got cut
            // off as soon as the window was narrower ("7" instead of "7%") and
            // hardly any width was left for the bars. Going smaller fixes both:
            // the labels fit again and the bars get the freed space.
            if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }

            const ImGuiStyle &st = ImGui::GetStyle();
            float regel = ImGui::GetTextLineHeight() + 2.0f;  // 2.0 = the tight ItemSpacing below
            float kopRegel = ImGui::GetTextLineHeightWithSpacing();
            float totaalVoorRij = ImGui::GetContentRegionAvail().x;

            // Five damage bars determine the height. If damage and trailer are
            // stacked (narrow window), a header line and two bars are added; we
            // estimate that here already.
            float schadeKaartBreedte = ( totaalVoorRij - 16.0f ) * 2.0f / 3.0f;
            float proefKolom = ( schadeKaartBreedte - 24.0f ) / 2.0f;
            bool verwachtNaastElkaar = proefKolom >= 150.0f;
            int balkRegels = verwachtNaastElkaar ? 5 : 8;  // 5 + header + trailer + cargo
            // The tachograph card now has two bars (rest and mandatory break) plus
            // three text lines. Without this comparison the damage card always
            // wins and the break line below got cut off.
            float schadeNodig = kopRegel + balkRegels * regel;
            float tachoNodig = kopRegel + ( regel + 3.0f ) + 3 * regel;  // 1 bar instead of 2
            float rijHoogte = st.WindowPadding.y * 2.0f + std::max( schadeNodig, tachoNodig ) + 4.0f;

            // How much width is there really? In a narrow window the tachograph
            // NO longer fits next to damage+trailer: then every column keeps about
            // 100px and everything gets cut off. In that case the tachograph gets
            // its own line over the full width, and damage+trailer sits below.
            // That keeps it readable however narrow you drag the window.
            float totaal = totaalVoorRij;

            // Tachograph is ALWAYS on the left, as wide as a card from the top
            // row. In a narrow window the damage card itself gives back space by
            // stacking its two columns (see below) -- that reads better than
            // moving the tachograph.
            const bool tachoErnaast = true;
            float derde = ( totaal - 16.0f ) / 3.0f;
            float tachoHoogte = rijHoogte;

            // --- Tachograph ---
            ImGui::PushStyleColor( ImGuiCol_ChildBg, TintKaartKleur( ImVec4( 0.9f, 0.65f, 0.2f, 1.0f ), 0.22f ) );
            ImGui::BeginChild( "bc_tacho", ImVec2( derde, tachoHoogte ), true, ImGuiWindowFlags_NoScrollbar );
            TekstGedimd( T( "TACHOGRAAF" ) );

            // ONE bar, over the full card width: the mandatory break.
            //
            // There used to be a second one above it, fed by
            // "game.next.rest.stop". That channel no longer exists since 1.60
            // (measured: all registration attempts are refused), so that bar
            // always sat there empty.
            {
                const bool inRust = m_vracht.TachograafInRust();

                float balkH = std::max( 5.0f, ImGui::GetFontSize() * 0.44f );
                float breed = ImGui::GetContentRegionAvail().x;
                ImDrawList *dl = ImGui::GetWindowDrawList();

                // The mandatory break (P icon in the game). ETS2 1.60 split fatigue
                // into two things -- the long rest and this break -- but the telemetry
                // passes neither, so this is our own counter, in step with the server
                // clock.
                {
                    const double totPauze = m_vracht.MinutenTotVerplichtePauze();
                    // Always the P counter, also at standstill -- in the game it simply
                    // keeps ticking. There was a "break in progress" branch here that
                    // counted down to nine hours at standstill; that gave a completely
                    // different number than your Route Advisor.
                    const float pf = static_cast<float>( std::min( 1.0, std::max( 0.0,
                            1.0 - totPauze / m_vracht.RijPeriodeSpelMinuten() ) ) );
                    // Amber from two hours ahead, like the game.
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

                    // --- Own tachograph (modes 2 and 3) -----------------------
                    // Two separate lines: break and daily driving time. You can exceed
                    // both, so both visible. In mode 1 MinutenTotPauzeEigen() returns -1
                    // and this stays away.
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
                    // How long you have been on the road since your last rest, in the
                    // normal font -- that is the figure you read.
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

            // --- One box with damage LEFT and trailer RIGHT ---
            ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
            ImGui::BeginChild( "bc_schade", ImVec2( 0, rijHoogte ), true, ImGuiWindowFlags_NoScrollbar );
            {
                float binnen = ImGui::GetContentRegionAvail().x;
                float kolom = ( binnen - 8.0f ) / 2.0f;

                // Does a readable bar still fit in such a column? If not, we put
                // damage and trailer BELOW each other instead of side by side. That is
                // where we give back space in a narrow window -- the tachograph just
                // stays on the left.
                float minKolom = ImGui::CalcTextSize( "Chassis" ).x + 6.0f
                                  + ImGui::CalcTextSize( "100%" ).x + 5.0f + 40.0f;
                bool naastElkaar = kolom >= minKolom;
                if( !naastElkaar ) kolom = binnen;

                // Label measured instead of estimated: no air between word and bar,
                // and it scales with the font.
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
                    // Only show this explanation if it really fits; otherwise you saw
                    // "Alleen ladingsc..." cut off halfway.
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

            // At the very bottom, over the full width: how economically you drive
            // this trip compared with your own average. One line, same form as
            // the convoy reminder. If there is nothing to compare, nothing appears
            // -- and so nothing shifts.
            TekenRijstijl();

            if( m_kleinFont ) { ImGui::PopFont(); m_kleinFontActief = false; }
        }
    }

    void Overlay::TekenSpelersTab()
    {
        // Update positions/headings before we fetch the list. This runs
        // inside the frame event and thus on the game thread -- the only place
        // where SDK getters answer.
        Logboek::Spoor( "players: refreshing positions" );
        m_spelers.VerversPosities();
        std::vector<SpelerRecord> spelers = m_spelers.GeefSpelers();

        // Diagnostics: how many players carry which flag? Without this you
        // cannot tell whether "no patron in view" means there are none, or
        // that the SDK does not pass that flag. At most once every 10 seconds.
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
                                "players=%d team=%d mod=%d patron=%d with_tag=%d coloured=%d",
                                static_cast<int>( spelers.size() ), team, mod, patron, metTag,
                                gekleurd );
                Logboek::Schrijf( "flags", regel );

                // A few examples as well: name, the patron flag and the tag colour
                // side by side. A coloured tag is a Patreon perk in TruckersMP, so if
                // that colour IS there and the flag is not, the SDK simply does not
                // pass that flag.
                int getoond = 0;
                for( const auto &s : spelers )
                {
                    const bool heeftKleur = ( s.tagKleurR < 0.95f || s.tagKleurG < 0.95f ||
                                               s.tagKleurB < 0.95f );
                    if( !heeftKleur && !s.isPatron ) continue;
                    char v[ 200 ];
                    std::snprintf( v, sizeof( v ), "  %s | tag='%s' patron=%d colour=%.2f,%.2f,%.2f",
                                    s.gebruikersnaam.c_str(), s.tagTekst.c_str(),
                                    s.isPatron ? 1 : 0,
                                    s.tagKleurR, s.tagKleurG, s.tagKleurB );
                    Logboek::Schrijf( "flags", v );
                    if( ++getoond >= 5 ) break;
                }
            }
        }


        // Update the scroll offset based on your real speed -- gives the
        // minimap a "living, moving" feel without inventing a real GPS
        // position the SDK does not give.
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
        float scrollPixels = fmodf( m_minimapScrollKm * 40.0f, 1000.0f );  // 40px per "km" feel, no real scale

        // --- Minimap: rectangular map with a crossroads, NOT a round radar ---
        const float mapGrootte = 200.0f;
        ImVec2 startPos = ImGui::GetCursorScreenPos();
        ImVec2 midden = ImVec2( startPos.x + mapGrootte / 2, startPos.y + mapGrootte / 2 );
        ImDrawList *draw = ImGui::GetWindowDrawList();

        // Terrain background
        draw->AddRectFilled( startPos, ImVec2( startPos.x + mapGrootte, startPos.y + mapGrootte ),
                              IM_COL32( 12, 14, 12, 235 ), 10.0f );

        // Fine street pattern (thin lines at a few angles), scrolls with your
        // speed for a "moving" feel.
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

        // Two thicker "main roads" crossing through the middle
        draw->AddLine( ImVec2( startPos.x, midden.y - 6 ), ImVec2( startPos.x + mapGrootte, midden.y + 6 ),
                        IM_COL32( 255, 255, 255, 55 ), 4.0f );
        draw->AddLine( ImVec2( midden.x - 8, startPos.y ), ImVec2( midden.x + 8, startPos.y + mapGrootte ),
                        IM_COL32( 255, 255, 255, 55 ), 4.0f );

        // Self (amber dot in the middle)
        draw->AddCircleFilled( midden, 5.5f, AccentKleurU32( 1.0f ) );
        draw->AddCircle( midden, 5.5f, IM_COL32( 0, 0, 0, 150 ), 12, 1.5f );

        // Players -- radius is the distance, angle is the REAL bearing from
        // Vehicle::GetPlacement(). Top of the map = straight ahead.
        const float bereikMeter = 800.0f;
        float maxStraal = mapGrootte / 2 - 10;

        // Distance rings with a little label. Without a scale you cannot tell
        // whether that dot is at 50 or at 700 metres.
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

        // How many dots do we draw? With fifty players the middle becomes a
        // tangle. We draw the NEAREST 50 -- the list is already sorted by
        // distance, so that is simply the first fifty. What falls outside is
        // too far away to do anything with anyway.
        const int MAX_STIPPEN = 50;

        int getekend = 0;
        int zonderPositie = 0;
        for( const SpelerRecord &s : spelers )
        {
            if( !s.positieBekend )
            {
                // No vehicle in the world: deliberately NOT drawn, instead of put at
                // an invented angle. Counted below the map.
                ++zonderPositie;
                continue;
            }
            if( getekend >= MAX_STIPPEN ) break;
            ++getekend;

            float straal = std::min( s.afstandMeter / bereikMeter, 1.0f ) * maxStraal;
            // Bearing is 0 = ahead, clockwise. On screen "ahead" is up (negative
            // Y), hence the -90 degree turn.
            float hoek = ( s.peilingGraden - 90.0f ) * 3.14159265f / 180.0f;
            ImVec2 punt( midden.x + straal * cosf( hoek ), midden.y + straal * sinf( hoek ) );

            // Own VTC goes BEFORE the role colours: you want to recognise a
            // colleague first. If the switch is off, nothing changes in the
            // existing colours.
            const bool eigenVtc = m_vtcRadarMarkering && IsEigenVtc( s );

            ImU32 kleur = eigenVtc                  ? IM_COL32( 91, 141, 239, 255 )
                        : s.isTeam || s.isManager ? IM_COL32( 232, 80, 70, 255 )
                        : s.isModerator            ? IM_COL32( 232, 80, 70, 255 )
                        : IsPatron( s )             ? IM_COL32( 220, 100, 220, 255 )
                                                     : IM_COL32( 63, 176, 138, 255 );

            // Nearby is larger and brighter than far away. That way whoever drives
            // right next to you stands out, even when it is busy.
            const float nabij = 1.0f - std::min( s.afstandMeter / bereikMeter, 1.0f );
            const float punthoogte = 2.6f + nabij * 2.4f;
            const int alpha = 130 + static_cast<int>( nabij * 125.0f );
            const ImU32 stipKleur = ( kleur & 0x00FFFFFF ) | ( static_cast<ImU32>( alpha ) << 24 );

            // Tick in his driving direction: that shows whether someone drives
            // your way or towards you. A LONG combination gets a longer and
            // thicker tick -- then you already see on the map that something big
            // is driving there, without opening the list.
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
        TekstGedimd( T( "Boven is vooruit. Wegen zijn decoratief." ) );

        ImGui::Spacing();

        // --- Header with count and legend ----------------------------------
        {
            int metPositie = 0;
            for( const SpelerRecord &s : spelers ) if( s.positieBekend ) ++metPositie;

            TekstSFmt( IM_COL32( 255, 255, 255, 255 ), T( "%d spelers in bereik" ), (int)spelers.size() );
            ImGui::SameLine( 0.0f, 10.0f );
            TekstGedimdFmt( T( "(%d op de kaart)" ), metPositie );

            // Colleagues added, in the same blue colour as their marking. Only
            // show if someone is really there -- a line with "0 colleagues" adds
            // nothing (see the rule that parts without news hide themselves).
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

            // Legend: which colour belongs to which role. Without this you have to
            // guess why someone is orange or blue.
            if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
            struct Legenda { ImU32 kleur; const char *naam; };
            // NOTE: the size and the loop run via the same constant. A loosely
            // typed number here was exactly the crash of August.
            static const Legenda legenda[] = {
                { IM_COL32( 232, 80, 70, 255 ),  "team/mod" },
                { IM_COL32( 220, 100, 220, 255 ), "patron" },
                { IM_COL32( 63, 176, 138, 255 ),  "speler" },
                { IM_COL32( 91, 141, 239, 255 ),  "eigen VTC" },
            };
            constexpr int AANTAL_LEGENDA = static_cast<int>( sizeof( legenda ) / sizeof( legenda[ 0 ] ) );
            for( int i = 0; i < AANTAL_LEGENDA; ++i )
            {
                // The VTC colour is last in the list; skip it if you do not use the
                // marking.
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

        // --- Player list as compact lines --------------------------------
        //
        // Used to be a card of 70 pixels per player. With fifty players in
        // view that is 3500 pixels in a window of 220 -- you scrolled
        // endlessly. Now one line per player, in the small font, with a
        // coloured tick for the role and the distance right-aligned.
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

                // Every other line a subtle band: keeps long lists readable without
                // drawing lines.
                if( nummer % 2 == 0 )
                {
                    ld->AddRectFilled( rijStart, ImVec2( rijStart.x + breedte, rijStart.y + regelHoogte ),
                                        IM_COL32( 255, 255, 255, 10 ), 3.0f );
                }
                // Role colour as a tick on the left.
                ld->AddRectFilled( ImVec2( rijStart.x, rijStart.y + 2.0f ),
                                    ImVec2( rijStart.x + 3.0f, rijStart.y + regelHoogte - 2.0f ),
                                    rolKleur, 1.5f );

                ImGui::SetCursorPosX( ImGui::GetCursorPosX() + 8.0f );
                TekstGedimdFmt( "%2d", nummer );
                ImGui::SameLine( 0.0f, 6.0f );

                // Name that does not fit: the same ladder as with "CRUISE CONTROL" --
                // first the full form, then a shorter one, and only as a last resort
                // cut off. With player names there is a useful intermediate step,
                // because they are often full of tags and decoration:
                //
                //   "[WEEDA] Barend V8 | NL"  ->  "Barend V8"
                //
                // That way you do not lose the name itself, only the decor around it.
                const float ruimteNaam = breedte - 150.0f;
                std::string naam = s.gebruikersnaam;

                if( ImGui::CalcTextSize( naam.c_str() ).x > ruimteNaam )
                {
                    // Step 1: everything between parentheses or square brackets out, plus
                    // what follows a separator.
                    std::string kort;
                    int diepte = 0;
                    for( char c : s.gebruikersnaam )
                    {
                        if( c == '[' || c == '(' || c == '{' ) { ++diepte; continue; }
                        if( c == ']' || c == ')' || c == '}' ) { if( diepte > 0 ) --diepte; continue; }
                        if( diepte == 0 )
                        {
                            if( c == '|' ) break;  // everything after a pipe is decoration
                            kort += c;
                        }
                    }
                    // Spaces at the edges off.
                    while( !kort.empty() && kort.front() == ' ' ) kort.erase( kort.begin() );
                    while( !kort.empty() && kort.back() == ' ' ) kort.pop_back();

                    if( !kort.empty() && ImGui::CalcTextSize( kort.c_str() ).x <= ruimteNaam )
                    {
                        naam = kort;
                    }
                    else
                    {
                        // Step 2: still does not fit -- cut off with a dot.
                        if( !kort.empty() ) naam = kort;
                        while( naam.size() > 3 && ImGui::CalcTextSize( naam.c_str() ).x > ruimteNaam )
                        {
                            naam.erase( naam.size() - 2 );
                            naam.back() = '.';
                        }
                    }
                }
                TekstS( naam.c_str() );

                // Marks after the name. STAFF first -- that is the only thing that
                // should change your behaviour. Then the combination: a semi-trailer is
                // about 13 to 16 metres, anything above is a double or a long
                // combination, and that is exactly what you want to know before you
                // overtake.
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

                // The full name on hover, so you never miss anything.
                if( naam != s.gebruikersnaam && ImGui::IsItemHovered() )
                {
                    ImGui::SetTooltip( "%s", s.gebruikersnaam.c_str() );
                }

                // Distance and ping right-aligned, so the columns line up.
                char rechts[ 48 ];
                snprintf( rechts, sizeof( rechts ), "%4.0f m   %3u ms", s.afstandMeter, (unsigned)s.pingMs );
                const float rechtsBreedte = ImGui::CalcTextSize( rechts ).x;
                ImGui::SameLine( 0.0f, 0.0f );
                ImGui::SetCursorPosX( ImGui::GetCursorPosX() +
                                       std::max( 6.0f, breedte - 40.0f - rechtsBreedte -
                                                  ( ImGui::GetCursorPosX() - 8.0f ) ) );
                TekstGedimd( rechts );

                // Menu button at the far right.
                ImGui::SameLine( breedte - 22.0f );
                TekenSpelerContextKnop( s, std::to_string( s.spelerId ) );
            }
            ImGui::EndChild();

            // Once, after the list -- not per player.
            TekenReportScherm();

            if( m_kleinFont ) { ImGui::PopFont(); m_kleinFontActief = false; }
        }
    }

    // Small helper: coloured "badge" label like in the mockups (e.g.
    // UITERLIJK, BRANDSTOFPRIJS, DISCORD), instead of bare flat text.
    // TekenSectieBadge used to be here: a button-like badge per section.
    // Now replaced by SectieStart/SectieEind, which put every group in
    // its own card with a coloured tick before the header.

    float Overlay::SectieHoogte( int tekstRegels, int velden, int spaties ) const
    {
        const ImGuiStyle &st = ImGui::GetStyle();
        const float tekst = ImGui::GetTextLineHeight();  // height of one line
        const float veld  = ImGui::GetFrameHeight();  // dropdown, input field, checkbox

        // Every element costs its own height PLUS the spacing ImGui puts below
        // it. Forgetting that spacing was my mistake: with the tachograph that
        // was 28 pixels off, and then you get a scrollbar.
        const float tussen = st.ItemSpacing.y;

        float hoogte = st.WindowPadding.y * 2.0f;  // top and bottom
        hoogte += tekst + tussen;  // the coloured header
        hoogte += tussen;  // the Spacing() after it (see SectieStart)
        hoogte += tekstRegels * ( tekst + tussen );
        hoogte += velden * ( veld + tussen );
        hoogte += spaties * tussen;
        return hoogte;
    }

    void Overlay::SectieStart( const char *naam, ImVec4 kleur, float vasteHoogte )
    {
        // A bit more air between sections than within a section -- that is
        // what makes the screen readable without drawing lines.
        ImGui::Spacing();
        ImGui::Spacing();

        // Limit the width. In a wide window a section ran all the way to the
        // edge, while the sliders in it only filled part of that space -- that
        // looks empty. With a ceiling the ratio between box and content stays
        // right.
        float sectieBreedte = ImGui::GetContentRegionAvail().x;
        if( sectieBreedte > 520.0f ) sectieBreedte = 520.0f;

        ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 12, 10 ) );

        // By default exactly as always: height 0 + AlwaysAutoResize, so the
        // card closes around its own content. Only if a vasteHoogte is passed
        // do we deviate -- then no AlwaysAutoResize, because those two clash.
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

        // Coloured tick left of the header: calmer than a button-like badge,
        // and it gives every section its own colour to recognise it by.
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
        // About two thirds of the box: enough to take away the emptiness, and
        // room is left for the label ImGui puts on the right.
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
        // As the only section with a fixed height: growing automatically gave
        // a far too high, half-empty box here. 150px is confirmed good (see
        // screenshot 30-08). Want it different, change this number.
        SectieStart( "UITERLIJK", ImVec4( m_accentKleur[ 0 ] + 0.2f, m_accentKleur[ 1 ] + 0.15f,
                                            m_accentKleur[ 2 ] + 0.1f, 1.0f ),
                      SectieHoogte( /*tekst*/ 1, /*velden*/ 4 ) );
        // Language choice. Dutch is the base; texts without a translation stay
        // Dutch, so nothing can ever be left empty.
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
        TekstGedimd( T( "Klik het vakje voor meer kleuren" ) );
        SectieEind();

        // The tachograph and the time scale were accidentally INSIDE the
        // appearance section -- between the header and the sliders. Now their
        // own section, where they belong.
        // Fixed height, but WHICH depends on the chosen mode: with "follow the
        // game" there are only two dropdowns, with the other two, four sliders
        // are added. One fixed number would cut off one case or leave the
        // other half empty -- hence we query the mode here already.
        {
            const TruckTracking::TachoInstelling voorHoogte = m_vracht.HuidigeTachoInstelling();
            const bool alleenKiezen =
                ( voorHoogte.stand == TruckTracking::TachoStand::SpelVolgen );
            SectieStart( "TACHOGRAAF", ImVec4( 0.90f, 0.72f, 0.35f, 1.0f ),
                          alleenKiezen ? SectieHoogte( 2, 2, 2 )
                                        : SectieHoogte( 4, 6, 2 ) );
        }
        // --- Tachograph ----------------------------------------------------
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

                        // The ATW mode is the same machinery as "own rules", but with the
                        // legal values already filled in. So you do not have to look them up.
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
                TekstGedimd( T( "Gelijk met de P-teller in het spel" ) );
            }
            else
            {
                // Only with "own rules" may you slide yourself; in ATW mode the values
                // are fixed, otherwise it is no longer a preset.
                const bool bewerkbaar = ( inst.stand == TruckTracking::TachoStand::EigenRegels );
                if( !bewerkbaar )
                {
                    TekstGedimd( T( "Vast. Kies Eigen regels om te wijzigen" ) );
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

                TekstGedimd( T( "Eigen tachograaf, niet die van ETS2" ) );
            }
            ImGui::Spacing();
        }

        // Emergency button for the time scale. Normally the plugin measures it
        // itself and locks it after five minutes; this is for when you want to
        // override it.
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
            TekstGedimd( T( "Spelminuten per echte minuut" ) );
            ImGui::Spacing();
        }

        SectieEind();

        // Per-vehicle counter: a button and a line. Height counted: 1 text
        // (the line, 38 characters), 1 field (the button), 1 space.
        SectieStart( "GEMIDDELDE", ImVec4( 0.35f, 0.75f, 0.60f, 1.0f ),
                      SectieHoogte( 1, 1, 1 ) );
        ImGui::Spacing();
        if( ImGui::Button( T( "Reset gemiddelde" ) ) )
        {
            m_vracht.ResetVoertuigTeller();
        }
        TekstGedimd( T( "Druk samen met de reset op je dashboard" ) );
        SectieEind();

        // Height counted for what is REALLY drawn: with automatic country
        // there is a checkbox and one text line; manual adds the dropdown.
        // One fixed number would cut off one case or leave the other half
        // empty -- so read the mode first.
        const bool landAuto = m_brandstof.LandAutomatisch();
        SectieStart( "BRANDSTOFPRIJS", ImVec4( 0.90f, 0.35f, 0.32f, 1.0f ),
                      SectieHoogte( 5, landAuto ? 4 : 5, 4 ) );

        // Country. The game does NOT pass it -- six channel names tried, all
        // six refused (in debug.log). But the position is known, and the map
        // table knows where every city is: nearest city -> country -> price,
        // and a large-garage site -> owner discount. The manual choice stays
        // as fallback for when there is no position (not in the world yet).
        {
            bool aan = landAuto;
            if( ImGui::Checkbox( T( "Land automatisch (positie)" ), &aan ) ) m_brandstof.ZetLandAutomatisch( aan );

            // Map table from the CabNavi repository: new map DLC -> new cities,
            // without a new plugin version. One small file, once per start.
            // Sits here because this is what it serves: country and garage
            // for the fuel price.
            bool kaart = m_kaartDownload;
            if( ImGui::Checkbox( T( "Kaartgegevens bijwerken via internet" ), &kaart ) )
            {
                m_kaartDownload = kaart;
                SlaUiterlijkOp();
                if( kaart ) Kaartdata::StartUpdate( InstellingenMap() );
            }

            if( landAuto )
            {
                const FuelCosts::Plaatsbepaling pl = m_brandstof.HuidigePlaats();
                if( pl.bekend )
                {
                    // Worst case in the table (bijelo_polje, montenegro) stays
                    // under 40 characters in this form; the "Now: ... EUR" form
                    // reached 46 and would wrap, breaking the section height.
                    TekstGedimdFmt( T( "%s, %s: %.2f%s" ), pl.stad.c_str(), pl.land.c_str(), pl.prijs,
                                     pl.bijGarage ? T( " garage" ) : "" );
                }
                else
                {
                    TekstGedimd( T( "Nu: geen positie, eigen prijs" ) );
                }
            }
            else
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
                    TekstGedimdFmt( T( "Uit brandstofprijzen.json: EUR %.2f" ),
                                     m_brandstof.PrijsVoorLand( huidig ) );
                }
            }
        }

        ImGui::Spacing();
        TekstGedimd( T( "Voor het omrekenen naar kosten" ) );
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
        TekstGedimd( T( "Direct opgeslagen in instellingen.json" ) );

        // Belongs with consumption, so it is here and not under UITERLIJK.
        ImGui::Spacing();
        if( ImGui::Checkbox( T( "Rijstijlregel tonen" ), &m_zuinigheidTonen ) )
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

        // Input field with a paste button next to it. Ctrl+V works (the
        // letters and Ctrl are passed to ImGui, see OpToets), but such a button
        // is more certain: you would rather not retype a webhook URL.
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

        // Counted what is REALLY in it, and every text line is short enough
        // NOT to wrap -- if one wrapped, it counted for two and you got a
        // scrollbar.
        //   off: 2 text (buffer explanation, "alleen aanzetten"),
        //        3 fields (buffer, verbose log, clock), 3 spaces
        //   on : + 1 field (interval slider), + 1 space. No sub-line: the
        //        slider says enough by itself.
        SectieStart( "INCIDENT-RECORDER", ImVec4( 0.93f, 0.50f, 0.45f, 1.0f ),
                      m_uitgebreidLog ? SectieHoogte( 2, 4, 4 )
                                      : SectieHoogte( 2, 3, 3 ) );
        ImGui::Spacing();
        ImGui::TextDisabled( T( "Bewaarde data, bevriest bij schade" ) );
        static int bufferMinuten = m_incident.BufferMinuten();
        ImGui::SetNextItemWidth( VeldBreedte() );
        if( ImGui::SliderInt( T( "Buffer-lengte (min)" ), &bufferMinuten, 2, 6 ) )
        {
            m_incident.ZetBufferMinuten( bufferMinuten );
        }

        // Diagnostics belongs here: it is the same kind of "record more to
        // look back later". Default off, because these lines write to disk
        // every few seconds.
        ImGui::Spacing();
        if( ImGui::Checkbox( T( "Uitgebreid logboek" ), &m_uitgebreidLog ) )
        {
            Logboek::Uitgebreid() = m_uitgebreidLog;
            SlaUiterlijkOp();
        }
        TekstGedimd( T( "Alleen bij het zoeken van een fout" ) );

        // Clock belongs here: the milliseconds in it hang on the checkbox
        // above, so the two sit together. NO sub-line below it: that wrapped to
        // two lines in a narrow window and then the section height no longer
        // matches.
        ImGui::Spacing();
        if( ImGui::Checkbox( T( "Klok tonen (echte tijd)" ), &m_klokTonen ) )
        {
            SlaUiterlijkOp();
        }

        // Only show when diagnostics is on -- otherwise it is a button that
        // does nothing. Shorter than three seconds is useful if you want to lay
        // a screen recording next to the log: a gear change takes less than a
        // log-line interval and otherwise falls between.
        if( m_uitgebreidLog )
        {
            ImGui::Spacing();
            ImGui::Indent();
            ImGui::SetNextItemWidth( VeldBreedte() );
            float interval = static_cast<float>( TruckTracking::VerbruikLogInterval() );
            if( ImGui::SliderFloat( T( "Logregel elke (sec)" ), &interval, 0.5f, 10.0f, "%.1f" ) )
            {
                TruckTracking::VerbruikLogInterval() = static_cast<double>( interval );
                SlaUiterlijkOp();
            }
            ImGui::Unindent();
        }
        SectieEind();

        // Some air at the bottom, so the last section does not stick to the
        // edge when scrolling through.
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

        // Filter buttons at the top (Alles / Vracht / Bus)
        static int filter = 0;  // 0=all 1=cargo 2=bus
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

        // NO own scroll area around the list: the tab already scrolls itself,
        // and two scrollbars on top of each other means your wheel keeps
        // grabbing the wrong one. This used to be BeginChild with a fixed
        // height of 320 px.
        for( auto it = recent.rbegin(); it != recent.rend(); ++it )
        {
            const Trip &t = *it;
            bool isBus = t.type == TripType::Bus;
            if( filter == 1 && isBus ) continue;
            if( filter == 2 && !isBus ) continue;

            ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
            // Height from KaartHoogte() instead of a fixed 78 px, so it grows with
            // the font.
            ImGui::BeginChild( ( "rit_" + t.id ).c_str(),
                                ImVec2( 0, KaartHoogte( true, true ) ),
                                true, ImGuiWindowFlags_NoScrollbar );

            ImVec2 kaartPos = ImGui::GetCursorScreenPos();
            ImDrawList *kd = ImGui::GetWindowDrawList();
            ImU32 statusKleur = t.status == TripStatus::Voltooid ? IM_COL32( 63, 176, 138, 255 ) : IM_COL32( 226, 85, 74, 255 );
            kd->AddRectFilled( kaartPos, ImVec2( kaartPos.x + 3, kaartPos.y + 44 ), statusKleur, 2.0f );

            // Compute the amount FIRST, because its width determines how much room
            // the text on the left gets.
            //
            // For a CANCELLED trip never show the estimated payout: that is money
            // you did not receive. It used to say "Buslijn -- 0 haltes ...
            // Geannuleerd" with an amount right next to it.
            const long long opbrengst = ( t.status == TripStatus::Geannuleerd )
                ? t.inkomen
                : ( t.inkomen != 0 ? t.inkomen : t.geschatUitbetaling );

            char bedrag[ 32 ];
            snprintf( bedrag, sizeof( bedrag ), "%lld", opbrengst );
            const float bedragBreedte = ImGui::CalcTextSize( bedrag ).x;

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

            // Right-align to the ACTUAL width of the amount, not to a fixed 110
            // px. In a narrow window the amount otherwise started in the middle of
            // the text next to it: "0 halte4000".
            const float rechtsRand = ImGui::GetWindowWidth()
                                     - ImGui::GetStyle().WindowPadding.x - bedragBreedte;
            if( rechtsRand > ImGui::GetCursorPosX() + 8.0f )
            {
                ImGui::SameLine( rechtsRand );
                ImGui::BeginGroup();
                ImGui::Text( "%s", bedrag );
                if( t.brandstofKostenEuro > 0.0 )
                    ImGui::TextDisabled( T( "-EUR %.2f" ), t.brandstofKostenEuro );
                ImGui::EndGroup();
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
    }

    void Overlay::TekenStatistiekenTab()
    {
        Totals totalen = m_logger.GeefTotalen();

        // NET = income minus ALL costs, not just fuel. Fines, tolls, ferries
        // and trains were already per trip in trips.jsonl but were summed
        // nowhere. MEASURED in a real log: 17,000 in fines fell out of view on
        // 54,055 income.
        const double totaleKosten = totalen.totaalBrandstofKostenEuro
                                  + (double)totalen.totaalBoeteKosten
                                  + (double)totalen.totaalTolKosten
                                  + (double)totalen.totaalVeerbootKosten
                                  + (double)totalen.totaalTreinKosten;
        double netto = (double)totalen.totaalInkomen - totaleKosten;

        // Responsive width (like the 3 cards on the Live tab) -- scales when
        // you make the window narrower/wider, instead of a fixed 150px that
        // stayed put.
        float breedte = ( ImGui::GetContentRegionAvail().x - 16 ) / 3.0f;

        // Same setup as the compact cards on the board computer: SMALL font for
        // the header, NORMAL for the value. No header font -- that does not fit
        // in a narrow window and then text drops out. The height comes from
        // KaartHoogte(), not from a fixed number.
        auto statKaart = [ & ]( const char *label, const std::string &waarde, bool accent = false )
        {
            ImVec4 achtergrond = accent
                ? TintKaartKleur( ImVec4( m_accentKleur[ 0 ], m_accentKleur[ 1 ], m_accentKleur[ 2 ], 1.0f ), 0.28f )
                : KaartKleur();
            ImGui::PushStyleColor( ImGuiCol_ChildBg, achtergrond );
            ImGui::BeginChild( label, ImVec2( breedte, KaartHoogte( false, true ) ),
                                true, ImGuiWindowFlags_NoScrollbar );
            ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing,
                                  ImVec2( ImGui::GetStyle().ItemSpacing.x, 2.0f ) );

            if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
            TekstGedimd( label );
            if( m_kleinFont ) ImGui::PopFont();

            TekstS( waarde.c_str() );

            ImGui::PopStyleVar();
            ImGui::EndChild();
            ImGui::PopStyleColor();
        };

        // FIXED short headers. No ladder that switches per window size -- that
        // flips between long and short and looks restless.
        statKaart( T( "VRACHT" ), std::to_string( totalen.aantalVrachtRitten ) );
        ImGui::SameLine();
        statKaart( T( "BUS" ), std::to_string( totalen.aantalBusRitten ) );
        ImGui::SameLine();
        statKaart( T( "AFSTAND" ), std::to_string( (int)totalen.totaalAfstandKm ) + " km" );

        // Amounts without cents: for a total over dozens of trips they say
        // nothing and only cost width.
        char buf[ 32 ];
        snprintf( buf, sizeof( buf ), "EUR %lld", (long long)totalen.totaalInkomen );
        statKaart( T( "VERDIEND" ), buf );
        ImGui::SameLine();
        snprintf( buf, sizeof( buf ), "EUR %.0f", totalen.totaalBrandstofKostenEuro );
        statKaart( T( "BRANDSTOF" ), buf );
        ImGui::SameLine();
        snprintf( buf, sizeof( buf ), "EUR %.0f", netto );
        statKaart( T( "NETTO" ), buf, true );

        // --- Fuel this session --------------------------------------------
        {
            const int aantal = m_brandstof.AantalTankbeurten();
            if( aantal > 0 )
            {
                const double liters = m_brandstof.TotaalGetanktLiters();
                const double kosten = liters * m_brandstof.PrijsPerLiter();

                ImGui::Spacing();
                if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }
                TekstGedimdFmt( T( "Getankt: %dx, %.0f liter, EUR %.0f" ),
                                 aantal, liters, kosten );
                TekstGedimdFmt( T( "EUR %.2f per liter (zelf ingesteld)" ), m_brandstof.PrijsPerLiter() );
                if( m_kleinFont ) { ImGui::PopFont(); m_kleinFontActief = false; }
            }
        }

        // The three lines about trips.jsonl and dashboard.html used to be
        // here. Removed: that is startup information you read once and that
        // then gets in the way every session. It is in the README.

        // --- TruckersMP Web API -------------------------------------------
        // Server status and upcoming events, fetched from api.truckersmp.com.
        // Default OFF: network traffic should be something you choose
        // yourself.
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        KopBalk( T( "TRUCKERSMP LIVE" ) );

        {
            // Checkbox in the small font, and without explanation below: that
            // saves two lines the list below can use better. What it does is in
            // the README.
            if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }

            bool aan = m_webApi.Ingeschakeld();
            if( ImGui::Checkbox( T( "Servergegevens en evenementen ophalen" ), &aan ) )
            {
                m_webApi.ZetIngeschakeld( aan );
                m_webApiAan = aan;
                SlaUiterlijkOp();   // remembered: on stays on, off stays off
            }


            // --- Your schedule ---
            // DELIBERATELY above the switch above: this hangs on your own convoy
            // sign-ups, not on the server status. Otherwise you would lose your
            // schedule as soon as you switch those lists off.
            // Only what YOU or your VTC signed up for, and only the coming month.
            // Otherwise it is not a schedule but a list.
            {
                const auto planning = MijnConvooien();
                if( !planning.empty() )
                {
                    // Limit at a month ahead, in the same text form as the API gives its
                    // times -- so comparing is enough.
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
                        if( !e.startTijd.empty() && e.startTijd > grens ) break;  // already sorted
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
                            // One line per convoy: date and time first, then the name. More compact
                            // than two lines, and you read a schedule by date anyway.
                            std::string regel = e.startTijd.size() >= 16
                                                    ? e.startTijd.substr( 5, 11 )  // MM-DD HH:MM
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
                return;  // off: nothing more to show
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
                    // Only ETS2 -- ATS servers mean nothing to you here.
                    if( s.spel != "ETS2" ) continue;
                    if( ++rij > 10 ) break;

                    const float breed = ImGui::GetContentRegionAvail().x;
                    TekstS( s.naam.c_str(), s.online ? IM_COL32( 255, 255, 255, 255 )
                                                      : IM_COL32( 190, 130, 125, 255 ) );

                    // Build as std::string, NOT with snprintf and a temporary .c_str().
                    // Such a temporary text is cleaned up before snprintf reads it -- that
                    // is exactly the kind of dangling pointer that crashed the overlay
                    // before.
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

            // --- Events ---
            const std::vector<EvenementInfo> evenementen = m_webApi.Evenementen();
            ImGui::Spacing();
            if( !evenementen.empty() )
            {
                const ImGuiStyle &stE = ImGui::GetStyle();
                const float regelE = ImGui::GetTextLineHeight() + 4.0f;
                const int aantal = (int)std::min<std::size_t>( evenementen.size(), 5 );

                ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
                // With checkboxes each line is a bit higher than the text alone;
                // otherwise the last one falls just outside the box.
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

                    // Checkbox to mark yourself that you are going here. Only visible if
                    // you have that on, otherwise it costs needless space.
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
            if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
            TekstGedimd( T( "Vult alleen bij een botsing" ) );
            if( m_kleinFont ) ImGui::PopFont();
            return;
        }

        ImGui::TextColored( ImVec4( 0.9f, 0.4f, 0.35f, 1.0f ), "%s", T( "INCIDENT VASTGELEGD" ) );
        ImGui::TextDisabled( T( "Vermoedelijk betrokken: %s" ), m_incident.VermoedelijkeSpelerId().c_str() );
        // KEEP SHORT. The previous text was 86 characters and wrapped to two
        // or three lines in a narrow window, giving the whole tab a scrollbar
        // -- and then text also drops out because the scrollbar takes space.
        ImGui::TextDisabled( T( "dichtstbijzijnde bij de schade -- geen bewijs" ) );
        ImGui::Spacing();

        int aantalFrames = m_incident.AantalFrames();

        // New incident? Then jump to the LAST moment right away: that is the
        // impact itself. It opened at frame 0, and that is the start of the
        // buffer -- minutes before the collision, often with no players in
        // view yet. Then the radar looks empty while the recording is fine.
        if( m_incidentTellerGezien != m_incident.IncidentTeller() )
        {
            m_incidentTellerGezien = m_incident.IncidentTeller();
            m_incidentFrameIndex = aantalFrames - 1;
        }

        if( m_incidentFrameIndex >= aantalFrames ) m_incidentFrameIndex = aantalFrames - 1;
        if( m_incidentFrameIndex < 0 ) m_incidentFrameIndex = 0;

        const IncidentFrame *frame = m_incident.GeefFrame( m_incidentFrameIndex );
        if( frame == nullptr )
        {
            ImGui::TextDisabled( T( "Geen data voor dit moment." ) );
            return;
        }

        // Timeline slider
        ImGui::SetNextItemWidth( -1 );
        ImGui::SliderInt( "##tijdlijn", &m_incidentFrameIndex, 0, std::max( 0, aantalFrames - 1 ), frame->tijdLabel.c_str() );
        ImGui::Spacing();

        // Minimap for this moment (same style as the Players tab: radius is
        // correct, angle is indicative -- see the note in PlayersNearby.hxx)
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
        for( const SpelerRecord &s : frame->spelers )
        {
            // Use the REAL bearing, like the players tab. There used to be an
            // invented angle here: everyone neatly spread over a circle,
            // regardless of where they really were. That is exactly the opposite
            // of what an incident recording is for -- you want to see who drove
            // WHERE.
            if( !s.positieBekend ) continue;
            float straal = std::min( s.afstandMeter / bereikMeter, 1.0f ) * maxStraal;
            // 0 degrees is straight ahead; on screen ahead is up, hence the 90
            // degree turn.
            float hoek = ( s.peilingGraden - 90.0f ) * 3.14159265f / 180.0f;
            ImVec2 punt( midden.x + straal * cosf( hoek ), midden.y + straal * sinf( hoek ) );

            // Nearby larger and brighter, like on the players tab.
            const float nabij = 1.0f - std::min( s.afstandMeter / bereikMeter, 1.0f );
            const float punthoogte = 2.6f + nabij * 2.4f;
            const int alpha = 130 + static_cast<int>( nabij * 125.0f );
            draw->AddCircleFilled( punt, punthoogte,
                                    IM_COL32( 226, 85, 74, static_cast<ImU32>( alpha ) ) );

            // Tick in his driving direction: that shows whether he came your way
            // or towards you -- in a collision that is the first thing you want to
            // know.
            float koersRad = ( s.koersVerschilGraden - 90.0f ) * 3.14159265f / 180.0f;
            const float streep = punthoogte + ( s.aanhangerLengteM > 16.5f ? 8.0f : 3.5f );
            draw->AddLine( punt,
                            ImVec2( punt.x + streep * cosf( koersRad ),
                                     punt.y + streep * sinf( koersRad ) ),
                            IM_COL32( 226, 85, 74, 200 ), 1.6f );
        }
        draw->AddRect( startPos, ImVec2( startPos.x + mapGrootte, startPos.y + mapGrootte ),
                        AccentKleurU32( 0.5f ), 10.0f, 0, 1.5f );
        ImGui::Dummy( ImVec2( mapGrootte, mapGrootte ) );

        ImGui::Spacing();
        ImGui::Text( T( "Spelers op dit moment (%d):" ), (int)frame->spelers.size() );
        // NO more own scroll area around this list. That gave a scrollbar
        // INSIDE a scrollbar: the tab already scrolled, and the box around it
        // too. Then your wheel keeps grabbing the wrong one and you see only
        // two players. The cards now simply sit in the tab; that scrolls by
        // itself.

        // Cards in the COMPACT form of the board computer instead of a fixed
        // 70 px. Saves almost half the height, so more than twice as many fit
        // in the same space.
        const float kaartHoogte = KaartHoogte( false, true );
        for( const SpelerRecord &s : frame->spelers )
        {
            ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
            ImGui::BeginChild( ( "inc_" + std::to_string( s.spelerId ) + "_" + std::to_string( m_incidentFrameIndex ) ).c_str(),
                                ImVec2( 0, kaartHoogte ), true, ImGuiWindowFlags_NoScrollbar );
            ImGui::Text( "%s", s.gebruikersnaam.c_str() );
            ImGui::SameLine( ImGui::GetWindowWidth() - 90 );
            ImGui::Text( "%.0f m", s.afstandMeter );
            ImGui::SameLine( ImGui::GetWindowWidth() - 30 );
            TekenSpelerContextKnop( s, "incident_" + std::to_string( s.spelerId ) );
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        // Once, after the list -- not per player.
        TekenReportScherm();
    }

    // --- Saving VTC settings -------------------------------------------
    // Own file next to the others: vtc.json. That way instellingen.json
    // stays for the fuel price, and you can discard the VTC side without
    // losing the rest.
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

            // Restore ticked convoys. Stored completely, not just the number: that
            // way your schedule stays right even if the API list no longer returns
            // that event.
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
            Logboek::Schrijf( "event", std::string( "vtc.json not readable: " ) + Logboek::KorteFout( ex.what() ) );
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
            Logboek::Schrijf( "event", std::string( "vtc.json not writable: " ) + Logboek::KorteFout( ex.what() ) );
        }
    }

    // Does this player belong to your VTC? We compare with the tags you
    // entered on the VTC settings tab, comma-separated.
    //
    // Why by TAG and not via the Web API: the SDK already passes the tag
    // shown in front of someone's name in the game, and for VTC members
    // that is almost always their company tag. Querying the API per player
    // would mean fifty requests with fifty players in view -- you do not
    // want that, and this also works immediately without internet.
    //
    // The user name is checked too, because not everyone puts his tag in
    // the tag field; some type "[WDA] Jojo" as their name.
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

    // Everything you are going to: ticked yourself plus what your VTC
    // takes part in. Sorted by time, and what is already past drops out.
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

        // Past? Leave out. Same text comparison as elsewhere: with the year
        // first that is immediately a date comparison.
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
        // One of the two is enough: your own sign-ups or your VTC's.
        if( !m_vtcAan && !m_eigenConvooien ) return {};

        // Now, in the same form as the API gives its times.
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
        if( regel.empty() ) return;  // nothing to report, so no empty bar either

        if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
        TekstS( regel.c_str(), IM_COL32( 91, 141, 239, 255 ) );
        if( m_kleinFont ) ImGui::PopFont();
    }

    void Overlay::TekenRijstijl()
    {
        if( !m_zuinigheidTonen ) return;

        const TruckTracking::RijstijlStatus r = m_vracht.HuidigeRijstijl();
        if( r.nu == TruckTracking::RijstijlStatus::Niets ) return;

        // Two layers on one line, words only. Left the assessment (only once
        // there is a reference), right the direct meter (always). Like a truck:
        // a running meter and an eco light. No percentages -- someone who does
        // not know them has no use for them.
        const ImU32 groen = IM_COL32( 63, 176, 138, 255 );
        const ImU32 grijs = IM_COL32( 154, 154, 154, 255 );
        const ImU32 oranje = IM_COL32( 224, 138, 90, 255 );
        const ImU32 dim = IM_COL32( 122, 122, 122, 255 );

        // Words like on a real dashboard: ECO / NORMAAL / POWER, and
        // STATIONAIR only when you stand still. Always exactly one of the four;
        // nothing only when the engine is off.
        const char *nuWoord = ""; ImU32 nuKleur = grijs;
        switch( r.nu )
        {
            case TruckTracking::RijstijlStatus::Uitrollen:
            case TruckTracking::RijstijlStatus::ZuinigNu:     nuWoord = "ECO";               nuKleur = groen; break;
            case TruckTracking::RijstijlStatus::Normaal:      nuWoord = T( "NORMAAL" );      nuKleur = grijs; break;
            case TruckTracking::RijstijlStatus::Trekken:      nuWoord = "POWER";             nuKleur = oranje; break;
            case TruckTracking::RijstijlStatus::StilMotorAan: nuWoord = T( "STATIONAIR" );   nuKleur = dim; break;
            default: break;
        }

        const char *standWoord = nullptr; ImU32 standKleur = grijs;
        switch( r.stand )
        {
            case TruckTracking::RijstijlStatus::Zuinig:   standWoord = T( "Zuinig" );   standKleur = groen; break;
            case TruckTracking::RijstijlStatus::Sportief: standWoord = T( "Sportief" ); standKleur = oranje; break;
            case TruckTracking::RijstijlStatus::Gewoon:   standWoord = T( "Gewoon" );   standKleur = grijs; break;
            default: break;
        }

        if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
        if( standWoord )
        {
            TekstS( standWoord, standKleur, false );
            ImGui::SameLine();
            TekstS( "\xc2\xb7", dim, false );  // middle dot, is in the font (Latin-1)
            ImGui::SameLine();
        }
        TekstS( nuWoord, nuKleur, false );
        if( m_kleinFont ) ImGui::PopFont();
    }

    bool Overlay::IsPatron( const SpelerRecord &s ) const
    {
        // The API first, because that is demonstrably right. Not looked up yet
        // or VTC integration off? Then the SDK flag, better than nothing.
        if( s.accountId != 0 )
        {
            const int viaApi = m_webApi.SpelerIsPatron( s.accountId );
            if( viaApi >= 0 ) return ( viaApi == 1 );
        }
        return s.isPatron;
    }

    bool Overlay::IsEigenVtc( const SpelerRecord &s ) const
    {
        // 1) The VTC NUMBER is the only hard source. A tag is text anyone can
        //    type; a number is given to a company once by TruckersMP. If this
        //    player has been looked up, this is the answer and we look nowhere
        //    else.
        if( m_vtcId > 0 && s.accountId != 0 )
        {
            const int gevonden = m_webApi.SpelerVtcId( s.accountId );
            if( gevonden >= 0 ) return ( gevonden == m_vtcId );
        }

        // 2) Not looked up yet (or no VTC set): fall back on the tag. That way
        //    you see something right away, and once the lookup arrives it gets
        //    more precise by itself.
        if( m_vtcTagsBuffer[ 0 ] == '\0' ) return false;

        auto naarKleineLetters = []( std::string t )
        {
            for( char &c : t ) c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
            return t;
        };
        const std::string tag = naarKleineLetters( s.tagTekst );
        const std::string naam = naarKleineLetters( s.gebruikersnaam );

        // Split the comma list, ignore spaces around it.
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
            TekstGedimd( T( "Nog niet ingesteld" ) );
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

        // Header: name and member count, same stat cards as everywhere.
        {
            const float halfB = ( ImGui::GetContentRegionAvail().x - 8 ) / 2.0f;
            std::string onder = vtc.tag.empty() ? std::string() : ( std::string( T( "tag " ) ) + vtc.tag );
            StatKaart( T( "BEDRIJF" ), vtc.naam, halfB, onder.c_str() );
            ImGui::SameLine();
            StatKaart( T( "LEDEN" ), std::to_string( vtc.leden ), halfB, T( "chauffeurs" ) );
        }

        if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }

        // Computed height per block instead of growing automatically. The
        // latter gives -- like with UITERLIJK -- a far too high, half-empty
        // box here. Header + one line per item is exactly enough.
        const ImGuiStyle &stV = ImGui::GetStyle();
        const float regelV = ImGui::GetTextLineHeight() + 4.0f;

        auto lijstBlok = [ & ]( const char *id, const char *kop,
                                 const std::vector<EvenementInfo> &items, int maxItems )
        {
            if( items.empty() ) return;  // nothing to report, then no empty box either

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
                // Date first, then the name -- one line, because you read a list by
                // date. The rest comes on hover.
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
            // Organised yourself, and what the company takes part in. Those are
            // two different things: most VTCs join more often than they organise.
            lijstBlok( "vtc_eigen", T( "EIGEN CONVOOIEN" ), m_webApi.VtcEvenementen(), 4 );
            lijstBlok( "vtc_mee", T( "MELDT ZICH AAN VOOR" ), m_webApi.VtcAangemeld(), 4 );
        }

        // News has its own form (title + date), so that stays separate.
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
        // Fixed height, like with UITERLIJK: growing automatically gave a far
        // too high, half-empty box here (seen 30-08).
        // First your personal sign-ups: those work without a VTC, so they do
        // not belong tucked away behind the VTC settings.
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

        // Make visible that the lookup is running. Without this you have to
        // guess whether anything happens.
        {
            int opgezocht = 0, inRij = 0;
            m_webApi.OpzoekStand( opgezocht, inRij );
            char regel[ 96 ];
            std::snprintf( regel, sizeof( regel ), T( "Opgezocht: %d  |  in de rij: %d" ),
                            opgezocht, inRij );
            TekstGedimd( regel );
        }

        // Quick button: fill in your own VTC's tag as soon as it is fetched.
        // Saves retyping, and you can still adjust it afterwards.
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

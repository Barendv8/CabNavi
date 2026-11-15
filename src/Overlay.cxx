#include "Overlay.hxx"
#include "Kaartdata.hxx"
#include "Eenheden.hxx"

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
    void TekenGezicht( ImDrawList *dl, const ImVec2 &midden, float straal, int trede )
    {
        static const ImU32 kleuren[ 5 ] = {
            IM_COL32( 229, 58, 47, 255 ), IM_COL32( 243, 154, 31, 255 ), IM_COL32( 242, 226, 27, 255 ),
            IM_COL32( 242, 226, 27, 255 ), IM_COL32( 46, 204, 64, 255 ) };
        if( trede < 0 ) trede = 0;
        if( trede > 4 ) trede = 4;
        const float pi = 3.14159265f;
        const float s = straal / 11.0f;
        const float dikte = 1.8f * s < 1.0f ? 1.0f : 1.8f * s;
        const ImU32 donker = IM_COL32( 29, 29, 29, 255 );
        const auto punt = [ & ]( float x, float y ) { return ImVec2( midden.x + x * s, midden.y + y * s ); };
        dl->AddCircleFilled( midden, straal, kleuren[ trede ], 24 );
        dl->AddCircleFilled( punt( -3.4f, -1.8f ), 1.5f * s, donker, 8 );
        dl->AddCircleFilled( punt( 3.4f, -1.8f ), 1.5f * s, donker, 8 );
        if( trede == 4 )
        {
            dl->PathArcTo( punt( 0.0f, 1.0f ), 5.0f * s, 0.0f, pi, 12 );
            dl->PathFillConvex( donker );
        }
        else if( trede == 3 )
        {
            dl->PathArcTo( punt( 0.0f, 0.0f ), 5.6f * s, 0.2f * pi, 0.8f * pi, 10 );
            dl->PathStroke( donker, 0, dikte );
        }
        else if( trede == 2 )
        {
            dl->AddLine( punt( -4.0f, 3.5f ), punt( 4.0f, 3.5f ), donker, dikte );
        }
        else
        {
            dl->PathArcTo( punt( 0.0f, 8.6f ), 5.6f * s, 1.2f * pi, 1.8f * pi, 10 );
            dl->PathStroke( donker, 0, dikte );
            if( trede == 0 )
            {
                dl->AddLine( punt( -5.5f, -5.0f ), punt( -1.5f, -3.2f ), donker, dikte );
                dl->AddLine( punt( 5.5f, -5.0f ), punt( 1.5f, -3.2f ), donker, dikte );
            }
        }
    }

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

    std::string FormatteerMinuten( double minuten )
    {
        if( minuten < 0.0 ) return Ritten::T( "onbekend" );

        int totaal = static_cast<int>( std::ceil( minuten - 0.0001 ) );
        int uren = totaal / 60;
        int rest = totaal % 60;
        if( uren <= 0 ) return std::to_string( rest ) + Ritten::T( " min" );
        return std::to_string( uren ) + Ritten::T( " uur " ) + std::to_string( rest ) + Ritten::T( " min" );
    }

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

    const ImU32 KLEUR_GEDIMD = IM_COL32( 255, 255, 255, 255 );

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
            dl->AddText( ImVec2( p.x + 1.0f, p.y + 1.0f ), IM_COL32( 0, 0, 0, 130 ), tekst );
        }

        dl->AddText( p, kleur, tekst );
        ImGui::Dummy( ImGui::CalcTextSize( tekst ) );
    }

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

    void TekstGedimdFmt( const char *fmt, ... )
    {
        char buf[ 256 ];
        va_list args;
        va_start( args, fmt );
        vsnprintf( buf, sizeof( buf ), fmt, args );
        va_end( args );
        TekstS( buf, KLEUR_GEDIMD, false );
    }

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
            case 0xBD: return ImGuiKey_Minus;
            case 0xBE: return ImGuiKey_Period;
            case 0xBC: return ImGuiKey_Comma;
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

            case 0x41: return ImGuiKey_A;
            case 0x43: return ImGuiKey_C;
            case 0x56: return ImGuiKey_V;
            case 0x58: return ImGuiKey_X;
            case 0x59: return ImGuiKey_Y;
            case 0x5A: return ImGuiKey_Z;

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
    static std::filesystem::path InstellingenMap();

    Overlay::Overlay( TripLogger &logger, BusTracking &bus, TruckTracking &vracht,
                       PlayersNearby &spelers, FuelCosts &brandstof, DiscordWebhook &discord,
                       VtcWebhook &vtc, VtcBron &vtcBron, IncidentRecorder &incident )
        : m_logger( logger ), m_bus( bus ), m_vracht( vracht ), m_spelers( spelers ), m_brandstof( brandstof ),
          m_discord( discord ), m_vtc( vtc ), m_vtcBron( vtcBron ), m_incident( incident )
    {
        LaadUiterlijk();
        std::snprintf( m_prijsBuffer, sizeof( m_prijsBuffer ), "%.2f",
                        Eenheden::PrijsPerVolume( m_brandstof.PrijsPerLiter() ) );
        LaadVtcInstellingen();

        m_webApi.ZetIngeschakeld( m_webApiAan );

        {
            std::string fout;
            const auto cache = InstellingenMap() / Kaartdata::CacheBestandsnaam();
            std::error_code ec;
            if( std::filesystem::exists( cache, ec ) && !Kaartdata::LaadBestand( cache, fout ) )
                Logboek::Schrijf( "start", "map table cache: " + fout );
            Logboek::Schrijf( "start", "map table " + Kaartdata::Versie() + " (" + Kaartdata::Bron() + ", " + std::to_string( Kaartdata::AantalSteden() ) + " cities)" );
            if( m_kaartDownload ) Kaartdata::StartUpdate( InstellingenMap() );
        }
    }

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
            if( m_doorzichtigheid < 0.35f ) m_doorzichtigheid = 0.35f;
            m_iconenDoorzichtigheid = j.value( "iconen_doorzichtigheid", m_iconenDoorzichtigheid );
            m_zuinigheidTonen = j.value( "zuinigheid_tonen", true );
            m_taal = j.value( "taal", 0 );
            m_eenheden = std::clamp( j.value( "eenheden", 0 ), 0, 2 );
            Eenheden::Keuze() = static_cast<EenheidKeuze>( m_eenheden );
            m_klokTonen = j.value( "klok_tonen", true );
            m_uitgebreidLog = j.value( "uitgebreid_log", false );
            Logboek::Uitgebreid() = m_uitgebreidLog;
            m_webApiAan = j.value( "web_api_aan", true );
            m_kaartDownload = j.value( "kaartdata_download", true );
            TruckTracking::VerbruikLogInterval() =
                std::clamp( j.value( "log_interval_sec", 3.0 ), 0.5, 10.0 );
            Taal::Kies( m_taal == 1 ? TaalKeuze::Engels : TaalKeuze::Nederlands );
            if( m_iconenDoorzichtigheid < 0.4f ) m_iconenDoorzichtigheid = 0.4f;
            m_accentKleur[ 0 ] = j.value( "accent_r", m_accentKleur[ 0 ] );
            m_accentKleur[ 1 ] = j.value( "accent_g", m_accentKleur[ 1 ] );
            m_accentKleur[ 2 ] = j.value( "accent_b", m_accentKleur[ 2 ] );
        }
        catch( ... ) { }
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
        j[ "eenheden" ] = m_eenheden;
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

    ImVec4 Overlay::KaartKleur() const
    {
        return ImVec4( 1.0f, 1.0f, 1.0f, 0.08f );
    }

    ImVec4 Overlay::TintKaartKleur( const ImVec4 &tint, float sterkte ) const
    {
        return ImVec4( tint.x, tint.y, tint.z, 0.10f + 0.14f * sterkte );
    }

    void Overlay::KopBalk( const char *tekst )
    {
        TekstS( tekst, AccentKleurU32( 1.0f ) );
        ImVec2 p = ImGui::GetCursorScreenPos();
        float breedte = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2( p.x, p.y + 1 ), ImVec2( p.x + breedte, p.y + 2 ), AccentKleurU32( 0.55f ) );
        ImGui::Dummy( ImVec2( 0, 6 ) );
    }

    float Overlay::KaartHoogte( bool metOnderschrift, bool compact ) const
    {
        const ImGuiStyle &st = ImGui::GetStyle();
        float klein = compact && m_kleinFont
                          ? m_kleinFont->FontSize + 2.0f
                          : ImGui::GetTextLineHeightWithSpacing();
        const float compactSpatie = 2.0f;
        float groot = compact
                          ? ImGui::GetFontSize() + compactSpatie
                          : ( m_kopFont ? m_kopFont->FontSize : ImGui::GetFontSize() ) + st.ItemSpacing.y;

        float hoogte = st.WindowPadding.y * 2.0f + klein + groot;
        if( metOnderschrift ) hoogte += klein;
        return hoogte + 6.0f;
    }

    void Overlay::StatKaart( const char *label, const std::string &waarde, float breedte,
                              const char *onderschrift, bool waarschuwing, bool compact )
    {
        ImGui::PushStyleColor( ImGuiCol_ChildBg,
            waarschuwing ? TintKaartKleur( ImVec4( 0.85f, 0.25f, 0.20f, 1.0f ), 0.5f ) : KaartKleur() );
        ImGui::BeginChild( label, ImVec2( breedte, KaartHoogte( onderschrift != nullptr, compact ) ),
                            true, ImGuiWindowFlags_NoScrollbar );

        if( compact ) ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( ImGui::GetStyle().ItemSpacing.x, 2.0f ) );

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
        float startY = ImGui::GetCursorPosY();
        float startX = ImGui::GetCursorPosX();
        float beschikbaar = ( breedte > 0.0f ) ? breedte : ImGui::GetContentRegionAvail().x;
        TekstGedimd( naam );

        if( m_kleinFontActief ) ImGui::PopFont();
        float pctBreedte = ImGui::CalcTextSize( "100%" ).x + 5.0f;
        if( m_kleinFontActief ) ImGui::PushFont( m_kleinFont );
        float balkHoogte = std::max( 5.0f, ImGui::GetFontSize() * 0.44f );
        float balkBreedte = std::max( 24.0f, beschikbaar - labelBreedte - pctBreedte );

        float balkMaximum = ImGui::GetFontSize() * 7.0f;
        if( balkBreedte > balkMaximum ) balkBreedte = balkMaximum;

        ImGui::SetCursorPosY( startY + ( ImGui::GetTextLineHeight() - balkHoogte ) * 0.5f );
        ImGui::SetCursorPosX( startX + labelBreedte );
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled( p, ImVec2( p.x + balkBreedte, p.y + balkHoogte ), IM_COL32( 0, 0, 0, 110 ), balkHoogte * 0.5f );

        if( percentage >= 0.0 )
        {
            float fractie = static_cast<float>( percentage / 100.0 );
            if( fractie > 1.0f ) fractie = 1.0f;

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
            if( m_kleinFontActief ) ImGui::PopFont();
            TekstS( pctTekst, kleurOverride ? IM_COL32( 255, 255, 255, 255 ) : kleur );
            if( m_kleinFontActief ) ImGui::PushFont( m_kleinFont );
        }
        else
        {
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
        Kaartdata::StopUpdate();
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

        m_iniPad = ( InstellingenMap() / "imgui.ini" ).string();
        io.IniFilename = m_iniPad.c_str();

        {
            ImFontConfig fontConfig;
            fontConfig.OversampleH = 3;
            fontConfig.OversampleV = 3;
            fontConfig.PixelSnapH = true;
            ImFont *font = io.Fonts->AddFontFromFileTTF(
                "C:\\Windows\\Fonts\\segoeui.ttf", 19.0f, &fontConfig );
            if( font == nullptr )
            {
                io.Fonts->AddFontDefault();
            }

            ImFontConfig kopFontConfig;
            kopFontConfig.OversampleH = 3;
            kopFontConfig.OversampleV = 3;
            kopFontConfig.PixelSnapH = true;
            m_kopFont = io.Fonts->AddFontFromFileTTF(
                "C:\\Windows\\Fonts\\seguisb.ttf", 32.0f, &kopFontConfig );
            if( m_kopFont == nullptr )
            {
                m_kopFont = io.Fonts->AddFontFromFileTTF(
                    "C:\\Windows\\Fonts\\segoeui.ttf", 32.0f, &kopFontConfig );
            }

            ImFontConfig kleinConfig;
            kleinConfig.OversampleH = 3;
            kleinConfig.OversampleV = 3;
            kleinConfig.PixelSnapH = true;
            m_kleinFont = io.Fonts->AddFontFromFileTTF(
                "C:\\Windows\\Fonts\\segoeui.ttf", 16.0f, &kleinConfig );
        }

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
        stijl.Colors[ ImGuiCol_TitleBg ] = ImVec4( 0.10f, 0.22f, 0.38f, 0.85f );
        stijl.Colors[ ImGuiCol_TitleBgCollapsed ] = ImVec4( 0.10f, 0.22f, 0.38f, 0.85f );
        stijl.Colors[ ImGuiCol_Header ] = ImVec4( 0.15f, 0.35f, 0.60f, 0.6f );

        stijl.Colors[ ImGuiCol_Button ] = ImVec4( 0.22f, 0.22f, 0.24f, 1.0f );
        stijl.Colors[ ImGuiCol_ButtonHovered ] = ImVec4( 0.30f, 0.30f, 0.33f, 1.0f );
        stijl.Colors[ ImGuiCol_ButtonActive ] = ImVec4( 0.35f, 0.35f, 0.38f, 1.0f );
        stijl.Colors[ ImGuiCol_FrameBg ] = ImVec4( 0.16f, 0.16f, 0.18f, 1.0f );
        stijl.Colors[ ImGuiCol_FrameBgHovered ] = ImVec4( 0.22f, 0.22f, 0.24f, 1.0f );
        stijl.Colors[ ImGuiCol_FrameBgActive ] = ImVec4( 0.26f, 0.26f, 0.28f, 1.0f );
        stijl.Colors[ ImGuiCol_CheckMark ] = ImVec4( 0.85f, 0.85f, 0.85f, 1.0f );
        stijl.Colors[ ImGuiCol_SliderGrab ] = ImVec4( 0.55f, 0.55f, 0.58f, 1.0f );
        stijl.Colors[ ImGuiCol_SliderGrabActive ] = ImVec4( 0.65f, 0.65f, 0.68f, 1.0f );

        stijl.Colors[ ImGuiCol_ScrollbarBg ] = ImVec4( 0.03f, 0.03f, 0.04f, 0.20f );
        stijl.Colors[ ImGuiCol_ScrollbarGrab ] = ImVec4( 0.45f, 0.45f, 0.50f, 0.55f );
        stijl.Colors[ ImGuiCol_ScrollbarGrabHovered ] = ImVec4( 0.58f, 0.58f, 0.63f, 0.70f );
        stijl.Colors[ ImGuiCol_ScrollbarGrabActive ] = ImVec4( 0.68f, 0.68f, 0.73f, 0.80f );
        stijl.ScrollbarSize = 11.0f;
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

        ImVec4 titelActief( m_accentKleur[ 0 ] * 0.5f, m_accentKleur[ 1 ] * 0.5f,
                             m_accentKleur[ 2 ] * 0.5f, m_doorzichtigheid );
        ImVec4 titelInactief( m_accentKleur[ 0 ] * 0.32f, m_accentKleur[ 1 ] * 0.32f,
                               m_accentKleur[ 2 ] * 0.32f, m_doorzichtigheid * 0.85f );

        ImGui::PushStyleColor( ImGuiCol_TitleBgActive, titelActief );
        ImGui::PushStyleColor( ImGuiCol_TitleBg, titelInactief );
        ImGui::PushStyleColor( ImGuiCol_TitleBgCollapsed, titelInactief );
        ImGui::PushStyleColor( ImGuiCol_Header,
            ImVec4( m_accentKleur[ 0 ], m_accentKleur[ 1 ], m_accentKleur[ 2 ], 0.5f ) );

        float frameAlpha = std::max( 0.55f, m_doorzichtigheid );
        ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( 0.16f, 0.16f, 0.18f, frameAlpha ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( 0.22f, 0.22f, 0.24f, frameAlpha ) );
        ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( 0.26f, 0.26f, 0.28f, frameAlpha ) );

        float scrollAlpha = std::max( 0.30f, 0.75f * m_doorzichtigheid );
        ImGui::PushStyleColor( ImGuiCol_ScrollbarBg, ImVec4( 0.03f, 0.03f, 0.04f, scrollAlpha * 0.45f ) );
        ImGui::PushStyleColor( ImGuiCol_ScrollbarGrab, ImVec4( 0.45f, 0.45f, 0.50f, scrollAlpha ) );
        ImGui::PushStyleColor( ImGuiCol_ScrollbarGrabHovered,
                                ImVec4( 0.58f, 0.58f, 0.63f, std::min( 1.0f, scrollAlpha + 0.15f ) ) );
        ImGui::PushStyleColor( ImGuiCol_ScrollbarGrabActive,
                                ImVec4( 0.68f, 0.68f, 0.73f, std::min( 1.0f, scrollAlpha + 0.25f ) ) );

        ImGui::SetNextWindowSize( ImVec2( 760, 620 ), ImGuiCond_FirstUseEver );
        ImGui::SetNextWindowSizeConstraints( ImVec2( 380, 320 ), ImVec2( 1400, 1000 ) );
        ImGui::SetNextWindowBgAlpha( m_doorzichtigheid );
        if( ImGui::Begin( "CabNavi", &m_zichtbaar, ImGuiWindowFlags_NoCollapse ) )
        {
            ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0, 0, 0, 0.15f * m_iconenDoorzichtigheid ) );
            ImGui::BeginChild( "zijbalk", ImVec2( 78, 0 ), true );

            ImGui::Spacing();

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

                if( !heeftPng )
                {
                    ImU32 badgeKleur = actief
                        ? IM_COL32( 212, 55, 47, static_cast<int>( 255 * m_iconenDoorzichtigheid ) )
                        : IM_COL32( 40, 40, 45, static_cast<int>( 220 * m_iconenDoorzichtigheid ) );
                    railDraw->AddCircleFilled( badgeMidden, 21.0f, badgeKleur, 32 );
                }
                if( actief )
                {
                    railDraw->AddCircle( badgeMidden, 24.0f, IM_COL32( 212, 55, 47, static_cast<int>( 140 * m_iconenDoorzichtigheid ) ), 32, 2.5f );
                }

                if( heeftPng )
                {
                    float iconGrootte = 38.0f;
                    ImVec2 iconMin( badgeMidden.x - iconGrootte / 2, badgeMidden.y - iconGrootte / 2 );
                    ImVec2 iconMax( badgeMidden.x + iconGrootte / 2, badgeMidden.y + iconGrootte / 2 );
                    float alpha = m_iconenDoorzichtigheid;
                    railDraw->AddImage( m_tabTexturen[ i ], iconMin, iconMax, ImVec2( 0, 0 ), ImVec2( 1, 1 ),
                                        IM_COL32( 255, 255, 255, static_cast<int>( 255 * alpha ) ) );
                }
                else
                {
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

            ImGui::BeginGroup();

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
                tekenKlok( false );
            }

            ImGui::TextDisabled( T( "Insert = verbergen | Rechts = muis" ) );

            ImGui::Separator();

            if( m_eigenConvooien )
            {
                m_webApi.ZetEigenAccount( m_spelers.EigenAccountId() );
            }

            if( m_vtcAan && m_vtcSpelersOpzoeken )
            {
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
        ImGui::PopStyleColor( 11 );

        ImGui::Render();

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
            case 0:
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
            case 1:
            {
                draw->PathArcTo( midden, r * 0.75f, 3.14159265f, 2.0f * 3.14159265f, 16 );
                draw->PathStroke( kleur, 0, 2.6f );
                draw->AddLine( midden,
                                ImVec2( midden.x + r * 0.55f * cosf( -0.9f ),
                                         midden.y + r * 0.55f * sinf( -0.9f ) ),
                                kleur, 2.6f );
                draw->AddCircleFilled( midden, 2.4f, kleur );
                break;
            }
            case 2:
            {
                for( int i = 1; i <= 3; ++i )
                {
                    draw->PathArcTo( midden, r * i / 3.0f, -2.3f, -0.8f, 16 );
                    draw->PathStroke( kleur, 0, 2.8f );
                }
                draw->AddCircleFilled( ImVec2( midden.x + r * 0.75f, midden.y + r * 0.55f ), 2.5f, kleur );
                break;
            }
            case 3:
            {
                draw->AddCircle( midden, r, kleur, 24, 2.8f );
                draw->AddLine( midden, ImVec2( midden.x, midden.y - r * 0.6f ), kleur, 2.8f );
                draw->AddLine( midden, ImVec2( midden.x + r * 0.45f, midden.y + r * 0.1f ), kleur, 2.8f );
                break;
            }
            case 4:
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
            case 5:
            {
                float b = r * 0.9f;
                draw->AddRect( ImVec2( midden.x - b, midden.y - b * 0.5f ), ImVec2( midden.x + b, midden.y + b ), kleur, 2.0f, 0, 2.8f );
                draw->AddLine( ImVec2( midden.x - b, midden.y - b * 0.1f ), ImVec2( midden.x + b, midden.y - b * 0.1f ), kleur, 2.8f );
                draw->AddLine( ImVec2( midden.x - b * 0.5f, midden.y - b * 0.5f ), ImVec2( midden.x - b * 0.15f, midden.y - b * 0.1f ), kleur, 2.0f );
                draw->AddLine( ImVec2( midden.x + b * 0.15f, midden.y - b * 0.5f ), ImVec2( midden.x + b * 0.5f, midden.y - b * 0.1f ), kleur, 2.0f );
                break;
            }
            case 6:
            {
                const float b = r * 0.95f;
                draw->AddRectFilled( ImVec2( midden.x + b * 0.05f, midden.y - r * 0.85f ),
                                      ImVec2( midden.x + b, midden.y + r * 0.9f ), kleur, 1.5f );
                draw->AddRectFilled( ImVec2( midden.x - b, midden.y - r * 0.15f ),
                                      ImVec2( midden.x - b * 0.05f, midden.y + r * 0.9f ), kleur, 1.5f );
                break;
            }

            case 7:
            {
                const float hoek = -0.785398f;
                ImVec2 kop( midden.x + r * 0.45f * cosf( hoek ), midden.y + r * 0.45f * sinf( hoek ) );
                ImVec2 staart( midden.x - r * 0.9f * cosf( hoek ), midden.y - r * 0.9f * sinf( hoek ) );
                draw->AddLine( staart, kop, kleur, 3.2f );
                draw->PathArcTo( kop, r * 0.5f, hoek + 0.7f, hoek + 5.6f, 20 );
                draw->PathStroke( kleur, 0, 3.0f );
                break;
            }

            case 8:
            {
                draw->AddCircle( midden, r * 0.55f, kleur, 16, 2.8f );
                for( int i = 0; i < 6; ++i )
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
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList *draw = ImGui::GetWindowDrawList();

        const float regel = ImGui::GetTextLineHeight();
        const float y0 = pos.y + ( regel - grootte ) * 0.5f;
        const float x0 = pos.x;

        const float hoofd = grootte * 0.30f;
        draw->AddCircleFilled( ImVec2( x0 + grootte * 0.5f, y0 + hoofd ), hoofd, kleur, 10 );

        draw->AddRectFilled( ImVec2( x0 + grootte * 0.15f, y0 + grootte * 0.55f ),
                              ImVec2( x0 + grootte * 0.85f, y0 + grootte ),
                              kleur, grootte * 0.25f );

        ImGui::Dummy( ImVec2( grootte, regel ) );
    }

    void Overlay::TekenRustIcoon( const bool verplichtePauze, const float grootte, const unsigned int kleur )
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList *draw = ImGui::GetWindowDrawList();

        const float regel = ImGui::GetTextLineHeight();
        const float g = grootte;
        const float x0 = pos.x;
        const float y0 = pos.y + ( regel - g ) * 0.5f;
        const float dik = std::max( 1.0f, g * 0.10f );

        if( verplichtePauze )
        {
            draw->AddRect( ImVec2( x0 + 0.5f, y0 + 0.5f ), ImVec2( x0 + g - 0.5f, y0 + g - 0.5f ), kleur, g * 0.18f, 0, dik );
            draw->AddLine( ImVec2( x0 + g * 0.38f, y0 + g * 0.24f ), ImVec2( x0 + g * 0.38f, y0 + g * 0.78f ), kleur, dik );
            draw->AddRect( ImVec2( x0 + g * 0.38f, y0 + g * 0.24f ), ImVec2( x0 + g * 0.68f, y0 + g * 0.54f ), kleur, g * 0.14f, 0, dik );
        }
        else
        {
            draw->AddLine( ImVec2( x0 + g * 0.06f, y0 + g * 0.22f ), ImVec2( x0 + g * 0.06f, y0 + g * 0.88f ), kleur, dik );
            draw->AddLine( ImVec2( x0 + g * 0.06f, y0 + g * 0.68f ), ImVec2( x0 + g * 0.94f, y0 + g * 0.68f ), kleur, dik );
            draw->AddLine( ImVec2( x0 + g * 0.94f, y0 + g * 0.50f ), ImVec2( x0 + g * 0.94f, y0 + g * 0.88f ), kleur, dik );
            draw->AddRectFilled( ImVec2( x0 + g * 0.38f, y0 + g * 0.46f ), ImVec2( x0 + g * 0.94f, y0 + g * 0.64f ), kleur, g * 0.08f );
            draw->AddCircleFilled( ImVec2( x0 + g * 0.23f, y0 + g * 0.52f ), g * 0.10f, kleur, 10 );
        }

        ImGui::Dummy( ImVec2( g, regel ) );
    }

    void Overlay::TekenVoertuigIcoon( bool isBus, float grootte )
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList *draw = ImGui::GetWindowDrawList();
        ImU32 kleurTegel = IM_COL32( 212, 55, 47, 255 );
        ImU32 kleurIcoon = IM_COL32( 255, 255, 255, 255 );

        draw->AddRectFilled( pos, ImVec2( pos.x + grootte, pos.y + grootte ), kleurTegel, grootte * 0.22f );

        float pad = grootte * 0.2f;
        float b = grootte - pad * 2;
        float x0 = pos.x + pad, y0 = pos.y + pad;

        if( isBus )
        {
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

        TekenConvooiHerinnering();

        if( m_vracht.HeeftActieveRit() )
        {
            iets = true;
            const Trip &t = m_vracht.HuidigeRit();

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
            ImGui::TextDisabled( Eenheden::Imperiaal() ? T( "%s | %.0f mi gepland" ) : T( "%s | %.0f km gepland" ),
                                  t.lading.empty() ? "-" : t.lading.c_str(), Eenheden::Km( t.geplandeAfstandKm ) );
            ImGui::EndGroup();
            ImGui::EndChild();
            ImGui::PopStyleColor();

            ImGui::Spacing();

            float breedte = ( ImGui::GetContentRegionAvail().x - 16 ) / 3.0f;
            {
                TruckTracking::VoertuigStatus vs = m_vracht.HuidigeVoertuigStatus();
                std::string onder = Eenheden::Imperiaal() ? "mph" : "km/h";
                if( vs.snelheidslimietKmh >= 0.0 )
                {
                    char lang[ 40 ], kort[ 24 ];
                    snprintf( lang, sizeof( lang ), "%s - limiet %.0f", onder.c_str(), Eenheden::Snelheid( vs.snelheidslimietKmh ) );
                    snprintf( kort, sizeof( kort ), T( "limiet %.0f" ), Eenheden::Snelheid( vs.snelheidslimietKmh ) );

                    const float ruimteKaart = breedte - ImGui::GetStyle().WindowPadding.x * 2.0f;
                    if( ImGui::CalcTextSize( lang ).x <= ruimteKaart )       onder = lang;
                    else if( ImGui::CalcTextSize( kort ).x <= ruimteKaart )  onder = kort;
                }
                StatKaart( T( "SNELHEID" ), std::to_string( (int)Eenheden::Snelheid( t.huidigeSnelheidKmh ) ),
                            breedte, onder.c_str(), m_vracht.RijdtTeHard() );
            }
            ImGui::SameLine();
            StatKaart( T( "BRANDSTOF" ), std::to_string( (int)t.brandstofPercentage ) + "%", breedte, "" );
            ImGui::SameLine();
            StatKaart( T( "SCHADE" ), std::to_string( (int)t.schadeChassisPercentage ) + "%", breedte, "" );

            ImGui::Spacing();
            TekenTachoStrip();

            ImGui::Spacing();

            BrandstofState bs = m_brandstof.HuidigeState();
            ImGui::PushStyleColor( ImGuiCol_ChildBg,
                TintKaartKleur( ImVec4( m_accentKleur[ 0 ], m_accentKleur[ 1 ], m_accentKleur[ 2 ], 1.0f ), 0.28f ) );
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
            ImGui::Text( "%s %.2f", SpelInfo::Munt(), bs.kostenDezeRitEuro );
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
            TekenRijstijl();

            std::int64_t onkosten = t.tolKosten + t.veerbootKosten + t.treinKosten + t.boeteKosten;
            if( onkosten > 0 )
            {
                ImGui::Spacing();
                KopBalk( T( "ONKOSTEN DEZE RIT" ) );
                const ImGuiStyle &stK = ImGui::GetStyle();
                float regelK = ImGui::GetTextLineHeightWithSpacing();
                int regels = 0;
                if( t.tolKosten > 0 ) regels++;
                if( t.veerbootKosten > 0 ) regels++;
                if( t.treinKosten > 0 ) regels++;
                if( t.boeteKosten > 0 ) regels += 1 + static_cast<int>( t.boetes.size() );
                float onkostenHoogte = stK.WindowPadding.y * 2.0f
                                        + regels * regelK
                                        + stK.ItemSpacing.y * 2.0f + 6.0f
                                        + ( m_kopFont ? m_kopFont->FontSize : ImGui::GetFontSize() ) + stK.ItemSpacing.y
                                        + regelK
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

            int voltooid = 0;
            for( const StopInfo &s : t.haltes ) if( s.voltooid ) voltooid++;

            {
                const ImGuiStyle &stB = ImGui::GetStyle();
                float hoogteB = stB.WindowPadding.y * 2.0f + ImGui::GetTextLineHeightWithSpacing() + 2.0f;

                ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
                ImGui::BeginChild( "buslijn_kaart", ImVec2( 0, hoogteB ), true, ImGuiWindowFlags_NoScrollbar );
                TekenVoertuigIcoon( true, 16.0f );
                ImGui::SameLine( 0.0f, 6.0f );
                TekstSFmt( IM_COL32( 255, 255, 255, 255 ), t.busMarkt == "tourist_job" ? T( "TOERISTENRIT" )
                           : !t.busMarkt.empty() ? T( "LIJNDIENST" ) : T( "BUSLIJN" ) );
                ImGui::SameLine( 0.0f, 8.0f );
                TekstGedimdFmt( T( "halte %d / %d" ), voltooid, (int)t.haltes.size() );

                if( t.passagiers > 0 )
                {
                    if( m_kleinFont ) ImGui::PushFont( m_kleinFont );

                    char pas[ 16 ];
                    snprintf( pas, sizeof( pas ), "%u", (unsigned)t.passagiers );

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

            double resterend = m_bus.GeschatteResterendeMinutenEcht();
            ImGui::PushStyleColor( ImGuiCol_ChildBg,
                TintKaartKleur( ImVec4( m_accentKleur[ 0 ], m_accentKleur[ 1 ], m_accentKleur[ 2 ], 1.0f ), 0.28f ) );
            {
                TruckTracking::VoertuigStatus vs = m_vracht.HuidigeVoertuigStatus();
                float breedte = ( ImGui::GetContentRegionAvail().x - 16 ) / 3.0f;

                std::string onder = Eenheden::Imperiaal() ? "mph" : "km/h";
                if( vs.snelheidslimietKmh >= 0.0 )
                {
                    char lang[ 40 ], kort[ 24 ];
                    snprintf( lang, sizeof( lang ), "%s - limiet %.0f", Eenheden::Imperiaal() ? "mph" : "km/h", Eenheden::Snelheid( vs.snelheidslimietKmh ) );
                    snprintf( kort, sizeof( kort ), "limiet %.0f", Eenheden::Snelheid( vs.snelheidslimietKmh ) );
                    const float ruimte = breedte - ImGui::GetStyle().WindowPadding.x * 2.0f;
                    if( ImGui::CalcTextSize( lang ).x <= ruimte )       onder = lang;
                    else if( ImGui::CalcTextSize( kort ).x <= ruimte )  onder = kort;
                }

                StatKaart( T( "SNELHEID" ), std::to_string( (int)Eenheden::Snelheid( m_vracht.LiveSnelheidKmh() ) ),
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
                    TekstSFmt( IM_COL32( 115, 217, 173, 255 ), "%.0f", Eenheden::Snelheid( vs.cruiseControlKmh ) );
                    if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
                    TekstGedimd( Eenheden::Imperiaal() ? "mph" : T( "km/h" ) );
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

            ImGui::Spacing();
            TekenTachoStrip();
            ImGui::Spacing();

            {
                const ImGuiStyle &stE = ImGui::GetStyle();
                float hoogteE = stE.WindowPadding.y * 2.0f + ImGui::GetTextLineHeightWithSpacing() + 2.0f;

                ImGui::BeginChild( "bus_eta_kaart", ImVec2( 0, hoogteE ), true, ImGuiWindowFlags_NoScrollbar );

                if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
                TekstS( T( "VOLGENDE HALTE" ), AccentKleurU32(), false );
                if( m_kleinFont ) ImGui::PopFont();

                ImGui::SameLine( 0.0f, 8.0f );
                TekstSFmt( IM_COL32( 255, 255, 255, 255 ), "%s", FormatteerMinuten( resterend ).c_str() );
                if( resterend >= 0.0 )
                {
                    ImGui::SameLine( 0.0f, 8.0f );
                    TekstGedimd( ( std::string( T( "rond " ) ) + KlokTijdOver( resterend ) ).c_str() );
                }
                ImGui::EndChild();
            }
            ImGui::PopStyleColor();
            ImGui::Spacing();

            if( m_bus.IsScsRit() && !t.bronStad.empty() )
            {
                const bool vertrokken = m_bus.ScsVertrokken();
                ImVec2 iconPos = ImGui::GetCursorScreenPos();
                ImDrawList *hd = ImGui::GetWindowDrawList();
                const ImU32 icoonKleur = vertrokken ? IM_COL32( 63, 176, 138, 255 ) : AccentKleurU32( 1.0f );
                hd->AddCircleFilled( ImVec2( iconPos.x + 10, iconPos.y + 10 ), 9.0f, icoonKleur );
                if( !t.haltes.empty() )
                {
                    hd->AddLine( ImVec2( iconPos.x + 10, iconPos.y + 20 ), ImVec2( iconPos.x + 10, iconPos.y + 38 ),
                                 IM_COL32( 255, 255, 255, 30 ), 2.0f );
                }
                ImGui::SetCursorPosX( ImGui::GetCursorPosX() + 26 );
                ImGui::BeginGroup();
                if( vertrokken )
                    ImGui::TextDisabled( "%s", t.bronStad.c_str() );
                else
                    ImGui::Text( "%s", t.bronStad.c_str() );
                ImGui::TextDisabled( "%s", vertrokken ? T( "voltooid" ) : T( "vertrek" ) );
                const int startInstappers = m_bus.ScsStartInstappers();
                if( startInstappers > 0 )
                {
                    ImGui::SameLine( 0.0f, 10.0f );
                    if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
                    TekenPassagierIcoon( ImGui::GetTextLineHeight() * 0.85f, IM_COL32( 190, 190, 195, 255 ) );
                    ImGui::SameLine( 0.0f, 4.0f );
                    TekstSFmt( IM_COL32( 63, 176, 138, 255 ), "+%d", startInstappers );
                    if( m_kleinFont ) ImGui::PopFont();
                }
                ImGui::EndGroup();
                ImGui::Spacing();
            }

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
                const double naarHalte = m_bus.GeschatteMinutenTotHalte( i );
                if( m_bus.IsScsRit() )
                {
                    const double restKm = m_bus.ScsResterendeKmTotHalte( i );
                    const bool kmBekend = m_bus.ScsEtappesExact() || i + 1 == t.haltes.size();
                    if( naarHalte >= 0.0 && restKm >= 0.0 )
                    {
                        ImGui::TextDisabled( Eenheden::Imperiaal() ? T( "%s  %.0f mi  rond %s" ) : T( "%s  %.0f km  rond %s" ),
                                              statusTekst, Eenheden::Km( restKm ),
                                              KlokTijdOver( naarHalte ).c_str() );
                    }
                    else if( naarHalte >= 0.0 )
                    {
                        ImGui::TextDisabled( T( "%s  rond %s" ), statusTekst, KlokTijdOver( naarHalte ).c_str() );
                    }
                    else if( kmBekend )
                    {
                        ImGui::TextDisabled( Eenheden::Imperiaal() ? T( "%s  %.0f mi vanaf start" ) : T( "%s  %.0f km vanaf start" ),
                                              statusTekst, Eenheden::Km( s.geplandeAfstandKm ) );
                    }
                    else
                    {
                        ImGui::TextDisabled( "%s", statusTekst );
                    }
                }
                else if( naarHalte >= 0.0 )
                {
                    ImGui::TextDisabled( Eenheden::Imperiaal() ? T( "%s  %.0f mi  rond %s" ) : T( "%s  %.0f km  rond %s" ),
                                          statusTekst, Eenheden::Km( s.geplandeAfstandKm ),
                                          KlokTijdOver( naarHalte ).c_str() );
                }
                else
                {
                    ImGui::TextDisabled( Eenheden::Imperiaal() ? T( "%s  %.0f mi vanaf start" ) : T( "%s  %.0f km vanaf start" ),
                                          statusTekst, Eenheden::Km( s.geplandeAfstandKm ) );
                }

                if( s.instappers > 0 || s.uitstappers > 0 )
                {
                    ImGui::SameLine( 0.0f, 10.0f );
                    if( m_kleinFont ) ImGui::PushFont( m_kleinFont );

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
            if( t.geschatUitbetaling > 0 )
            {
                ImGui::Text( T( "Geschatte uitbetaling: %lld" ), (long long)t.geschatUitbetaling );
            }
            else
            {
                TekstGedimd( T( "Uitbetaling niet doorgegeven" ) );
            }
            if( m_bus.IsScsRit() && t.tevredenheid >= 0.0 && t.passagiers > 0 )
            {
                ImGui::Text( T( "Tevredenheid: %d%%" ), (int)( t.tevredenheid * 100.0 + 0.5 ) );
                ImGui::SameLine( 0.0f, 6.0f );
                const float gezichtMaat = ImGui::GetTextLineHeight();
                const ImVec2 gezichtPos = ImGui::GetCursorScreenPos();
                ImGui::Dummy( ImVec2( gezichtMaat, gezichtMaat ) );
                int gezichtTrede = (int)( t.tevredenheid * 5.0 );
                if( gezichtTrede > 4 ) gezichtTrede = 4;
                TekenGezicht( ImGui::GetWindowDrawList(), ImVec2( gezichtPos.x + gezichtMaat * 0.5f, gezichtPos.y + gezichtMaat * 0.5f ), gezichtMaat * 0.5f, gezichtTrede );
            }
            ImGui::Text( T( "Onderweg: %s" ), FormatteerMinuten( m_bus.VerstrekenMinutenEcht() ).c_str() );

            {
                const std::int64_t busOnkosten =
                    t.tolKosten + t.veerbootKosten + t.treinKosten + t.boeteKosten;
                if( busOnkosten > 0 )
                {
                    if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }

                    const ImGuiStyle &stO = ImGui::GetStyle();
                    float regelO = ImGui::GetTextLineHeightWithSpacing();
                    int regels = 1;
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

            if( m_bus.IsScsRit() )
            {
                const double verschil = m_bus.ScsVertragingMinuten();
                if( verschil <= -1e8 )
                {
                    TekstGedimd( T( "Wacht op navigatiedata" ) );
                }
                else if( verschil > 0.5 )
                {
                    TekstSFmt( IM_COL32( 240, 140, 130, 255 ), T( "Achter op schema: %d min" ), (int)( verschil + 0.5 ) );
                }
                else
                {
                    TekstSFmt( IM_COL32( 115, 217, 173, 255 ), T( "Voor op schema: %d min" ), (int)( -verschil + 0.5 ) );
                }
            }
            else
            {
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
                    TekstGedimd( ( std::string( T( "Achter op schema, nog " ) ) +
                                    std::to_string( (int)( 60.0 - vertraging ) ) +
                                    T( " min speling voor de boete ingaat." ) ).c_str() );
                }
            }
            else
            {
                TekstGedimd( T( "Wacht op navigatiedata" ) );
            }
            }
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
                    m_reportPopupOpenen = true;
                }
            }
            else
            {
                ImGui::TextDisabled( T( "TruckersMP-ID onbekend" ) );
            }

            ImGui::EndPopup();
        }
    }

    void Overlay::TekenReportScherm()
    {
        if( m_reportPopupOpenen )
        {
            m_reportPopupOpenen = false;
            ImGui::OpenPopup( "report_scherm" );
        }

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

        const TruckTracking::SpelTeller tellerP = m_vracht.SpelTellerPauze();
        const TruckTracking::SpelTeller tellerBed = m_vracht.SpelTellerRust();
        const bool spelTellers = tellerP.loopt || tellerBed.loopt;
        if( tellerP.loopt && tellerBed.loopt ) hoogte += ImGui::GetTextLineHeight() + 2.0f;

        ImGui::PushStyleColor( ImGuiCol_ChildBg, TintKaartKleur( ImVec4( 0.9f, 0.65f, 0.2f, 1.0f ), 0.22f ) );
        ImGui::BeginChild( "live_tacho_strip", ImVec2( 0, hoogte ), true, ImGuiWindowFlags_NoScrollbar );

        TekstGedimd( T( "TACHOGRAAF" ) );
        ImGui::SameLine( 0.0f, 8.0f );

        if( spelTellers )
        {
            auto tijdTekst = []( const int minuten ) -> std::string
            {
                return minuten <= 0 ? std::string( T( "over tijd" ) ) : FormatteerMinuten( minuten );
            };

            float tekstBreedte = 0.0f;
            if( tellerP.loopt ) tekstBreedte = std::max( tekstBreedte, ImGui::CalcTextSize( tijdTekst( tellerP.minuten ).c_str() ).x );
            if( tellerBed.loopt ) tekstBreedte = std::max( tekstBreedte, ImGui::CalcTextSize( tijdTekst( tellerBed.minuten ).c_str() ).x );
            tekstBreedte += 8.0f;

            const float links = ImGui::GetCursorPosX();
            const float icoon = ImGui::GetTextLineHeight() * 0.85f;
            const float balkH = std::max( 5.0f, ImGui::GetFontSize() * 0.44f );
            ImDrawList *dl = ImGui::GetWindowDrawList();

            auto rij = [ & ]( const bool verplichtePauze, const int minuten )
            {
                TekenRustIcoon( verplichtePauze, icoon, IM_COL32( 190, 190, 195, 255 ) );
                ImGui::SameLine( 0.0f, 6.0f );

                const float fractie = static_cast<float>( std::min( 1.0, std::max( 0.0,
                            1.0 - minuten / TruckTracking::MAX_RIJ_SPELMINUTEN ) ) );
                const ImU32 kleur = minuten <= TruckTracking::WAARSCHUW_SPELMINUTEN ? IM_COL32( 212, 71, 61, 190 )
                                                                                      : IM_COL32( 217, 164, 66, 220 );
                const float balkBreedte = std::max( 30.0f, ImGui::GetContentRegionAvail().x - tekstBreedte );

                ImVec2 p = ImGui::GetCursorScreenPos();
                p.y += ( ImGui::GetTextLineHeight() - balkH ) * 0.5f;
                dl->AddRectFilled( p, ImVec2( p.x + balkBreedte, p.y + balkH ), IM_COL32( 0, 0, 0, 110 ), balkH * 0.5f );
                if( fractie > 0.01f )
                {
                    dl->AddRectFilled( p, ImVec2( p.x + balkBreedte * fractie, p.y + balkH ), kleur, balkH * 0.5f );
                }
                ImGui::Dummy( ImVec2( balkBreedte, balkH ) );
                ImGui::SameLine( 0.0f, 8.0f );
                TekstSFmt( minuten <= 0 ? IM_COL32( 240, 140, 130, 255 ) : IM_COL32( 255, 255, 255, 255 ),
                            "%s", tijdTekst( minuten ).c_str() );
            };

            ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( st.ItemSpacing.x, 2.0f ) );
            if( tellerP.loopt ) rij( true, tellerP.minuten );
            if( tellerBed.loopt )
            {
                if( tellerP.loopt ) ImGui::SetCursorPosX( links );
                rij( false, tellerBed.minuten );
            }
            ImGui::PopStyleVar();

            ImGui::EndChild();
            ImGui::PopStyleColor();

            if( m_kleinFont ) { ImGui::PopFont(); m_kleinFontActief = false; }
            return;
        }

        const double totRust = m_vracht.MinutenTotRustSpel();
        const double periode = m_vracht.VolledigeRustperiodeMinuten();
        const bool spelData = totRust >= 0.0 && periode > 1.0;
        const bool inRust = m_vracht.TachograafInRust();

        const double totPauze = m_vracht.MinutenTotVerplichtePauze();

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

            {
                float derdeB = ( ImGui::GetContentRegionAvail().x - 16 ) / 3.0f;

                std::string onder = Eenheden::Imperiaal() ? "mph" : "km/h";
                if( vs.snelheidslimietKmh >= 0.0 )
                {
                    char lang[ 40 ], kort[ 24 ];
                    snprintf( lang, sizeof( lang ), "%s - limiet %.0f", Eenheden::Imperiaal() ? "mph" : "km/h", Eenheden::Snelheid( vs.snelheidslimietKmh ) );
                    snprintf( kort, sizeof( kort ), "limiet %.0f", Eenheden::Snelheid( vs.snelheidslimietKmh ) );

                    const float ruimteKaart = derdeB - ImGui::GetStyle().WindowPadding.x * 2.0f;
                    if( ImGui::CalcTextSize( lang ).x <= ruimteKaart )       onder = lang;
                    else if( ImGui::CalcTextSize( kort ).x <= ruimteKaart )  onder = kort;
                }
                StatKaart( T( "SNELHEID" ), std::to_string( (int)Eenheden::Snelheid( m_vracht.LiveSnelheidKmh() ) ),
                            derdeB, onder.c_str(), m_vracht.RijdtTeHard() );
                ImGui::SameLine();
                StatKaart( T( "BRANDSTOF" ), getal( m_vracht.HuidigeRit().brandstofPercentage, "%" ),
                            derdeB, "" );
                ImGui::SameLine();

                const bool cruiseAan = vs.cruiseControlKmh > 1.0;
                ImGui::PushStyleColor( ImGuiCol_ChildBg,
                    cruiseAan ? TintKaartKleur( ImVec4( 0.25f, 0.69f, 0.54f, 1.0f ), 0.30f ) : KaartKleur() );
                ImGui::BeginChild( "bc_cruise", ImVec2( derdeB, KaartHoogte( true ) ), true,
                                    ImGuiWindowFlags_NoScrollbar );

                const float ruimte = ImGui::GetContentRegionAvail().x;
                const char *kopje = T( "CRUISE CONTROL" );
                if( ImGui::CalcTextSize( kopje ).x > ruimte )      kopje = T( "CRUISE" );
                if( ImGui::CalcTextSize( kopje ).x > ruimte )      kopje = "CC";
                TekstGedimd( kopje );
                if( m_kopFont ) ImGui::PushFont( m_kopFont );
                if( cruiseAan )
                {
                    TekstSFmt( IM_COL32( 115, 217, 173, 255 ), "%.0f", Eenheden::Snelheid( vs.cruiseControlKmh ) );
                    if( m_kopFont ) ImGui::PopFont();
                    TekstGedimd( Eenheden::Imperiaal() ? "mph" : T( "km/h" ) );
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

            float breedte = ( ImGui::GetContentRegionAvail().x - 16 ) / 3.0f;
            StatKaart( T( "BEREIK" ),
                        vs.bereikKm > 0.0 ? MetPunten( Eenheden::Km( vs.bereikKm ) ) : "--", breedte,
                        Eenheden::Imperiaal() ? T( "mi te gaan" ) : T( "km te gaan" ) );
            ImGui::SameLine();
            auto naarKmPerLiter = []( double literPer100Km ) -> double
            {
                if( literPer100Km <= 0.0 ) return -1.0;
                return 100.0 / literPer100Km;
            };

            auto verbruikGetoond = []( double kmpl ) -> double
            {
                double v = Eenheden::Verbruik( kmpl );
                const double plafond = Eenheden::Imperiaal() ? 235.0 : 99.9;
                return v > plafond ? plafond : v;
            };

            std::string verbruikWaarde = "--";
            std::string verbruikOnder;
            if( vs.staatStil && vs.verbruikLiterPerUur >= 0.0 )
            {
                verbruikWaarde = getal( Eenheden::LitersPerUur( vs.verbruikLiterPerUur ), "", 1 );
                verbruikOnder = vs.echtStil
                    ? ( Eenheden::Imperiaal() ? T( "gal/uur - stationair" ) : T( "l/uur - stationair" ) )
                    : T( Eenheden::PerUurLabel() );
                const float ruimte = breedte - ImGui::GetStyle().WindowPadding.x * 2.0f;
                if( ImGui::CalcTextSize( verbruikOnder.c_str() ).x > ruimte )
                    verbruikOnder = Eenheden::Imperiaal() ? T( "gal/uur stat." ) : T( "l/uur stat." );
                if( ImGui::CalcTextSize( verbruikOnder.c_str() ).x > ruimte ) verbruikOnder = T( Eenheden::PerUurLabel() );
            }
            else
            {
                const double nuKmpl = naarKmPerLiter( vs.verbruikNuLiterPer100Km );
                const double gemKmpl = naarKmPerLiter( vs.verbruikGemiddeldLiterPer100Km );
                if( nuKmpl > 0.0 )
                {
                    verbruikWaarde = getal( verbruikGetoond( nuKmpl ), "", 1 );
                }
                if( gemKmpl > 0.0 )
                {
                    const std::string gem = getal( verbruikGetoond( gemKmpl ), "", 1 );
                    verbruikOnder = std::string( Eenheden::VerbruikLabel() ) + " - gem " + gem;
                    const float ruimte = breedte - ImGui::GetStyle().WindowPadding.x * 2.0f;
                    if( ImGui::CalcTextSize( verbruikOnder.c_str() ).x > ruimte ) verbruikOnder = "gem " + gem;
                    if( ImGui::CalcTextSize( verbruikOnder.c_str() ).x > ruimte ) verbruikOnder = Eenheden::VerbruikLabel();
                }
                else
                {
                    verbruikOnder = Eenheden::VerbruikLabel();
                }
            }
            StatKaart( T( "VERBRUIK" ), verbruikWaarde, breedte, verbruikOnder.c_str() );
            ImGui::SameLine();
            StatKaart( T( "KM-STAND" ),
                        vs.kilometerstandKm >= 0.0 ? MetPunten( Eenheden::Km( vs.kilometerstandKm ) ) : "--",
                        breedte, Eenheden::Imperiaal() ? T( "mi totaal" ) : T( "km totaal" ) );

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
                        snprintf( links, sizeof( links ), "+%.0f %s%s", Eenheden::Liters( t.liters ),
                                   Eenheden::VolumeLabel(), t.garage ? T( " garage" ) : "" );
                        TekstS( links );

                        std::string rechtsTekst = std::string( SpelInfo::Munt() ) + " " + getal( t.kostenEuro, "", 2 );
                        if( t.kmStand > 0.0 ) rechtsTekst += T( "   bij " ) + MetPunten( Eenheden::Km( t.kmStand ) )
                                                             + " " + Eenheden::AfstandLabel();
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

            if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }

            const ImGuiStyle &st = ImGui::GetStyle();
            float regel = ImGui::GetTextLineHeight() + 2.0f;
            float kopRegel = ImGui::GetTextLineHeightWithSpacing();
            float totaalVoorRij = ImGui::GetContentRegionAvail().x;

            float schadeKaartBreedte = ( totaalVoorRij - 16.0f ) * 2.0f / 3.0f;
            float proefKolom = ( schadeKaartBreedte - 24.0f ) / 2.0f;
            bool verwachtNaastElkaar = proefKolom >= 150.0f;
            int balkRegels = verwachtNaastElkaar ? 5 : 8;
            float schadeNodig = kopRegel + balkRegels * regel;
            float tachoNodig = kopRegel + ( regel + 3.0f ) + 3 * regel;
            float rijHoogte = st.WindowPadding.y * 2.0f + std::max( schadeNodig, tachoNodig ) + 4.0f;

            float totaal = totaalVoorRij;

            const bool tachoErnaast = true;
            float derde = ( totaal - 16.0f ) / 3.0f;
            float tachoHoogte = rijHoogte;

            ImGui::PushStyleColor( ImGuiCol_ChildBg, TintKaartKleur( ImVec4( 0.9f, 0.65f, 0.2f, 1.0f ), 0.22f ) );
            ImGui::BeginChild( "bc_tacho", ImVec2( derde, tachoHoogte ), true, ImGuiWindowFlags_NoScrollbar );
            TekstGedimd( T( "TACHOGRAAF" ) );

            {
                const bool inRust = m_vracht.TachograafInRust();

                float balkH = std::max( 5.0f, ImGui::GetFontSize() * 0.44f );
                float breed = ImGui::GetContentRegionAvail().x;
                ImDrawList *dl = ImGui::GetWindowDrawList();

                const TruckTracking::SpelTeller tellerP = m_vracht.SpelTellerPauze();
                const TruckTracking::SpelTeller tellerBed = m_vracht.SpelTellerRust();
                const bool spelTellers = tellerP.loopt || tellerBed.loopt;

                bool krap = false;
                if( tellerP.loopt && tellerBed.loopt )
                {
                    const float sp = st.ItemSpacing.y;
                    const float klein = ImGui::GetTextLineHeight();
                    const float nodig = 2.0f * ( balkH + 3.0f + sp + klein + sp ) + klein * 1.25f + sp + klein;
                    krap = nodig > ImGui::GetContentRegionAvail().y;
                }
                if( krap ) ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( st.ItemSpacing.x, 2.0f ) );

                if( spelTellers )
                {
                    auto spelRij = [ & ]( const bool verplichtePauze, const int minuten )
                    {
                        const float f = static_cast<float>( std::min( 1.0, std::max( 0.0,
                                1.0 - minuten / TruckTracking::MAX_RIJ_SPELMINUTEN ) ) );
                        const ImU32 k = minuten <= 0 ? IM_COL32( 212, 71, 61, 190 )
                                        : minuten <= TruckTracking::WAARSCHUW_SPELMINUTEN
                                              ? IM_COL32( 217, 164, 66, 220 )
                                              : IM_COL32( 130, 170, 210, 200 );

                        ImVec2 bp = ImGui::GetCursorScreenPos();
                        dl->AddRectFilled( bp, ImVec2( bp.x + breed, bp.y + balkH ), IM_COL32( 0, 0, 0, 110 ), balkH * 0.5f );
                        if( f > 0.01f )
                        {
                            dl->AddRectFilled( bp, ImVec2( bp.x + breed * f, bp.y + balkH ), k, balkH * 0.5f );
                        }
                        ImGui::Dummy( ImVec2( breed, balkH + ( krap ? 1.0f : 3.0f ) ) );

                        const ImU32 tk = minuten <= 0 ? IM_COL32( 240, 140, 130, 255 ) : IM_COL32( 190, 190, 195, 255 );
                        TekenRustIcoon( verplichtePauze, ImGui::GetTextLineHeight() * 0.85f, tk );
                        ImGui::SameLine( 0.0f, 6.0f );
                        if( minuten <= 0 ) TekstSFmt( tk, "%s", T( "over tijd" ) );
                        else TekstGedimd( FormatteerMinuten( minuten ).c_str() );
                    };
                    if( tellerP.loopt ) spelRij( true, tellerP.minuten );
                    if( tellerBed.loopt ) spelRij( false, tellerBed.minuten );
                }

                if( !spelTellers )
                {
                    const double totPauze = m_vracht.MinutenTotVerplichtePauze();
                    const float pf = static_cast<float>( std::min( 1.0, std::max( 0.0,
                            1.0 - totPauze / m_vracht.RijPeriodeSpelMinuten() ) ) );
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
                    if( m_kleinFont ) ImGui::PopFont();
                    TekstSFmt( IM_COL32( 255, 255, 255, 255 ), "%s",
                                FormatteerMinuten( m_vracht.TachograafRijtijdMinuten() ).c_str() );
                    if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
                    TekstGedimd( T( "gereden" ) );
                }
                if( krap ) ImGui::PopStyleVar();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();

            if( tachoErnaast ) ImGui::SameLine();

            ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
            ImGui::BeginChild( "bc_schade", ImVec2( 0, rijHoogte ), true, ImGuiWindowFlags_NoScrollbar );
            {
                float binnen = ImGui::GetContentRegionAvail().x;
                float kolom = ( binnen - 8.0f ) / 2.0f;

                float minKolom = ImGui::CalcTextSize( "Chassis" ).x + 6.0f
                                  + ImGui::CalcTextSize( "100%" ).x + 5.0f + 40.0f;
                bool naastElkaar = kolom >= minKolom;
                if( !naastElkaar ) kolom = binnen;

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

            TekenRijstijl();

            if( m_kleinFont ) { ImGui::PopFont(); m_kleinFontActief = false; }
        }
    }

    void Overlay::TekenSpelersTab()
    {
        Logboek::Spoor( "players: refreshing positions" );
        m_spelers.VerversPosities();
        std::vector<SpelerRecord> spelers = m_spelers.GeefSpelers();

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
        float scrollPixels = fmodf( m_minimapScrollKm * 40.0f, 1000.0f );

        const float mapGrootte = 200.0f;
        ImVec2 startPos = ImGui::GetCursorScreenPos();
        ImVec2 midden = ImVec2( startPos.x + mapGrootte / 2, startPos.y + mapGrootte / 2 );
        ImDrawList *draw = ImGui::GetWindowDrawList();

        draw->AddRectFilled( startPos, ImVec2( startPos.x + mapGrootte, startPos.y + mapGrootte ),
                              IM_COL32( 12, 14, 12, 235 ), 10.0f );

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

        draw->AddLine( ImVec2( startPos.x, midden.y - 6 ), ImVec2( startPos.x + mapGrootte, midden.y + 6 ),
                        IM_COL32( 255, 255, 255, 55 ), 4.0f );
        draw->AddLine( ImVec2( midden.x - 8, startPos.y ), ImVec2( midden.x + 8, startPos.y + mapGrootte ),
                        IM_COL32( 255, 255, 255, 55 ), 4.0f );

        draw->AddCircleFilled( midden, 5.5f, AccentKleurU32( 1.0f ) );
        draw->AddCircle( midden, 5.5f, IM_COL32( 0, 0, 0, 150 ), 12, 1.5f );

        const float bereikMeter = 800.0f;
        float maxStraal = mapGrootte / 2 - 10;

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

        const int MAX_STIPPEN = 50;

        int getekend = 0;
        int zonderPositie = 0;
        for( const SpelerRecord &s : spelers )
        {
            if( !s.positieBekend )
            {
                ++zonderPositie;
                continue;
            }
            if( getekend >= MAX_STIPPEN ) break;
            ++getekend;

            float straal = std::min( s.afstandMeter / bereikMeter, 1.0f ) * maxStraal;
            float hoek = ( s.peilingGraden - 90.0f ) * 3.14159265f / 180.0f;
            ImVec2 punt( midden.x + straal * cosf( hoek ), midden.y + straal * sinf( hoek ) );

            const bool eigenVtc = m_vtcRadarMarkering && IsEigenVtc( s );

            ImU32 kleur = eigenVtc                  ? IM_COL32( 91, 141, 239, 255 )
                        : s.isTeam || s.isManager ? IM_COL32( 232, 80, 70, 255 )
                        : s.isModerator            ? IM_COL32( 232, 80, 70, 255 )
                        : IsPatron( s )             ? IM_COL32( 220, 100, 220, 255 )
                                                     : IM_COL32( 63, 176, 138, 255 );

            const float nabij = 1.0f - std::min( s.afstandMeter / bereikMeter, 1.0f );
            const float punthoogte = 2.6f + nabij * 2.4f;
            const int alpha = 130 + static_cast<int>( nabij * 125.0f );
            const ImU32 stipKleur = ( kleur & 0x00FFFFFF ) | ( static_cast<ImU32>( alpha ) << 24 );

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

        {
            int metPositie = 0;
            for( const SpelerRecord &s : spelers ) if( s.positieBekend ) ++metPositie;

            TekstSFmt( IM_COL32( 255, 255, 255, 255 ), T( "%d spelers in bereik" ), (int)spelers.size() );
            ImGui::SameLine( 0.0f, 10.0f );
            TekstGedimdFmt( T( "(%d op de kaart)" ), metPositie );

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

            if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
            struct Legenda { ImU32 kleur; const char *naam; };
            static const Legenda legenda[] = {
                { IM_COL32( 232, 80, 70, 255 ),  "team/mod" },
                { IM_COL32( 220, 100, 220, 255 ), "patron" },
                { IM_COL32( 63, 176, 138, 255 ),  "speler" },
                { IM_COL32( 91, 141, 239, 255 ),  "eigen VTC" },
            };
            constexpr int AANTAL_LEGENDA = static_cast<int>( sizeof( legenda ) / sizeof( legenda[ 0 ] ) );
            for( int i = 0; i < AANTAL_LEGENDA; ++i )
            {
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

                if( nummer % 2 == 0 )
                {
                    ld->AddRectFilled( rijStart, ImVec2( rijStart.x + breedte, rijStart.y + regelHoogte ),
                                        IM_COL32( 255, 255, 255, 10 ), 3.0f );
                }
                ld->AddRectFilled( ImVec2( rijStart.x, rijStart.y + 2.0f ),
                                    ImVec2( rijStart.x + 3.0f, rijStart.y + regelHoogte - 2.0f ),
                                    rolKleur, 1.5f );

                ImGui::SetCursorPosX( ImGui::GetCursorPosX() + 8.0f );
                TekstGedimdFmt( "%2d", nummer );
                ImGui::SameLine( 0.0f, 6.0f );

                const float ruimteNaam = breedte - 150.0f;
                std::string naam = s.gebruikersnaam;

                if( ImGui::CalcTextSize( naam.c_str() ).x > ruimteNaam )
                {
                    std::string kort;
                    int diepte = 0;
                    for( char c : s.gebruikersnaam )
                    {
                        if( c == '[' || c == '(' || c == '{' ) { ++diepte; continue; }
                        if( c == ']' || c == ')' || c == '}' ) { if( diepte > 0 ) --diepte; continue; }
                        if( diepte == 0 )
                        {
                            if( c == '|' ) break;
                            kort += c;
                        }
                    }
                    while( !kort.empty() && kort.front() == ' ' ) kort.erase( kort.begin() );
                    while( !kort.empty() && kort.back() == ' ' ) kort.pop_back();

                    if( !kort.empty() && ImGui::CalcTextSize( kort.c_str() ).x <= ruimteNaam )
                    {
                        naam = kort;
                    }
                    else
                    {
                        if( !kort.empty() ) naam = kort;
                        while( naam.size() > 3 && ImGui::CalcTextSize( naam.c_str() ).x > ruimteNaam )
                        {
                            naam.erase( naam.size() - 2 );
                            naam.back() = '.';
                        }
                    }
                }
                TekstS( naam.c_str() );

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

                if( naam != s.gebruikersnaam && ImGui::IsItemHovered() )
                {
                    ImGui::SetTooltip( "%s", s.gebruikersnaam.c_str() );
                }

                char rechts[ 48 ];
                snprintf( rechts, sizeof( rechts ), "%4.0f m   %3u ms", s.afstandMeter, (unsigned)s.pingMs );
                const float rechtsBreedte = ImGui::CalcTextSize( rechts ).x;
                ImGui::SameLine( 0.0f, 0.0f );
                ImGui::SetCursorPosX( ImGui::GetCursorPosX() +
                                       std::max( 6.0f, breedte - 40.0f - rechtsBreedte -
                                                  ( ImGui::GetCursorPosX() - 8.0f ) ) );
                TekstGedimd( rechts );

                ImGui::SameLine( breedte - 22.0f );
                TekenSpelerContextKnop( s, std::to_string( s.spelerId ) );
            }
            ImGui::EndChild();

            TekenReportScherm();

            if( m_kleinFont ) { ImGui::PopFont(); m_kleinFontActief = false; }
        }
    }

    float Overlay::SectieHoogte( int tekstRegels, int velden, int spaties ) const
    {
        const ImGuiStyle &st = ImGui::GetStyle();
        const float tekst = ImGui::GetTextLineHeight();
        const float veld  = ImGui::GetFrameHeight();

        const float tussen = st.ItemSpacing.y;

        float hoogte = st.WindowPadding.y * 2.0f;
        hoogte += tekst + tussen;
        hoogte += tussen;
        hoogte += tekstRegels * ( tekst + tussen );
        hoogte += velden * ( veld + tussen );
        hoogte += spaties * tussen;
        return hoogte;
    }

    void Overlay::SectieStart( const char *naam, ImVec4 kleur, float vasteHoogte )
    {
        ImGui::Spacing();
        ImGui::Spacing();

        float sectieBreedte = ImGui::GetContentRegionAvail().x;
        if( sectieBreedte > 520.0f ) sectieBreedte = 520.0f;

        ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 12, 10 ) );

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
        SectieStart( "UITERLIJK", ImVec4( m_accentKleur[ 0 ] + 0.2f, m_accentKleur[ 1 ] + 0.15f,
                                            m_accentKleur[ 2 ] + 0.1f, 1.0f ),
                      SectieHoogte( 1, 5 ) );
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

        {
            static const char *keuzes[] = { "Automatisch", "Metrisch (km, liter)", "Imperiaal (mi, gallon)" };
            ImGui::SetNextItemWidth( VeldBreedte() );
            if( ImGui::BeginCombo( T( "Eenheden" ), T( keuzes[ std::clamp( m_eenheden, 0, 2 ) ] ) ) )
            {
                for( int i = 0; i < 3; ++i )
                {
                    if( ImGui::Selectable( T( keuzes[ i ] ), m_eenheden == i ) )
                    {
                        m_eenheden = i;
                        Eenheden::Keuze() = static_cast<EenheidKeuze>( i );
                        std::snprintf( m_prijsBuffer, sizeof( m_prijsBuffer ), "%.2f",
                                        Eenheden::PrijsPerVolume( m_brandstof.PrijsPerLiter() ) );
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

        {
            const TruckTracking::TachoInstelling voorHoogte = m_vracht.HuidigeTachoInstelling();
            const bool alleenKiezen =
                ( voorHoogte.stand == TruckTracking::TachoStand::SpelVolgen );
            SectieStart( "TACHOGRAAF", ImVec4( 0.90f, 0.72f, 0.35f, 1.0f ),
                          alleenKiezen ? SectieHoogte( 2, 2, 2 )
                                        : SectieHoogte( 4, 6, 2 ) );
        }
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

        SectieStart( "GEMIDDELDE", ImVec4( 0.35f, 0.75f, 0.60f, 1.0f ),
                      SectieHoogte( 1, 1, 1 ) );
        ImGui::Spacing();
        if( ImGui::Button( T( "Reset gemiddelde" ) ) )
        {
            m_vracht.ResetVoertuigTeller();
        }
        TekstGedimd( T( "Druk samen met de reset op je dashboard" ) );
        SectieEind();

        const bool landAuto = m_brandstof.LandAutomatisch();
        SectieStart( "BRANDSTOFPRIJS", ImVec4( 0.90f, 0.35f, 0.32f, 1.0f ),
                      SectieHoogte( 5, landAuto ? 4 : 5, 4 ) );

        {
            bool aan = landAuto;
            if( ImGui::Checkbox( T( "Land automatisch (positie)" ), &aan ) ) m_brandstof.ZetLandAutomatisch( aan );

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
                    TekstGedimdFmt( T( "Uit brandstofprijzen.json: %s %.2f" ),
                                     SpelInfo::Munt(), m_brandstof.PrijsVoorLand( huidig ) );
                }
            }
        }

        ImGui::Spacing();
        TekstGedimd( T( "Reserve als tankbeurt en plek onbekend zijn" ) );
        ImGui::Spacing();
        ImGui::SetNextItemWidth( VeldBreedte() );
        const std::string prijsLabel = std::string( SpelInfo::Munt() )
            + ( Eenheden::Imperiaal() ? T( " per gallon" ) : T( " per liter" ) );
        if( ImGui::InputText( prijsLabel.c_str(), m_prijsBuffer, sizeof( m_prijsBuffer ),
                               ImGuiInputTextFlags_CharsDecimal ) )
        {
            double waarde = atof( m_prijsBuffer );
            if( waarde > 0.0 )
            {
                m_brandstof.ZetPrijsPerLiter( Eenheden::PrijsInvoerNaarLiter( waarde ) );
            }
        }
        ImGui::Spacing();
        TekstGedimd( T( "Direct opgeslagen in instellingen.json" ) );

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

        const bool envelop = ( m_vtc.Formaat() == 1 );
        SectieStart( "VTC-WEBHOOK", ImVec4( 0.55f, 0.85f, 0.60f, 1.0f ),
                      SectieHoogte( envelop ? 5 : 4, envelop ? 7 : 6, 3 ) );
        ImGui::Spacing();
        ImGui::TextDisabled( T( "Elke afgeronde rit als JSON naar je VTC" ) );
        ImGui::Spacing();

        if( !m_vtcBuffersGeladen )
        {
            std::snprintf( m_vtcUrlBuffer, sizeof( m_vtcUrlBuffer ), "%s", m_vtc.EndpointUrl().c_str() );
            std::snprintf( m_vtcSleutelBuffer, sizeof( m_vtcSleutelBuffer ), "%s", m_vtc.Sleutel().c_str() );
            m_vtcBuffersGeladen = true;
        }

        bool vtcAan = m_vtc.IsIngeschakeld();
        if( ImGui::Checkbox( T( "Ritten doorsturen" ), &vtcAan ) )
        {
            m_vtc.ZetIngeschakeld( vtcAan );
        }

        float vtcKnop = ImGui::CalcTextSize( T( "Plakken" ) ).x + 20.0f;
        ImGui::SetNextItemWidth( -( vtcKnop + 8.0f ) );
        if( ImGui::InputText( "##vtcurl", m_vtcUrlBuffer, sizeof( m_vtcUrlBuffer ) ) )
        {
            m_vtc.ZetEndpointUrl( m_vtcUrlBuffer );
        }
        ImGui::SameLine();
        if( ImGui::Button( ( std::string( T( "Plakken" ) ) + "##vtcurlplak" ).c_str() ) )
        {
            const std::string uitKlembord = LeesVanKlembord();
            if( !uitKlembord.empty() )
            {
                std::snprintf( m_vtcUrlBuffer, sizeof( m_vtcUrlBuffer ), "%s", uitKlembord.c_str() );
                m_vtc.ZetEndpointUrl( m_vtcUrlBuffer );
            }
        }
        ImGui::TextDisabled( T( "Adres (https), van je VTC gekregen" ) );

        ImGui::SetNextItemWidth( -( vtcKnop + 8.0f ) );
        if( ImGui::InputText( "##vtcsleutel", m_vtcSleutelBuffer, sizeof( m_vtcSleutelBuffer ),
                              ImGuiInputTextFlags_Password ) )
        {
            m_vtc.ZetSleutel( m_vtcSleutelBuffer );
        }
        ImGui::SameLine();
        if( ImGui::Button( ( std::string( T( "Plakken" ) ) + "##vtcsleutelplak" ).c_str() ) )
        {
            const std::string uitKlembord = LeesVanKlembord();
            if( !uitKlembord.empty() )
            {
                std::snprintf( m_vtcSleutelBuffer, sizeof( m_vtcSleutelBuffer ), "%s", uitKlembord.c_str() );
                m_vtc.ZetSleutel( m_vtcSleutelBuffer );
            }
        }
        ImGui::TextDisabled( T( "Sleutel (gaat mee als Authorization: Bearer)" ) );

        {
            static const char *formaten[] = { "CabNavi (open ritformaat)", "Envelop met handtekening" };
            const int huidigF = std::clamp( m_vtc.Formaat(), 0, 1 );
            if( ImGui::BeginCombo( T( "Formaat" ), T( formaten[ huidigF ] ) ) )
            {
                for( int i = 0; i < 2; ++i )
                    if( ImGui::Selectable( T( formaten[ i ] ), huidigF == i ) ) { m_vtc.ZetFormaat( i ); m_vtcBuffersGeladen = false; }
                ImGui::EndCombo();
            }
            if( envelop )
            {
                if( !m_hubGeheimGeladen )
                {
                    std::snprintf( m_hubGeheimBuffer, sizeof( m_hubGeheimBuffer ), "%s", m_vtc.HubGeheim().c_str() );
                    m_hubGeheimGeladen = true;
                }
                ImGui::SetNextItemWidth( -( vtcKnop + 8.0f ) );
                if( ImGui::InputText( "##hubgeheim", m_hubGeheimBuffer, sizeof( m_hubGeheimBuffer ), ImGuiInputTextFlags_Password ) )
                    m_vtc.ZetHubGeheim( m_hubGeheimBuffer );
                ImGui::SameLine();
                if( ImGui::Button( ( std::string( T( "Plakken" ) ) + "##hubgeheimplak" ).c_str() ) )
                {
                    const std::string uitKlembord = LeesVanKlembord();
                    if( !uitKlembord.empty() )
                    {
                        std::snprintf( m_hubGeheimBuffer, sizeof( m_hubGeheimBuffer ), "%s", uitKlembord.c_str() );
                        m_vtc.ZetHubGeheim( m_hubGeheimBuffer );
                    }
                }
                ImGui::TextDisabled( T( "Geheim van de hub, voor de handtekening" ) );
            }
        }

        {
            bool live = m_vtc.LivePositie();
            if( ImGui::Checkbox( T( "Positie live delen met mijn VTC" ), &live ) ) m_vtc.ZetLivePositie( live );
            TekstGedimd( T( "Elke 10 s je plek naar het adres hierboven" ) );
        }

        ImGui::Spacing();
        if( ImGui::Button( ( std::string( T( "Testbericht sturen" ) ) + "##vtctest" ).c_str() ) )
        {
            m_vtc.StuurTestbericht();
        }
        SectieEind();

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

        ImGui::Spacing();
        if( ImGui::Checkbox( T( "Uitgebreid logboek" ), &m_uitgebreidLog ) )
        {
            Logboek::Uitgebreid() = m_uitgebreidLog;
            SlaUiterlijkOp();
        }
        TekstGedimd( T( "Alleen bij het zoeken van een fout" ) );

        ImGui::Spacing();
        if( ImGui::Checkbox( T( "Klok tonen (echte tijd)" ), &m_klokTonen ) )
        {
            SlaUiterlijkOp();
        }

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

        static int filter = 0;
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

        for( auto it = recent.rbegin(); it != recent.rend(); ++it )
        {
            const Trip &t = *it;
            bool isBus = t.type == TripType::Bus;
            if( filter == 1 && isBus ) continue;
            if( filter == 2 && !isBus ) continue;

            ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
            ImGui::BeginChild( ( "rit_" + t.id ).c_str(),
                                ImVec2( 0, KaartHoogte( true, true ) ),
                                true, ImGuiWindowFlags_NoScrollbar );

            ImVec2 kaartPos = ImGui::GetCursorScreenPos();
            ImDrawList *kd = ImGui::GetWindowDrawList();
            ImU32 statusKleur = t.status == TripStatus::Voltooid ? IM_COL32( 63, 176, 138, 255 ) : IM_COL32( 226, 85, 74, 255 );
            kd->AddRectFilled( kaartPos, ImVec2( kaartPos.x + 3, kaartPos.y + 44 ), statusKleur, 2.0f );

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
            if( isBus && t.busMarkt == "tourist_job" )
                ImGui::Text( T( "Toeristenrit -- %d haltes" ), (int)t.haltes.size() );
            else if( isBus && !t.busMarkt.empty() )
                ImGui::Text( T( "Lijndienst -- %d haltes" ), (int)t.haltes.size() );
            else if( isBus )
                ImGui::Text( T( "Buslijn -- %d haltes" ), (int)t.haltes.size() );
            else
                ImGui::Text( "%s -> %s", t.bronStad.c_str(), t.bestemmingStad.c_str() );
            if( isBus && t.status == TripStatus::Voltooid && t.tevredenheid >= 0.0 )
                ImGui::TextDisabled( "%.0f %s -- %s -- %d%%", Eenheden::Km( t.afgelegdeAfstandKm ), Eenheden::AfstandLabel(),
                                      T( "Voltooid" ), (int)( t.tevredenheid * 100.0 + 0.5 ) );
            else
            ImGui::TextDisabled( "%.0f %s -- %s", Eenheden::Km( t.afgelegdeAfstandKm ), Eenheden::AfstandLabel(),
                                  t.status == TripStatus::Voltooid ? T( "Voltooid" ) : T( "Geannuleerd" ) );
            ImGui::EndGroup();

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

        const double onderwegKosten = (double)totalen.totaalBoeteKosten
                                    + (double)totalen.totaalTolKosten
                                    + (double)totalen.totaalVeerbootKosten
                                    + (double)totalen.totaalTreinKosten;
        const double totaleKosten = totalen.totaalBrandstofKostenEuro
                                  + onderwegKosten
                                  + totalen.geannuleerdKosten;
        double netto = (double)totalen.totaalInkomen - totaleKosten;

        float breedte = ( ImGui::GetContentRegionAvail().x - 16 ) / 3.0f;

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

        statKaart( T( "VRACHT" ), std::to_string( totalen.aantalVrachtRitten ) );
        ImGui::SameLine();
        statKaart( T( "BUS" ), std::to_string( totalen.aantalBusRitten ) );
        ImGui::SameLine();
        statKaart( T( "AFSTAND" ), std::to_string( (int)Eenheden::Km( totalen.totaalAfstandKm ) )
                    + " " + Eenheden::AfstandLabel() );

        char buf[ 32 ];
        snprintf( buf, sizeof( buf ), "%s %lld", SpelInfo::Munt(), (long long)totalen.totaalInkomen );
        statKaart( T( "VERDIEND" ), buf );
        ImGui::SameLine();
        snprintf( buf, sizeof( buf ), "%s %.0f", SpelInfo::Munt(), totalen.totaalBrandstofKostenEuro );
        statKaart( T( "BRANDSTOF" ), buf );
        ImGui::SameLine();
        snprintf( buf, sizeof( buf ), "%s %.0f", SpelInfo::Munt(), netto );
        statKaart( T( "NETTO" ), buf, true );

        {
            const FuelCosts::LeegRijden leeg = m_brandstof.LeegTotaal();
            const int aantal = m_brandstof.AantalTankbeurten();
            const FuelCosts::PrijsInfo prijs = m_brandstof.HuidigePrijsInfo();

            if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }
            const ImGuiStyle &stK = ImGui::GetStyle();
            const float regelK = ImGui::GetTextLineHeight() + 3.0f;

            int regels = 6;
            const bool metTanken = aantal > 0;
            if( metTanken ) regels += 3 + ( prijs.land.empty() ? 0 : 1 );

            auto regelLR = [ & ]( const std::string &links, const std::string &rechts )
            {
                const float breed = ImGui::GetContentRegionAvail().x;
                TekstS( links.c_str() );
                const float rb = ImGui::CalcTextSize( rechts.c_str() ).x;
                ImGui::SameLine( 0.0f, 0.0f );
                ImGui::SetCursorPosX( ImGui::GetCursorPosX() +
                                       std::max( 8.0f, breed - rb - ImGui::GetCursorPosX() + stK.WindowPadding.x ) );
                TekstGedimd( rechts.c_str() );
            };
            auto munt = [ & ]( double v, int dec )
            {
                char b[ 32 ];
                std::snprintf( b, sizeof( b ), dec == 2 ? "%s %.2f" : "%s %.0f", SpelInfo::Munt(), v );
                return std::string( b );
            };

            ImGui::Spacing();
            ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
            ImGui::BeginChild( "stat_kostenvak", ImVec2( 0, stK.WindowPadding.y * 2.0f + regels * regelK + 4.0f ),
                                true, ImGuiWindowFlags_NoScrollbar );

            TekstGedimd( T( "KOSTEN BUITEN BRANDSTOF" ) );
            regelLR( T( "Boetes, tol, veer en trein" ), munt( onderwegKosten, 0 ) );
            regelLR( T( "Geannuleerde ritten" ), munt( totalen.geannuleerdKosten, 0 ) );

            TekstGedimd( T( "LEEG GEREDEN" ) );
            {
                char l[ 64 ];
                std::snprintf( l, sizeof( l ), "%.0f %s \xc2\xb7 %.0f %s", Eenheden::Km( leeg.km ), Eenheden::AfstandLabel(),
                               Eenheden::Liters( leeg.liters ), Eenheden::VolumeLabel() );
                regelLR( l, munt( leeg.kosten, 0 ) );
                regelLR( T( "Netto na leeg rijden" ), munt( netto - leeg.kosten, 0 ) );
            }

            if( metTanken )
            {
                TekstGedimd( T( "GETANKT" ) );
                char l[ 64 ];
                std::snprintf( l, sizeof( l ), "%dx \xc2\xb7 %.0f %s", aantal,
                               Eenheden::Liters( m_brandstof.TotaalGetanktLiters() ), Eenheden::VolumeLabel() );
                regelLR( l, munt( m_brandstof.TotaalGetanktKosten(), 0 ) );
                const char *bronTekst = prijs.bron == 0 ? T( "Prijs laatste tankbeurt" )
                                      : prijs.bron == 1 ? T( "Prijs hier" ) : T( "Prijs ingesteld" );
                regelLR( bronTekst, munt( Eenheden::PrijsPerVolume( prijs.prijs ), 2 ) );
                if( !prijs.land.empty() )
                {
                    std::string plek = prijs.land;
                    if( !plek.empty() ) plek[ 0 ] = (char)std::toupper( (unsigned char)plek[ 0 ] );
                    if( prijs.garage ) plek += std::string( " \xc2\xb7 " ) + T( "eigen garage" );
                    TekstGedimd( plek.c_str() );
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            if( m_kleinFont ) { ImGui::PopFont(); m_kleinFontActief = false; }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        KopBalk( T( "TRUCKERSMP LIVE" ) );

        {
            if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }

            bool aan = m_webApi.Ingeschakeld();
            if( ImGui::Checkbox( T( "Servergegevens en evenementen ophalen" ), &aan ) )
            {
                m_webApi.ZetIngeschakeld( aan );
                m_webApiAan = aan;
                SlaUiterlijkOp();
            }

            {
                const auto planning = MijnConvooien();
                if( !planning.empty() )
                {
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
                        if( !e.startTijd.empty() && e.startTijd > grens ) break;
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
                            std::string regel = e.startTijd.size() >= 16
                                                    ? e.startTijd.substr( 5, 11 )
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
                return;
            }

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
                    if( s.spel != "ETS2" ) continue;
                    if( ++rij > 10 ) break;

                    const float breed = ImGui::GetContentRegionAvail().x;
                    TekstS( s.naam.c_str(), s.online ? IM_COL32( 255, 255, 255, 255 )
                                                      : IM_COL32( 190, 130, 125, 255 ) );

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

            const std::vector<EvenementInfo> evenementen = m_webApi.Evenementen();
            ImGui::Spacing();
            if( !evenementen.empty() )
            {
                const ImGuiStyle &stE = ImGui::GetStyle();
                const float regelE = ImGui::GetTextLineHeight() + 4.0f;
                const int aantal = (int)std::min<std::size_t>( evenementen.size(), 5 );

                ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
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
        ImGui::TextDisabled( T( "dichtstbijzijnde bij de schade -- geen bewijs" ) );
        ImGui::Spacing();

        int aantalFrames = m_incident.AantalFrames();

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

        ImGui::SetNextItemWidth( -1 );
        ImGui::SliderInt( "##tijdlijn", &m_incidentFrameIndex, 0, std::max( 0, aantalFrames - 1 ), frame->tijdLabel.c_str() );
        ImGui::Spacing();

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
            if( !s.positieBekend ) continue;
            float straal = std::min( s.afstandMeter / bereikMeter, 1.0f ) * maxStraal;
            float hoek = ( s.peilingGraden - 90.0f ) * 3.14159265f / 180.0f;
            ImVec2 punt( midden.x + straal * cosf( hoek ), midden.y + straal * sinf( hoek ) );

            const float nabij = 1.0f - std::min( s.afstandMeter / bereikMeter, 1.0f );
            const float punthoogte = 2.6f + nabij * 2.4f;
            const int alpha = 130 + static_cast<int>( nabij * 125.0f );
            draw->AddCircleFilled( punt, punthoogte,
                                    IM_COL32( 226, 85, 74, static_cast<ImU32>( alpha ) ) );

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

        TekenReportScherm();
    }

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
        if( !m_vtcAan && !m_eigenConvooien ) return {};

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
        if( regel.empty() ) return;

        if( m_kleinFont ) ImGui::PushFont( m_kleinFont );
        TekstS( regel.c_str(), IM_COL32( 91, 141, 239, 255 ) );
        if( m_kleinFont ) ImGui::PopFont();
    }

    void Overlay::TekenRijstijl()
    {
        if( !m_zuinigheidTonen ) return;

        const TruckTracking::RijstijlStatus r = m_vracht.HuidigeRijstijl();
        if( r.nu == TruckTracking::RijstijlStatus::Niets ) return;

        const ImU32 groen = IM_COL32( 63, 176, 138, 255 );
        const ImU32 grijs = IM_COL32( 154, 154, 154, 255 );
        const ImU32 oranje = IM_COL32( 224, 138, 90, 255 );
        const ImU32 dim = IM_COL32( 122, 122, 122, 255 );

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
            TekstS( "\xc2\xb7", dim, false );
            ImGui::SameLine();
        }
        TekstS( nuWoord, nuKleur, false );
        if( m_kleinFont ) ImGui::PopFont();
    }

    bool Overlay::IsPatron( const SpelerRecord &s ) const
    {
        if( s.accountId != 0 )
        {
            const int viaApi = m_webApi.SpelerIsPatron( s.accountId );
            if( viaApi >= 0 ) return ( viaApi == 1 );
        }
        return s.isPatron;
    }

    bool Overlay::IsEigenVtc( const SpelerRecord &s ) const
    {
        if( m_vtcId > 0 && s.accountId != 0 )
        {
            const int gevonden = m_webApi.SpelerVtcId( s.accountId );
            if( gevonden >= 0 ) return ( gevonden == m_vtcId );
        }

        if( m_vtcTagsBuffer[ 0 ] == '\0' ) return false;

        auto naarKleineLetters = []( std::string t )
        {
            for( char &c : t ) c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
            return t;
        };
        const std::string tag = naarKleineLetters( s.tagTekst );
        const std::string naam = naarKleineLetters( s.gebruikersnaam );

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
        if( m_vtcBron.Type() != VtcBronType::Geen )
        {
            const VtcOverzicht o = m_vtcBron.Overzicht();
            ImGui::Spacing();
            KopBalk( o.bronNaam.empty() ? T( "BRON" ) : o.bronNaam.c_str() );

            if( !o.geldig )
            {
                TekstGedimd( o.status.empty() ? T( "Ophalen..." ) : o.status.c_str() );
            }
            else
            {
                const float halfB = ( ImGui::GetContentRegionAvail().x - 8 ) / 2.0f;
                std::string onder = o.tag.empty() ? std::string() : ( std::string( T( "tag " ) ) + o.tag );
                StatKaart( T( "BEDRIJF" ), o.bedrijf.empty() ? "-" : o.bedrijf, halfB, onder.c_str() );
                ImGui::SameLine();
                if( !o.mijnNaam.empty() )
                {
                    char km[ 48 ]; std::snprintf( km, sizeof( km ), "%.0f km", Eenheden::Km( o.mijnKm ) );
                    StatKaart( T( "IK" ), o.mijnNaam, halfB, o.mijnRol.empty() ? km : o.mijnRol.c_str() );
                }
                else
                    StatKaart( T( "LEDEN" ), std::to_string( o.leden ), halfB, T( "chauffeurs" ) );

                if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }
                const float regelB = ImGui::GetTextLineHeight() + 4.0f;

                if( !o.opdrachten.empty() )
                {
                    const int n = (int)std::min<std::size_t>( o.opdrachten.size(), 5 );
                    int regels = 1;
                    for( int i = 0; i < n; ++i )
                    {
                        const Opdracht &op = o.opdrachten[ i ];
                        regels += 1 + ( ( !op.lading.empty() || !op.deadline.empty() || op.status == "accepted" ) ? 1 : 0 );
                    }
                    ImGui::Spacing();
                    ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
                    ImGui::BeginChild( "##bronopdrachten", ImVec2( 0, regelB * regels + 12 ), true, ImGuiWindowFlags_NoScrollbar );
                    TekstGedimd( T( "OPDRACHTEN" ) );
                    for( int i = 0; i < n; ++i )
                    {
                        const Opdracht &op = o.opdrachten[ i ];
                        ImGui::Text( "%s -> %s", op.vertrek.c_str(), op.aankomst.c_str() );
                        std::string sub = op.lading;
                        if( !op.deadline.empty() ) sub += ( sub.empty() ? "" : "  |  " ) + op.deadline;
                        if( op.status == "accepted" ) sub += ( sub.empty() ? "" : "  |  " ) + std::string( T( "geaccepteerd" ) );
                        if( !sub.empty() ) TekstGedimd( sub.c_str() );
                        if( op.accepteerbaar )
                        {
                            ImGui::SameLine( ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( T( "Accepteren" ) ).x - 8 );
                            if( ImGui::SmallButton( ( std::string( T( "Accepteren" ) ) + "##op" + op.id ).c_str() ) )
                                m_vtcBron.AccepteerOpdracht( op.id );
                        }
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }

                if( !o.online.empty() )
                {
                    std::string regel = std::string( T( "Online: " ) );
                    for( std::size_t i = 0; i < o.online.size() && i < 6; ++i ) regel += ( i ? ", " : "" ) + o.online[ i ];
                    if( o.online.size() > 6 ) regel += " +" + std::to_string( o.online.size() - 6 );
                    ImGui::Spacing();
                    TekstGedimd( regel.c_str() );
                }

                if( !o.convooien.empty() )
                {
                    const int n = (int)std::min<std::size_t>( o.convooien.size(), 4 );
                    int regels = 1;
                    for( int i = 0; i < n; ++i )
                    {
                        const BronConvooi &k = o.convooien[ i ];
                        regels += 1 + ( ( !k.start.empty() || !k.vertrek.empty() ) ? 1 : 0 );
                    }
                    ImGui::Spacing();
                    ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
                    ImGui::BeginChild( "##bronconvooien", ImVec2( 0, regelB * regels + 12 ), true, ImGuiWindowFlags_NoScrollbar );
                    TekstGedimd( T( "CONVOOIEN" ) );
                    for( int i = 0; i < n; ++i )
                    {
                        const BronConvooi &k = o.convooien[ i ];
                        ImGui::Text( "%s", k.titel.empty() ? "-" : k.titel.c_str() );
                        std::string sub = k.start;
                        if( !k.vertrek.empty() ) sub += ( sub.empty() ? "" : "  |  " ) + k.vertrek + " -> " + k.aankomst;
                        if( !sub.empty() ) TekstGedimd( sub.c_str() );
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }

                if( !o.nieuws.empty() )
                {
                    const int n = (int)std::min<std::size_t>( o.nieuws.size(), 3 );
                    ImGui::Spacing();
                    ImGui::PushStyleColor( ImGuiCol_ChildBg, KaartKleur() );
                    ImGui::BeginChild( "##bronnieuws", ImVec2( 0, regelB * ( 1 + n ) + 12 ), true, ImGuiWindowFlags_NoScrollbar );
                    TekstGedimd( T( "NIEUWS" ) );
                    for( int i = 0; i < n; ++i ) ImGui::Text( "%s", o.nieuws[ i ].titel.c_str() );
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }

                if( m_kleinFontActief ) { ImGui::PopFont(); m_kleinFontActief = false; }
            }
            ImGui::Spacing();
        }

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

        {
            const float halfB = ( ImGui::GetContentRegionAvail().x - 8 ) / 2.0f;
            std::string onder = vtc.tag.empty() ? std::string() : ( std::string( T( "tag " ) ) + vtc.tag );
            StatKaart( T( "BEDRIJF" ), vtc.naam, halfB, onder.c_str() );
            ImGui::SameLine();
            StatKaart( T( "LEDEN" ), std::to_string( vtc.leden ), halfB, T( "chauffeurs" ) );
        }

        if( m_kleinFont ) { ImGui::PushFont( m_kleinFont ); m_kleinFontActief = true; }

        const ImGuiStyle &stV = ImGui::GetStyle();
        const float regelV = ImGui::GetTextLineHeight() + 4.0f;

        auto lijstBlok = [ & ]( const char *id, const char *kop,
                                 const std::vector<EvenementInfo> &items, int maxItems )
        {
            if( items.empty() ) return;

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
            lijstBlok( "vtc_eigen", T( "EIGEN CONVOOIEN" ), m_webApi.VtcEvenementen(), 4 );
            lijstBlok( "vtc_mee", T( "MELDT ZICH AAN VOOR" ), m_webApi.VtcAangemeld(), 4 );
        }

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

        {
            int opgezocht = 0, inRij = 0;
            m_webApi.OpzoekStand( opgezocht, inRij );
            char regel[ 96 ];
            std::snprintf( regel, sizeof( regel ), T( "Opgezocht: %d  |  in de rij: %d" ),
                            opgezocht, inRij );
            TekstGedimd( regel );
        }

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

        {
            const VtcBronType huidig = m_vtcBron.Type();
            const bool metSleutel = ( huidig == VtcBronType::Horizon || huidig == VtcBronType::TruckersHub
                                      || huidig == VtcBronType::Trucky || huidig == VtcBronType::Vtlog );
            const int tekstRegels = 1 + ( huidig == VtcBronType::Geen ? 0 : 1 );
            const int velden = 1 + ( metSleutel ? 1 : 0 );
            SectieStart( "VTC-BRON", ImVec4( 0.55f, 0.85f, 0.60f, 1.0f ),
                          SectieHoogte( tekstRegels, velden, 2 ) );
            ImGui::Spacing();
            ImGui::TextDisabled( T( "Waar leeft je bedrijf? Dat vult de VTC-tab." ) );
            ImGui::Spacing();

            static const char *keuzes[] = { "Geen", "CabNavi bot (dispatch)", "Trucky", "Horizon Dispatch", "TruckersHub", "VTLog" };
            int keuze = static_cast<int>( huidig );
            if( ImGui::BeginCombo( T( "Bron" ), T( keuzes[ std::clamp( keuze, 0, 5 ) ] ) ) )
            {
                for( int i = 0; i < 6; ++i )
                    if( ImGui::Selectable( T( keuzes[ i ] ), keuze == i ) )
                    {
                        m_vtcBron.ZetType( static_cast<VtcBronType>( i ) );
                        m_bronBufferGeladen = false;
                    }
                ImGui::EndCombo();
            }

            if( metSleutel )
            {
                if( !m_bronBufferGeladen )
                {
                    std::snprintf( m_bronSleutelBuffer, sizeof( m_bronSleutelBuffer ), "%s", m_vtcBron.Sleutel().c_str() );
                    m_bronBufferGeladen = true;
                }
                float knopB = ImGui::CalcTextSize( T( "Plakken" ) ).x + 20.0f;
                ImGui::SetNextItemWidth( -( knopB + 8.0f ) );
                if( ImGui::InputText( "##bronsleutel", m_bronSleutelBuffer, sizeof( m_bronSleutelBuffer ), ImGuiInputTextFlags_Password ) )
                    m_vtcBron.ZetSleutel( m_bronSleutelBuffer );
                ImGui::SameLine();
                if( ImGui::Button( ( std::string( T( "Plakken" ) ) + "##bronplak" ).c_str() ) )
                {
                    const std::string uitKlembord = LeesVanKlembord();
                    if( !uitKlembord.empty() )
                    {
                        std::snprintf( m_bronSleutelBuffer, sizeof( m_bronSleutelBuffer ), "%s", uitKlembord.c_str() );
                        m_vtcBron.ZetSleutel( m_bronSleutelBuffer );
                    }
                }
                if( huidig == VtcBronType::Horizon )          TekstGedimd( T( "Publieke pk_-sleutel (Dashboard > API)" ) );
                else if( huidig == VtcBronType::TruckersHub ) TekstGedimd( T( "Bedrijfstoken: geheim, kan leden wissen" ) );
                else if( huidig == VtcBronType::Vtlog )       TekstGedimd( T( "Optioneel: VTC-sleutel voor leden en live" ) );
                else                                          TekstGedimd( T( "Optioneel: bedrijfstoken voor evenementen" ) );
            }
            else if( huidig == VtcBronType::Trucky )
                TekstGedimd( T( "Vindt je account zelf via je Steam-ID" ) );
            else if( huidig == VtcBronType::Bot )
                TekstGedimd( T( "Gebruikt adres en sleutel van de VTC-webhook" ) );
            SectieEind();
        }
    }
}

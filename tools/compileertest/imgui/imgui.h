#pragma once
// STUB-imgui, alleen voor compileertests buiten Windows.
//
// Dit is GEEN echte Dear ImGui. Het doel is uitsluitend om Overlay.cxx te
// kunnen typechecken: verkeerd gespelde methodenamen, ontbrekende
// declaraties in onze eigen headers, verkeerde argumenttypes in onze eigen
// functies, en dat soort fouten.
//
// Wat dit NIET garandeert: dat de signatures exact overeenkomen met de
// echte imgui. Als een echte functie een extra defaultparameter heeft of
// een ander returntype, merk je dat hier niet. Het vervangt de bouw op
// Windows dus niet -- het vangt alleen fouten die anders pas daar opduiken.

#include <cstdarg>
#include <cstddef>

typedef unsigned int ImU32;
typedef unsigned int ImGuiID;
typedef int ImGuiCol;
typedef int ImGuiCond;
typedef int ImGuiStyleVar;
typedef int ImGuiWindowFlags;
typedef int ImGuiInputTextFlags;
typedef int ImGuiHoveredFlags;
typedef void *ImTextureID;

struct ImVec2
{
    float x = 0, y = 0;
    ImVec2() {}
    ImVec2( float a, float b ) : x( a ), y( b ) {}
};

struct ImVec4
{
    float x = 0, y = 0, z = 0, w = 0;
    ImVec4() {}
    ImVec4( float a, float b, float c, float d ) : x( a ), y( b ), z( c ), w( d ) {}
};

#define IM_COL32( r, g, b, a ) \
    ( ( (ImU32)( a ) << 24 ) | ( (ImU32)( b ) << 16 ) | ( (ImU32)( g ) << 8 ) | (ImU32)( r ) )

enum
{
    ImGuiCol_Text, ImGuiCol_TextDisabled, ImGuiCol_WindowBg, ImGuiCol_ChildBg,
    ImGuiCol_PopupBg, ImGuiCol_Border, ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered,
    ImGuiCol_FrameBgActive, ImGuiCol_TitleBg, ImGuiCol_TitleBgActive,
    ImGuiCol_TitleBgCollapsed, ImGuiCol_ScrollbarBg, ImGuiCol_ScrollbarGrab,
    ImGuiCol_ScrollbarGrabHovered, ImGuiCol_ScrollbarGrabActive, ImGuiCol_CheckMark,
    ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive, ImGuiCol_Button,
    ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive, ImGuiCol_Header,
    ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive, ImGuiCol_Separator,
    ImGuiCol_PlotHistogram, ImGuiCol_Tab, ImGuiCol_TabHovered, ImGuiCol_TabActive,
    ImGuiCol_COUNT
};

enum { ImGuiCond_None = 0, ImGuiCond_Always = 1, ImGuiCond_Once = 2,
       ImGuiCond_FirstUseEver = 4, ImGuiCond_Appearing = 8 };

enum { ImGuiWindowFlags_None = 0, ImGuiWindowFlags_NoTitleBar = 1 << 0,
       ImGuiWindowFlags_NoResize = 1 << 1, ImGuiWindowFlags_NoMove = 1 << 2,
       ImGuiWindowFlags_NoScrollbar = 1 << 3, ImGuiWindowFlags_NoCollapse = 1 << 5,
       ImGuiWindowFlags_AlwaysAutoResize = 1 << 6 };

enum { ImGuiStyleVar_ItemSpacing, ImGuiStyleVar_FramePadding, ImGuiStyleVar_WindowPadding };

enum { ImGuiInputTextFlags_None = 0, ImGuiInputTextFlags_CharsDecimal = 1 << 0 };

enum { ImGuiHoveredFlags_None = 0 };


typedef int ImGuiKey;
enum
{
    ImGuiKey_None = 0, ImGuiKey_Tab, ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
    ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Home, ImGuiKey_End,
    ImGuiKey_Insert, ImGuiKey_Delete, ImGuiKey_Backspace, ImGuiKey_Space,
    ImGuiKey_Enter, ImGuiKey_Escape, ImGuiKey_Comma, ImGuiKey_Minus,
    ImGuiKey_Period,
    ImGuiKey_0, ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4,
    ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9,
    ImGuiKey_Keypad0, ImGuiKey_Keypad1, ImGuiKey_Keypad2, ImGuiKey_Keypad3,
    ImGuiKey_Keypad4, ImGuiKey_Keypad5, ImGuiKey_Keypad6, ImGuiKey_Keypad7,
    ImGuiKey_Keypad8, ImGuiKey_Keypad9, ImGuiKey_KeypadDecimal,
    ImGuiKey_A, ImGuiKey_C, ImGuiKey_V, ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z,
    ImGuiKey_LeftCtrl, ImGuiKey_RightCtrl, ImGuiKey_LeftShift, ImGuiKey_RightShift,
    ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt
};

enum { ImGuiConfigFlags_None = 0, ImGuiConfigFlags_NavEnableKeyboard = 1 << 0 };

struct ImFontConfig
{
    bool MergeMode = false;
    bool PixelSnapH = false;
    int OversampleH = 3, OversampleV = 1;
    float GlyphOffsetY = 0.0f;
};

#define IMGUI_CHECKVERSION() ( (void)0 )

struct ImFont
{
    float FontSize = 13.0f;
};

struct ImFontAtlas
{
    ImFont *AddFontFromFileTTF( const char *, float, const ImFontConfig * = nullptr, const unsigned short * = nullptr );
    ImFont *AddFontDefault();
    void Clear();
    bool Build();
};

struct ImGuiIO
{
    ImFontAtlas *Fonts = nullptr;
    ImVec2 DisplaySize;
    float DeltaTime = 1.0f / 60.0f;
    const char *IniFilename = nullptr;
    bool WantCaptureMouse = false;
    bool WantCaptureKeyboard = false;
    int ConfigFlags = 0;
    float FontGlobalScale = 1.0f;
    void AddKeyEvent( ImGuiKey, bool );
    void AddInputCharacter( unsigned int );
    void AddMousePosEvent( float, float );
    void AddMouseButtonEvent( int, bool );
    void AddMouseWheelEvent( float, float );
};

struct ImGuiStyle
{
    float WindowRounding = 0, ChildRounding = 0, FrameRounding = 0;
    float GrabRounding = 0, ScrollbarRounding = 0, ScrollbarSize = 14;
    float WindowBorderSize = 1;
    ImVec2 WindowPadding, FramePadding, ItemSpacing;
    ImVec4 Colors[ ImGuiCol_COUNT ];
    void ScaleAllSizes( float );
};

struct ImDrawList
{
    void AddText( const ImVec2 &, ImU32, const char *, const char * = nullptr );
    void AddRectFilled( const ImVec2 &, const ImVec2 &, ImU32, float = 0.0f, int = 0 );
    void AddRect( const ImVec2 &, const ImVec2 &, ImU32, float = 0.0f, int = 0, float = 1.0f );
    void AddLine( const ImVec2 &, const ImVec2 &, ImU32, float = 1.0f );
    void AddCircleFilled( const ImVec2 &, float, ImU32, int = 0 );
    void AddCircle( const ImVec2 &, float, ImU32, int = 0, float = 1.0f );
    void AddImage( ImTextureID, const ImVec2 &, const ImVec2 &,
                    const ImVec2 & = ImVec2( 0, 0 ), const ImVec2 & = ImVec2( 1, 1 ),
                    ImU32 = 0xFFFFFFFF );
    void PathArcTo( const ImVec2 &, float, float, float, int = 0 );
    void PathStroke( ImU32, int = 0, float = 1.0f );
    void PushClipRect( const ImVec2 &, const ImVec2 &, bool = false );
    void PopClipRect();
};

struct ImDrawData;
struct ImGuiContext;

namespace ImGui
{
    ImGuiContext *CreateContext( ImFontAtlas * = nullptr );
    void DestroyContext( ImGuiContext * = nullptr );
    ImGuiIO &GetIO();
    ImGuiStyle &GetStyle();
    void StyleColorsDark( ImGuiStyle * = nullptr );

    void NewFrame();
    void Render();
    ImDrawData *GetDrawData();

    bool Begin( const char *, bool * = nullptr, ImGuiWindowFlags = 0 );
    void End();
    bool BeginChild( const char *, const ImVec2 & = ImVec2( 0, 0 ), bool = false, ImGuiWindowFlags = 0 );
    void EndChild();
    void BeginGroup();
    void EndGroup();

    bool BeginPopup( const char *, ImGuiWindowFlags = 0 );
    void EndPopup();
    void OpenPopup( const char *, int = 0 );
    void CloseCurrentPopup();
    bool MenuItem( const char *, const char * = nullptr, bool = false, bool = true );

    void Text( const char *, ... );
    void TextColored( const ImVec4 &, const char *, ... );
    void TextDisabled( const char *, ... );
    void SetTooltip( const char *, ... );

    bool Button( const char *, const ImVec2 & = ImVec2( 0, 0 ) );
    bool SmallButton( const char * );
    bool Checkbox( const char *, bool * );
    bool SliderFloat( const char *, float *, float, float, const char * = nullptr, int = 0 );
    bool SliderInt( const char *, int *, int, int, const char * = nullptr, int = 0 );
    bool ColorEdit3( const char *, float[ 3 ], int = 0 );
    bool InputText( const char *, char *, std::size_t, ImGuiInputTextFlags = 0 );
    bool InputTextMultiline( const char *, char *, std::size_t, const ImVec2 & = ImVec2( 0, 0 ), ImGuiInputTextFlags = 0 );
    void ProgressBar( float, const ImVec2 & = ImVec2( -1, 0 ), const char * = nullptr );
    void Image( ImTextureID, const ImVec2 &,
                 const ImVec2 & = ImVec2( 0, 0 ), const ImVec2 & = ImVec2( 1, 1 ),
                 const ImVec4 & = ImVec4( 1, 1, 1, 1 ), const ImVec4 & = ImVec4( 0, 0, 0, 0 ) );

    void SameLine( float = 0.0f, float = -1.0f );
    void Spacing();
    void Separator();
    void NewLine();
    void Dummy( const ImVec2 & );

    ImVec2 GetCursorScreenPos();
    float GetCursorPosX();
    float GetCursorPosY();
    void SetCursorPosX( float );
    void SetCursorPosY( float );
    ImVec2 GetContentRegionAvail();
    float GetWindowWidth();
    ImVec2 CalcTextSize( const char *, const char * = nullptr, bool = false, float = -1.0f );
    float GetTextLineHeight();
    float GetTextLineHeightWithSpacing();
    float GetFontSize();
    ImDrawList *GetWindowDrawList();
    ImU32 GetColorU32( const ImVec4 & );
    ImU32 GetColorU32( ImGuiCol, float = 1.0f );

    void PushStyleColor( ImGuiCol, const ImVec4 & );
    void PushStyleColor( ImGuiCol, ImU32 );
    void PopStyleColor( int = 1 );
    void PushStyleVar( ImGuiStyleVar, const ImVec2 & );
    void PushStyleVar( ImGuiStyleVar, float );
    void PopStyleVar( int = 1 );
    void PushFont( ImFont * );
    void PopFont();
    float GetFrameHeight();
    void PushID( int );
    void PushID( const char * );
    void PopID();

    void SetNextItemWidth( float );
    void SetNextWindowSize( const ImVec2 &, ImGuiCond = 0 );
    void SetNextWindowSizeConstraints( const ImVec2 &, const ImVec2 & );
    void SetNextWindowBgAlpha( float );
    bool IsItemHovered( ImGuiHoveredFlags = 0 );
}
namespace ImGui {
    bool BeginCombo( const char *, const char *, int = 0 );
    void EndCombo();
    bool Selectable( const char *, bool = false, int = 0 );
}

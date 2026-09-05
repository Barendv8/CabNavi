// Plugin.cxx
//
// CabNavi -- all-in-one trip log + live HUD for TruckersMP.
//   - Regular cargo jobs: via the SCS Telemetry SDK (scs_telemetry_init).
//   - Bus line jobs: via TruckersMP::BusModule (TruckersMP Client SDK).
//   - Live overlay: ImGui, drawn via the Render module (DirectX11).
//   - History: written to %APPDATA%\CabNavi\trips.jsonl.
//
// Build instructions are in README.md.

#include <TruckersMP/TruckersMP.hxx>
#include <scssdk_telemetry.h>

#include <windows.h>
#include <d3d11.h>

#include "BusTracking.hxx"
#include "CallbackHulp.hxx"
#include "Logboek.hxx"
#include "DiscordWebhook.hxx"
#include "IncidentRecorder.hxx"
#include "FuelCosts.hxx"
#include "Kaartdata.hxx"
#include "Overlay.hxx"
#include "PlayersNearby.hxx"
#include "TripLogger.hxx"
#include "TruckTracking.hxx"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>
#include <system_error>

namespace
{
    std::unique_ptr<TruckersMP::Session> g_session;
    std::unique_ptr<Ritten::TripLogger> g_logger;
    std::unique_ptr<Ritten::FuelCosts> g_brandstof;
    std::unique_ptr<Ritten::DiscordWebhook> g_discord;
    std::unique_ptr<Ritten::IncidentRecorder> g_incidentRecorder;
    std::unique_ptr<Ritten::BusTracking> g_busTracking;
    std::unique_ptr<Ritten::TruckTracking> g_vrachtTracking;
    std::unique_ptr<Ritten::PlayersNearby> g_spelers;
    std::unique_ptr<Ritten::Overlay> g_overlay;

    // The TruckersMP Client SDK does not pass an HWND of the game window
    // -- this plugin runs in the same process as the game (it is a DLL
    // loaded into it), so we look up the real, visible main window of
    // this process ourselves via the standard Windows approach for this
    // kind of in-process plugin.
    HWND ZoekSpelvenster()
    {
        struct ZoekData
        {
            DWORD processId;
            HWND gevonden = nullptr;
        };
        ZoekData data{ GetCurrentProcessId() };

        EnumWindows(
            []( HWND hwnd, LPARAM lparam ) -> BOOL
            {
                auto *data = reinterpret_cast<ZoekData *>( lparam );
                DWORD windowProcessId = 0;
                GetWindowThreadProcessId( hwnd, &windowProcessId );
                if( windowProcessId == data->processId && GetWindow( hwnd, GW_OWNER ) == nullptr
                    && IsWindowVisible( hwnd ) )
                {
                    data->gevonden = hwnd;
                    return FALSE;  // stop, we have it
                }
                return TRUE;  // keep searching
            },
            reinterpret_cast<LPARAM>( &data ) );

        return data.gevonden;
    }
    // Requests the mouse cursor from the game and LOCKS the game itself
    // for mouse input, so the camera does not turn while you click on the
    // overlay. NOTE: in the previous attempt this logic was inverted
    // (false instead of true when opening) -- "locked" means the GAME
    // does not use the mouse, not that the cursor is locked for us. See
    // InputModule in the SDK docs.
    bool g_muisOpgevraagd = false;

    void ZetMuisVoorOverlay( bool actief )
    {
        // NO more "syncing" with IsGameMouseLocked(). MEASURED 03-09: that
        // query returns the GAME's state, including what it does itself in
        // menus. Pulling our bool towards it kept detaching it from our own
        // IncreaseMouseRef counter -- and that is exactly how a mouse can get
        // stuck. What does fix the hang is releasing the mouse as soon as the
        // game pauses (see OnPostRender), and that works.
        if( !g_session || actief == g_muisOpgevraagd ) return;

        if( actief )
        {
            g_session->Input().IncreaseMouseRef();  // request the cursor
            g_session->Input().SetGameMouseLocked( true );  // game now ignores the mouse
        }
        else
        {
            g_session->Input().SetGameMouseLocked( false );  // game gets the mouse back
            g_session->Input().DecreaseMouseRef();  // give the cursor back
        }
        g_muisOpgevraagd = actief;
    }

    void ZetOverlayOp()
    {
        if( !g_session || g_overlay ) return;

        if( g_session->Render().GetRendererID() != TruckersMP::RendererID::DirectX11 )
        {
            Ritten::Logboek::Schrijf( "start", "ABORTED: renderer is not DirectX11" );
            g_session->Core().LogMessage( TruckersMP::LogLevel::Warning,
                "CabNavi: alleen DirectX11 wordt momenteel ondersteund voor de overlay-rendering." );
            return;
        }

        auto *device = reinterpret_cast<ID3D11Device *>( g_session->Render().GetDeviceHandle().value_or( 0 ) );
        if( !device )
        {
            Ritten::Logboek::Schrijf( "start", "ABORTED: no D3D11 device received from the SDK" );
            return;
        }

        g_overlay = std::make_unique<Ritten::Overlay>(
            *g_logger, *g_busTracking, *g_vrachtTracking, *g_spelers, *g_brandstof, *g_discord,
            *g_incidentRecorder );

        HWND spelVenster = ZoekSpelvenster();
        if( spelVenster == nullptr )
        {
            g_session->Core().LogMessage( TruckersMP::LogLevel::Warning,
                "CabNavi: kon het spelvenster niet vinden, overlay wordt niet getekend." );
            g_overlay.reset();
            return;
        }

        if( !g_overlay->InitDirectX11( device, spelVenster ) )
        {
            g_session->Core().LogMessage( TruckersMP::LogLevel::Warning,
                "CabNavi: initialiseren van de overlay is mislukt." );
            g_overlay.reset();
            return;
        }

        g_session->Core().LogMessage( TruckersMP::LogLevel::Info,
            "CabNavi: overlay succesvol geinitialiseerd, spelvenster gevonden." );

        g_session->Render().OnPostRender.Register( []
        {
            // Record the thread ID once. Compare this with the line from
            // GameTimeCallback: if the two numbers differ, drawing and SCS
            // telemetry run on separate threads and share the same fields in
            // TruckTracking without a lock.
            static bool renderThreadGelogd = false;
            if( !renderThreadGelogd )
            {
                renderThreadGelogd = true;
                Ritten::Logboek::Schrijf( "flags", "thread id OnPostRender (drawing): "
                                                        + std::to_string( Ritten::Logboek::HuidigeThreadId() ) );
            }

            if( g_incidentRecorder && g_spelers )
            {
                // FIRST refresh the positions, THEN the snapshot. This loop used to
                // live only in TekenSpelersTab, so a player's bearing was as old as
                // the last time you looked at that tab. If you were on Live when you
                // hit something, the recorder captured players without a usable
                // position and the incident radar stayed empty. VerversPosities has
                // its own brake, so this does not cost work every frame.
                g_spelers->VerversPosities();
                g_incidentRecorder->Tick( g_spelers->GeefSpelers() );
            }

            // Stay out of the way while you are not in the world.
            //
            // Two signals, because the TruckersMP SDK has no query like "is a
            // menu open":
            //   1. Not connected -- that is the main menu at startup.
            //   2. Paused -- the game reports that itself via SCS telemetry as
            //      soon as the simulation halts, and that happens in a menu or
            //      a garage screen.
            //
            // In both cases also give the MOUSE back. Otherwise the overlay's
            // orange cursor stayed over the game's menu, while the TruckersMP HUD
            // does remove its own.
            const bool verbonden = g_session
                                   && g_session->Network().IsConnected().value_or( true );
            const bool gepauzeerd = g_vrachtTracking && g_vrachtTracking->IsGepauzeerd();
            const bool uitDeWeg = !verbonden || gepauzeerd;

            // Give the mouse back ONCE when entering the pause, not every frame.
            // Otherwise we queried the mouse state from the SDK sixty times a
            // second, and the sync message could land in debug.log every frame.
            static bool wasUitDeWeg = false;
            if( uitDeWeg && !wasUitDeWeg )
            {
                ZetMuisVoorOverlay( false );
                Ritten::Logboek::Schrijf( "flags", std::string( "overlay out of the way: " )
                    + ( !verbonden ? "not connected" : "paused" ) );
            }
            if( !uitDeWeg && wasUitDeWeg )
            {
                Ritten::Logboek::Schrijf( "flags", "overlay back" );
            }
            wasUitDeWeg = uitDeWeg;

            // Measurement: what does the SDK say about the mouse when the game
            // itself shows a menu or garage? Only log when something changes.
            {
                const auto zichtbaar = g_session->Input().IsMouseVisible().value_or( false );
                const auto vergrendeld = g_session->Input().IsGameMouseLocked().value_or( false );
                static bool vorigZ = false, vorigV = false, eerste = true;
                if( eerste || zichtbaar != vorigZ || vergrendeld != vorigV )
                {
                    eerste = false; vorigZ = zichtbaar; vorigV = vergrendeld;
                    Ritten::Logboek::Schrijf( "flags", std::string( "mouse: visible=" )
                        + ( zichtbaar ? "yes" : "no" ) + " gameMouseLocked=" + ( vergrendeld ? "yes" : "no" )
                        + " requestedByUs=" + ( g_muisOpgevraagd ? "yes" : "no" ) );
                }
            }
            if( uitDeWeg ) return;

            // Also check g_overlay. The two above were already checked, this one
            // not -- while this event can also fire while the plugin is shutting
            // down or not ready yet.
            // Draw behind a safety net. An error here would otherwise take the
            // game down; now it lands in debug.log with the last location.
            if( g_overlay )
            {
                try
                {
                    g_overlay->Teken();
                }
                catch( const std::exception &ex )
                {
                    Ritten::Logboek::Schrijf( "ERROR",
                        std::string( "Exception while drawing: " ) + Ritten::Logboek::KorteFout( ex.what() )
                        + " | last location: " + Ritten::Logboek::LaatstBekend() );
                }
                catch( ... )
                {
                    Ritten::Logboek::Schrijf( "ERROR",
                        std::string( "Unknown exception while drawing | last location: " )
                        + Ritten::Logboek::LaatstBekend() );
                }
            }
        } );

        // Insert toggles the overlay itself on/off. This ALWAYS happens (even
        // when the overlay is hidden), so you can bring it back.
        // 0x2D is the Windows virtual-key code for Insert (VK_INSERT).
        g_session->Input().OnKey.Register( []( TruckersMP::InputKeyEvent &e )
        {
            if( !g_overlay ) return;

            constexpr TruckersMP::Uint8 VK_INSERT_CODE = 0x2D;

            if( e.GetKey() == VK_INSERT_CODE && e.GetDown() )
            {
                g_overlay->SchakelZichtbaarheid();
                if( !g_overlay->IsZichtbaar() )
                {
                    // Overlay hidden: give the mouse back to the game regardless, even if
                    // the right mouse button happened to still be down.
                    ZetMuisVoorOverlay( false );
                }
                return;
            }

            // Pass the keyboard to the overlay (for typing in the fuel price /
            // webhook field on Settings).
            g_overlay->OpToets( e.GetKey(), e.GetDown() );
        } );

        g_session->Input().OnChar.Register( []( TruckersMP::InputCharEvent &e )
        {
            if( !g_overlay ) return;
            g_overlay->OpKarakter( e.GetCharacter() );
        } );

        // NOTE: no more SetBlock() on individual mouse clicks -- that broke
        // TMP's own interface earlier. SetGameMouseLocked above now handles
        // at SDK level whether the game itself does anything with the mouse;
        // so we no longer need to block anything manually, only pass the
        // events to our own overlay so it can react.
        g_session->Input().OnMouseMove.Register( []( TruckersMP::InputMouseMoveEvent &e )
        {
            if( !g_overlay ) return;
            g_overlay->OpMuisBeweging( e.GetX(), e.GetY() );
        } );

        g_session->Input().OnMouseButton.Register( []( TruckersMP::InputMouseButtonEvent &e )
        {
            if( !g_overlay ) return;

            // One click of the right mouse button toggles "mouse mode" on/off
            // (no holding) -- like Insert for the overlay itself.
            if( e.GetButton() == TruckersMP::MouseButton::Right && e.GetDown() && g_overlay->IsZichtbaar() )
            {
                // Is the game paused? Then you are in an ETS2 menu -- the map, the
                // garage, the pause menu. There the right mouse button belongs to the
                // game: on the map you drag the map with it. We do not intercept that
                // click, otherwise our mouse toggles on every drag. TruckersMP also
                // keeps quiet in those menus.
                //
                // MEASURED 03-09: IsMouseVisible() does NOT see the game's menu mouse
                // (only returns our own request), but the SCS pause event does fire
                // on TruckersMP. Hence this signal.
                if( g_vrachtTracking && g_vrachtTracking->IsGepauzeerd() )
                {
                    return;  // leave it to the game, do nothing
                }

                ZetMuisVoorOverlay( !g_muisOpgevraagd );
                return;  // do not also pass this click as a "click on the overlay"
            }

            g_overlay->OpMuisKnop( static_cast<int>( e.GetButton() ), e.GetDown() );
        } );

        g_session->Input().OnMouseWheel.Register( []( TruckersMP::InputMouseWheelEvent &e )
        {
            if( !g_overlay ) return;

            // Windows counts the wheel in steps of 120 per notch; ImGui expects
            // about 1.0 per notch. Passing it undivided turned one notch into a
            // hundred and twenty lines, and you jumped from top to bottom without
            // seeing anything in between. Only divide when the number really
            // comes from that scale -- if the SDK ever gives a normalised value,
            // it is left alone.
            float delta = static_cast<float>( e.GetDelta() );
            if( delta > 2.0f || delta < -2.0f ) delta /= 120.0f;
            g_overlay->OpMuisWiel( delta );
        } );
    }
}

namespace Ritten
{
    // Product version of the game executable this DLL runs in, e.g.
    // "1.60.1.7" -- from the file version resource. NOT the SCS telemetry
    // version in scs_telemetry_init (that is 1.19 for ETS2 1.60; MEASURED
    // 05-09, it made the map-table comparison nonsense). Empty if unreadable.
    std::string SpelVersie()
    {
#ifdef _WIN32
        wchar_t buf[ MAX_PATH ];
        const DWORD n = GetModuleFileNameW( nullptr, buf, MAX_PATH );
        if( n == 0 || n >= MAX_PATH ) return {};
        DWORD dummy = 0;
        const DWORD maat = GetFileVersionInfoSizeW( buf, &dummy );
        if( maat == 0 ) return {};
        std::vector<std::uint8_t> blok( maat );
        if( !GetFileVersionInfoW( buf, 0, maat, blok.data() ) ) return {};
        VS_FIXEDFILEINFO *ffi = nullptr; UINT len = 0;
        if( !VerQueryValueW( blok.data(), L"\\", reinterpret_cast<LPVOID *>( &ffi ), &len ) || !ffi ) return {};
        return std::to_string( HIWORD( ffi->dwProductVersionMS ) ) + "." + std::to_string( LOWORD( ffi->dwProductVersionMS ) ) + "."
             + std::to_string( HIWORD( ffi->dwProductVersionLS ) ) + "." + std::to_string( LOWORD( ffi->dwProductVersionLS ) );
#else
        return {};
#endif
    }

    // Folder that holds base.scs / def.scs, derived from the game executable
    // this DLL is loaded into. Empty if it does not look like a game folder.
    std::filesystem::path SpelMap()
    {
#ifdef _WIN32
        wchar_t buf[ MAX_PATH ];
        const DWORD n = GetModuleFileNameW( nullptr, buf, MAX_PATH );
        if( n == 0 || n >= MAX_PATH ) return {};
        std::filesystem::path exe( buf );
        // <game>/bin/win_x64/eurotrucks2.exe
        std::filesystem::path map = exe.parent_path().parent_path().parent_path();
        std::error_code ec;
        if( std::filesystem::exists( map / "base.scs", ec ) || std::filesystem::exists( map / "def.scs", ec ) ) return map;
#endif
        return {};
    }
}

// ---------------------------------------------------------------------
// TruckersMP Client SDK entry points
// ---------------------------------------------------------------------

// Called by the Beschermd() wrapper in CallbackHulp.hxx when a
// callback lets an exception escape. Logs to the client log, because
// that is where users and the TMP team already look (recommendation
// from the docs). Falls back quietly when there is no session (yet)
// -- for example during shutdown.
namespace Ritten
{
    void LogPluginFout( const std::string &bericht )
    {
        if( g_session )
        {
            g_session->Core().LogMessage( TruckersMP::LogLevel::Error, bericht );
        }
    }
}

TMP_EXPORT bool TMP_API truckersmp_init( const TruckersMP_Host *host, TruckersMP_PluginDesc *desc )
{
    TruckersMP::PluginInfo info;
    info.m_name = "CabNavi";
    info.m_author = "Jij";
    info.m_version = "1.0.0";
    info.m_description = "Alles-in-1 overlay die je vracht- en buslijnritten live bijhoudt en logt.";
    TruckersMP::FillPluginDesc( desc, info );

    g_session = TruckersMP::Session::Create( host );
    if( !g_session )
    {
        return false;
    }

    // FIRST OF ALL: migrate data from the old folder. The plugin used to
    // be called "Ritten Overlay" and kept everything in
    // %APPDATA%\RittenOverlay. Without this step someone who updates
    // would have "lost" his trip log, tachograph state and settings --
    // they are still there, but the plugin looks in the new place.
    //
    // Only copy, never move or delete: if something goes wrong, the
    // original is still there. And only while the new folder is still
    // empty, so this happens exactly once.
    try
    {
        const char *appdata = std::getenv( "APPDATA" );
        if( appdata != nullptr )
        {
            const std::filesystem::path oud = std::filesystem::path( appdata ) / "RittenOverlay";
            const std::filesystem::path nieuw = std::filesystem::path( appdata ) / "CabNavi";

            std::error_code ec;
            if( std::filesystem::exists( oud, ec ) )
            {
                // Check PER FILE, not whether the new FOLDER already exists.
                // Measured 31-08: that folder is already created by another
                // component before this code runs, so the whole migration was skipped
                // and it looked as if everything was gone.
                std::filesystem::create_directories( nieuw, ec );

                for( const auto &item : std::filesystem::directory_iterator( oud, ec ) )
                {
                    const std::filesystem::path doel = nieuw / item.path().filename();
                    if( std::filesystem::exists( doel, ec ) ) continue;  // overwrite nothing

                    if( item.is_directory( ec ) )
                    {
                        std::filesystem::copy( item.path(), doel,
                                                std::filesystem::copy_options::recursive |
                                                std::filesystem::copy_options::skip_existing, ec );
                    }
                    else
                    {
                        std::filesystem::copy_file( item.path(), doel,
                                                     std::filesystem::copy_options::skip_existing, ec );
                    }
                }

                // The logo used to be named after the builder's company; now it is
                // simply logo.png. Take it along, otherwise it says "no logo" while
                // the file is there.
                const std::filesystem::path oudLogo = nieuw / "weeda-logo.png";
                const std::filesystem::path nieuwLogo = nieuw / "logo.png";
                if( std::filesystem::exists( oudLogo, ec ) &&
                    !std::filesystem::exists( nieuwLogo, ec ) )
                {
                    std::filesystem::copy_file( oudLogo, nieuwLogo,
                                                 std::filesystem::copy_options::skip_existing, ec );
                }
            }
        }
    }
    catch( ... ) { /* verhuizen mag nooit het opstarten blokkeren */ }

    g_logger = std::make_unique<Ritten::TripLogger>();
    g_logger->LaadGeschiedenis();
    g_brandstof = std::make_unique<Ritten::FuelCosts>();
    g_discord = std::make_unique<Ritten::DiscordWebhook>();
    g_incidentRecorder = std::make_unique<Ritten::IncidentRecorder>();

    // Every time a trip is completed/cancelled, also try to send a
    // Discord message (DiscordWebhook checks itself whether that is on
    // and whether a URL is set -- we do not need to care here).
    g_logger->ZetVoltooidCallback( []( const Ritten::Trip &trip ) { g_discord->StuurRitVoltooid( trip ); } );

    // FIRST empty the log, THEN create the components. The other way
    // round, everything a constructor logged was wiped right away --
    // measured 31-08: the line "afstandsfactor uit vorige sessie" never
    // appeared, while the reading did happen.
    Ritten::Logboek::StartNieuweSessie( "CabNavi started" );
    {
        std::error_code ec;
        if( const char *ad = std::getenv( "APPDATA" ) )
        {
            const std::filesystem::path oudePad = std::filesystem::path( ad ) / "RittenOverlay";
            if( std::filesystem::exists( oudePad, ec ) )
            {
                Ritten::Logboek::Schrijf( "start",
                    "old folder RittenOverlay found -- data has been copied to CabNavi. "
                    "The old folder is left in place; you may delete it yourself." );
            }
        }
    }

    g_busTracking = std::make_unique<Ritten::BusTracking>( *g_session, *g_logger );
    g_vrachtTracking = std::make_unique<Ritten::TruckTracking>( *g_logger, *g_brandstof );
    g_vrachtTracking->ZetBusTracking( g_busTracking.get() );
    g_spelers = std::make_unique<Ritten::PlayersNearby>( *g_session );
    g_vrachtTracking->ZetIncidentKoppeling( g_spelers.get(), g_incidentRecorder.get() );

    Ritten::Logboek::Schrijf( "start", "plugin loaded, session set up" );

    g_session->Core().LogMessage( TruckersMP::LogLevel::Info, "CabNavi geladen." );

    g_session->Render().OnPostRender.Register( [] { ZetOverlayOp(); } );

    g_session->Network().OnConnected.Register( []
    {
        g_session->UserInterface().ShowNotification(
            TruckersMP::NotificationType::Success, "CabNavi actief -- druk op Insert om te tonen/verbergen." );
    } );

    return true;
}

TMP_EXPORT void TMP_API truckersmp_shutdown( void )
{
    Ritten::Logboek::Schrijf( "event", "plugin shutting down" );
    ZetMuisVoorOverlay( false );
    if( g_overlay ) g_overlay->Shutdown();
    g_overlay.reset();
    // Order matters: TruckTracking holds RAW pointers to PlayersNearby
    // and IncidentRecorder (see ZetIncidentKoppeling) and uses them in
    // SchadeCallback. Those two must go AFTER TruckTracking, not before
    // -- otherwise an incoming damage update can read freed memory.
    g_vrachtTracking.reset();
    g_spelers.reset();
    g_busTracking.reset();
    g_discord.reset();
    g_incidentRecorder.reset();
    g_brandstof.reset();
    g_logger.reset();
    g_session.reset();  // automatically detaches all registered events
}

// ---------------------------------------------------------------------
// SCS Telemetry SDK entry points (for regular cargo jobs; see
// TruckTracking.hxx for why this runs separately from the TruckersMP SDK)
// ---------------------------------------------------------------------

SCSAPI_RESULT scs_telemetry_init( const scs_u32_t version, const scs_telemetry_init_params_t *params )
{
    if( version != SCS_TELEMETRY_VERSION_1_01 )
    {
        return SCS_RESULT_unsupported;
    }
    const auto *p = static_cast<const scs_telemetry_init_params_v101_t *>( params );

    if( !g_vrachtTracking )
    {
        // Can happen when scs_telemetry_init runs before truckersmp_init (see
        // the "main file shares globals" note in the how-to); create a logger
        // + tracker already so nothing is lost.
        g_logger = std::make_unique<Ritten::TripLogger>();
        g_brandstof = std::make_unique<Ritten::FuelCosts>();
        g_vrachtTracking = std::make_unique<Ritten::TruckTracking>( *g_logger, *g_brandstof );
    }

    g_vrachtTracking->RegistreerBijTelemetrie( p );

    // Fuel prices straight from the game files. We run INSIDE the game
    // process, so the executable's own path tells us where the game is:
    // <game>\bin\win_x64\eurotrucks2.exe -> three levels up. No registry,
    // no guessing about Steam library folders. Cache key = game version plus
    // the size and date of def.scs, so a patch that touches the data is
    // picked up and an unchanged game costs nothing after the first read.
    {
        const std::filesystem::path spelmap = Ritten::SpelMap();
        if( spelmap.empty() )
        {
            Ritten::Logboek::Schrijf( "event", "fuel prices: game folder not found next to the executable, keeping file" );
        }
        else
        {
            std::string sleutel = std::string( p->common.game_id ? p->common.game_id : "game" ) + " " + Ritten::SpelVersie();
            std::error_code ec;
            const auto def = spelmap / "def.scs";
            if( std::filesystem::exists( def, ec ) )
            {
                sleutel += "|" + std::to_string( std::filesystem::file_size( def, ec ) ) + "|"
                           + std::to_string( std::filesystem::last_write_time( def, ec ).time_since_epoch().count() );
            }
            g_brandstof->StartSpelPrijzen( spelmap, sleutel );

            // The embedded city table is a snapshot of one map version. Prices
            // and country centres follow the game automatically; city positions
            // cannot (they live in the map data). So say it when the game has
            // moved on, and the table should be regenerated
            // (tools/kaartdata/maak_tabel.py).
            const std::string spelVersie = Ritten::SpelVersie();   // "1.60.1.7", from the exe
            const std::string kaart = Ritten::Kaartdata::Versie();
            if( spelVersie.empty() )
            {
                Ritten::Logboek::Schrijf( "start", "map table " + kaart + "; game version unreadable" );
            }
            else if( Ritten::Kaartdata::VersieNieuwer( spelVersie, kaart ) )
            {
                Ritten::Logboek::Schrijf( "event", "map table is from game " + kaart + ", running " + spelVersie
                                          + ": cities of a newer map DLC fall back to country centres until the table is regenerated" );
            }
            else
            {
                Ritten::Logboek::Schrijf( "start", "map table " + kaart + " matches game " + spelVersie + " (" + std::to_string( Ritten::Kaartdata::AantalSteden() ) + " cities)" );
            }
        }
    }

    // Confirmation in the GAME log file (not debug.log) that the SCS
    // Telemetry side of the plugin actually started. If you do not see
    // this line in client.log, scs_telemetry_init is not called by the
    // game at all -- then the problem is not in the job detection itself,
    // but already in loading this part of the plugin.
    p->common.log( SCS_LOG_TYPE_message, "CabNavi: SCS telemetry geinitialiseerd, kanalen geregistreerd." );

    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_telemetry_shutdown( void )
{
    // Channels/events are detached automatically by the host.
}

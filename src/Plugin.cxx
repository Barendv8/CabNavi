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
#include "Spel.hxx"
#include "Overlay.hxx"
#include "PlayersNearby.hxx"
#include "TripLogger.hxx"
#include "TruckTracking.hxx"
#include "VtcWebhook.hxx"
#include "VtcBron.hxx"
#include "RitGeheugen.hxx"
#include "SpelConfig.hxx"

#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <memory>
#include <vector>
#include <system_error>

namespace
{
    std::unique_ptr<TruckersMP::Session> g_session;
    std::unique_ptr<Ritten::TripLogger> g_logger;
    std::unique_ptr<Ritten::FuelCosts> g_brandstof;
    std::unique_ptr<Ritten::DiscordWebhook> g_discord;
    std::unique_ptr<Ritten::VtcWebhook> g_vtcWebhook;
    std::unique_ptr<Ritten::VtcBron> g_vtcBron;
    std::unique_ptr<Ritten::RitGeheugen> g_ritGeheugen;
    std::unique_ptr<Ritten::RitGeheugen> g_ritGeheugenBus;
    std::unique_ptr<Ritten::SpelConfig> g_spelConfig;
    bool g_ritWasActief = false;
    bool g_busRitWasActief = false;
    std::unique_ptr<Ritten::IncidentRecorder> g_incidentRecorder;
    std::unique_ptr<Ritten::BusTracking> g_busTracking;
    std::unique_ptr<Ritten::TruckTracking> g_vrachtTracking;
    std::unique_ptr<Ritten::PlayersNearby> g_spelers;
    std::unique_ptr<Ritten::Overlay> g_overlay;

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
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>( &data ) );

        return data.gevonden;
    }
    bool g_muisOpgevraagd = false;

    void ZetMuisVoorOverlay( bool actief )
    {
        if( !g_session || actief == g_muisOpgevraagd ) return;

        if( actief )
        {
            g_session->Input().IncreaseMouseRef();
            g_session->Input().SetGameMouseLocked( true );
        }
        else
        {
            g_session->Input().SetGameMouseLocked( false );
            g_session->Input().DecreaseMouseRef();
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
            *g_vtcWebhook, *g_vtcBron, *g_incidentRecorder );

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
            static bool renderThreadGelogd = false;
            if( !renderThreadGelogd )
            {
                renderThreadGelogd = true;
                Ritten::Logboek::Schrijf( "flags", "thread id OnPostRender (drawing): "
                                                        + std::to_string( Ritten::Logboek::HuidigeThreadId() ) );
            }

            if( g_incidentRecorder && g_spelers )
            {
                g_spelers->VerversPosities();
                g_incidentRecorder->Tick( g_spelers->GeefSpelers() );

                if( g_vtcBron )
                    if( const auto lokaal = g_session->Player().GetLocalPlayer() )
                        g_vtcBron->ZetEigenSteamId( lokaal->GetSteamID().value_or( 0 ) );

                if( g_ritGeheugen && g_vrachtTracking )
                {
                    const bool actief = g_vrachtTracking->HeeftActieveRit();
                    if( actief && !g_ritWasActief && g_spelConfig ) g_spelConfig->Laad();
                    g_ritWasActief = actief;
                    double x = 0.0, z = 0.0;
                    const bool posBekend = g_spelers->EigenPositie( x, z );
                    g_ritGeheugen->Voed( g_vrachtTracking->LiveSnelheidKmh(),
                                         g_vrachtTracking->HuidigeVoertuigStatus().snelheidslimietKmh,
                                         posBekend, x, z, actief );

                    if( g_ritGeheugenBus && g_busTracking )
                    {
                        const bool busActief = g_busTracking->HeeftActieveRit();
                        if( busActief && !g_busRitWasActief && g_spelConfig ) g_spelConfig->Laad();
                        g_busRitWasActief = busActief;
                        g_ritGeheugenBus->Voed( g_vrachtTracking->LiveSnelheidKmh(),
                                                g_vrachtTracking->HuidigeVoertuigStatus().snelheidslimietKmh,
                                                posBekend, x, z, busActief );
                    }

                    if( g_vtcWebhook && g_vtcWebhook->LivePositie() )
                    {
                        Ritten::VtcWebhook::LiveStand stand;
                        stand.bekend = posBekend; stand.x = x; stand.z = z;
                        stand.snelheidKmh = g_vrachtTracking->LiveSnelheidKmh();
                        stand.opRit = actief;
                        if( actief )
                        {
                            const auto &rit = g_vrachtTracking->HuidigeRit();
                            stand.van = rit.bronStad; stand.naar = rit.bestemmingStad; stand.server = rit.serverNaam;
                        }
                        g_vtcWebhook->ZetLive( stand );
                    }
                }
            }

            const bool verbonden = g_session
                                   && g_session->Network().IsConnected().value_or( true );
            const bool gepauzeerd = g_vrachtTracking && g_vrachtTracking->IsGepauzeerd();
            const bool uitDeWeg = !verbonden || gepauzeerd;

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

        g_session->Input().OnKey.Register( []( TruckersMP::InputKeyEvent &e )
        {
            if( !g_overlay ) return;

            constexpr TruckersMP::Uint8 VK_INSERT_CODE = 0x2D;

            if( e.GetKey() == VK_INSERT_CODE && e.GetDown() )
            {
                g_overlay->SchakelZichtbaarheid();
                if( !g_overlay->IsZichtbaar() )
                {
                    ZetMuisVoorOverlay( false );
                }
                return;
            }

            g_overlay->OpToets( e.GetKey(), e.GetDown() );
        } );

        g_session->Input().OnChar.Register( []( TruckersMP::InputCharEvent &e )
        {
            if( !g_overlay ) return;
            g_overlay->OpKarakter( e.GetCharacter() );
        } );

        g_session->Input().OnMouseMove.Register( []( TruckersMP::InputMouseMoveEvent &e )
        {
            if( !g_overlay ) return;
            g_overlay->OpMuisBeweging( e.GetX(), e.GetY() );
        } );

        g_session->Input().OnMouseButton.Register( []( TruckersMP::InputMouseButtonEvent &e )
        {
            if( !g_overlay ) return;

            if( e.GetButton() == TruckersMP::MouseButton::Right && e.GetDown() && g_overlay->IsZichtbaar() )
            {
                if( g_vrachtTracking && g_vrachtTracking->IsGepauzeerd() )
                {
                    return;
                }

                ZetMuisVoorOverlay( !g_muisOpgevraagd );
                return;
            }

            g_overlay->OpMuisKnop( static_cast<int>( e.GetButton() ), e.GetDown() );
        } );

        g_session->Input().OnMouseWheel.Register( []( TruckersMP::InputMouseWheelEvent &e )
        {
            if( !g_overlay ) return;

            float delta = static_cast<float>( e.GetDelta() );
            if( delta > 2.0f || delta < -2.0f ) delta /= 120.0f;
            g_overlay->OpMuisWiel( delta );
        } );
    }
}

namespace Ritten
{
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

    std::filesystem::path SpelMap()
    {
#ifdef _WIN32
        wchar_t buf[ MAX_PATH ];
        const DWORD n = GetModuleFileNameW( nullptr, buf, MAX_PATH );
        if( n == 0 || n >= MAX_PATH ) return {};
        std::filesystem::path exe( buf );
        std::filesystem::path map = exe.parent_path().parent_path().parent_path();
        std::error_code ec;
        if( std::filesystem::exists( map / "base.scs", ec ) || std::filesystem::exists( map / "def.scs", ec ) ) return map;
#endif
        return {};
    }
}

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
                std::filesystem::create_directories( nieuw, ec );

                for( const auto &item : std::filesystem::directory_iterator( oud, ec ) )
                {
                    const std::filesystem::path doel = nieuw / item.path().filename();
                    if( std::filesystem::exists( doel, ec ) ) continue;

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
            }
        }
    }
    catch( ... ) { }

    g_logger = std::make_unique<Ritten::TripLogger>();
    g_logger->LaadGeschiedenis();
    g_brandstof = std::make_unique<Ritten::FuelCosts>();
    g_discord = std::make_unique<Ritten::DiscordWebhook>();
    g_vtcWebhook = std::make_unique<Ritten::VtcWebhook>();
    g_vtcBron = std::make_unique<Ritten::VtcBron>( *g_vtcWebhook );
    g_ritGeheugen = std::make_unique<Ritten::RitGeheugen>();
    g_ritGeheugenBus = std::make_unique<Ritten::RitGeheugen>();
    g_spelConfig = std::make_unique<Ritten::SpelConfig>();
    g_spelConfig->Laad();
    g_incidentRecorder = std::make_unique<Ritten::IncidentRecorder>();

    g_logger->ZetVoltooidCallback( []( const Ritten::Trip &trip )
    {
        const bool isBus = trip.type == Ritten::TripType::Bus;

        const auto leeg = !g_brandstof ? Ritten::FuelCosts::LeegRijden{}
                          : isBus ? g_brandstof->LeegVoorBusRit() : g_brandstof->LeegVoorDezeRit();
        const auto prijs = g_brandstof ? g_brandstof->HuidigePrijsInfo() : Ritten::FuelCosts::PrijsInfo{};
        g_discord->StuurRitVoltooid( trip, leeg.km, leeg.kosten );

        Ritten::VtcWebhook::TachoMoment tacho;
        if( g_vrachtTracking )
        {
            tacho.geldig = true;
            tacho.rijMinutenSindsRust = g_vrachtTracking->TachograafRijtijdMinuten();
            tacho.inRust = g_vrachtTracking->TachograafInRust();
        }
        Ritten::VtcWebhook::RitContext ctx;
        ctx.prijsPerLiter = prijs.prijs; ctx.prijsBron = prijs.bron;
        ctx.leegKm = leeg.km; ctx.leegLiters = leeg.liters; ctx.leegKosten = leeg.kosten;

        ctx.hub.game = Ritten::SpelInfo::IsAts() ? "ats" : "eut2";
        if( g_session )
            if( const auto lokaal = g_session->Player().GetLocalPlayer() )
            {
                ctx.hub.steamId = lokaal->GetSteamID().value_or( 0 );
                ctx.hub.spelerNaam = lokaal->GetUsername().value_or( std::string() );
                if( const auto id = lokaal->GetPlayerID() ) ctx.hub.tmpSpelerId = std::to_string( *id );
            }
        if( g_vrachtTracking )
        {
            const auto vs = g_vrachtTracking->HuidigeVoertuigStatus();
            ctx.hub.kmStandEind = std::max( 0.0, vs.kilometerstandKm );
            ctx.hub.schadeCabine = std::max( 0.0, vs.schadeCabine ); ctx.hub.schadeChassis = std::max( 0.0, vs.schadeChassis );
            ctx.hub.schadeMotor = std::max( 0.0, vs.schadeMotor );   ctx.hub.schadeBak = std::max( 0.0, vs.schadeBak );
            ctx.hub.schadeWielen = std::max( 0.0, vs.schadeWielen );
            ctx.hub.aanhangerSchade = std::max( 0.0, vs.aanhangerSchade ); ctx.hub.ladingSchade = std::max( 0.0, vs.ladingSchade );
        }
        ctx.hub.brandstofPrijsPerLiter = prijs.prijs;
        if( const Ritten::RitGeheugen *geheugen = isBus ? g_ritGeheugenBus.get() : g_ritGeheugen.get() )
        {
            ctx.hub.topKmh = geheugen->TopSnelheidKmh();
            for( const auto &pt : geheugen->Route() ) ctx.hub.route.push_back( { pt.tijd, pt.x, pt.z } );
            for( const auto &ov : geheugen->Overtredingen() )
                ctx.hub.overtredingen.push_back( { ov.maxKmh, ov.limietKmh, ov.start, ov.eind, ov.x, ov.z } );
            ctx.hub.teleports = geheugen->Teleports();
        }
        if( g_spelConfig ) ctx.hub.realistischeInstellingen = g_spelConfig->RealistischeInstellingen();
        if( g_vrachtTracking ) { ctx.hub.kenteken = g_vrachtTracking->Kenteken(); ctx.hub.kentekenLand = g_vrachtTracking->KentekenLand(); }
        if( g_brandstof )
            for( const auto &t : ( isBus ? g_brandstof->TankbeurtenBusRit() : g_brandstof->TankbeurtenDezeRit() ) )
                ctx.hub.tankbeurten.push_back( { t.liters, t.kostenEuro } );
        g_vtcWebhook->StuurRit( trip, tacho, ctx );
    } );

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
        if( const auto id = g_session->Network().GetGameServerID() )
            Ritten::Logboek::Schrijf( "start", "connected to game server id " + std::to_string( *id ) );
        else
            Ritten::Logboek::Schrijf( "start", "connected; the SDK gives no game server id" );
    } );

    return true;
}

TMP_EXPORT void TMP_API truckersmp_shutdown( void )
{
    Ritten::Logboek::Schrijf( "event", "plugin shutting down" );
    ZetMuisVoorOverlay( false );
    if( g_overlay ) g_overlay->Shutdown();
    g_overlay.reset();
    g_vrachtTracking.reset();
    g_spelers.reset();
    g_busTracking.reset();
    g_discord.reset();
    g_vtcBron.reset();
    g_ritGeheugen.reset();
    g_ritGeheugenBus.reset();
    g_spelConfig.reset();
    g_vtcWebhook.reset();
    g_incidentRecorder.reset();
    g_brandstof.reset();
    g_logger.reset();
    g_session.reset();
}

SCSAPI_RESULT scs_telemetry_init( const scs_u32_t version, const scs_telemetry_init_params_t *params )
{
    if( version != SCS_TELEMETRY_VERSION_1_01 )
    {
        return SCS_RESULT_unsupported;
    }
    const auto *p = static_cast<const scs_telemetry_init_params_v101_t *>( params );

    {
        std::string melding;
        Ritten::SpelInfo::Bevestig( p->common.game_id, melding );
        Ritten::Logboek::Schrijf( "start", std::string( "game: " ) + Ritten::SpelInfo::Naam()
                                  + " (" + ( p->common.game_id ? p->common.game_id : "?" ) + ")" );
        if( !melding.empty() ) Ritten::Logboek::Schrijf( "event", "game check: " + melding );
    }

    if( !g_vrachtTracking )
    {
        g_logger = std::make_unique<Ritten::TripLogger>();
        g_brandstof = std::make_unique<Ritten::FuelCosts>();
        g_vrachtTracking = std::make_unique<Ritten::TruckTracking>( *g_logger, *g_brandstof );
    }

    g_vrachtTracking->RegistreerBijTelemetrie( p );

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

            const std::string spelVersie = Ritten::SpelVersie();
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

    p->common.log( SCS_LOG_TYPE_message, "CabNavi: SCS telemetry geinitialiseerd, kanalen geregistreerd." );

    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_telemetry_shutdown( void )
{
}

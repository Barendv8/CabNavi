// Plugin.cxx
//
// CabNavi -- alles-in-1 rittenlogboek + live HUD voor TruckersMP.
//   - Reguliere vrachtjobs: via de SCS Telemetry SDK (scs_telemetry_init).
//   - Buslijn-jobs: via TruckersMP::BusModule (TruckersMP Client SDK).
//   - Live overlay: ImGui, getekend via de Render-module (DirectX11).
//   - Geschiedenis: weggeschreven naar %APPDATA%\CabNavi\trips.jsonl.
//
// Bouwinstructies staan in README.md.

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
#include "Overlay.hxx"
#include "PlayersNearby.hxx"
#include "TripLogger.hxx"
#include "TruckTracking.hxx"

#include <cstdlib>
#include <filesystem>
#include <memory>
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

    // De TruckersMP Client SDK geeft geen HWND van het spelvenster mee --
    // deze plugin draait in hetzelfde proces als het spel (het is een DLL
    // die erin geladen wordt), dus we zoeken het echte, zichtbare
    // hoofdvenster van dit proces zelf op via de standaard Windows-aanpak
    // voor dit soort in-process plugins.
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
                    return FALSE; // stoppen, we hebben 'm
                }
                return TRUE; // doorgaan zoeken
            },
            reinterpret_cast<LPARAM>( &data ) );

        return data.gevonden;
    }
    // Vraagt de muiscursor op bij het spel en blokkeert (LOCKT) het spel
    // zelf voor muisinput, zodat de camera niet meedraait terwijl je op de
    // overlay klikt. LET OP: bij de vorige poging stond deze logica
    // omgedraaid (false i.p.v. true bij het openen) -- "locked" betekent
    // dat het SPEL de muis niet gebruikt, niet dat de cursor vergrendeld
    // wordt voor ons. Zie InputModule in de SDK-docs.
    bool g_muisOpgevraagd = false;
    void ZetMuisVoorOverlay( bool actief )
    {
        if( !g_session || actief == g_muisOpgevraagd ) return;

        if( actief )
        {
            g_session->Input().IncreaseMouseRef();   // vraag de cursor op
            g_session->Input().SetGameMouseLocked( true ); // spel negeert de muis nu
        }
        else
        {
            g_session->Input().SetGameMouseLocked( false ); // spel krijgt de muis weer terug
            g_session->Input().DecreaseMouseRef();   // geef de cursor terug
        }
        g_muisOpgevraagd = actief;
    }

    void ZetOverlayOp()
    {
        if( !g_session || g_overlay ) return;

        if( g_session->Render().GetRendererID() != TruckersMP::RendererID::DirectX11 )
        {
            Ritten::Logboek::Schrijf( "start", "AFGEBROKEN: renderer is geen DirectX11" );
            g_session->Core().LogMessage( TruckersMP::LogLevel::Warning,
                "CabNavi: alleen DirectX11 wordt momenteel ondersteund voor de overlay-rendering." );
            return;
        }

        auto *device = reinterpret_cast<ID3D11Device *>( g_session->Render().GetDeviceHandle().value_or( 0 ) );
        if( !device )
        {
            Ritten::Logboek::Schrijf( "start", "AFGEBROKEN: geen D3D11-device van de SDK gekregen" );
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
            if( g_incidentRecorder && g_spelers )
            {
                g_incidentRecorder->Tick( g_spelers->GeefSpelers() );
            }
            // Ook g_overlay controleren. De twee hierboven werden al
            // gecontroleerd, deze niet -- terwijl dit event ook kan vuren
            // terwijl de plugin wordt afgesloten of nog niet klaar is.
            // Tekenen achter een vangnet. Een fout hier zou anders het spel
            // meesleuren; nu belandt hij in debug.log met de laatste plek erbij.
            if( g_overlay )
            {
                try
                {
                    g_overlay->Teken();
                }
                catch( const std::exception &ex )
                {
                    Ritten::Logboek::Schrijf( "FOUT",
                        std::string( "Exceptie tijdens tekenen: " ) + ex.what()
                        + " | laatste plek: " + Ritten::Logboek::LaatstBekend() );
                }
                catch( ... )
                {
                    Ritten::Logboek::Schrijf( "FOUT",
                        std::string( "Onbekende exceptie tijdens tekenen | laatste plek: " )
                        + Ritten::Logboek::LaatstBekend() );
                }
            }
        } );

        // Insert schakelt de overlay zelf aan/uit. Dit gebeurt ALTIJD (ook
        // als de overlay verborgen is), zodat je 'm weer terug kunt halen.
        // 0x2D is de Windows virtual-key code voor Insert (VK_INSERT).
        g_session->Input().OnKey.Register( []( TruckersMP::InputKeyEvent &e )
        {
            if( !g_overlay ) return;

            constexpr TruckersMP::Uint8 VK_INSERT_CODE = 0x2D;

            if( e.GetKey() == VK_INSERT_CODE && e.GetDown() )
            {
                g_overlay->SchakelZichtbaarheid();
                if( !g_overlay->IsZichtbaar() )
                {
                    // Overlay verborgen: muis sowieso teruggeven aan het
                    // spel, ook als de rechtermuisknop toevallig nog
                    // ingedrukt was.
                    ZetMuisVoorOverlay( false );
                }
                return;
            }

            // Toetsenbord doorgeven aan de overlay (voor het typen in het
            // brandstofprijs-/webhook-veld op Instellingen).
            g_overlay->OpToets( e.GetKey(), e.GetDown() );
        } );

        g_session->Input().OnChar.Register( []( TruckersMP::InputCharEvent &e )
        {
            if( !g_overlay ) return;
            g_overlay->OpKarakter( e.GetCharacter() );
        } );

        // LET OP: geen SetBlock() meer op individuele muisklikken -- dat
        // brak eerder TMP's eigen interface. SetGameMouseLocked hierboven
        // regelt nu op SDK-niveau of het spel zelf iets met de muis doet;
        // wij hoeven dus niks meer handmatig te blokkeren, alleen de
        // events doorgeven aan onze eigen overlay zodat die erop kan
        // reageren.
        g_session->Input().OnMouseMove.Register( []( TruckersMP::InputMouseMoveEvent &e )
        {
            if( !g_overlay ) return;
            g_overlay->OpMuisBeweging( e.GetX(), e.GetY() );
        } );

        g_session->Input().OnMouseButton.Register( []( TruckersMP::InputMouseButtonEvent &e )
        {
            if( !g_overlay ) return;

            // Eén klik op de rechtermuisknop schakelt "muismodus" aan/uit
            // (niet vasthouden) -- net als Insert voor de overlay zelf.
            if( e.GetButton() == TruckersMP::MouseButton::Right && e.GetDown() && g_overlay->IsZichtbaar() )
            {
                ZetMuisVoorOverlay( !g_muisOpgevraagd );
                return; // deze klik zelf niet ook nog als "klik op de overlay" doorgeven
            }

            g_overlay->OpMuisKnop( static_cast<int>( e.GetButton() ), e.GetDown() );
        } );

        g_session->Input().OnMouseWheel.Register( []( TruckersMP::InputMouseWheelEvent &e )
        {
            if( !g_overlay ) return;
            g_overlay->OpMuisWiel( static_cast<float>( e.GetDelta() ) );
        } );
    }
}

// ---------------------------------------------------------------------
// TruckersMP Client SDK entry points
// ---------------------------------------------------------------------

// Wordt aangeroepen door de Beschermd()-wrapper in CallbackHulp.hxx zodra
// een callback een exceptie laat ontsnappen. Logt naar de clientlog, want
// daar kijken gebruikers en het TMP-team toch al (aanbeveling uit de docs).
// Valt stil terug als er (nog) geen sessie is -- bijvoorbeeld tijdens
// afsluiten.
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

    // ALLEREERST: gegevens uit de oude map meeverhuizen. De plugin heette
    // vroeger "Ritten Overlay" en bewaarde alles in %APPDATA%\RittenOverlay.
    // Zonder deze stap zou iemand die bijwerkt zijn rittenlogboek, zijn
    // tachograafstand en zijn instellingen "kwijt" zijn -- ze staan er nog,
    // maar de plugin kijkt op de nieuwe plek.
    //
    // Alleen kopieren, nooit verplaatsen of verwijderen: gaat er iets mis,
    // dan staat het origineel er nog. En alleen als de nieuwe map nog leeg
    // is, zodat dit precies een keer gebeurt.
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
                // PER BESTAND kijken, niet of de nieuwe MAP al bestaat.
                // Gemeten 31-08: die map wordt al aangemaakt door een ander
                // onderdeel voordat deze code aan de beurt is, waardoor de
                // hele verhuizing werd overgeslagen en het leek alsof alles
                // weg was.
                std::filesystem::create_directories( nieuw, ec );

                for( const auto &item : std::filesystem::directory_iterator( oud, ec ) )
                {
                    const std::filesystem::path doel = nieuw / item.path().filename();
                    if( std::filesystem::exists( doel, ec ) ) continue; // niets overschrijven

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

                // Het logo heette vroeger naar het bedrijf van de bouwer;
                // nu heet het gewoon logo.png. Even meenemen, anders staat
                // er "geen logo" terwijl het bestand er wel is.
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

    // Elke keer dat een rit wordt afgerond/geannuleerd, ook een Discord-
    // melding proberen te sturen (DiscordWebhook checkt zelf of dat aan
    // staat en of er een URL is ingesteld -- hier hoeven we daar niet op te
    // letten).
    g_logger->ZetVoltooidCallback( []( const Ritten::Trip &trip ) { g_discord->StuurRitVoltooid( trip ); } );

    // EERST het logboek leegmaken, DAARNA pas de onderdelen aanmaken.
    // Andersom werd alles wat een constructor logde meteen weer gewist --
    // gemeten 31-08: de regel "afstandsfactor uit vorige sessie" verscheen
    // nooit, terwijl het inlezen wel degelijk gebeurde.
    Ritten::Logboek::StartNieuweSessie( "CabNavi gestart" );
    {
        std::error_code ec;
        if( const char *ad = std::getenv( "APPDATA" ) )
        {
            const std::filesystem::path oudePad = std::filesystem::path( ad ) / "RittenOverlay";
            if( std::filesystem::exists( oudePad, ec ) )
            {
                Ritten::Logboek::Schrijf( "start",
                    "oude map RittenOverlay gevonden -- gegevens zijn gekopieerd naar CabNavi. "
                    "De oude map blijft staan en mag je zelf weggooien." );
            }
        }
    }

    g_busTracking = std::make_unique<Ritten::BusTracking>( *g_session, *g_logger );
    g_vrachtTracking = std::make_unique<Ritten::TruckTracking>( *g_logger, *g_brandstof );
    g_vrachtTracking->ZetBusTracking( g_busTracking.get() );
    g_spelers = std::make_unique<Ritten::PlayersNearby>( *g_session );
    g_vrachtTracking->ZetIncidentKoppeling( g_spelers.get(), g_incidentRecorder.get() );

    Ritten::Logboek::Schrijf( "start", "plugin geladen, sessie opgezet" );

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
    Ritten::Logboek::Schrijf( "gebeurt", "plugin wordt afgesloten" );
    ZetMuisVoorOverlay( false );
    if( g_overlay ) g_overlay->Shutdown();
    g_overlay.reset();
    g_spelers.reset();
    g_vrachtTracking.reset();
    g_busTracking.reset();
    g_discord.reset();
    g_incidentRecorder.reset();
    g_brandstof.reset();
    g_logger.reset();
    g_session.reset(); // ontkoppelt automatisch alle geregistreerde events
}

// ---------------------------------------------------------------------
// SCS Telemetry SDK entry points (voor reguliere vrachtjobs; zie
// TruckTracking.hxx voor waarom dit apart van de TruckersMP SDK loopt)
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
        // Kan gebeuren als scs_telemetry_init voor truckersmp_init draait
        // (zie de "hoofdbestand deelt globals" opmerking in de how-to);
        // maak alvast een logger + tracker aan zodat er niets verloren gaat.
        g_logger = std::make_unique<Ritten::TripLogger>();
        g_brandstof = std::make_unique<Ritten::FuelCosts>();
        g_vrachtTracking = std::make_unique<Ritten::TruckTracking>( *g_logger, *g_brandstof );
    }

    g_vrachtTracking->RegistreerBijTelemetrie( p );

    // Bevestiging in het SPEL-logbestand (niet debug.log) dat de SCS
    // Telemetry-kant van de plugin daadwerkelijk is opgestart. Als je deze
    // regel niet ziet in client.log, wordt scs_telemetry_init helemaal
    // niet aangeroepen door het spel -- dan zit het probleem dus niet in de
    // job-herkenning zelf, maar al bij het laden van dit deel van de plugin.
    p->common.log( SCS_LOG_TYPE_message, "CabNavi: SCS telemetry geinitialiseerd, kanalen geregistreerd." );

    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_telemetry_shutdown( void )
{
    // Kanalen/events worden automatisch losgekoppeld door de host.
}

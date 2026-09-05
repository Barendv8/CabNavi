cmake_minimum_required(VERSION 3.21)
project(cabnavi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# De C++-runtime MEEBAKKEN in de DLL in plaats van ernaar verwijzen.
# Zonder dit heeft de gebruiker de "Visual C++ Redistributable" nodig; heeft
# hij die niet, dan laadt de plugin gewoon niet en snapt niemand waarom.
# Met deze regel is de DLL zelfstandig: neerzetten en klaar.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if (NOT WIN32)
    message(FATAL_ERROR "Deze plugin bouwt alleen voor Windows x64 (dat is de enige doelplatform van de TruckersMP Client SDK).")
endif ()

# ---------------------------------------------------------------------------
# Afhankelijkheden
# ---------------------------------------------------------------------------
include(FetchContent)

# Dear ImGui -- voor de in-game overlay (rendert via de DirectX11 backend)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.1
)
FetchContent_MakeAvailable(imgui)

# nlohmann/json -- voor het ritten-logboek (trips.jsonl) en instellingen
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

# stb_image -- simpele header-only PNG-decoder, voor het laden van het
# VTC-logo (logo.png) als DirectX11-textuur.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(stb)

# TruckersMP GameClientSDK -- header-only. Clone hem zelf naast dit project:
#   git clone https://github.com/TruckersMP/GameClientSDK.git
# en pas onderstaand pad aan indien nodig.
set(TMP_SDK_DIR "${CMAKE_SOURCE_DIR}/../GameClientSDK/include" CACHE PATH
    "Pad naar de include/ map van TruckersMP/GameClientSDK")

if (NOT EXISTS "${TMP_SDK_DIR}/TruckersMP/TruckersMP.hxx")
    message(WARNING
        "TruckersMP GameClientSDK niet gevonden op ${TMP_SDK_DIR}.\n"
        "Clone de SDK: git clone https://github.com/TruckersMP/GameClientSDK.git\n"
        "en geef het pad mee met -DTMP_SDK_DIR=... indien het niet naast dit project staat.")
endif ()

# SCS Telemetry SDK -- ANDERE SDK dan hierboven, van SCS Software zelf, nodig
# voor reguliere vrachtjobs (snelheid/brandstof/schade/job-events). Download
# de "SDK" van https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry
# (zip met o.a. include/scssdk_telemetry.h) en pak uit naast dit project, of
# geef het pad hieronder mee met -DSCS_SDK_DIR=...
set(SCS_SDK_DIR "${CMAKE_SOURCE_DIR}/../scssdk/include" CACHE PATH
    "Pad naar de include/ map van de SCS Telemetry SDK (scssdk_telemetry.h)")

if (NOT EXISTS "${SCS_SDK_DIR}/scssdk_telemetry.h")
    message(WARNING
        "SCS Telemetry SDK niet gevonden op ${SCS_SDK_DIR}.\n"
        "Download hem van https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry\n"
        "pak uit naast dit project in een map 'scssdk', of geef het pad mee met -DSCS_SDK_DIR=...")
endif ()

# ---------------------------------------------------------------------------
# Plugin DLL
# ---------------------------------------------------------------------------
add_library(cabnavi SHARED
    src/Plugin.cxx
    src/TripLogger.cxx
    src/BusTracking.cxx
    src/TruckTracking.cxx
    src/PlayersNearby.cxx
    src/FuelCosts.cxx
    src/DiscordWebhook.cxx
    src/WebApi.cxx
    src/IncidentRecorder.cxx
    src/Overlay.cxx
    src/SiiDecode.cxx
    src/SaveLezer.cxx
    src/ScsArchief.cxx
    src/Kaartdata.cxx
    src/stb_image_impl.cxx

    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
)

target_include_directories(cabnavi PRIVATE
    "${TMP_SDK_DIR}"
    "${SCS_SDK_DIR}"
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
    "${stb_SOURCE_DIR}"
    src
)

target_link_libraries(cabnavi PRIVATE
    nlohmann_json::nlohmann_json
    d3d11
    dxgi
    winhttp
    version
    shell32
    user32
)

target_compile_definitions(cabnavi PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# De SCS-macro's (SCSAPI_RESULT/SCSAPI_VOID) exporteren hun functies -- anders
# dan TruckersMP's eigen TMP_EXPORT-macro -- NIET automatisch uit de DLL. Het
# officiele SCS-voorbeeldproject regelt dit via een los .def-bestand
# (zie telemetry.def in scssdk/examples/telemetry); dit doet hetzelfde voor
# scs_telemetry_init/scs_telemetry_shutdown. Zonder dit werd de vracht-
# telemetrie helemaal niet aangeroepen door het spel.
if( MSVC )
    target_link_options( cabnavi PRIVATE "/DEF:${CMAKE_SOURCE_DIR}/cabnavi.def" )
endif()

# Naam van het uiteindelijke DLL-bestand dat in de plugins-map van het spel komt
set_target_properties(cabnavi PROPERTIES OUTPUT_NAME "cabnavi")

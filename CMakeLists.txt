cmake_minimum_required(VERSION 3.21)
project(cabnavi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if (NOT WIN32)
    message(FATAL_ERROR "Deze plugin bouwt alleen voor Windows x64 (dat is de enige doelplatform van de TruckersMP Client SDK).")
endif ()

include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.1
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(stb)

set(TMP_SDK_DIR "${CMAKE_SOURCE_DIR}/../GameClientSDK/include" CACHE PATH
    "Pad naar de include/ map van TruckersMP/GameClientSDK")

if (NOT EXISTS "${TMP_SDK_DIR}/TruckersMP/TruckersMP.hxx")
    message(WARNING
        "TruckersMP GameClientSDK niet gevonden op ${TMP_SDK_DIR}.\n"
        "Clone de SDK: git clone https://github.com/TruckersMP/GameClientSDK.git\n"
        "en geef het pad mee met -DTMP_SDK_DIR=... indien het niet naast dit project staat.")
endif ()

set(SCS_SDK_DIR "${CMAKE_SOURCE_DIR}/../scssdk/include" CACHE PATH
    "Pad naar de include/ map van de SCS Telemetry SDK (scssdk_telemetry.h)")

if (NOT EXISTS "${SCS_SDK_DIR}/scssdk_telemetry.h")
    message(WARNING
        "SCS Telemetry SDK niet gevonden op ${SCS_SDK_DIR}.\n"
        "Download hem van https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry\n"
        "pak uit naast dit project in een map 'scssdk', of geef het pad mee met -DSCS_SDK_DIR=...")
endif ()

add_library(cabnavi SHARED
    src/Plugin.cxx
    src/TripLogger.cxx
    src/BusTracking.cxx
    src/TruckTracking.cxx
    src/PlayersNearby.cxx
    src/FuelCosts.cxx
    src/DiscordWebhook.cxx
    src/VtcWebhook.cxx
    src/VtcBron.cxx
    src/HubFormaat.cxx
    src/RitGeheugen.cxx
    src/RitBoek.cxx
    src/SpelConfig.cxx
    src/WebApi.cxx
    src/IncidentRecorder.cxx
    src/Overlay.cxx
    src/SiiDecode.cxx
    src/SaveLezer.cxx
    src/BusSaveLezer.cxx
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
    crypt32
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

if( MSVC )
    target_link_options( cabnavi PRIVATE "/DEF:${CMAKE_SOURCE_DIR}/cabnavi.def" )
endif()

set_target_properties(cabnavi PROPERTIES OUTPUT_NAME "cabnavi")

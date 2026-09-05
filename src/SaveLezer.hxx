#pragma once
// ---------------------------------------------------------------------------
// SaveLezer -- searches the player's saves for the truck with a given
// odometer reading, and returns the dashboard trip counter.
//
// WHY: the SDK does not give the dashboard average. The game does store
// it, per truck, in the save: trip_fuel (litres) and trip_distance (km)
// since the last reset on the dashboard. By reading that at load time the
// HUD counter starts at exactly the same value as the dashboard, also for
// someone who never presses reset, and also after reloading an autosave.
//
// WHY BY ODOMETER: my_truck and assigned_truck are 'null' in the save
// (MEASURED 04-09 on two saves, even in an owned truck). The odometer with
// its fractional part is unique, and at load time the live value from the
// SDK is exactly that of the save.
//
// READ ONLY. The file is opened with GENERIC_READ and nothing else, with
// FILE_SHARE_WRITE so the game can keep writing to it meanwhile. There is
// not a single write call in this file or in SiiDecode, and it must stay
// that way.
// ---------------------------------------------------------------------------

#include "SiiDecode.hxx"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Ritten
{
    struct SaveTripteller
    {
        double kilometerstandKm = 0.0;  // odometer + fraction, for checking
        double onderhoudKm = 0.0;  // integrity_odometer + fraction
        double tripLiters = 0.0;  // trip_fuel_l + trip_fuel
        double tripKm = 0.0;  // trip_distance_km + trip_distance/1000
        std::string bron;  // short name of the save folder, e.g. "autosave"
    };

    class SaveLezer
    {
    public:
        // All game.sii under the ETS2 profiles, newest first.
        static std::vector<std::filesystem::path> ZoekSaveBestanden();

        // Read a save (read-only) and find the vehicle block with this
        // odometer, within 3 metres. Empty if nothing matches.
        static std::optional<SaveTripteller> ZoekInSave( const std::filesystem::path &pad, double kilometerstandKm, std::string &fout );

        // Search ALL saves, newest first, and return the first hit. Costs
        // 100-300 ms per save: only on a background thread.
        static std::optional<SaveTripteller> ZoekInAlleSaves( double kilometerstandKm, std::string &fout );

        // File read-only into memory.
        static bool LeesBestand( const std::filesystem::path &pad, std::vector<std::uint8_t> &uit );
    };
}

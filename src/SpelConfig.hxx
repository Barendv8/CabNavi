#pragma once
// SpelConfig.hxx
//
// The game's own gameplay settings -- fuel simulation, damage, fatigue,
// speed limiter, hardcore simulation -- live in plain-text config files
// next to the saves: `uset g_fuel_simulation "1"` and friends. Company
// hubs want to know them (a trip with fuel simulation off is not the same
// as one with it on), and the established trackers read exactly these.
//
// Read only, like the save reader: the profile's config_local.cfg (the
// most recently touched profile) first, the game's global config.cfg as
// fallback per key. Loaded at start and at every trip start; that is cheap
// and catches a setting the player changed in between.

#include <map>
#include <string>

namespace Ritten
{
    class SpelConfig
    {
    public:
        // Re-read the files. Never throws, never writes.
        void Laad();

        // Raw value for a key ("g_fuel_simulation"), "" when absent.
        std::string Waarde( const std::string &sleutel ) const;

        // Convenience: "1"/"true" -> true. Absent -> `standaard`.
        bool Aan( const std::string &sleutel, bool standaard = false ) const;

        // The keys hubs care about, as a name -> bool map with the `g_`
        // prefix stripped: { "fuel_simulation": true, ... }. Only keys that
        // were actually present are included.
        std::map<std::string, bool> RealistischeInstellingen() const;

        // Everything read, for the open trip format.
        const std::map<std::string, std::string> &Alles() const { return m_waarden; }

    private:
        void LeesBestand( const std::wstring &pad );
        std::map<std::string, std::string> m_waarden;
    };
}

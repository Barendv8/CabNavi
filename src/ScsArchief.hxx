#pragma once
// ---------------------------------------------------------------------------
// ScsArchief -- READ the game's own .scs archives (HashFS version 2) and pull
// the per-country fuel price out of def/country/*.sui.
//
// WHY: the SDK does not pass the pump price. The game does keep it, per
// country, in its data files (fuel_price). Reading it from the installed game
// means the prices are always those of the version you actually run -- an
// SCS update or a new map DLC is picked up automatically, nobody maintains a
// list.
//
// MEASURED 04-09 on def.scs of 1.60.1.7: 66,928 entries, 36 countries,
// netherlands 2.253, germany 2.273 -- identical to the manually extracted
// files.
//
// Format (from nautofon's Archive::SCS::HashFS2, public):
//   header 0x34 bytes: 'SCS#', u16 version (2), u16 salt, 'CITY', u32 entry
//   count, u32 size1, u32 words2, u32 size2, u64 start1, u64 start2, u64 cert.
//   Index 1 (zlib, start1): per entry 16 bytes: u64 hash, u32 part offset,
//   u16 part count, u16 flags (bit0 = directory).
//   Index 2 (zlib, start2): parts of 4 bytes (u16 offset, u8 offset high,
//   u8 kind); a data part (kind & 0x80) is 16 bytes at offset*4: zsize,
//   flags (0x10 = zlib), usize, unused, u32 data offset (*16).
//   Directory entry: u32 count, count bytes of name lengths, then the names;
//   a leading '/' marks a subdirectory.
//   Lookup key: CityHash64 of the path without leading slash.
//
// READ ONLY. Not a single write call in this module, and it must stay that
// way. Portable (no Windows API) so it can be tested on Linux.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Ritten
{
    class ScsArchief
    {
    public:
        // Google CityHash64 (v1.0.3), the hash SCS uses for archive paths.
        static std::uint64_t CityHash64( const std::string &s );

        // Open an archive. Reads and inflates both indexes into memory (a few
        // MB); the file data itself is read on demand.
        bool Open( const std::string &pad, std::string &fout );

        // Read one entry by path ("def/country/germany.sui"). Empty + false if
        // the path is not in this archive.
        bool Lees( const std::string &pad, std::vector<std::uint8_t> &uit ) const;

        // Directory listing by path ("def/country"). Sub-directories come
        // back without the leading '/'.
        bool LeesMap( const std::string &pad, std::vector<std::string> &mappen, std::vector<std::string> &bestanden ) const;

        bool Bevat( const std::string &pad ) const;
        std::size_t AantalEntries() const { return m_entries.size(); }

        // Convenience: all fuel prices in this archive, token -> price.
        // Empty if the archive has no def/country.
        std::map<std::string, double> Brandstofprijzen() const;

        // Convenience: parse one country file's fuel_price. -1 if absent.
        static double FuelPriceUit( const std::vector<std::uint8_t> &sui );

        // Country centres from def/country ("pos: (x, y, z)"), token -> X/Z.
        // Fallback for the position lookup in map regions the embedded city
        // table does not know yet (new map DLC): coarse, but the right country.
        struct XZ { double x = 0.0, z = 0.0; };
        std::map<std::string, XZ> Landposities() const;

        // fuel_discount_in_garage from def/economy_data.sii (0.15 = 15%).
        // -1 if this archive has no economy_data. MEASURED 04-09: 0.15, and
        // the pump price at an own large garage was country price x 0.85.
        double GarageKorting() const;

    private:
        struct Entry
        {
            std::uint64_t offset = 0;
            std::uint32_t zsize = 0;
            std::uint32_t size = 0;
            std::uint8_t compressie = 0;
            bool isMap = false;
        };
        std::string m_pad;
        std::map<std::uint64_t, Entry> m_entries;

        bool LeesEntry( const Entry &e, std::vector<std::uint8_t> &uit ) const;
    };
}

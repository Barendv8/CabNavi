#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Ritten
{
    class ScsArchief
    {
    public:
        static std::uint64_t CityHash64( const std::string &s );

        bool Open( const std::string &pad, std::string &fout );

        bool Lees( const std::string &pad, std::vector<std::uint8_t> &uit ) const;

        bool LeesMap( const std::string &pad, std::vector<std::string> &mappen, std::vector<std::string> &bestanden ) const;

        bool Bevat( const std::string &pad ) const;
        std::size_t AantalEntries() const { return m_entries.size(); }

        std::map<std::string, double> Brandstofprijzen() const;

        static double FuelPriceUit( const std::vector<std::uint8_t> &sui );

        struct XZ { double x = 0.0, z = 0.0; };
        std::map<std::string, XZ> Landposities() const;

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

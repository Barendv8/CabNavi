#include "SaveLezer.hxx"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shlobj.h>
#endif

namespace Ritten
{
#ifdef _WIN32
    std::vector<std::filesystem::path> SaveLezer::ZoekSaveBestanden()
    {
        std::vector<std::filesystem::path> uit;
        PWSTR doc = nullptr;
        if( SHGetKnownFolderPath( FOLDERID_Documents, 0, nullptr, &doc ) != S_OK || !doc ) return uit;
        const std::filesystem::path basis = std::filesystem::path( doc ) / L"Euro Truck Simulator 2" / L"profiles";
        CoTaskMemFree( doc );
        std::error_code ec;
        if( !std::filesystem::is_directory( basis, ec ) ) return uit;
        for( const auto &profiel : std::filesystem::directory_iterator( basis, ec ) )
        {
            const auto saveMap = profiel.path() / L"save";
            if( !std::filesystem::is_directory( saveMap, ec ) ) continue;
            for( const auto &slot : std::filesystem::directory_iterator( saveMap, ec ) )
            {
                const auto g = slot.path() / L"game.sii";
                if( std::filesystem::is_regular_file( g, ec ) ) uit.push_back( g );
            }
        }
        // Newest first: the save that was just loaded is usually also the
        // most recently written.
        std::sort( uit.begin(), uit.end(), [ &ec ]( const auto &a, const auto &b )
        {
            return std::filesystem::last_write_time( a, ec ) > std::filesystem::last_write_time( b, ec );
        } );
        return uit;
    }

    bool SaveLezer::LeesBestand( const std::filesystem::path &pad, std::vector<std::uint8_t> &uit )
    {
        // GENERIC_READ and nothing else. FILE_SHARE_WRITE so the game can
        // simply overwrite the file meanwhile; we hold nothing.
        HANDLE h = CreateFileW( pad.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
        if( h == INVALID_HANDLE_VALUE ) return false;
        LARGE_INTEGER groot{};
        if( !GetFileSizeEx( h, &groot ) || groot.QuadPart <= 0 || groot.QuadPart > ( 512LL << 20 ) ) { CloseHandle( h ); return false; }
        uit.resize( static_cast<std::size_t>( groot.QuadPart ) );
        std::size_t gelezen = 0;
        while( gelezen < uit.size() )
        {
            DWORD n = 0;
            const DWORD wil = static_cast<DWORD>( std::min<std::size_t>( uit.size() - gelezen, 1u << 20 ) );
            if( !ReadFile( h, uit.data() + gelezen, wil, &n, nullptr ) || n == 0 ) { CloseHandle( h ); return false; }
            gelezen += n;
        }
        CloseHandle( h );
        return true;
    }
#else
    // Non-Windows: only for the compile test and the standalone test on Linux.
    std::vector<std::filesystem::path> SaveLezer::ZoekSaveBestanden() { return {}; }

    bool SaveLezer::LeesBestand( const std::filesystem::path &pad, std::vector<std::uint8_t> &uit )
    {
        std::ifstream in( pad, std::ios::binary );
        if( !in ) return false;
        uit.assign( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
        return true;
    }
#endif

    std::optional<SaveTripteller> SaveLezer::ZoekInSave( const std::filesystem::path &pad, const double kilometerstandKm, std::string &fout )
    {
        std::vector<std::uint8_t> ruw;
        if( !LeesBestand( pad, ruw ) ) { fout = "not readable"; return std::nullopt; }
        const std::vector<SiiDecode::SaveTruck> trucks = SiiDecode::LeesTrucks( ruw, fout );
        if( !fout.empty() ) return std::nullopt;

        // The SDK gives the reading as float32. At 19,000 km the resolution
        // is ~2 metres, at 930,000 km ~62 metres. The tolerance scales along:
        // two ULPs, with 3 metres as the floor. Dealer stock sits on round
        // numbers without a fractional part; a driven truck is always more
        // than a few metres away from that.
        const double tolerantie = std::max( 0.003, kilometerstandKm * 2.4e-7 );
        for( const SiiDecode::SaveTruck &t : trucks )
        {
            if( std::fabs( t.kmStand - kilometerstandKm ) < tolerantie )
            {
                SaveTripteller r;
                r.kilometerstandKm = t.kmStand;
                r.onderhoudKm = t.integriteit;
                r.tripLiters = t.tripLiters;
                r.tripKm = t.tripKm;
                r.bron = pad.parent_path().filename().string();
                return r;
            }
        }
        return std::nullopt;
    }

    std::optional<SaveTripteller> SaveLezer::ZoekInAlleSaves( const double kilometerstandKm, std::string &fout )
    {
        fout.clear();
        std::string laatsteFout;
        for( const auto &pad : ZoekSaveBestanden() )
        {
            std::string f;
            if( auto r = ZoekInSave( pad, kilometerstandKm, f ) ) return r;
            if( !f.empty() ) laatsteFout = f;
        }
        fout = laatsteFout;
        return std::nullopt;
    }
}

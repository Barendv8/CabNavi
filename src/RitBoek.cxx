#include "RitBoek.hxx"
#include "Spel.hxx"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>

using json = nlohmann::json;

namespace Ritten
{
    namespace
    {
        constexpr int FORMAAT = 1;

        json NaarJson( const FuelCosts::LeegRijden &l )
        {
            return { { "km", l.km }, { "liters", l.liters }, { "kosten", l.kosten } };
        }

        FuelCosts::LeegRijden LeegVan( const json &j )
        {
            FuelCosts::LeegRijden l;
            if( !j.is_object() ) return l;
            l.km = j.value( "km", 0.0 );
            l.liters = j.value( "liters", 0.0 );
            l.kosten = j.value( "kosten", 0.0 );
            return l;
        }

        const char *DoorgangNaam( const DoorgangType t )
        {
            return t == DoorgangType::Tol ? "tol" : t == DoorgangType::Veerboot ? "veerboot" : "trein";
        }

        DoorgangType DoorgangVan( const std::string &s )
        {
            return s == "veerboot" ? DoorgangType::Veerboot : s == "trein" ? DoorgangType::Trein : DoorgangType::Tol;
        }
    }

    std::filesystem::path RitBoek::Pad( const Soort soort )
    {
        std::filesystem::path basis;
        if( const char *appdata = std::getenv( "APPDATA" ) ) basis = appdata;
        else basis = std::filesystem::current_path();
        basis /= "CabNavi";
        std::error_code ec;
        std::filesystem::create_directories( basis, ec );
        std::string naam = soort == Soort::Bus ? "lopende_busrit" : "lopende_rit";
        if( SpelInfo::IsAts() ) naam += "_ats";
        return basis / ( naam + ".json" );
    }

    bool RitBoek::Lees( const Soort soort, LopendeRit &uit )
    {
        try
        {
            std::ifstream in( Pad( soort ) );
            if( !in ) return false;
            json j; in >> j;
            if( !j.is_object() || j.value( "formaat", 0 ) != FORMAAT ) return false;

            LopendeRit r;
            r.sleutel = j.value( "sleutel", std::string() );
            if( r.sleutel.empty() ) return false;
            r.id = j.value( "id", std::string() );
            r.startTijdIso = j.value( "start_tijd", std::string() );
            r.economyStartTijd = j.value( "economy_start", static_cast<std::uint32_t>( 0 ) );
            r.voertuigMerk = j.value( "voertuig_merk", std::string() );
            r.voertuigModel = j.value( "voertuig_model", std::string() );
            r.kmStand = j.value( "km_stand", 0.0 );
            r.liters = j.value( "liters", 0.0 );
            r.kosten = j.value( "kosten", 0.0 );
            r.km = j.value( "km", 0.0 );
            if( j.contains( "leeg_voor" ) ) r.leegVoor = LeegVan( j[ "leeg_voor" ] );
            r.boeteKosten = j.value( "boete_kosten", static_cast<std::int64_t>( 0 ) );
            r.tolKosten = j.value( "tol_kosten", static_cast<std::int64_t>( 0 ) );
            r.veerbootKosten = j.value( "veerboot_kosten", static_cast<std::int64_t>( 0 ) );
            r.treinKosten = j.value( "trein_kosten", static_cast<std::int64_t>( 0 ) );
            r.bewaardIso = j.value( "bewaard", std::string() );

            if( j.contains( "tankbeurten" ) && j[ "tankbeurten" ].is_array() )
            {
                for( const json &t : j[ "tankbeurten" ] )
                {
                    if( !t.is_object() ) continue;
                    FuelCosts::Tankbeurt tb;
                    tb.liters = t.value( "liters", 0.0 );
                    tb.kostenEuro = t.value( "kosten", 0.0 );
                    tb.kmStand = t.value( "km_stand", 0.0 );
                    tb.land = t.value( "land", std::string() );
                    tb.stad = t.value( "stad", std::string() );
                    tb.garage = t.value( "garage", false );
                    tb.prijsPerLiter = t.value( "prijs", 0.0 );
                    r.tankbeurten.push_back( tb );
                }
            }
            if( j.contains( "boetes" ) && j[ "boetes" ].is_array() )
            {
                for( const json &b : j[ "boetes" ] )
                {
                    if( !b.is_object() ) continue;
                    Boete boete;
                    boete.reden = b.value( "reden", std::string() );
                    boete.bedrag = b.value( "bedrag", static_cast<std::int64_t>( 0 ) );
                    r.boetes.push_back( boete );
                }
            }
            if( j.contains( "doorgangen" ) && j[ "doorgangen" ].is_array() )
            {
                for( const json &d : j[ "doorgangen" ] )
                {
                    if( !d.is_object() ) continue;
                    Doorgang dg;
                    dg.type = DoorgangVan( d.value( "soort", std::string() ) );
                    dg.bedrag = d.value( "bedrag", static_cast<std::int64_t>( 0 ) );
                    dg.vanaf = d.value( "vanaf", std::string() );
                    dg.naar = d.value( "naar", std::string() );
                    r.doorgangen.push_back( dg );
                }
            }

            if( j.contains( "momenten" ) && j[ "momenten" ].is_array() )
            {
                for( const json &m : j[ "momenten" ] )
                {
                    if( !m.is_object() ) continue;
                    RitMoment rm;
                    rm.kmStand = m.value( "s", 0.0 );
                    rm.liters = m.value( "l", 0.0 );
                    rm.kosten = m.value( "k", 0.0 );
                    rm.km = m.value( "r", 0.0 );
                    rm.boetes = m.value( "b", static_cast<std::uint32_t>( 0 ) );
                    rm.doorgangen = m.value( "d", static_cast<std::uint32_t>( 0 ) );
                    rm.tankbeurten = m.value( "t", static_cast<std::uint32_t>( 0 ) );
                    if( rm.kmStand <= 0.0 || ( !r.momenten.empty() && rm.kmStand <= r.momenten.back().kmStand ) ) continue;
                    r.momenten.push_back( rm );
                }
            }

            if( r.liters < 0.0 || r.kosten < 0.0 || r.km < 0.0 ) return false;

            uit = r;
            return true;
        }
        catch( ... )
        {
            return false;
        }
    }

    bool RitBoek::Bewaar( const Soort soort, const LopendeRit &rit )
    {
        try
        {
            json j;
            j[ "formaat" ] = FORMAAT;
            j[ "sleutel" ] = rit.sleutel;
            j[ "id" ] = rit.id;
            j[ "start_tijd" ] = rit.startTijdIso;
            j[ "economy_start" ] = rit.economyStartTijd;
            j[ "voertuig_merk" ] = rit.voertuigMerk;
            j[ "voertuig_model" ] = rit.voertuigModel;
            j[ "km_stand" ] = rit.kmStand;
            j[ "liters" ] = rit.liters;
            j[ "kosten" ] = rit.kosten;
            j[ "km" ] = rit.km;
            j[ "leeg_voor" ] = NaarJson( rit.leegVoor );
            j[ "boete_kosten" ] = rit.boeteKosten;
            j[ "tol_kosten" ] = rit.tolKosten;
            j[ "veerboot_kosten" ] = rit.veerbootKosten;
            j[ "trein_kosten" ] = rit.treinKosten;
            j[ "bewaard" ] = rit.bewaardIso;

            json tank = json::array();
            for( const FuelCosts::Tankbeurt &t : rit.tankbeurten )
                tank.push_back( { { "liters", t.liters }, { "kosten", t.kostenEuro }, { "km_stand", t.kmStand },
                                  { "land", t.land }, { "stad", t.stad }, { "garage", t.garage },
                                  { "prijs", t.prijsPerLiter } } );
            j[ "tankbeurten" ] = tank;

            json boetes = json::array();
            for( const Boete &b : rit.boetes ) boetes.push_back( { { "reden", b.reden }, { "bedrag", b.bedrag } } );
            j[ "boetes" ] = boetes;

            json doorgangen = json::array();
            for( const Doorgang &d : rit.doorgangen )
                doorgangen.push_back( { { "soort", DoorgangNaam( d.type ) }, { "bedrag", d.bedrag },
                                        { "vanaf", d.vanaf }, { "naar", d.naar } } );
            j[ "doorgangen" ] = doorgangen;

            json momenten = json::array();
            for( const RitMoment &m : rit.momenten )
                momenten.push_back( { { "s", m.kmStand }, { "l", m.liters }, { "k", m.kosten }, { "r", m.km },
                                      { "b", m.boetes }, { "d", m.doorgangen }, { "t", m.tankbeurten } } );
            j[ "momenten" ] = momenten;

            const std::filesystem::path doel = Pad( soort );
            std::filesystem::path tijdelijk = doel;
            tijdelijk += ".tmp";
            {
                std::ofstream uit( tijdelijk, std::ios::trunc );
                if( !uit ) return false;
                uit << j.dump();
                if( !uit ) return false;
            }
            std::error_code ec;
            std::filesystem::rename( tijdelijk, doel, ec );
            return !ec;
        }
        catch( ... )
        {
            return false;
        }
    }

    void RitBoek::VoegMomentToe( std::vector<RitMoment> &momenten, const RitMoment &moment )
    {
        if( moment.kmStand <= 0.0 ) return;
        if( !momenten.empty() )
        {
            const double verder = moment.kmStand - momenten.back().kmStand;
            if( verder < -0.05 ) return;
            if( verder <= 0.05 )
            {
                const double stand = momenten.back().kmStand;
                momenten.back() = moment;
                momenten.back().kmStand = stand;
                return;
            }
        }
        momenten.push_back( moment );
        if( momenten.size() > 1000 )
        {
            std::vector<RitMoment> dunner;
            dunner.reserve( 760 );
            for( std::size_t i = 0; i < momenten.size(); ++i )
                if( i >= 500 || i % 2 == 0 ) dunner.push_back( momenten[ i ] );
            momenten.swap( dunner );
        }
    }

    bool RitBoek::KnipTerug( const LopendeRit &rit, const double kmNu, LopendeRit &uit )
    {
        if( kmNu <= 0.0 || rit.kmStand <= 0.0 ) return false;
        if( kmNu > rit.kmStand + SPELING_KM ) return false;
        if( kmNu >= rit.kmStand - GELIJK_KM ) { uit = rit; return true; }

        std::size_t gekozen = rit.momenten.size();
        for( std::size_t i = 0; i < rit.momenten.size(); ++i )
        {
            if( rit.momenten[ i ].kmStand <= kmNu + GELIJK_KM ) gekozen = i;
            else break;
        }
        if( gekozen == rit.momenten.size() ) return false;

        const RitMoment &m = rit.momenten[ gekozen ];
        uit = rit;
        uit.kmStand = m.kmStand;
        uit.liters = m.liters;
        uit.kosten = m.kosten;
        uit.km = m.km;
        uit.momenten.resize( gekozen + 1 );

        if( uit.boetes.size() > m.boetes ) uit.boetes.resize( m.boetes );
        if( uit.doorgangen.size() > m.doorgangen ) uit.doorgangen.resize( m.doorgangen );
        uit.boeteKosten = 0;
        for( const Boete &b : uit.boetes ) uit.boeteKosten += b.bedrag;
        uit.tolKosten = uit.veerbootKosten = uit.treinKosten = 0;
        for( const Doorgang &d : uit.doorgangen )
        {
            switch( d.type )
            {
                case DoorgangType::Tol:      uit.tolKosten += d.bedrag; break;
                case DoorgangType::Veerboot: uit.veerbootKosten += d.bedrag; break;
                case DoorgangType::Trein:    uit.treinKosten += d.bedrag; break;
            }
        }
        if( uit.tankbeurten.size() > m.tankbeurten )
            uit.tankbeurten.erase( uit.tankbeurten.begin(),
                                   uit.tankbeurten.begin() + static_cast<std::ptrdiff_t>( uit.tankbeurten.size() - m.tankbeurten ) );
        return true;
    }

    void RitBoek::Wis( const Soort soort )
    {
        try
        {
            std::error_code ec;
            std::filesystem::remove( Pad( soort ), ec );
        }
        catch( ... ) { }
    }
}

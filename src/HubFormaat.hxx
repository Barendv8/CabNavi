#pragma once

#include "TripTypes.hxx"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Ritten::HubFormaat
{
    struct Context
    {
        std::uint64_t steamId = 0;
        std::string spelerNaam;
        std::string tmpSpelerId;
        std::string game;
        double kmStandEind = 0.0;
        double schadeCabine = 0.0, schadeChassis = 0.0, schadeMotor = 0.0, schadeBak = 0.0, schadeWielen = 0.0;
        double aanhangerSchade = 0.0, ladingSchade = 0.0;
        double brandstofPrijsPerLiter = 0.0;
        struct Tank { double liters = 0.0; double kosten = 0.0; };
        std::vector<Tank> tankbeurten;
        std::string clientVersie = "1.2.0";

        double topKmh = 0.0;
        struct Punt { long long tijd = 0; double x = 0.0, z = 0.0; };
        std::vector<Punt> route;
        struct Overtreding { double maxKmh = 0.0, limietKmh = 0.0; long long start = 0, eind = 0; double x = 0.0, z = 0.0; };
        std::vector<Overtreding> overtredingen;
        int teleports = 0;
        std::string kenteken, kentekenLand;
        std::map<std::string, bool> realistischeInstellingen;
    };

    std::string BouwBody( const Trip &trip, const Context &ctx );

    std::string Handtekening( const std::string &geheim, const std::string &body );

    inline const char *HANDTEKENING_HEADER = "X-CabNavi-Signature";
    inline const char *HANDTEKENING_HEADER_GENERIEK = "Signature";

    std::string Sha256Hex( const std::string &data );
    std::string PyFloat( double v );
    std::string PyString( const std::string &s );
}

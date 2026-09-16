#pragma once
// HubFormaat.hxx
//
// A second shape for the VTC webhook, for company hubs that expect an
// event envelope -- { object, type, data } -- with an HMAC-SHA256 signature
// of the body in a request header. Several hubs in the trucking scene
// already speak this shape, so a company that has one can point its
// drivers at CabNavi without touching its hub.
//
// Two things matter for that to work, and both are handled here:
//
// 1. The signature is computed over the body EXACTLY as a Python
//    `json.dumps(data)` would print it -- ", " and ": " separators, keys in
//    the order they were written, non-ASCII escaped as \uXXXX, floats in
//    shortest round-trip form with a ".0" when whole. Receivers that parse
//    and re-serialise before verifying only match when the body already
//    has that form, and receivers that sign the raw bytes match anyway.
//
// 2. HMAC-SHA256 is implemented here, portable, so the type check covers it
//    and nothing depends on a Windows crypto handle at run time.
//
// The hub's own name is not used anywhere in CabNavi; this is simply
// "the envelope format" in the settings.

#include "TripTypes.hxx"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Ritten::HubFormaat
{
    // What the envelope needs beyond the Trip itself.
    struct Context
    {
        std::uint64_t steamId = 0;
        std::string spelerNaam;
        std::string tmpSpelerId;         // TruckersMP id as text, "" if unknown
        std::string game;                // "eut2" / "ats"
        double kmStandEind = 0.0;        // odometer at trip end, km
        double schadeCabine = 0.0, schadeChassis = 0.0, schadeMotor = 0.0, schadeBak = 0.0, schadeWielen = 0.0;   // percent 0-100
        double aanhangerSchade = 0.0, ladingSchade = 0.0;   // percent
        double brandstofPrijsPerLiter = 0.0;
        struct Tank { double liters = 0.0; double kosten = 0.0; };
        std::vector<Tank> tankbeurten;   // this trip
        std::string clientVersie = "1.2.0";

        // From the trip's memory (RitGeheugen) and the game config.
        double topKmh = 0.0;
        struct Punt { long long tijd = 0; double x = 0.0, z = 0.0; };
        std::vector<Punt> route;
        struct Overtreding { double maxKmh = 0.0, limietKmh = 0.0; long long start = 0, eind = 0; double x = 0.0, z = 0.0; };
        std::vector<Overtreding> overtredingen;
        int teleports = 0;
        std::string kenteken, kentekenLand;                 // plate text, country token
        std::map<std::string, bool> realistischeInstellingen;
    };

    // The complete request body for one finished/cancelled trip.
    std::string BouwBody( const Trip &trip, const Context &ctx );

    // Hex HMAC-SHA256 of `body` under `geheim`, as the header value.
    std::string Handtekening( const std::string &geheim, const std::string &body );

    // The signature travels under our own name. A hub reads whatever header
    // it is told to read; that is a one-line change on the receiving side.
    inline const char *HANDTEKENING_HEADER = "X-CabNavi-Signature";
    // The same value also goes out under the generic name that open-source
    // hubs read for "custom" trackers.
    inline const char *HANDTEKENING_HEADER_GENERIEK = "Signature";

    // Exposed for the unit-style checks in tools/compileertest.
    std::string Sha256Hex( const std::string &data );
    std::string PyFloat( double v );
    std::string PyString( const std::string &s );
}

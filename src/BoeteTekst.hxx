#pragma once
// BoeteTekst.hxx
//
// The game reports the reason for a fine as a short English code in the
// "fine.offence" attribute of the player.fined gameplay event. This table
// translates it to Dutch.
//
// Kept separate because both the overlay (Live tab) and the Discord
// webhook need it; it used to live only in Overlay.cxx where the webhook
// could not reach it.
//
// Unknown codes are returned RAW instead of translated to something vague
// like "offence": a code you can look up yourself is more useful than an
// invented description.

#include <string>

namespace Ritten
{
    inline std::string VertaalOffence( const std::string &code )
    {
        if( code == "speeding" )                 return "te hard rijden";
        if( code == "red_signal" )               return "door rood";
        if( code == "crash" )                    return "aanrijding";
        if( code == "no_lights" )                return "zonder licht";
        if( code == "wrong_way" )                return "verkeerde rijrichting";
        if( code == "sleeping" )                 return "rijtijd overschreden";
        if( code == "avoid_sleeping" )           return "rusttijd ontweken";
        if( code == "damage" )                   return "schade aan lading";
        if( code == "avoid_weighing" )           return "weegstation ontweken";
        if( code == "illegal_trailer" )          return "illegale aanhanger";
        if( code == "generic" )                  return "overtreding";
        if( code == "avoid_inspection" )         return "controle ontweken";
        if( code == "illegal_border_crossing" )  return "illegale grensovergang";
        if( code == "hard_shoulder_violation" )  return "vluchtstrook gebruikt";
        return code.empty() ? "onbekend" : code;
    }
}

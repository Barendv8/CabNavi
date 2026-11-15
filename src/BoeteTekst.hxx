#pragma once

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

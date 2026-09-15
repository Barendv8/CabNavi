#pragma once
// Eenheden.hxx
//
// The DISPLAY unit layer, and nothing more than that. Every calculation in
// this plugin runs, and keeps running, in the game's own units: metres,
// litres, kilometres, km/h, price per litre. This header only translates a
// finished number at the very last moment, on its way to the screen or to
// a webhook text. Protected zones (consumption, distance factor, driving
// style) never see it.
//
// Three choices, set in the Settings tab and stored in uiterlijk.json:
//   Automatisch  -- follow the game: ETS2 metric, ATS imperial. The default,
//                   so an existing ETS2 install behaves exactly as before.
//   Metrisch     -- always km / litres, also in ATS.
//   Imperiaal    -- always miles / gallons, also in ETS2.
//
// US gallons (3.785 l), because ATS is the reason this exists. Consumption
// flips from km/l to mpg -- the unit on the dashboard of an American truck,
// so the "gem" check there compares like with like.

#include "Spel.hxx"

namespace Ritten
{
    enum class EenheidKeuze
    {
        Automatisch = 0,
        Metrisch    = 1,
        Imperiaal   = 2,
    };

    class Eenheden
    {
    public:
        static EenheidKeuze &Keuze()
        {
            static EenheidKeuze keuze = EenheidKeuze::Automatisch;
            return keuze;
        }

        // The one question everything display-side asks.
        static bool Imperiaal()
        {
            switch( Keuze() )
            {
                case EenheidKeuze::Metrisch:  return false;
                case EenheidKeuze::Imperiaal: return true;
                default:                      return SpelInfo::IsAts();
            }
        }

        // --- conversions, input always the metric value the code computed ---
        static constexpr double KM_PER_MIJL = 1.609344;
        static constexpr double LITER_PER_GALLON = 3.785411784;

        static double Km( double km )            { return Imperiaal() ? km / KM_PER_MIJL : km; }
        static double Snelheid( double kmu )     { return Imperiaal() ? kmu / KM_PER_MIJL : kmu; }
        static double Liters( double l )         { return Imperiaal() ? l / LITER_PER_GALLON : l; }
        static double Verbruik( double kmPerL )  { return Imperiaal() ? kmPerL * KM_PER_MIJL * LITER_PER_GALLON : kmPerL; }  // km/l -> mpg
        static double LitersPerUur( double lu )  { return Imperiaal() ? lu / LITER_PER_GALLON : lu; }
        static double PrijsPerVolume( double perLiter ) { return Imperiaal() ? perLiter * LITER_PER_GALLON : perLiter; }
        static double PrijsInvoerNaarLiter( double invoer ) { return Imperiaal() ? invoer / LITER_PER_GALLON : invoer; }

        // --- labels, already in the language layer's hands (pass through T) ---
        static const char *AfstandLabel()   { return Imperiaal() ? "mi" : "km"; }
        static const char *SnelheidLabel()  { return Imperiaal() ? "mph" : "km/u"; }
        static const char *VolumeLabel()    { return Imperiaal() ? "gal" : "l"; }
        static const char *VerbruikLabel()  { return Imperiaal() ? "mpg" : "km/l"; }
        static const char *PerUurLabel()    { return Imperiaal() ? "gal/uur" : "l/uur"; }
    };
}

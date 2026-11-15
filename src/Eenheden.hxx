#pragma once

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

        static bool Imperiaal()
        {
            switch( Keuze() )
            {
                case EenheidKeuze::Metrisch:  return false;
                case EenheidKeuze::Imperiaal: return true;
                default:                      return SpelInfo::IsAts();
            }
        }

        static constexpr double KM_PER_MIJL = 1.609344;
        static constexpr double LITER_PER_GALLON = 3.785411784;

        static double Km( double km )            { return Imperiaal() ? km / KM_PER_MIJL : km; }
        static double Snelheid( double kmu )     { return Imperiaal() ? kmu / KM_PER_MIJL : kmu; }
        static double Liters( double l )         { return Imperiaal() ? l / LITER_PER_GALLON : l; }
        static double Verbruik( double kmPerL )  { return Imperiaal() ? kmPerL * KM_PER_MIJL * LITER_PER_GALLON : kmPerL; }
        static double LitersPerUur( double lu )  { return Imperiaal() ? lu / LITER_PER_GALLON : lu; }
        static double PrijsPerVolume( double perLiter ) { return Imperiaal() ? perLiter * LITER_PER_GALLON : perLiter; }
        static double PrijsInvoerNaarLiter( double invoer ) { return Imperiaal() ? invoer / LITER_PER_GALLON : invoer; }

        static const char *AfstandLabel()   { return Imperiaal() ? "mi" : "km"; }
        static const char *SnelheidLabel()  { return Imperiaal() ? "mph" : "km/u"; }
        static const char *VolumeLabel()    { return Imperiaal() ? "gal" : "l"; }
        static const char *VerbruikLabel()  { return Imperiaal() ? "mpg" : "km/l"; }
        static const char *PerUurLabel()    { return Imperiaal() ? "gal/uur" : "l/uur"; }
    };
}

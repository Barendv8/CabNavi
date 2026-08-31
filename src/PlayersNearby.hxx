#pragma once
// PlayersNearby.hxx
//
// Houdt een live lijst bij van spelers die in streaming-bereik zijn, via
// TruckersMP::Player (zie de Player-moduledocs). Belangrijk om te weten:
//
//   De SDK geeft ons per speler: naam, tag, voertuig/aanhanger, afstand,
//   ping en rechten (patron/moderator/team/manager). De SDK geeft ONS NIET
//   de lading, bron/bestemming of het inkomen van andermans job -- dat is
//   privé jobdata die alleen bij die speler zelf bekend is. Dus: "wat ze
//   vervoeren" tonen we als "beladen / leeg" (op basis van of er een
//   aanhanger gekoppeld is), niet als exacte lading -- dat zou verzonnen
//   zijn.
//
// OnUpdate vuurt op netwerksnelheid (zie de docs: "many times per second").
// We lezen daar dus alleen waarden uit een bestaande PlayerRecord bij, geen
// allocaties/lookups; de daadwerkelijke lijst wordt periodiek (elke frame,
// niet elke update) opnieuw opgebouwd door de overlay.

#include <TruckersMP/TruckersMP.hxx>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Ritten
{
    struct SpelerRecord
    {
        std::int32_t spelerId = 0;
        std::uint64_t accountId = 0;   // TruckersMP-ID, voor truckersmp.com/user/<id>
        std::uint64_t steamId = 0;     // Steam64-ID, voor steamcommunity.com/profiles/<id>
        std::string gebruikersnaam;
        std::string tagTekst;
        float tagKleurR = 1.f, tagKleurG = 1.f, tagKleurB = 1.f;

        bool isPatron = false;
        bool isModerator = false;
        bool isTeam = false;
        bool isManager = false;

        float afstandMeter = 0.f;
        std::uint16_t pingMs = 0;

        // --- Echte positie (SDK 1.0+, Vehicles-and-Trailers-module) -----
        // `peilingGraden` is de richting waarin deze speler ZICH BEVINDT,
        // gezien vanaf jou: 0 = recht vooruit, 90 = rechts, 180 = achter,
        // 270 = links. Dus niet de rijrichting van die speler zelf.
        // `koersVerschilGraden` is wel de rijrichting: het verschil tussen
        // zijn koers en die van jou, zodat je tegenliggers (rond 180) van
        // medereizigers (rond 0) kunt onderscheiden.
        // Beide alleen geldig als `positieBekend` waar is -- zonder
        // voertuig in de wereld geeft de SDK niets, en dan moet de radar
        // hem niet op een verzonnen plek zetten.
        bool positieBekend = false;
        float peilingGraden = 0.f;
        float koersVerschilGraden = 0.f;

        bool heeftAanhanger = false;
        std::string aanhangerType;

        // Lengte van de aanhanger in meters, uit Trailer::GetBoundingBox()
        // (SDK 1.1.0). Hiermee zie je of iemand een gewone oplegger trekt of
        // een dubbele combinatie -- dat scheelt nogal of je aan een
        // inhaalactie begint. -1 = niet bekend.
        float aanhangerLengteM = -1.0f;
    };

    // RADAR-WEERGAVE: sinds de Vehicles-and-Trailers-module geeft
    // `Vehicle::GetPlacement()` de ECHTE positie en rotatie in
    // wereldcoordinaten. De radar toont dus een werkelijke peiling, geen
    // gelijkmatig verdeelde hoek meer.
    //
    // Hier stond eerder dat de SDK alleen een afstand gaf en geen richting;
    // dat klopte bij de oudere documentatie, maar niet meer. Wat er wel is:
    //
    //   - Jouw eigen voertuig: Player().GetLocalPlayer() -> GetVehicle().
    //   - Elke andere speler:  speler.GetVehicle() -> GetPlacement().
    //   - Positie is Double3 (wereldmeters), rotatie een Quaternion.
    //
    // De peiling rekenen we uit in het horizontale vlak (X/Z); Y is hoogte
    // en doet voor een radar niet mee. De koers van een voertuig halen we
    // uit de yaw van de quaternion.
    //
    // Belangrijk: zonder voertuig in de wereld (net ingeladen, in een
    // menu) geeft de SDK niets terug. Dan blijft `positieBekend` false en
    // hoort de overlay die speler NIET op de radar te tekenen -- liever
    // een speler tijdelijk missen dan hem op een verzonnen plek zetten.

    class PlayersNearby
    {
    public:
        explicit PlayersNearby( TruckersMP::Session &session );

        // Je eigen TruckersMP-ID. Komt uit de SDK, dus geen verzoek nodig.
        // 0 = niet beschikbaar (bv. nog niet ingelogd).
        std::uint64_t EigenAccountId() const;

        // Thread-safe snapshot voor de overlay, gesorteerd op afstand (dichtstbij eerst).
        std::vector<SpelerRecord> GeefSpelers() const;

        // Haalt voor elke bekende speler de actuele positie/koers op.
        //
        // MOET vanuit een frame-event aangeroepen worden (dus op de
        // game-thread), niet vanuit een eigen thread -- SDK-getters buiten
        // de game-thread geven simpelweg niets terug.
        //
        // Waarom niet in OnUpdate: dat event vuurt op netwerksnelheid en
        // moet volgens de docs een pure datatap blijven, zonder SDK-
        // aanroepen. Posities zijn "state", en state hoor je te pollen --
        // precies wat deze functie doet, één keer per frame.
        void VerversPosities();

    private:
        void VerversRecord( const TruckersMP::Player &speler );

        // Vult peiling, koersverschil en aanhangerstatus in `r`, op basis
        // van het voertuig van deze speler en dat van de lokale speler.
        // Doet niets als een van beide voertuigen niet in de wereld staat.
        void VerversPositie( const TruckersMP::Player &speler, SpelerRecord &r );

        TruckersMP::Session &m_session;
        std::map<std::int32_t, SpelerRecord> m_spelers;
    };
}

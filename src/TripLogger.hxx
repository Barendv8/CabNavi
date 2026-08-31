#pragma once
// TripLogger.hxx
//
// Schrijft elke afgeronde rit als één JSON-regel weg naar trips.jsonl
// (in %APPDATA%/CabNavi/trips.jsonl), en houdt een in-memory
// overzicht + totalen bij zodat de overlay direct statistieken kan tonen
// zonder het hele bestand opnieuw te lezen.
//
// Belangrijk (zie Threading in de SDK-docs): callbacks vanuit de SDK/
// telemetry lopen op de game-thread en mogen geen I/O doen. Daarom heeft
// TripLogger een eigen achtergrond-thread met een simpele queue: events
// worden op de game-thread alleen op de queue gezet, en pas op de
// achtergrond-thread daadwerkelijk weggeschreven.

#include "TripTypes.hxx"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Ritten
{
    struct Totals
    {
        int aantalVrachtRitten = 0;
        int aantalBusRitten = 0;
        double totaalAfstandKm = 0.0;
        std::int64_t totaalInkomen = 0;
        double totaalBrandstofKostenEuro = 0.0;

        // Liters EN kilometers van ritten waar allebei bekend is. Alleen
        // die twee samen geven een eerlijk gemiddelde: een rit zonder
        // brandstofgegevens zou anders het gemiddelde omlaag trekken.
        double gemetenLiters = 0.0;
        double gemetenKm = 0.0;
    };

    class TripLogger
    {
    public:
        TripLogger();
        ~TripLogger();

        // Veilig aan te roepen vanaf de game-thread: zet het record op de
        // schrijf-queue en keert direct terug.
        void RegisterVoltooideRit( Trip trip );

        // Wordt aangeroepen (op de game-thread, dus zelf ook non-blocking
        // houden!) telkens als een rit is afgerond -- gebruikt door Plugin.cxx
        // om DiscordWebhook te triggeren zonder dat de trackers zelf iets
        // van Discord hoeven te weten.
        void ZetVoltooidCallback( std::function<void( const Trip & )> callback ) { m_voltooidCallback = std::move( callback ); }

        // Laadt de bestaande geschiedenis van disk (aangeroepen bij opstarten,
        // niet op de game-thread nodig, mag blokkeren).
        void LaadGeschiedenis();

        // Thread-safe snapshots voor de overlay.
        std::vector<Trip> GeefRecenteRitten( std::size_t maxAantal ) const;
        Totals GeefTotalen() const;

    private:
        void WorkerLoop();
        void SchrijfNaarDisk( const Trip &trip );
        static std::filesystem::path BepaalOpslagPad();

        std::filesystem::path m_bestandsPad;

        mutable std::mutex m_dataMutex;
        std::vector<Trip> m_geschiedenis; // meest recente laatst
        Totals m_totalen;
        std::function<void( const Trip & )> m_voltooidCallback;

        std::mutex m_queueMutex;
        std::condition_variable m_queueCv;
        std::deque<Trip> m_wachtrij;

        std::atomic<bool> m_stoppen{ false };
        std::thread m_worker;
    };
}

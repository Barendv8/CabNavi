#pragma once
// TripLogger.hxx
//
// Writes every completed trip as one JSON line to trips.jsonl
// (in %APPDATA%/CabNavi/trips.jsonl), and keeps an in-memory overview +
// totals so the overlay can show statistics immediately without re-reading
// the whole file.
//
// Important (see Threading in the SDK docs): callbacks from the SDK/
// telemetry run on the game thread and must not do I/O. That is why
// TripLogger has its own background thread with a simple queue: events
// are only put on the queue on the game thread, and actually written on
// the background thread.

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

        // The other cost items. They were missing, so the NET on the
        // statistics tab was only income minus fuel. MEASURED in a real
        // trips.jsonl: 17,000 in fines fell completely out of view on an
        // income of 54,055.
        std::int64_t totaalBoeteKosten = 0;
        std::int64_t totaalTolKosten = 0;
        std::int64_t totaalVeerbootKosten = 0;
        std::int64_t totaalTreinKosten = 0;

        // Cancelled trips do NOT count in the figures above, but are kept
        // separately -- otherwise it looks like they never happened.
        int aantalGeannuleerd = 0;

        // Litres AND kilometres of trips where both are known. Only those
        // two together give an honest average: a trip without fuel data
        // would otherwise pull the average down.
        double gemetenLiters = 0.0;
        double gemetenKm = 0.0;
    };

    class TripLogger
    {
    public:
        TripLogger();
        ~TripLogger();

        // Safe to call from the game thread: puts the record on the write
        // queue and returns immediately.
        void RegisterVoltooideRit( Trip trip );

        // Called (on the game thread, so keep it non-blocking too!) whenever
        // a trip is completed -- used by Plugin.cxx to trigger DiscordWebhook
        // without the trackers having to know anything about Discord.
        void ZetVoltooidCallback( std::function<void( const Trip & )> callback ) { m_voltooidCallback = std::move( callback ); }

        // Loads the existing history from disk (called at startup, not
        // needed on the game thread, may block).
        void LaadGeschiedenis();

        // Thread-safe snapshots for the overlay.
        std::vector<Trip> GeefRecenteRitten( std::size_t maxAantal ) const;
        Totals GeefTotalen() const;

    private:
        void WorkerLoop();
        void SchrijfNaarDisk( const Trip &trip );
        static std::filesystem::path BepaalOpslagPad();

        std::filesystem::path m_bestandsPad;

        // Add one trip to m_totalen. The caller must already hold
        // m_dataMutex.
        void TelMee( const Trip &t );

        mutable std::mutex m_dataMutex;
        std::vector<Trip> m_geschiedenis;  // most recent last
        Totals m_totalen;
        std::function<void( const Trip & )> m_voltooidCallback;

        std::mutex m_queueMutex;
        std::condition_variable m_queueCv;
        std::deque<Trip> m_wachtrij;

        std::atomic<bool> m_stoppen{ false };
        std::thread m_worker;
    };
}

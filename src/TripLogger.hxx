#pragma once

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

        std::int64_t totaalBoeteKosten = 0;
        std::int64_t totaalTolKosten = 0;
        std::int64_t totaalVeerbootKosten = 0;
        std::int64_t totaalTreinKosten = 0;

        int aantalGeannuleerd = 0;

        double geannuleerdKosten = 0.0;

        double gemetenLiters = 0.0;
        double gemetenKm = 0.0;
    };

    class TripLogger
    {
    public:
        TripLogger();
        ~TripLogger();

        void RegisterVoltooideRit( Trip trip );

        void ZetVoltooidCallback( std::function<void( const Trip & )> callback ) { m_voltooidCallback = std::move( callback ); }

        void LaadGeschiedenis();

        std::vector<Trip> GeefRecenteRitten( std::size_t maxAantal ) const;
        Totals GeefTotalen() const;

    private:
        void WorkerLoop();
        void SchrijfNaarDisk( const Trip &trip );
        static std::filesystem::path BepaalOpslagPad();

        std::filesystem::path m_bestandsPad;

        void TelMee( const Trip &t );

        mutable std::mutex m_dataMutex;
        std::vector<Trip> m_geschiedenis;
        Totals m_totalen;
        std::function<void( const Trip & )> m_voltooidCallback;

        std::mutex m_queueMutex;
        std::condition_variable m_queueCv;
        std::deque<Trip> m_wachtrij;

        std::atomic<bool> m_stoppen{ false };
        std::thread m_worker;
    };
}

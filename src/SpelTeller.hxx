#pragma once

namespace Ritten
{
    class SpelTellerVolger
    {
    public:
        void Waarde( const int nieuw )
        {
            if( !m_gezien )
            {
                m_gezien = true;
                m_waarde = nieuw;
                return;
            }
            const int stap = nieuw - m_waarde;
            m_waarde = nieuw;
            if( stap == 0 ) return;
            if( stap < 0 && stap >= -TIK_GRENS )
            {
                m_loopt = true;
                m_stil = 0;
            }
            else if( m_loopt )
            {
                m_stil = 0;
            }
        }

        void KlokStap( const long long stap )
        {
            if( !m_loopt || stap < 1 || stap > TIK_GRENS ) return;
            m_stil += static_cast<int>( stap );
            if( m_stil >= STIL_GRENS ) m_loopt = false;
        }

        bool Loopt() const { return m_loopt; }
        int Minuten() const { return m_waarde; }

    private:
        static constexpr int TIK_GRENS = 5;
        static constexpr int STIL_GRENS = 6;

        bool m_gezien = false;
        bool m_loopt = false;
        int m_waarde = 0;
        int m_stil = 0;
    };
}

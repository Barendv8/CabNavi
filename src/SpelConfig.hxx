#pragma once

#include <map>
#include <string>

namespace Ritten
{
    class SpelConfig
    {
    public:
        void Laad();

        std::string Waarde( const std::string &sleutel ) const;

        bool Aan( const std::string &sleutel, bool standaard = false ) const;

        std::map<std::string, bool> RealistischeInstellingen() const;

        const std::map<std::string, std::string> &Alles() const { return m_waarden; }

    private:
        void LeesBestand( const std::wstring &pad );
        std::map<std::string, std::string> m_waarden;
    };
}

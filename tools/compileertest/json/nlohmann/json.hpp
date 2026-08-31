#pragma once
// STUB-nlohmann/json -- alleen voor compileertests op Linux.
// Genoeg om de aanroepen te typechecken (operator[], value, dump, >>, <<),
// niet om echt JSON te verwerken.
#include <istream>
#include <ostream>
#include <string>
#include <vector>
#include <cstdint>

namespace nlohmann
{
    class json
    {
    public:
        json() = default;
        json( std::initializer_list<json> ) {}
        template <typename T> json( const T & ) {}

        json &operator[]( const char * ) { return *this; }
        json &operator[]( const std::string & ) { return *this; }
        const json &operator[]( const char * ) const { return *this; }
        std::size_t size() const { return 0; }
        json &operator[]( std::size_t ) { return *this; }

        template <typename T> json &operator=( const T & ) { return *this; }

        template <typename T> T value( const char *, const T &standaard ) const { return standaard; }
        std::string value( const char *, const char *standaard ) const { return standaard; }
        int value( const char *, int standaard ) const { return standaard; }
        bool value( const char *, bool standaard ) const { return standaard; }

        std::string dump( int = -1 ) const { return {}; }
        bool contains( const char * ) const { return false; }
        bool is_array() const { return false; }
        bool is_object() const { return false; }
        bool is_boolean() const { return false; }
        bool is_number() const { return false; }
        bool is_string() const { return false; }
        template <typename T> T get() const { return T{}; }
        void push_back( const json & ) {}

        static json array() { return {}; }
        static json array( std::initializer_list<json> ) { return {}; }
        static json parse( const std::string & ) { return {}; }

        // range-for en key/value-iteratie
        struct iterator
        {
            bool operator!=( const iterator & ) const { return false; }
            iterator &operator++() { return *this; }
            const json &operator*() const { static json j; return j; }
            std::string key() const { return {}; }
            const json &value() const { static json j; return j; }
        };
        iterator begin() const { return {}; }
        iterator end() const { return {}; }
    };

    inline std::istream &operator>>( std::istream &in, json & ) { return in; }
    inline std::ostream &operator<<( std::ostream &uit, const json & ) { return uit; }
}

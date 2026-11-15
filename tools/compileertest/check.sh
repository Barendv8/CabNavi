#!/usr/bin/env bash

set -u
HIER="$( cd "$( dirname "$0" )" && pwd )"
WORTEL="$( cd "$HIER/../.." && pwd )"
SDK="${1:-$HIER/sdk}"

INC="-I$HIER/win -I$HIER/imgui -I$HIER/json -I$HIER/scs -I$SDK -I$WORTEL/src"

if [ ! -f "$SDK/TruckersMP/TruckersMP.hxx" ]; then
    echo "Echte TruckersMP-SDK niet gevonden in: $SDK"
    echo "Geef het pad mee, bv: bash $0 ../GameClientSDK/include"
    exit 1
fi

totaal=0
for bestand in "$WORTEL"/src/*.cxx; do
    naam="$( basename "$bestand" )"
    fouten="$( g++ -std=c++17 -fsyntax-only $INC "$bestand" 2>&1 | grep -c 'error:' )"
    totaal=$(( totaal + fouten ))
    if [ "$fouten" -eq 0 ]; then
        echo "  OK    $naam"
    else
        echo "  FOUT  $naam ($fouten)"
        g++ -std=c++17 -fsyntax-only $INC "$bestand" 2>&1 | grep 'error:' | head -5 | sed 's/^/          /'
    fi
done

echo ""
if ! python3 "$HIER/definities.py"; then
    totaal=$(( totaal + 1 ))
fi

if ! python3 "$HIER/tabelmaten.py"; then
    totaal=$(( totaal + 1 ))
fi

echo ""
if [ "$totaal" -eq 0 ]; then
    echo "Alles typecheckt. Bouw daarna op Windows met CMake voor de echte controle."
else
    echo "$totaal fout(en) gevonden."
fi
exit $(( totaal > 0 ))

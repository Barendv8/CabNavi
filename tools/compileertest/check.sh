#!/usr/bin/env bash
# Snelle typecheck van alle bronbestanden op een niet-Windows machine.
#
# Dit is GEEN vervanging voor de echte build op Windows: de headers in
# imgui/, win/, json/ en scs/ zijn STUBS. Ze bevatten alleen de namen en
# ruwe signatures die dit project gebruikt, zodat je typefouten, ontbrekende
# declaraties en verkeerde argumenten vangt zonder Visual Studio te starten.
# Verschillen in defaultparameters of exacte overloads merk je hier NIET.
#
# De echte TruckersMP-headers horen in sdk/ te staan (of pas SDK hieronder aan).
#
# Gebruik:  bash tools/compileertest/check.sh [pad-naar-echte-TruckersMP-SDK]

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
# Ontbrekende definities opsporen. De syntaxcheck hierboven ziet die NIET --
# dat komt er pas uit bij het linken op Windows (LNK2019).
if ! python3 "$HIER/definities.py"; then
    totaal=$(( totaal + 1 ))
fi

# Tabellen die kleiner zijn dan de lus die eroverheen gaat. Leest geheugen
# buiten de tabel; gaf een EXCEPTION_ACCESS_VIOLATION in ucrtbase.
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

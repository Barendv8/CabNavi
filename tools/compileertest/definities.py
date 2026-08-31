#!/usr/bin/env python3
"""Controleert of elke methode die in een header staat, ook echt een
definitie heeft in het bijbehorende .cxx-bestand.

Waarom: de syntaxcheck (-fsyntax-only) merkt NIET dat een functie ontbreekt.
Dat komt er pas uit bij het linken op Windows, met een LNK2019-fout. Deze
controle vangt dat hier al af.
"""
import re, sys, pathlib

wortel = pathlib.Path(__file__).resolve().parents[2] / "src"
fouten = []

for hxx in sorted(wortel.glob("*.hxx")):
    cxx = hxx.with_suffix(".cxx")
    if not cxx.exists():
        continue
    klasse = hxx.stem
    kop = hxx.read_text(encoding="utf-8")
    bron = cxx.read_text(encoding="utf-8")

    for regel in kop.splitlines():
        r = regel.strip()
        # Declaraties eindigen op ");" of op "const;" / "noexcept;" enz.
        # Alleen op ");" filteren miste alle const-methodes -- precies de
        # categorie waar het misging.
        if not r.endswith(";") or "(" not in r:
            continue
        if "{" in r or "}" in r:      # inline gedefinieerd: heeft al een body
            continue
        if "=" in r.split("(")[0]:    # een lidvariabele met initialisatie
            continue
        if r.startswith(("//", "*", "friend", "using", "typedef")):
            continue
        # De naam is die VOOR het eerste haakje. re.search pakte anders een
        # naam uit een parametertype, zoals de "void" in std::function<void(...)>.
        m = re.match(r"[^(]*?(\w+)\s*\(", r)
        if not m:
            continue
        naam = m.group(1)
        if naam in ("if", "for", "while", "switch", "return", "sizeof",
                     "void", "int", "bool", "double", "float", "auto"):
            continue
        # Constructors/destructors en inline-gedefinieerde methodes overslaan
        if naam == klasse or naam.startswith("~"):
            continue
        if f"{klasse}::{naam}" not in bron:
            fouten.append(f"{hxx.name}: {naam}() gedeclareerd maar geen definitie in {cxx.name}")

if fouten:
    print("ONTBREKENDE DEFINITIES (dit wordt een LNK2019 op Windows):")
    for f in fouten:
        print("  " + f)
    sys.exit(1)
print("  OK    elke gedeclareerde methode heeft een definitie")

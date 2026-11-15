#!/usr/bin/env python3
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
        if not r.endswith(";") or "(" not in r:
            continue
        if "{" in r or "}" in r:
            continue
        if "=" in r.split("(")[0]:
            continue
        if r.startswith(("//", "*", "friend", "using", "typedef")):
            continue
        m = re.match(r"[^(]*?(\w+)\s*\(", r)
        if not m:
            continue
        naam = m.group(1)
        if naam in ("if", "for", "while", "switch", "return", "sizeof",
                     "void", "int", "bool", "double", "float", "auto"):
            continue
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

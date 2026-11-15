#!/usr/bin/env python3
import re, sys, pathlib

wortel = pathlib.Path(__file__).resolve().parents[2] / "src"

waarden = {}
for bestand in list(wortel.glob("*.hxx")) + list(wortel.glob("*.cxx")):
    for m in re.finditer(r"\b([A-Z_][A-Z0-9_]{2,})\s*=\s*(\d+)\s*;", bestand.read_text(encoding="utf-8")):
        waarden.setdefault(m.group(1), int(m.group(2)))

fouten = []
for bestand in sorted(list(wortel.glob("*.cxx")) + list(wortel.glob("*.hxx"))):
    regels = bestand.read_text(encoding="utf-8").split("\n")
    tekst = "\n".join(regels)

    tabellen = { m.group(1): int(m.group(2))
                 for m in re.finditer(r"(\w+)\s*\[\s*(\d+)\s*\]\s*=\s*\{", tekst) }
    if not tabellen:
        continue

    for nr, regel in enumerate(regels):
        m = re.search(r"for\s*\(\s*[\w:]+\s+(\w+)\s*=\s*0;\s*\1\s*<\s*([A-Z_][A-Z0-9_]*)", regel)
        if not m:
            continue
        lusvar, grens = m.group(1), m.group(2)
        if grens not in waarden:
            continue

        niveau, lichaam = 0, []
        for r in regels[nr:]:
            lichaam.append(r)
            niveau += r.count("{") - r.count("}")
            if niveau <= 0 and len(lichaam) > 1:
                break

        body = "\n".join(lichaam)
        for naam, maat in tabellen.items():
            if re.search(rf"\b{naam}\s*\[\s*{lusvar}\s*\]", body) and maat != waarden[grens]:
                fouten.append(
                    f"{bestand.name}:{nr+1}  tabel '{naam}[{maat}]' maar de lus gaat tot {grens} (={waarden[grens]})" )

if fouten:
    print("MAATVERSCHIL TUSSEN TABEL EN LUS (leest geheugen buiten de tabel):")
    for f in sorted(set(fouten)):
        print("  " + f)
    sys.exit(1)
print("  OK    tabelmaten komen overeen met de lusgrenzen")

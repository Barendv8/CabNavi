#!/usr/bin/env python3
import json, os, sys
d = sys.argv[1]
c = json.load(open(os.path.join(d, 'usa-cities.json')))
p = json.load(open(os.path.join(d, 'usa-pois.json')))
versie = open(os.path.join(d, 'usa-version.txt')).read().strip()
steden = sorted({(x['token'], x['countryToken'], round(x['x'], 1), round(x['y'], 1)) for x in c})
garages = sorted({(round(x['x'], 1), round(x['y'], 1)) for x in p if x.get('icon') == 'garage_large_ico'})
import math
alle_pompen = [(x['x'], x['y']) for x in p if x.get('icon') == 'gas_ico']
garage_pompen = set()
for gx, gz in garages:
    if not alle_pompen: break
    px, pz = min(alle_pompen, key=lambda q: math.hypot(q[0] - gx, q[1] - gz))
    if math.hypot(px - gx, pz - gz) < 70.0:
        garage_pompen.add((round(px, 1), round(pz, 1)))
pompen = sorted({(round(x, 1), round(z, 1), (round(x, 1), round(z, 1)) in garage_pompen) for x, z in alle_pompen})
def f(v):
    return f'{float(v):.1f}f'
def arr(naam, rows, fmt):
    return f"    static const {naam}[] = {{\n" + "\n".join("        " + fmt(r) + "," for r in rows) + "\n    };\n"
h = f'''#pragma once

namespace Ritten::KaartAts
{{
    constexpr const char *KAART_VERSIE = "{versie}";

{arr("Kaart::Stad STEDEN", steden, lambda r: f'{{ "{r[0]}", "{r[1]}", {f(r[2])}, {f(r[3])} }}')}
{arr("Kaart::Punt GARAGES", garages, lambda r: f'{{ {f(r[0])}, {f(r[1])} }}')}
{arr("Kaart::Pomp POMPEN", pompen, lambda r: f'{{ {f(r[0])}, {f(r[1])}, {"true" if r[2] else "false"} }}')}
    constexpr int AANTAL_STEDEN = {len(steden)};
    constexpr int AANTAL_GARAGES = {len(garages)};
    constexpr int AANTAL_POMPEN = {len(pompen)};
}}
'''
uit = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'KaartdataTabelAts.hxx')
open(uit, 'w', encoding='utf-8').write(h)
jpad = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'data', 'kaartdata_ats.json')
os.makedirs(os.path.dirname(jpad), exist_ok=True)
json.dump({'versie': versie,
           'spel': 'ats',
           'steden': [[t, l, x, y] for t, l, x, y in steden],
           'garages': [[x, y] for x, y in garages],
           'pompen': [[x, y, int(g)] for x, y, g in pompen]},
          open(jpad, 'w', encoding='utf-8'), separators=(',', ':'))
print(f"also wrote {jpad}")
print(f"{len(steden)} cities, {len(garages)} garages, {len(pompen)} fuel stations ({sum(1 for r in pompen if r[2])} at garages), version {versie} -> {uit}")

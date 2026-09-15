# Privacy

CabNavi runs inside your game and keeps everything it produces on your own
PC. This page says exactly what it reads, what it stores, and what leaves
your machine.

## What CabNavi reads

| Source | What | How |
|---|---|---|
| SCS Telemetry SDK | speed, fuel, damage, odometer, job data | official plugin interface |
| TruckersMP Client SDK | nearby players, bus lines, your vehicle position, rendering | official plugin interface |
| Your `game.sii` save file | the dashboard trip counter of your truck (litres and km since your last reset) | **read only**, in memory, matched by odometer |
| The game's data archives (`def.scs`, `base.scs`, `dlc_*.scs`) | fuel price per country, garage fuel discount, country centres | **read only**, once per game version |
| `eurotrucks2.exe` | its version number (file properties) | read only |

CabNavi never writes to your save, to the game files or to the game folder
other than placing its own DLL there at installation. It does not read game
memory, does not hook game code and does not touch anything of other players
beyond what the TruckersMP SDK hands to every plugin.

## What CabNavi stores

Everything lives in `%APPDATA%\CabNavi\`: your trip log, settings, tachograph
state, per-truck counters, cached fuel prices, the map table and `debug.log`.
Nothing is stored anywhere else. Uninstalling keeps this folder; delete it
yourself if you want it gone.

## What leaves your PC

Two switches, both on by default, both under Settings, both remembered:

- **Server data and events** — requests to `api.truckersmp.com` for server
  status, events and VTC data. Slow and cached. When *Look up players* is on
  it also asks the API for the VTC of players near you, by their public
  TruckersMP ID.
- **Update map data via internet** — one download of `data/kaartdata.json`
  from the CabNavi GitHub repository per start. It is a plain download; no
  request carries anything about you.

And one thing that is off until you set it up:

- **Discord webhook** — if you enter a webhook URL, a summary of each finished
  trip is posted to that channel. The URL is stored locally and never logged.

There is no telemetry, no analytics, no account, no identifier sent to the
author. Switch off both network switches and CabNavi contacts nobody.

## debug.log

Meant to be shared when something goes wrong. It contains no Windows user
name (paths are shortened to `%APPDATA%`), no webhook URL, no player names
and no TruckersMP IDs of others. What it does contain: your truck model,
odometer readings, refuelling amounts and the nearest city at the time. The
verbose categories (`fuel`, `flags`, `vtc`, `trace`) only appear when you
tick *Verbose logging*.

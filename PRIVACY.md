# Privacy Policy

_Last updated: 5 September 2026_

> **In short: CabNavi collects nothing.**
>
> There is no server, no account, no database and no analytics. Every file it
> writes sits on your own computer, in a folder you can open and delete. The
> author has no way to see who uses CabNavi, let alone what they drive.
>
> Two features contact the internet — the TruckersMP API and a map-table
> download from this repository — and both can be switched off in Settings.
> A Discord webhook is only used if you fill one in. Details below.

CabNavi is a plugin that runs inside your own copy of Euro Truck Simulator 2.

## What CabNavi reads on your PC

| Source | What | How |
|---|---|---|
| SCS Telemetry SDK | speed, fuel, damage, odometer, job data | official plugin interface |
| TruckersMP Client SDK | nearby players, bus lines, your vehicle position, rendering | official plugin interface |
| Your `game.sii` save file | the dashboard trip counter of your truck (litres and km since your last reset) | **read only**, in memory, matched by odometer |
| The game's data archives (`def.scs`, `base.scs`, `dlc_*.scs`) | fuel price per country, garage fuel discount, country centres | **read only**, once per game version |
| `eurotrucks2.exe` | its version number, as shown under file properties | read only |

CabNavi never writes to your save, to the game files or to the game folder
other than placing its own DLL there at installation. It does not read game
memory, does not hook game code and does not touch anything of other players
beyond what the TruckersMP SDK hands to every plugin.

## What CabNavi stores, and where

Everything CabNavi records is written to `%APPDATA%\CabNavi\` on your own
machine and stays there. Nothing is uploaded.

| File | What it holds |
|---|---|
| `trips.jsonl` | your trip log: distance, income, fuel, damage, delays |
| `voertuigen.txt` | per-truck counters: litres, km, driving-style reference |
| `uiterlijk.json` | colours, opacity, language, the two network switches |
| `instellingen.json` | fuel price settings and your Discord webhook URL, if you set one |
| `tachograaf.json` | tachograph state |
| `brandstofprijzen.json` | fuel prices per country, read from your game files |
| `kaartdata.json` | the map table, if a newer one was downloaded |
| `vtc.json` | VTC settings and the convoys you ticked |
| `spelers_vtc.json` | cached lookups: TruckersMP player ID, VTC number, patron flag |
| `meting.txt` | a measured scale factor used for fuel calculations |
| `imgui.ini` | window position and size |
| `debug.log` | diagnostics, cleared every time the game starts |

To delete everything CabNavi knows about you, delete that folder. The plugin
recreates what it needs on the next start. Uninstalling leaves the folder in
place on purpose.

## When CabNavi contacts the internet

Two switches, both on by default, both under Settings, both remembered.
Switch them off and CabNavi contacts nobody.

**TruckersMP Web API** (`api.truckersmp.com`) — *Fetch server status and
events*. CabNavi requests public server status, public event listings, public
VTC information and, when *Look up players* is on, public profile data for
players near you (by their public TruckersMP ID). These are anonymous GET
requests: no account details, no credentials, and nothing about you is sent.
TruckersMP receives your IP address, as any website does. Requests are
deliberately slow — server status once a minute, events every quarter hour,
player lookups at most a few per second — and cached locally to keep the
load off community infrastructure.

**Map table** (`raw.githubusercontent.com`) — *Update map data via
internet*. One download of `data/kaartdata.json` from this repository per
start, so a new map DLC's cities reach you without a new plugin version. It
is a plain download of a public file; the request carries nothing about you.
GitHub receives your IP address, as any website does.

**Discord webhook** — only if you paste a webhook URL into the settings. When
a trip finishes, CabNavi posts a summary of that trip to the webhook you
configured: route, distance, income, fuel, damage, delay and server name. It
goes to your webhook and nowhere else. Leave the field empty and nothing is
ever sent. The URL is stored in plain text in `instellingen.json` and is
never written to `debug.log`; treat it as a secret, since anyone holding it
can post to your Discord channel.

CabNavi also builds links to `truckersmp.com` and `steamcommunity.com` profile
pages. These open in your browser only when you click them.

## Data about other players

The player radar and list show data the game and the public TruckersMP API
already make available: names, player IDs, VTC membership and staff or patron
status. The incident recorder keeps the last few minutes of this data locally
when you take damage, so you can see who was nearby. None of it is shared, and
it is overwritten as you keep driving.

## debug.log

Meant to be shared when something goes wrong. It contains no Windows user
name (paths are shortened to `%APPDATA%`), no webhook URL, no player names
and no TruckersMP IDs of others. What it does contain: your truck model,
odometer readings, refuelling amounts and the nearest city at the time. The
verbose categories (`fuel`, `flags`, `vtc`, `trace`) only appear when you
tick *Verbose logging*.

## GDPR

The GDPR governs organisations that collect and process other people's
personal data. CabNavi does not do that, and the reason is structural rather
than a promise:

- There is no server to send anything to, and no account to tie anything to.
- Nothing is written outside your own `%APPDATA%\CabNavi\` folder.
- The author receives no data of any kind and cannot receive any, because
  CabNavi never contacts anything the author controls. The map-table download
  is served by GitHub, not by the author, and carries no user data.

So there is no data controller, no processor, no retention schedule and no
breach procedure, because there is no collection to begin with. You are the
only person holding your data, on your own machine.

## Your rights

Since nothing is held about you anywhere, there is nothing to request,
correct, export or erase. If you want your data gone, delete
`%APPDATA%\CabNavi\`. That is the whole of it.

Requests about data held by TruckersMP itself should go to TruckersMP:
<https://truckersmp.com/privacy>

## Changes

Changes to this policy will appear in this file in the repository, with the
date above updated.

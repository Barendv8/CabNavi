# Privacy Policy

_Last updated: 31 August 2026_

> **In short: CabNavi collects nothing.**
>
> There is no server, no account, no database and no analytics. Every file it
> writes sits on your own computer, in a folder you can open and delete. The
> author has no way to see who uses CabNavi, let alone what they drive.
>
> The only time anything leaves your machine is if you switch on the
> TruckersMP API or fill in a Discord webhook yourself. Both are off by
> default. Details below.

CabNavi is a plugin that runs inside your own copy of Euro Truck Simulator 2.

## What CabNavi stores, and where

Everything CabNavi records is written to `%APPDATA%\CabNavi\` on your own
machine and stays there. Nothing is uploaded.

| File | What it holds |
|---|---|
| `trips.jsonl` | your trip log: distance, income, fuel, damage, delays |
| `uiterlijk.json` | colours, opacity, language |
| `instellingen.json` | fuel price and your Discord webhook URL, if you set one |
| `tachograaf.json` | tachograph state |
| `brandstofprijzen.json` | fuel prices per country |
| `vtc.json` | VTC settings and the convoys you ticked |
| `spelers_vtc.json` | cached lookups: TruckersMP player ID, VTC number, patron flag |
| `meting.txt` | a measured scale factor used for fuel calculations |
| `imgui.ini` | window position and size |
| `debug.log` | diagnostics, cleared every time the game starts |

To delete everything CabNavi knows about you, delete that folder. The plugin
recreates what it needs on the next start.

## When CabNavi contacts the internet

By default, never. Every network feature is off until you switch it on
yourself in the settings.

**TruckersMP Web API** (`api.truckersmp.com`) — when enabled, CabNavi requests
public server status, public event listings, public VTC information, and
public profile data for players near you. These are anonymous GET requests:
no account details, no credentials, and nothing about you is sent. TruckersMP
receives your IP address, as any website does. Requests are deliberately
rate-limited to one per second and cached locally to keep the load off
community infrastructure.

**Discord webhook** — only if you paste a webhook URL into the settings. When
a trip finishes, CabNavi posts a summary of that trip to the webhook you
configured: route, distance, income, fuel, damage, delay and server name. It
goes to your webhook and nowhere else. Leave the field empty and nothing is
ever sent. The URL is stored in plain text in `instellingen.json`; treat it as
a secret, since anyone holding it can post to your Discord channel.

CabNavi also builds links to `truckersmp.com` and `steamcommunity.com` profile
pages. These open in your browser only when you click them.

## Data about other players

The player radar and list show data the game and the public TruckersMP API
already make available: names, player IDs, VTC membership and staff or patron
status. The incident recorder keeps the last few minutes of this data locally
when you take damage, so you can see who was nearby. None of it is shared, and
it is overwritten as you keep driving.

## GDPR

The GDPR governs organisations that collect and process other people's
personal data. CabNavi does not do that, and the reason is structural rather
than a promise:

- There is no server to send anything to, and no account to tie anything to.
- Nothing is written outside your own `%APPDATA%\CabNavi\` folder.
- The author receives no data of any kind and cannot receive any, because
  CabNavi never contacts anything the author controls.

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

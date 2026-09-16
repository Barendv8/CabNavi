# The CabNavi trip format

**`cabnavi-trip`, version 1** — an open JSON format for one finished (or
cancelled) trip in Euro Truck Simulator 2 or American Truck Simulator.

This format is deliberately not tied to CabNavi. Anyone may implement it,
read it, or emit it — including commercial platforms — without asking. If
your tracker speaks it too, drivers can switch tools without their company
having to rebuild anything.

## How it is delivered

When the driver has switched the VTC webhook on and filled in your address,
CabNavi sends one HTTP request per finished trip:

```
POST <your https address>
Content-Type: application/json
Authorization: Bearer <key the driver pasted in>
User-Agent: CabNavi/<version>
```

The body is the JSON document below. A `2xx` response means delivered; on
failure CabNavi retries a few times with delays, then drops the message —
it never blocks the game and never queues to disk.

The driver's key travels as a standard `Bearer` token. Hand every driver
their own key, and you know who is posting; revoke a key, and that driver
is out. CabNavi attaches it verbatim and stores it only on the driver's own
machine.

There is also a hand-fired test message (a button in CabNavi's settings):

```json
{ "format": "cabnavi-trip", "format_version": 1, "test": true }
```

Answer it with a `2xx` so the driver sees the connection work.

## The document

Field order is not guaranteed. Optional fields are simply absent when
unknown or zero — never `null`, never empty strings. Numbers are plain JSON
numbers. All amounts are in the **game's own currency** (`currency` says
which one), all quantities in **game units** (kilometres, litres): the
receiver gets facts, display taste stays on the driver's side.

```json
{
  "format": "cabnavi-trip",
  "format_version": 1,
  "game": "ets2",
  "currency": "EUR",

  "trip_id": "…",
  "type": "freight",
  "status": "completed",
  "started_at": "2026-09-14T19:02:11Z",
  "ended_at": "2026-09-14T20:40:03Z",
  "server": "Simulation 1",

  "vehicle": { "brand": "…", "model": "…" },

  "route": {
    "from_city": "…", "from_company": "…",
    "to_city": "…",   "to_company": "…"
  },

  "cargo": { "name": "…", "weight_kg": 19500 },

  "distance": { "driven_km": 612.4, "planned_km": 598.0 },
  "duration_game_minutes": 512,

  "fuel": { "used_litres": 214.7, "estimated_cost": 354.26,
            "price_per_litre": 1.649, "price_source": "last_refuel" },

  "empty_run": { "km": 38.2, "fuel_litres": 14.1, "estimated_cost": 23.25 },

  "money": {
    "income": 91260,
    "toll": 210, "ferry": 0, "train": 0, "fines": 150
  },

  "damage": { "chassis_pct": 4.0, "cargo_pct": 0.0, "trailer_pct": 2.5 },

  "on_time": true,

  "tachograph": {
    "driving_minutes_since_rest": 187.5,
    "resting": false
  },

  "top_speed_kmh": 91.4,
  "speeding": [ { "max_kmh": 97.0, "limit_kmh": 80, "seconds": 41 } ],
  "teleports": 0,
  "route": [ { "t": 1789340531, "x": -31200.5, "z": 12044.1 } ],
  "license_plate": { "plate": "PP20013", "country": "california" },
  "game_settings": { "fuel_simulation": true, "fatigue": false, "police": false }
}
```

Field notes, only where a value needs a definition:

- **`format` / `format_version`** — always present. Check both; ignore
  documents you do not recognise. Version 1 will only ever *gain* optional
  fields; anything breaking becomes version 2.
- **`game`** — `"ets2"` or `"ats"`. **`currency`** — `"EUR"` or `"USD"`.
- **`type`** — `"freight"` or `"bus"`. **`status`** — `"completed"` or
  `"cancelled"`; a cancelled trip may carry `cancel_reason` (free text).
- **`started_at` / `ended_at`** — real-world UTC, ISO 8601.
- **`distance.driven_km`** — measured on the truck's odometer, not derived
  from positions. `planned_km` is the game's route estimate at job start.
- **`duration_game_minutes`** — in-game economy time, not real time.
- **`fuel.estimated_cost`** — measured litres priced per litre burnt.
  `price_per_litre` says at what price, `price_source` how trustworthy:
  `last_refuel` (what this truck actually paid at the pump), `location`
  (the price where the driver is, no refuel known yet) or `manual`.
- **`empty_run`** — the run without cargo between the previous job's end and
  this job's start: km, litres, cost. Not part of this job's cost; it is how
  a planning reports a job. Absent when there was none.
- **`money.income`** — what the game actually paid; for cancelled or
  unpaid-yet trips `income_estimated` may appear instead.
- **`bus`** — only for `type: "bus"`: `stops` (array of `{ name, boarded,
  alighted }`) and `passengers_total`.
- **`tachograph`** — driving minutes on CabNavi's own tachograph at the
  moment the trip closed, and whether the driver was resting. Present only
  when the tachograph was active. This is the block a company that wants
  driving-time realism cannot get from any other tracker.
- **`top_speed_kmh`** — highest speed of the trip. **`speeding`** — every
  stretch more than 5 km/h over the posted limit for at least 3 s, with the
  peak, the limit and its duration. **`teleports`** — position jumps no truck
  could drive (F7 to service, a save reload); absent when zero.
- **`route`** — one point every 5 seconds: unix time and the game's own x/z
  coordinates. Around 1,400 points on a two-hour trip; absent when empty.
- **`license_plate`** — plate text and the game's country token.
- **`game_settings`** — the gameplay options as the game had them at trip
  start (fuel simulation, fatigue, hardcore simulation, speed limiter, …),
  read from the game's own config files. `police` is always false on
  TruckersMP.

## Live position (optional, off by default)

When the driver switches *Share my live position* on, CabNavi also POSTs
every 10 seconds to `<address>/live` (the trip address with `/trip` swapped
for `/live`), same key:

```json
{ "format": "cabnavi-live", "format_version": 1, "game": "ets2",
  "x": -31200.5, "z": 12044.1, "speed_kmh": 84, "on_job": true,
  "from_city": "Rotterdam", "to_city": "Berlin", "server": "Simulation 1" }
```

Coordinates are the game's, not GPS. Nothing is sent while the position is
unknown or the switch is off.

## Receiving it: a complete example

Twenty lines of PHP, no framework — enough for a VTC site to start logging
today. Save as `cabnavi.php`, point drivers at its URL, give each a key.

```php
<?php
$keys = [ "geheim-van-jojo" => "Jojo", "geheim-van-piet" => "Piet" ];

$auth = $_SERVER["HTTP_AUTHORIZATION"] ?? "";
$key  = str_starts_with( $auth, "Bearer " ) ? substr( $auth, 7 ) : "";
if( !isset( $keys[ $key ] ) ) { http_response_code( 401 ); exit; }

$trip = json_decode( file_get_contents( "php://input" ), true );
if( ( $trip[ "format" ] ?? "" ) !== "cabnavi-trip" ) { http_response_code( 400 ); exit; }
if( $trip[ "test" ] ?? false ) { http_response_code( 200 ); exit; }

$regel = json_encode( [ "driver" => $keys[ $key ], "trip" => $trip ] );
file_put_contents( __DIR__ . "/trips.jsonl", $regel . "\n", FILE_APPEND | LOCK_EX );
http_response_code( 200 );
```

From `trips.jsonl` on, it is your data on your server: leaderboards,
statistics, a Discord bot — whatever your company builds.

Also in the repository, under `receivers/`: the same receiver in **Node.js**
(no dependencies, with `/live`), a **Google Sheets** Apps Script (no server
at all — every trip a row), and the **Discord bot** with dispatch.

## A second shape: the signed envelope

Some company hubs already receive trips as an event envelope — `{ "object":
"event", "type": "job.delivered" | "job.cancelled", "data": { "object": {
… } } }` — with an HMAC-SHA256 hex signature of the body in the `X-CabNavi-Signature`
header, computed with the hub's own webhook secret. CabNavi can send that shape
instead: in the VTC webhook settings choose *Envelope with signature* and
paste the hub's secret. The body is written exactly as Python's
`json.dumps()` would print it, so a hub that parses and re-serialises
before verifying gets the same bytes CabNavi signed. Fields CabNavi does not
have (license plate, top speed, event positions) are sent empty or as the
nearest known value; speeds are in metres per second and `fuel_used` is
negative in that shape. The signature travels twice: as `X-CabNavi-Signature`
and under the generic name `Signature`.

**Open-source Drivers Hubs** that accept a "custom" tracker in this shape
work out of the box -- the Drivers Hub Project (CHub,
<https://github.com/CharlesWithC/HubBackend>) is one, and it hosts many
companies' hubs. In the hub's config add a tracker of type `custom`
with a `webhook_secret`, let the driver choose that tracker in their hub
profile, and in CabNavi set the address to the hub's `/custom/update`
endpoint with that secret. The hub verifies the `Signature` header exactly
the way CabNavi computes it.

## Ground rules for implementers

1. **Unknown fields must be ignored, never rejected.** That is how version
   1 stays compatible while it grows.
2. **Absent means unknown.** Do not read meaning into a missing field
   beyond "CabNavi did not have this".
3. **Trust nothing blindly.** The document is produced on the driver's own
   computer. Treat it like any user input: validate, and run your own
   plausibility checks (does `driven_km` fit the duration?) before feeding
   rankings.

Questions, corrections, or a tracker of your own that speaks this format:
open an issue at <https://github.com/Barendv8/CabNavi/issues>.

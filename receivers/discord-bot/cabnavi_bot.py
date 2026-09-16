#!/usr/bin/env python3
"""
CabNavi Discord bot -- the receiving end of the CabNavi trip webhook,
plus dispatch: hand out jobs from Discord, see them accepted and delivered.

One file, one SQLite database, no server of anyone else's. Runs on the
VTC owner's own machine or any small host.

    pip install discord.py aiohttp
    python cabnavi_bot.py

Configuration is read from config.json next to this file (see README.md).

What it does
  * Listens on http://0.0.0.0:<port>/trip for CabNavi's trip webhook.
    The driver's Bearer key says who it is; the trip is stored and posted
    to the trips channel as an embed.
  * /koppel   -- a driver links their Discord account and gets a key to
                paste into CabNavi (Settings -> VTC webhook).
  * /sleutel  -- show your key again.
  * /stats    -- your totals, or someone else's.
  * /top      -- leaderboard by distance, trips or income.
  * /dispatch -- give a driver a job. It appears in their CabNavi within a
                minute; they accept it in the cab; you see it here.
  * /opdrachten -- open jobs.
  * GET  /dispatch          -- CabNavi asks: anything for me?
  * POST /dispatch/accept   -- CabNavi says: taken.

Trip format: docs/TRIP_FORMAT.md in the CabNavi repository.
"""

import asyncio
import collections
import hmac
import json
import secrets
import sqlite3
import time
from pathlib import Path

import discord
from discord import app_commands
from aiohttp import web

HIER = Path(__file__).resolve().parent
CONFIG = json.loads((HIER / "config.json").read_text(encoding="utf-8"))

TOKEN        = CONFIG["discord_token"]
GUILD_ID     = int(CONFIG["guild_id"])
TRIPS_KANAAL = int(CONFIG["trips_channel_id"])
DISPATCH_ROL = CONFIG.get("dispatcher_role", "Dispatcher")
POORT        = int(CONFIG.get("port", 8787))
DB_PAD       = HIER / CONFIG.get("database", "cabnavi_bot.sqlite")

# ---------------------------------------------------------------------------
# Database
# ---------------------------------------------------------------------------
db = sqlite3.connect(DB_PAD)
db.row_factory = sqlite3.Row
db.executescript("""
CREATE TABLE IF NOT EXISTS drivers (
    discord_id INTEGER PRIMARY KEY,
    name       TEXT NOT NULL,
    key        TEXT NOT NULL UNIQUE,
    created    INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS trips (
    trip_id    TEXT PRIMARY KEY,
    discord_id INTEGER NOT NULL,
    game       TEXT,
    type       TEXT,
    status     TEXT,
    ended_at   TEXT,
    from_city  TEXT,
    to_city    TEXT,
    cargo      TEXT,
    km         REAL,
    income     REAL,
    fuel_l     REAL,
    damage_pct REAL,
    on_time    INTEGER,
    tacho_min  REAL,
    empty_km   REAL,
    received   INTEGER NOT NULL,
    raw        TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS dispatch (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    discord_id INTEGER NOT NULL,
    by_id      INTEGER NOT NULL,
    from_city  TEXT NOT NULL,
    to_city    TEXT NOT NULL,
    cargo      TEXT,
    note       TEXT,
    deadline   TEXT,
    status     TEXT NOT NULL DEFAULT 'open',   -- open / accepted / delivered / cancelled
    created    INTEGER NOT NULL,
    accepted   INTEGER,
    delivered  INTEGER,
    trip_id    TEXT
);
""")
db.commit()


def driver_by_key(key: str):
    if not key or len(key) > 128:
        return None
    d = db.execute("SELECT * FROM drivers WHERE key = ?", (key,)).fetchone()
    # The lookup already decides; the constant-time compare is belt and braces
    # so a near-miss key never leaks timing.
    if d is None or not hmac.compare_digest(d["key"], key):
        return None
    return d


def knip(v, n=100):
    """Cap any string that ends up in Discord or the database."""
    if v is None:
        return None
    s = str(v)
    return s if len(s) <= n else s[:n - 1] + "\u2026"


def driver_by_id(discord_id: int):
    return db.execute("SELECT * FROM drivers WHERE discord_id = ?", (discord_id,)).fetchone()


# ---------------------------------------------------------------------------
# Discord
# ---------------------------------------------------------------------------
intents = discord.Intents.default()
bot = discord.Client(intents=intents)
tree = app_commands.CommandTree(bot)
GUILD = discord.Object(id=GUILD_ID)


def is_dispatcher(member: discord.Member) -> bool:
    if member.guild_permissions.administrator:
        return True
    return any(r.name == DISPATCH_ROL for r in member.roles)


def fmt_money(v, currency):
    return f"{currency} {v:,.0f}".replace(",", ".")


def trip_embed(trip: dict, driver_name: str) -> discord.Embed:
    cancelled = trip.get("status") == "cancelled"
    is_bus = trip.get("type") == "bus"
    game = (trip.get("game") or "").upper()
    cur = trip.get("currency", "")
    route = trip.get("route", {})
    dist = trip.get("distance", {})
    money = trip.get("money", {})
    fuel = trip.get("fuel", {})
    dmg = trip.get("damage", {})
    tacho = trip.get("tachograph")

    titel = ("Bus line " if is_bus else "Freight job ") + ("cancelled" if cancelled else "completed")
    kleur = discord.Color.red() if cancelled else (discord.Color.blue() if is_bus else discord.Color.green())
    e = discord.Embed(title=f"{titel}  [{game}]", color=kleur)
    e.set_author(name=driver_name)
    e.add_field(name="From", value=route.get("from_city", "-"), inline=True)
    e.add_field(name="To", value=route.get("to_city", "-"), inline=True)
    if is_bus:
        b = trip.get("bus", {})
        e.add_field(name="Passengers", value=str(b.get("passengers_total", 0)), inline=True)
    else:
        c = trip.get("cargo", {})
        e.add_field(name="Cargo", value=c.get("name", "-"), inline=True)
    km = dist.get("driven_km")
    if km is not None:
        e.add_field(name="Distance", value=f"{km:,.0f} km".replace(",", "."), inline=True)
    inc = money.get("income", money.get("income_estimated"))
    if inc is not None:
        e.add_field(name="Income", value=fmt_money(inc, cur), inline=True)
    if fuel.get("used_litres") is not None:
        e.add_field(name="Fuel", value=f"{fuel['used_litres']:.0f} L", inline=True)
    if dmg:
        e.add_field(name="Damage", value=f"{dmg.get('chassis_pct', 0):.0f}% / cargo {dmg.get('cargo_pct', 0):.0f}%", inline=True)
    if "on_time" in trip and not cancelled:
        e.add_field(name="On time", value="yes" if trip["on_time"] else "no", inline=True)
    if tacho:
        e.add_field(name="Tachograph", value=f"{tacho.get('driving_minutes_since_rest', 0):.0f} min since rest", inline=True)
    leeg = trip.get("empty_run")
    if leeg and leeg.get("km", 0) > 0.5:
        e.add_field(name="Empty run", value=f"{leeg['km']:.0f} km \u00b7 {cur} {leeg.get('estimated_cost', 0):.0f}", inline=True)
    if cancelled and trip.get("cancel_reason"):
        e.add_field(name="Reason", value=trip["cancel_reason"], inline=False)
    return e


@bot.event
async def on_ready():
    await tree.sync(guild=GUILD)
    print(f"logged in as {bot.user}; slash commands synced")


@tree.command(name="koppel", description="Link your Discord account and get your CabNavi key", guild=GUILD)
async def koppel(inter: discord.Interaction):
    d = driver_by_id(inter.user.id)
    if d is None:
        key = "cn_" + secrets.token_urlsafe(24)
        db.execute("INSERT INTO drivers (discord_id, name, key, created) VALUES (?,?,?,?)",
                   (inter.user.id, inter.user.display_name, key, int(time.time())))
        db.commit()
    else:
        key = d["key"]
        # Refresh the display name: nicknames change, keys do not.
        db.execute("UPDATE drivers SET name = ? WHERE discord_id = ?", (inter.user.display_name, inter.user.id))
        db.commit()
    tekst = (f"Your CabNavi key (keep it to yourself):\n`{key}`\n\n"
             f"In CabNavi: Settings -> VTC webhook -> address `{CONFIG['public_url'].rstrip('/')}/trip`, key as above, switch on.")
    try:
        await inter.user.send(tekst)                       # DM: never in a channel, not even ephemeral
        await inter.response.send_message("Sent you a DM with your key.", ephemeral=True)
    except discord.Forbidden:                              # DMs closed: ephemeral is the fallback
        await inter.response.send_message(tekst, ephemeral=True)


@tree.command(name="sleutel", description="Show your CabNavi key again", guild=GUILD)
async def sleutel(inter: discord.Interaction):
    d = driver_by_id(inter.user.id)
    if d is None:
        await inter.response.send_message("You are not linked yet. Use /koppel first.", ephemeral=True)
        return
    try:
        await inter.user.send(f"`{d['key']}`")
        await inter.response.send_message("Sent you a DM with your key.", ephemeral=True)
    except discord.Forbidden:
        await inter.response.send_message(f"`{d['key']}`", ephemeral=True)


@tree.command(name="stats", description="Trip totals for you or another driver", guild=GUILD)
@app_commands.describe(driver="Leave empty for yourself")
async def stats(inter: discord.Interaction, driver: discord.Member | None = None):
    wie = driver or inter.user
    r = db.execute("""SELECT COUNT(*) n, COALESCE(SUM(km),0) km, COALESCE(SUM(income),0) inc,
                             COALESCE(SUM(fuel_l),0) fuel, SUM(CASE WHEN on_time=1 THEN 1 ELSE 0 END) ontime,
                             COALESCE(SUM(empty_km),0) leeg
                      FROM trips WHERE discord_id = ? AND status = 'completed'""", (wie.id,)).fetchone()
    if r["n"] == 0:
        await inter.response.send_message(f"No completed trips for {wie.display_name} yet.", ephemeral=True)
        return
    e = discord.Embed(title=f"Stats -- {wie.display_name}", color=discord.Color.blurple())
    e.add_field(name="Trips", value=str(r["n"]))
    e.add_field(name="Distance", value=f"{r['km']:,.0f} km".replace(",", "."))
    e.add_field(name="Income", value=f"{r['inc']:,.0f}".replace(",", "."))
    e.add_field(name="Fuel", value=f"{r['fuel']:,.0f} L".replace(",", "."))
    e.add_field(name="On time", value=f"{r['ontime']}/{r['n']}")
    if r["leeg"] > 0.5:
        e.add_field(name="Empty km", value=f"{r['leeg']:,.0f} km".replace(",", "."))
    await inter.response.send_message(embed=e)


@tree.command(name="top", description="Leaderboard", guild=GUILD)
@app_commands.describe(by="distance, trips or income")
@app_commands.choices(by=[app_commands.Choice(name="distance", value="km"),
                          app_commands.Choice(name="trips", value="n"),
                          app_commands.Choice(name="income", value="inc")])
async def top(inter: discord.Interaction, by: app_commands.Choice[str] | None = None):
    col = by.value if by else "km"
    sortering = {"km": "km DESC", "n": "n DESC", "inc": "inc DESC"}[col]   # fixed map, never user text
    rows = db.execute(f"""SELECT d.name, COUNT(*) n, COALESCE(SUM(t.km),0) km, COALESCE(SUM(t.income),0) inc
                          FROM trips t JOIN drivers d ON d.discord_id = t.discord_id
                          WHERE t.status = 'completed'
                          GROUP BY t.discord_id ORDER BY {sortering} LIMIT 10""").fetchall()
    if not rows:
        await inter.response.send_message("No trips yet.", ephemeral=True)
        return
    regels = []
    for i, r in enumerate(rows, 1):
        waarde = {"km": f"{r['km']:,.0f} km", "n": f"{r['n']} trips", "inc": f"{r['inc']:,.0f}"}[col].replace(",", ".")
        regels.append(f"**{i}.** {r['name']} -- {waarde}")
    e = discord.Embed(title="Leaderboard", description="\n".join(regels), color=discord.Color.gold())
    await inter.response.send_message(embed=e)


@tree.command(name="dispatch", description="Give a driver a job (dispatcher only)", guild=GUILD)
@app_commands.describe(driver="Who drives it", from_city="Pickup", to_city="Delivery",
                       cargo="Cargo (optional)", deadline="e.g. 21:00 (optional)", note="Anything else (optional)")
async def dispatch(inter: discord.Interaction, driver: discord.Member, from_city: str, to_city: str,
                   cargo: str | None = None, deadline: str | None = None, note: str | None = None):
    if not is_dispatcher(inter.user):
        await inter.response.send_message("Dispatchers only.", ephemeral=True)
        return
    if driver_by_id(driver.id) is None:
        await inter.response.send_message(f"{driver.display_name} has not linked CabNavi yet (/koppel).", ephemeral=True)
        return
    from_city, to_city = knip(from_city, 60), knip(to_city, 60)
    cargo, deadline, note = knip(cargo, 60), knip(deadline, 30), knip(note, 200)
    cur = db.execute("""INSERT INTO dispatch (discord_id, by_id, from_city, to_city, cargo, note, deadline, created)
                        VALUES (?,?,?,?,?,?,?,?)""",
                     (driver.id, inter.user.id, from_city, to_city, cargo, note, deadline, int(time.time())))
    db.commit()
    e = discord.Embed(title=f"Job #{cur.lastrowid} -> {driver.display_name}", color=discord.Color.orange())
    e.add_field(name="Route", value=f"{from_city} -> {to_city}", inline=False)
    if cargo:    e.add_field(name="Cargo", value=cargo)
    if deadline: e.add_field(name="Deadline", value=deadline)
    if note:     e.add_field(name="Note", value=note, inline=False)
    e.set_footer(text="Shows up in CabNavi within a minute. Waiting for the driver to accept.")
    await inter.response.send_message(embed=e)


@tree.command(name="opdrachten", description="Open and accepted jobs", guild=GUILD)
async def opdrachten(inter: discord.Interaction):
    rows = db.execute("""SELECT j.*, d.name FROM dispatch j JOIN drivers d ON d.discord_id = j.discord_id
                         WHERE j.status IN ('open','accepted') ORDER BY j.created""").fetchall()
    if not rows:
        await inter.response.send_message("No open jobs.", ephemeral=True)
        return
    regels = [f"**#{r['id']}** {r['name']}: {r['from_city']} -> {r['to_city']}"
              + (f" ({r['cargo']})" if r['cargo'] else "") + f" -- *{r['status']}*" for r in rows]
    await inter.response.send_message("\n".join(regels))


# ---------------------------------------------------------------------------
# HTTP: the CabNavi side
# ---------------------------------------------------------------------------
# Per-IP request budget: 30 requests per 10 seconds is far more than one
# cab ever sends; anything above is noise and gets a 429 without touching
# the database.
_venster = collections.defaultdict(collections.deque)

def te_snel(request: web.Request) -> bool:
    ip = request.headers.get("X-Forwarded-For", request.remote or "?").split(",")[0].strip()
    nu = time.monotonic()
    q = _venster[ip]
    while q and nu - q[0] > 10.0:
        q.popleft()
    if len(q) >= 30:
        return True
    q.append(nu)
    return False


def uit_browser(request: web.Request) -> bool:
    """A request from a web page carries Origin (or Sec-Fetch-Site); CabNavi
    never does. Refusing those closes the DNS-rebinding / CSRF door to a bot
    that listens on localhost, whatever key the page might guess."""
    return "Origin" in request.headers or "Sec-Fetch-Site" in request.headers


def bearer(request: web.Request):
    if uit_browser(request):
        return None
    auth = request.headers.get("Authorization", "")
    if not auth.startswith("Bearer "):
        return None
    return driver_by_key(auth[7:].strip())


async def http_trip(request: web.Request):
    if uit_browser(request):
        return web.json_response({"error": "browsers are not allowed here"}, status=403)
    if te_snel(request):
        return web.json_response({"error": "slow down"}, status=429, headers={"Retry-After": "10"})
    d = bearer(request)
    if d is None:
        return web.json_response({"error": "unknown key"}, status=401)
    try:
        trip = await request.json()
    except Exception:
        return web.json_response({"error": "not json"}, status=400)
    if trip.get("format") != "cabnavi-trip":
        return web.json_response({"error": "unknown format"}, status=400)
    if trip.get("test"):
        return web.json_response({"ok": True, "hello": d["name"]})

    route = trip.get("route", {}); dist = trip.get("distance", {}); money = trip.get("money", {})
    fuel = trip.get("fuel", {}); dmg = trip.get("damage", {}); tacho = trip.get("tachograph") or {}
    cargo = (trip.get("cargo") or {}).get("name") if trip.get("type") != "bus" else "passengers"
    # Everything that came over the wire and ends up in an embed or a row is capped.
    for k in ("from_city", "to_city", "from_company", "to_company"):
        if k in route: route[k] = knip(route[k], 60)
    cargo = knip(cargo, 60)
    if "cancel_reason" in trip: trip["cancel_reason"] = knip(trip["cancel_reason"], 200)
    if "server" in trip: trip["server"] = knip(trip["server"], 40)
    if "trip_id" in trip: trip["trip_id"] = knip(trip["trip_id"], 64)
    db.execute("""INSERT OR REPLACE INTO trips
                  (trip_id, discord_id, game, type, status, ended_at, from_city, to_city, cargo, km, income,
                   fuel_l, damage_pct, on_time, tacho_min, empty_km, received, raw)
                  VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)""",
               (trip.get("trip_id"), d["discord_id"], trip.get("game"), trip.get("type"), trip.get("status"),
                trip.get("ended_at"), route.get("from_city"), route.get("to_city"), cargo,
                dist.get("driven_km"), money.get("income", money.get("income_estimated")),
                fuel.get("used_litres"), dmg.get("chassis_pct"),
                1 if trip.get("on_time") else 0, tacho.get("driving_minutes_since_rest"),
                (trip.get("empty_run") or {}).get("km"),
                int(time.time()), json.dumps(trip)))

    # Tie the trip to the driver's accepted job, if the route matches.
    job = db.execute("""SELECT id FROM dispatch WHERE discord_id = ? AND status = 'accepted'
                        AND lower(to_city) = lower(?) ORDER BY created LIMIT 1""",
                     (d["discord_id"], route.get("to_city", ""))).fetchone()
    if job and trip.get("status") == "completed":
        db.execute("UPDATE dispatch SET status='delivered', delivered=?, trip_id=? WHERE id=?",
                   (int(time.time()), trip.get("trip_id"), job["id"]))
    db.commit()

    kanaal = bot.get_channel(TRIPS_KANAAL)
    if kanaal:
        e = trip_embed(trip, d["name"])
        if job and trip.get("status") == "completed":
            e.set_footer(text=f"Job #{job['id']} delivered")
        asyncio.create_task(kanaal.send(embed=e))
    return web.json_response({"ok": True})


async def http_dispatch_get(request: web.Request):
    if uit_browser(request):
        return web.json_response({"error": "browsers are not allowed here"}, status=403)
    if te_snel(request):
        return web.json_response({"error": "slow down"}, status=429, headers={"Retry-After": "10"})
    d = bearer(request)
    if d is None:
        return web.json_response({"error": "unknown key"}, status=401)
    rows = db.execute("""SELECT * FROM dispatch WHERE discord_id = ? AND status IN ('open','accepted')
                         ORDER BY created""", (d["discord_id"],)).fetchall()
    jobs = [{"id": r["id"], "from_city": r["from_city"], "to_city": r["to_city"], "cargo": r["cargo"],
             "deadline": r["deadline"], "note": r["note"], "status": r["status"]} for r in rows]
    return web.json_response({"format": "cabnavi-dispatch", "format_version": 1, "jobs": jobs})


async def http_dispatch_accept(request: web.Request):
    if uit_browser(request):
        return web.json_response({"error": "browsers are not allowed here"}, status=403)
    if te_snel(request):
        return web.json_response({"error": "slow down"}, status=429, headers={"Retry-After": "10"})
    d = bearer(request)
    if d is None:
        return web.json_response({"error": "unknown key"}, status=401)
    body = await request.json()
    job = db.execute("SELECT * FROM dispatch WHERE id = ? AND discord_id = ? AND status = 'open'",
                     (body.get("id"), d["discord_id"])).fetchone()
    if job is None:
        return web.json_response({"error": "no such open job"}, status=404)
    db.execute("UPDATE dispatch SET status='accepted', accepted=? WHERE id=?", (int(time.time()), job["id"]))
    db.commit()
    kanaal = bot.get_channel(TRIPS_KANAAL)
    if kanaal:
        asyncio.create_task(kanaal.send(f"**{d['name']}** accepted job #{job['id']}: {job['from_city']} -> {job['to_city']}"))
    return web.json_response({"ok": True})


async def http_health(request: web.Request):
    return web.json_response({"ok": True, "bot": str(bot.user) if bot.user else None})


async def main():
    app = web.Application(client_max_size=256 * 1024)
    async def geen_browser(request: web.Request):
        return web.Response(status=403)
    app.add_routes([web.options("/{tail:.*}", geen_browser),
                    web.post("/trip", http_trip),
                    web.get("/dispatch", http_dispatch_get),
                    web.post("/dispatch/accept", http_dispatch_accept),
                    web.get("/health", http_health)])
    runner = web.AppRunner(app)
    await runner.setup()
    await web.TCPSite(runner, "0.0.0.0", POORT).start()
    print(f"webhook listening on port {POORT}")
    await bot.start(TOKEN)


if __name__ == "__main__":
    asyncio.run(main())

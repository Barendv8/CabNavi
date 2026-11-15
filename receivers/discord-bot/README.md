# CabNavi Discord bot

The receiving end of the CabNavi trip webhook, with dispatch. One Python
file, one SQLite database, runs on your own machine or any small host.
Nothing goes through anyone else's server.

**What you get**

- Every finished trip posted in a channel, as an embed
- `/koppel` — a driver links their Discord account and gets their CabNavi key
- `/stats`, `/top` — personal totals and a leaderboard
- `/dispatch` — give a driver a job; it shows up in their CabNavi within a
  minute, they accept it in the cab, you see it here, and the delivery is
  tied to the job automatically

## Setup, five steps

1. **Create a bot** at https://discord.com/developers/applications → New
   Application → Bot → Reset Token → copy it. Under OAuth2 → URL Generator
   tick `bot` and `applications.commands`, open the generated link, invite
   the bot to your server.

2. **Install**
   ```
   pip install discord.py aiohttp
   ```

3. **Configure.** Copy `config.example.json` to `config.json` and fill in:
   - `discord_token` — from step 1
   - `guild_id` — right-click your server → Copy Server ID (enable Developer
     Mode under Settings → Advanced if you don't see it)
   - `trips_channel_id` — right-click the channel where trips should land
   - `public_url` — where CabNavi can reach this machine, e.g.
     `https://vtc.example.com:8787`. On a home PC, forward the port on your
     router or use a tunnel such as Cloudflare Tunnel or ngrok.

4. **Run**
   ```
   python cabnavi_bot.py
   ```
   It prints `logged in as …` and `webhook listening on port 8787`.

5. **Drivers:** type `/koppel` in Discord; the bot sends the key by DM
   (open your DMs for the server). Paste the key and address into CabNavi
   (Settings → VTC webhook), switch it on, press *Send test message*. Done.

## Safety notes

- Keys are only ever sent by DM, never in a channel.
- Requests that come from a web page (they carry an `Origin` header) are
  refused outright, so a site you visit cannot talk to a bot on localhost.
- Requests are limited to 30 per 10 seconds per IP; more gets a 429.
- Request bodies are capped at 256 kB, all text fields are cut to a sane
  length before they reach Discord or the database.
- CabNavi only talks to `https://` addresses -- with one exception: plain
  `http://localhost:<port>` when the bot runs on the same PC as the game
  (handy for testing). For drivers on other machines put TLS in front
  (Cloudflare Tunnel, ngrok, a reverse proxy).
- `config.json` holds your bot token: keep the file readable by you only.

## Roles

Anyone with the `Dispatcher` role (or admin) can use `/dispatch`. Change
the role name in `config.json`.

## The HTTP side (for people building their own)

| Method | Path               | Who      | What                                   |
|--------|--------------------|----------|----------------------------------------|
| POST   | `/trip`            | CabNavi  | a finished trip, `cabnavi-trip` format |
| GET    | `/dispatch`        | CabNavi  | open/accepted jobs for this driver     |
| POST   | `/dispatch/accept` | CabNavi  | `{"id": 12}` — driver takes the job     |
| GET    | `/health`          | anyone   | is it up                               |

All CabNavi calls carry `Authorization: Bearer <driver key>`. Formats are
documented in `docs/TRIP_FORMAT.md` and `docs/DISPATCH_FORMAT.md` in the
CabNavi repository.

## Data

Everything lives in `cabnavi_bot.sqlite` next to the script: drivers and
keys, trips, jobs. Back it up like any file. Delete it to start over.

# CabNavi trip receiver for Node.js

No dependencies.

```
node cabnavi_receiver.js
```

Listens on port 8788 for the CabNavi VTC webhook and appends every trip to
`trips.jsonl` next to the script. Give each driver their own key in `KEYS`
at the top of `cabnavi_receiver.js`. Put TLS in front (a reverse proxy,
Cloudflare Tunnel, ngrok): CabNavi only talks to `https://` addresses.

Endpoints:

- `POST /trip` — a finished trip
- `POST /live` — position, when the driver switched that on; the last known
  position per driver is kept in `live.json`

Format: `docs/TRIP_FORMAT.md` in the CabNavi repository.

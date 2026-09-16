// CabNavi trip receiver -- Node.js, no dependencies.
//
//   node cabnavi_receiver.js
//
// Listens on port 8788 for the CabNavi VTC webhook and appends every trip to
// trips.jsonl next to this file. Give each driver their own key below. Put
// TLS in front (a reverse proxy, Cloudflare Tunnel, ngrok): CabNavi only
// talks to https:// addresses.
//
// Endpoints: POST /trip (a finished trip), POST /live (position, when the
// driver switched that on). Format: docs/TRIP_FORMAT.md in the CabNavi repo.

const http = require("http");
const fs = require("fs");
const path = require("path");

const KEYS = { "geheim-van-jojo": "Jojo", "geheim-van-piet": "Piet" };
const PORT = 8788;
const LOG = path.join(__dirname, "trips.jsonl");
const LIVE = path.join(__dirname, "live.json");   // last known position per driver

const live = {};

function driverFor(req) {
  const auth = req.headers["authorization"] || "";
  const key = auth.startsWith("Bearer ") ? auth.slice(7).trim() : "";
  return KEYS[key] || null;
}

http.createServer((req, res) => {
  if (req.method !== "POST") { res.writeHead(405); return res.end(); }
  // A browser page cannot talk to this receiver: browsers send Origin/Referer, trackers do not.
  if (req.headers["origin"] || req.headers["referer"]) { res.writeHead(403); return res.end(); }
  const driver = driverFor(req);
  if (!driver) { res.writeHead(401); return res.end(); }

  let body = "";
  req.on("data", c => { body += c; if (body.length > 4 * 1024 * 1024) req.destroy(); });
  req.on("end", () => {
    let data;
    try { data = JSON.parse(body); } catch { res.writeHead(400); return res.end(); }

    if (req.url === "/trip" && data.format === "cabnavi-trip") {
      if (data.test) { res.writeHead(200); return res.end(); }
      fs.appendFile(LOG, JSON.stringify({ driver, trip: data }) + "\n", () => {});
      res.writeHead(200); return res.end();
    }
    if (req.url === "/live" && data.format === "cabnavi-live") {
      live[driver] = { ...data, at: new Date().toISOString() };
      fs.writeFile(LIVE, JSON.stringify(live, null, 1), () => {});
      res.writeHead(200); return res.end();
    }
    res.writeHead(400); res.end();
  });
}).listen(PORT, () => console.log(`CabNavi receiver on port ${PORT}`));

# CabNavi trip receiver for Google Sheets

No server at all. Every finished trip becomes one row.

1. Open a Google Sheet, Extensions → Apps Script, paste `Code.gs`.
2. Fill `KEYS` with one secret per driver.
3. Deploy → New deployment → Web app → Execute as: Me, Who has access:
   Anyone → Deploy. Copy the URL.
4. Drivers paste that URL and their key into CabNavi (Settings → VTC
   webhook). The key travels as a Bearer token; Apps Script cannot read
   request headers, so CabNavi's format is accepted here by the key inside
   the URL: give each driver URL + `?key=<secret>`.

Format: `docs/TRIP_FORMAT.md` in the CabNavi repository.

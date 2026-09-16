// CabNavi trip receiver -- Google Sheets, no server at all.
//
// 1. Open a Google Sheet, Extensions -> Apps Script, paste this file.
// 2. Fill KEYS with one secret per driver.
// 3. Deploy -> New deployment -> Web app -> Execute as: Me,
//    Who has access: Anyone -> Deploy. Copy the URL.
// 4. Drivers paste that URL and their key into CabNavi (Settings -> VTC
//    webhook). The key travels as a Bearer token; Apps Script cannot read
//    request headers, so CabNavi's format is accepted here by the key
//    inside the URL: give each driver URL + "?key=<secret>".
//
// Every finished trip becomes one row. Format: docs/TRIP_FORMAT.md.

var KEYS = { "geheim-van-jojo": "Jojo", "geheim-van-piet": "Piet" };

function doPost(e) {
  var key = (e.parameter && e.parameter.key) || "";
  var driver = KEYS[key];
  if (!driver) return ContentService.createTextOutput("unknown key").setMimeType(ContentService.MimeType.TEXT);

  var t;
  try { t = JSON.parse(e.postData.contents); } catch (err) { return ContentService.createTextOutput("bad json"); }
  if (t.format !== "cabnavi-trip") return ContentService.createTextOutput("unknown format");
  if (t.test) return ContentService.createTextOutput("ok");

  var sheet = SpreadsheetApp.getActiveSpreadsheet().getSheets()[0];
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(["Received", "Driver", "Game", "Type", "Status", "From", "To", "Cargo",
                     "Driven km", "Planned km", "Fuel L", "Fuel cost", "Income", "On time",
                     "Damage chassis %", "Tacho min since rest", "Empty run km", "Top speed"]);
  }
  var r = t.route_summary || {};
  sheet.appendRow([
    new Date(), driver, t.game || "", t.type || "", t.status || "",
    (t.route || {}).from_city || "", (t.route || {}).to_city || "", (t.cargo || {}).name || "",
    (t.distance || {}).driven_km || "", (t.distance || {}).planned_km || "",
    (t.fuel || {}).used_litres || "", (t.fuel || {}).estimated_cost || "",
    (t.money || {}).income || "", t.on_time === undefined ? "" : (t.on_time ? "yes" : "no"),
    (t.damage || {}).chassis_pct || "", (t.tachograph || {}).driving_minutes_since_rest || "",
    (t.empty_run || {}).km || "", t.top_speed_kmh || ""
  ]);
  return ContentService.createTextOutput("ok");
}

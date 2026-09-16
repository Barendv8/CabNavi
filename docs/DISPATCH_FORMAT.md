# The CabNavi dispatch format

**`cabnavi-dispatch`, version 1** — how a company hands a driver a job and
hears back. The mirror image of the trip format: trips flow out of the
cab, jobs flow in.

CabNavi asks your system once a minute, with the same address and key the
driver configured for the trip webhook. If the webhook address ends in
`/trip`, CabNavi replaces that with `/dispatch`; otherwise it appends
`/dispatch`.

## Asking for jobs

```
GET <address>/dispatch
Authorization: Bearer <driver key>
```

Reply with the driver's jobs that are still open or accepted:

```json
{
  "format": "cabnavi-dispatch",
  "format_version": 1,
  "jobs": [
    {
      "id": 12,
      "from_city": "Rotterdam",
      "to_city": "Berlin",
      "cargo": "Refrigerated goods",
      "deadline": "21:00",
      "note": "Take the A2, roadworks on the A1",
      "status": "open"
    }
  ]
}
```

- **`id`** — your identifier, echoed back on accept. Number or string.
- **`from_city` / `to_city`** — required. Free text; shown as given.
- **`cargo`, `deadline`, `note`** — optional, shown when present.
- **`status`** — `"open"` (not yet taken) or `"accepted"` (this driver
  took it). Delivered and cancelled jobs are simply left out.

An empty `jobs` array means nothing to do. A non-2xx reply or an
unreachable address is treated the same way and retried a minute later —
never shown to the driver as an error.

## Accepting a job

When the driver presses *Accept* in CabNavi:

```
POST <address>/dispatch/accept
Authorization: Bearer <driver key>
Content-Type: application/json

{ "id": 12 }
```

Reply `2xx` and mark the job accepted on your side. From then on it comes
back with `"status": "accepted"` until it is delivered.

## Tying the delivery to the job

When the trip arrives on `/trip` (see `TRIP_FORMAT.md`), match it to the
driver's accepted job however suits you — the reference implementation
matches on destination city. Then stop returning the job.

## Ground rules

1. Unknown fields must be ignored, never rejected.
2. Absent means unknown.
3. This is a poll, so keep the reply small. Ten jobs is plenty.

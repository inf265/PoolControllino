#!/usr/bin/env python3
"""
Generate self-contained HTML reports from 13000_pool.log* controller logs.

One page per day (including the live, in-progress day) plus an index page with
cross-day trends. Pure stdlib, no network, safe to run from cron.

    ./pool_report.py --src /var/log/pool --out /var/www/pool

Cron every 10 minutes (rebuilds only what changed):
    */10 * * * * /usr/local/bin/pool_report.py --src /var/log/pool --out /var/www/pool >> /var/log/pool_report.log 2>&1

Options:
    --src DIR      directory holding 13000_pool.log and 13000_pool.log.YYYY-MM-DD
    --out DIR      directory to write index.html and day-YYYY-MM-DD.html into
    --prefix NAME  log basename (default: 13000_pool.log)
    --step SEC     downsample bucket size in seconds (default: 60)
    --days N       only build the newest N days (default: all)
    --force        rebuild every day even if nothing changed
    --quiet        only report errors
"""

import argparse
import datetime as dt
import json
import os
import re
import sys
import tempfile

# ---------------------------------------------------------------------------
# Field configuration
#
# Panels group fields that share a unit, so each panel gets ONE y-axis. Adding a
# new firmware field here is all it takes to chart it; anything numeric that is
# not listed lands in the auto-detected "Other" panel.
# ---------------------------------------------------------------------------

PANELS = [
    {"key": "redox",  "title": "Redox / ORP",        "unit": "mV",
     "fields": [("redox", "Redox"), ("redoxmedian", "Redox median")]},
    {"key": "ph",     "title": "pH",                 "unit": "pH",
     "fields": [("ph", "pH"), ("phmedian", "pH median")]},
    {"key": "temp",   "title": "Temperature",        "unit": "°C",
     "fields": [("temperature", "Water"), ("housingtemperature", "Housing")]},
    {"key": "adc",    "title": "Raw ADC values",     "unit": "counts",
     "fields": [("phadcvalue", "pH ADC"), ("redoxadcvalue", "Redox ADC"),
                ("waterflowswitchadcvalue", "Flow ADC"),
                ("powersupplyadcvalue", "Power ADC")]},
]

# Digital signals get lanes in a strip chart, not lines.
DIGITAL = [
    ("redox-pomp",     "Redox pump"),
    ("ph-pomp",        "pH pump"),
    ("water-pomp",     "Water pump"),
    ("waterflowswitch", "Water flow"),
    ("powersupply",    "Power supply"),
    ("redox-man",      "Redox manual"),
    ("ph-man",         "pH manual"),
    ("water-man",      "Water manual"),
    ("redox-man-rem",  "Redox manual (remote)"),
    ("ph-man-rem",     "pH manual (remote)"),
    ("water-man-rem",  "Water manual (remote)"),
    ("error",          "Error"),
    ("warning",        "Warning"),
]

# Pumps we report run-time totals for.
RUNTIME_FIELDS = [("redox-pomp", "Redox pump"), ("ph-pomp", "pH pump"),
                  ("water-pomp", "Water pump"), ("waterflowswitch", "Circulation")]

# Text fields whose changes are worth a timeline entry.
EVENT_FIELDS = ["errortext", "warningtext"]

# A gap longer than this means the logger stopped; do not bill it as run time.
MAX_GAP = 30.0

LINE_RE = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d),\d+\s")


# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------

def parse_line(line):
    """Return (datetime, payload dict) or None if the line is not a sample."""
    m = LINE_RE.match(line)
    if not m:
        return None
    i = line.find("{")
    j = line.rfind("}")
    if i < 0 or j < i:
        return None
    try:
        payload = json.loads(line[i:j + 1])
    except ValueError:
        return None
    try:
        ts = dt.datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S")
    except ValueError:
        return None
    return ts, payload


def peek_range(path):
    """Cheaply read the first and last sample timestamp of a log file.

    Lets us decide which files can contribute to a given day without parsing
    15 MB of JSON.
    """
    try:
        size = os.path.getsize(path)
        if size == 0:
            return None
        with open(path, "rb") as fh:
            head = fh.read(65536).decode("utf-8", "replace").splitlines()
            fh.seek(max(0, size - 65536))
            tail = fh.read().decode("utf-8", "replace").splitlines()
    except OSError:
        return None
    lo = None
    for line in head:
        m = LINE_RE.match(line)
        if m:
            lo = m.group(1)[:10]
            break
    if lo is None:
        return None
    hi = lo
    for line in reversed(tail):
        m2 = LINE_RE.match(line)
        if m2:
            hi = m2.group(1)[:10]
            break
    # Out-of-order or clock-stepped lines can invert the range; normalise so the
    # caller's day loop still covers everything the file might hold.
    return (lo, hi) if lo <= hi else (hi, lo)


def read_samples(path, want_day):
    """Yield (datetime, payload) for rows whose date is want_day."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if not line.startswith(want_day):
                continue
            rec = parse_line(line)
            if rec:
                yield rec


# ---------------------------------------------------------------------------
# Aggregation
# ---------------------------------------------------------------------------

def to_num(v):
    if isinstance(v, bool):
        return 1.0 if v else 0.0
    if isinstance(v, (int, float)):
        return float(v)
    if isinstance(v, str):
        try:
            return float(v)
        except ValueError:
            return None
    return None


def build_day(rows, step):
    """Downsample one day's rows into fixed buckets plus summary statistics."""
    rows.sort(key=lambda r: r[0])
    midnight = rows[0][0].replace(hour=0, minute=0, second=0, microsecond=0)
    nbuckets = (24 * 3600 + step - 1) // step

    known = {f for p in PANELS for f, _ in p["fields"]}
    digital = {f for f, _ in DIGITAL}
    extra = []
    for _, payload in rows[:50]:
        for k, v in payload.items():
            if k in known or k in digital or k in extra:
                continue
            if k in ("uptime", "date"):
                continue
            if to_num(v) is not None and not isinstance(v, str):
                extra.append(k)
    panels = [dict(p) for p in PANELS]
    if extra:
        panels.append({"key": "other", "title": "Other values", "unit": "",
                       "fields": [(k, k) for k in sorted(extra)]})

    analog_fields = [f for p in panels for f, _ in p["fields"]]
    sums = {f: [0.0] * nbuckets for f in analog_fields}
    cnts = {f: [0] * nbuckets for f in analog_fields}
    don = {f: [0.0] * nbuckets for f, _ in DIGITAL}
    dcnt = [0] * nbuckets

    runtime = {f: 0.0 for f, _ in RUNTIME_FIELDS}
    hourly = {f: [0.0] * 24 for f, _ in RUNTIME_FIELDS}
    episodes = {f: [] for f, _ in RUNTIME_FIELDS}
    open_ep = {f: None for f, _ in RUNTIME_FIELDS}
    events = []
    last_text = {}
    gaps = []

    for idx, (ts, payload) in enumerate(rows):
        off = int((ts - midnight).total_seconds())
        b = min(max(off // step, 0), nbuckets - 1)
        dcnt[b] += 1
        for f in analog_fields:
            v = to_num(payload.get(f))
            if v is not None:
                sums[f][b] += v
                cnts[f][b] += 1
        for f, _ in DIGITAL:
            if to_num(payload.get(f)):
                don[f][b] += 1.0

        # Integrate run time over the interval that follows this sample.
        if idx + 1 < len(rows):
            raw = (rows[idx + 1][0] - ts).total_seconds()
            gaps.append(raw)
            delta = min(raw, MAX_GAP)
        else:
            delta = 0.0
        for f, _ in RUNTIME_FIELDS:
            on = bool(to_num(payload.get(f)))
            if on:
                runtime[f] += delta
                hourly[f][min(ts.hour, 23)] += delta
                if open_ep[f] is None:
                    open_ep[f] = [off, off]
                else:
                    open_ep[f][1] = off
            elif open_ep[f] is not None:
                episodes[f].append(open_ep[f])
                open_ep[f] = None

        for f in EVENT_FIELDS:
            txt = payload.get(f)
            if isinstance(txt, str) and txt and last_text.get(f) != txt:
                events.append({"t": off, "kind": f.replace("text", ""), "text": txt})
                last_text[f] = txt

    for f, _ in RUNTIME_FIELDS:
        if open_ep[f] is not None:
            episodes[f].append(open_ep[f])

    used = [b for b in range(nbuckets) if dcnt[b]]
    lo_b, hi_b = (used[0], used[-1]) if used else (0, 0)
    times = list(range(lo_b, hi_b + 1))

    analog = {}
    for f in analog_fields:
        col = []
        for b in times:
            col.append(round(sums[f][b] / cnts[f][b], 3) if cnts[f][b] else None)
        if any(v is not None for v in col):
            analog[f] = col

    dig = {}
    for f, _ in DIGITAL:
        col = [round(don[f][b] / dcnt[b], 3) if dcnt[b] else None for b in times]
        if any(v for v in col if v):
            dig[f] = col

    panels_out = []
    for p in panels:
        fields = [{"k": k, "label": lbl} for k, lbl in p["fields"] if k in analog]
        if fields:
            panels_out.append({"key": p["key"], "title": p["title"],
                               "unit": p["unit"], "fields": fields})

    gaps_sorted = sorted(gaps)
    coverage = {
        "samples": len(rows),
        "first": int((rows[0][0] - midnight).total_seconds()),
        "last": int((rows[-1][0] - midnight).total_seconds()),
        "median_gap": round(gaps_sorted[len(gaps_sorted) // 2], 1) if gaps_sorted else 0,
        "max_gap": round(gaps_sorted[-1], 1) if gaps_sorted else 0,
        "dropouts": sum(1 for g in gaps if g > MAX_GAP),
    }

    return {
        "date": midnight.strftime("%Y-%m-%d"),
        "step": step,
        "t0": lo_b * step,
        "t": [b * step for b in times],
        "analog": analog,
        "digital": dig,
        "digitalLabels": [{"k": k, "label": lbl} for k, lbl in DIGITAL if k in dig],
        "panels": panels_out,
        "runtime": {f: round(runtime[f]) for f, _ in RUNTIME_FIELDS},
        "runtimeLabels": [{"k": k, "label": lbl} for k, lbl in RUNTIME_FIELDS],
        "hourly": {f: [round(v) for v in hourly[f]] for f, _ in RUNTIME_FIELDS},
        "episodes": {f: episodes[f] for f, _ in RUNTIME_FIELDS},
        "events": events,
        "coverage": coverage,
    }


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def write_atomic(path, text):
    d = os.path.dirname(os.path.abspath(path))
    fd, tmp = tempfile.mkstemp(dir=d, suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.write(text)
        # mkstemp gives 0600; the webserver needs to be able to read these.
        os.chmod(tmp, 0o644)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def render(template, data, title, subtitle, extra=None):
    payload = json.dumps(data, separators=(",", ":"))
    # Keep the JSON from terminating the enclosing <script> element.
    payload = payload.replace("</", "<\\/")
    out = template.replace("__TITLE__", title).replace("__SUBTITLE__", subtitle)
    out = out.replace("__EXTRA__", extra or "")
    return out.replace("__DATA__", payload)


def main():
    ap = argparse.ArgumentParser(description="Build HTML reports from pool controller logs.")
    ap.add_argument("--src", default=".", help="directory containing the logs")
    ap.add_argument("--out", default="./html", help="output directory")
    ap.add_argument("--prefix", default="13000_pool.log", help="log file basename")
    ap.add_argument("--step", type=int, default=60, help="bucket size in seconds")
    ap.add_argument("--days", type=int, default=0, help="only build the newest N days")
    ap.add_argument("--force", action="store_true", help="rebuild everything")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    def say(msg):
        if not args.quiet:
            print(msg, flush=True)

    src, out = args.src, args.out
    if not os.path.isdir(src):
        sys.exit("source directory not found: %s" % src)
    os.makedirs(out, exist_ok=True)

    # Map every log file to the range of dates it can contribute to.
    ranges = {}
    live = os.path.join(src, args.prefix)
    if os.path.exists(live):
        r = peek_range(live)
        if r:
            ranges[live] = r
    pat = re.compile(re.escape(args.prefix) + r"\.(\d{4}-\d\d-\d\d)$")
    for name in sorted(os.listdir(src)):
        if pat.match(name):
            path = os.path.join(src, name)
            r = peek_range(path)
            if r:
                ranges[path] = r

    if not ranges:
        sys.exit("no readable %s* files in %s" % (args.prefix, src))

    days = {}
    for path, (lo, hi) in ranges.items():
        d = dt.date.fromisoformat(lo)
        end = dt.date.fromisoformat(hi)
        while d <= end:
            days.setdefault(d.isoformat(), []).append(path)
            d += dt.timedelta(days=1)

    all_days = sorted(days)
    if args.days > 0:
        all_days = all_days[-args.days:]

    template = HTML_DAY
    summaries = []
    built = 0
    for day in all_days:
        target = os.path.join(out, "day-%s.html" % day)
        sources = days[day]
        stale = args.force or not os.path.exists(target)
        if not stale:
            tmt = os.path.getmtime(target)
            stale = any(os.path.getmtime(p) > tmt for p in sources)

        cache = os.path.join(out, ".cache-%s.json" % day)
        if not stale and os.path.exists(cache):
            try:
                with open(cache, encoding="utf-8") as fh:
                    summaries.append(json.load(fh))
                continue
            except (OSError, ValueError):
                stale = True

        rows = []
        for p in sources:
            rows.extend(read_samples(p, day))
        if not rows:
            say("%s: no samples, skipped" % day)
            continue

        data = build_day(rows, args.step)
        subtitle = "%s &middot; %s samples &middot; %s" % (
            dt.date.fromisoformat(day).strftime("%A, %d %B %Y"),
            format(data["coverage"]["samples"], ",d"),
            ", ".join(os.path.basename(p) for p in sources))
        html = render(template, data, "Pool %s" % day, subtitle)
        write_atomic(target, html)
        built += 1
        say("%s: %s samples -> %s" % (day, format(len(rows), ',d'), os.path.basename(target)))

        summary = {"date": day, "runtime": data["runtime"],
                   "samples": data["coverage"]["samples"],
                   "dropouts": data["coverage"]["dropouts"],
                   "events": len(data["events"])}
        for p in data["panels"]:
            for f in p["fields"]:
                col = [v for v in data["analog"][f["k"]] if v is not None]
                if col:
                    summary.setdefault("avg", {})[f["k"]] = round(sum(col) / len(col), 2)
                    summary.setdefault("min", {})[f["k"]] = round(min(col), 2)
                    summary.setdefault("max", {})[f["k"]] = round(max(col), 2)
        summaries.append(summary)
        write_atomic(cache, json.dumps(summary, separators=(",", ":")))

    summaries.sort(key=lambda s: s["date"])
    index = render(HTML_INDEX,
                   {"days": summaries,
                    "runtimeLabels": [{"k": k, "label": lbl} for k, lbl in RUNTIME_FIELDS]},
                   "Pool controller 13000",
                   "%d days &middot; %s to %s" % (
                       len(summaries),
                       summaries[0]["date"] if summaries else "-",
                       summaries[-1]["date"] if summaries else "-"))
    write_atomic(os.path.join(out, "index.html"), index)
    say("index.html written (%d days, %d rebuilt)" % (len(summaries), built))


# ---------------------------------------------------------------------------
# Templates
# ---------------------------------------------------------------------------

SHARED_CSS = r"""
:root{
  color-scheme:light;
  --surface-1:#fcfcfb; --plane:#f9f9f7;
  --text-primary:#0b0b0b; --text-secondary:#52514e; --muted:#898781;
  --grid:#e1e0d9; --axis:#c3c2b7; --border:rgba(11,11,11,0.10);
  --wash:rgba(11,11,11,0.035); --hover:rgba(11,11,11,0.05);
  --s1:#2a78d6; --s2:#008300; --s3:#e87ba4; --s4:#eda100;
  --s5:#1baf7a; --s6:#eb6834; --s7:#4a3aa7; --s8:#e34948;
  --critical:#d03b3b; --warning:#fab219;
}
@media (prefers-color-scheme:dark){:root:where(:not([data-theme="light"])){
  color-scheme:dark;
  --surface-1:#1a1a19; --plane:#0d0d0d;
  --text-primary:#fff; --text-secondary:#c3c2b7; --muted:#898781;
  --grid:#2c2c2a; --axis:#383835; --border:rgba(255,255,255,0.10);
  --wash:rgba(255,255,255,0.05); --hover:rgba(255,255,255,0.07);
  --s1:#3987e5; --s2:#008300; --s3:#d55181; --s4:#c98500;
  --s5:#199e70; --s6:#d95926; --s7:#9085e9; --s8:#e66767;
}}
:root[data-theme="dark"]{
  color-scheme:dark;
  --surface-1:#1a1a19; --plane:#0d0d0d;
  --text-primary:#fff; --text-secondary:#c3c2b7; --muted:#898781;
  --grid:#2c2c2a; --axis:#383835; --border:rgba(255,255,255,0.10);
  --wash:rgba(255,255,255,0.05); --hover:rgba(255,255,255,0.07);
  --s1:#3987e5; --s2:#008300; --s3:#d55181; --s4:#c98500;
  --s5:#199e70; --s6:#d95926; --s7:#9085e9; --s8:#e66767;
}
*{box-sizing:border-box}
body{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;background:var(--plane);
  color:var(--text-primary);margin:0;padding:24px 20px 64px;-webkit-text-size-adjust:100%}
.wrap{max-width:1040px;margin:0 auto}
a{color:var(--s1)}
h1{font-size:1.45rem;line-height:1.25;margin:0 0 4px;font-weight:650;letter-spacing:-.01em}
.sub{color:var(--text-secondary);font-size:.85rem;margin:0 0 20px}
.card{background:var(--surface-1);border:1px solid var(--border);border-radius:10px;
  padding:18px;margin-bottom:16px}
.card h2{font-size:.95rem;margin:0 0 2px;font-weight:600}
.card p.note{font-size:.78rem;color:var(--text-secondary);margin:0 0 12px}
.tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(148px,1fr));gap:12px;margin-bottom:16px}
.tile{background:var(--surface-1);border:1px solid var(--border);border-radius:10px;padding:13px 15px}
.tile .lbl{font-size:.7rem;text-transform:uppercase;letter-spacing:.05em;color:var(--muted);margin-bottom:5px}
.tile .val{font-size:1.55rem;font-weight:650;letter-spacing:-.02em;line-height:1.1}
.tile .val small{font-size:.85rem;font-weight:500;color:var(--text-secondary);margin-left:2px}
.tile .sfx{font-size:.75rem;color:var(--text-secondary);margin-top:4px}
.legend{display:flex;flex-wrap:wrap;gap:6px 14px;margin:0 0 10px;align-items:center}
.legend label{display:inline-flex;align-items:center;gap:6px;font-size:.8rem;
  color:var(--text-secondary);cursor:pointer;user-select:none;padding:3px 6px;border-radius:6px;
  min-height:28px}
.legend label:hover{background:var(--hover)}
.legend input{accent-color:var(--s1);width:14px;height:14px;margin:0;cursor:pointer}
.legend .sw{width:16px;height:3px;border-radius:2px;flex:none}
.legend label.off{opacity:.45}
.legend label.off .sw{background:var(--muted)!important}
.tools{margin-left:auto;display:flex;gap:6px}
.tools button{font:inherit;font-size:.73rem;color:var(--text-secondary);background:none;
  border:1px solid var(--border);border-radius:6px;padding:4px 9px;cursor:pointer}
.tools button:hover{background:var(--hover)}
.chartbox{position:relative;overflow-x:auto;overflow-y:hidden}
svg{display:block;width:100%;height:auto;min-width:620px;touch-action:pan-y}
.tt{position:absolute;pointer-events:none;background:var(--surface-1);border:1px solid var(--border);
  border-radius:8px;padding:8px 10px;font-size:.77rem;line-height:1.55;
  box-shadow:0 4px 14px rgba(0,0,0,.14);white-space:nowrap;opacity:0;transition:opacity .1s;z-index:5}
.tt .k{color:var(--text-secondary)}
.tt b{font-weight:600}
.tt .dotc{display:inline-block;width:8px;height:8px;border-radius:2px;margin-right:5px}
table{border-collapse:collapse;width:100%;font-size:.81rem;font-variant-numeric:tabular-nums}
th,td{text-align:left;padding:7px 9px;border-bottom:1px solid var(--grid);white-space:nowrap}
th{color:var(--muted);font-weight:600;font-size:.7rem;text-transform:uppercase;letter-spacing:.04em}
td.num,th.num{text-align:right}
tbody tr:hover{background:var(--hover)}
details{margin-top:6px}
summary{cursor:pointer;font-size:.8rem;color:var(--text-secondary);padding:7px 0}
.scroll{overflow-x:auto}
.nav{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-bottom:18px;font-size:.83rem}
.nav a{text-decoration:none;border:1px solid var(--border);border-radius:7px;padding:5px 11px;
  color:var(--text-secondary);background:var(--surface-1)}
.nav a:hover{background:var(--hover)}
.nav a.cur{color:var(--text-primary);font-weight:600;border-color:var(--axis)}
.empty{color:var(--text-secondary);font-size:.85rem;padding:20px 0}
.evt{display:flex;gap:9px;align-items:flex-start;font-size:.8rem;line-height:1.5;padding:7px 0;
  border-bottom:1px solid var(--grid)}
.evt .badge{flex:none;font-size:.68rem;text-transform:uppercase;letter-spacing:.04em;font-weight:600;
  padding:2px 7px;border-radius:5px;margin-top:1px}
.evt .badge.warning{background:rgba(250,178,25,.18);color:#8a6100}
.evt .badge.error{background:rgba(208,59,59,.16);color:var(--critical)}
:root[data-theme="dark"] .evt .badge.warning{color:var(--warning)}
@media (prefers-color-scheme:dark){
  :root:where(:not([data-theme="light"])) .evt .badge.warning{color:var(--warning)}
}
.evt time{flex:none;color:var(--muted);font-variant-numeric:tabular-nums}
"""

SHARED_JS = r"""
const PAL=['--s1','--s2','--s3','--s4','--s5','--s6','--s7','--s8'];
const cvar=n=>getComputedStyle(document.documentElement).getPropertyValue(n).trim();
const NS='http://www.w3.org/2000/svg';
const el=(n,a)=>{const e=document.createElementNS(NS,n);for(const k in a)e.setAttribute(k,a[k]);return e;};
const p2=n=>String(n).padStart(2,'0');
const hhmm=s=>p2(Math.floor(s/3600))+':'+p2(Math.floor(s/60)%60);
const hhmmss=s=>hhmm(s)+':'+p2(s%60);
const dur=s=>{const h=Math.floor(s/3600),m=Math.floor(s/60)%60;
  return h?`${h} h ${p2(m)} min`:(s>=60?`${m} min ${p2(s%60)} s`:`${s} s`);};
function niceScale(lo,hi,n){
  if(!isFinite(lo)||!isFinite(hi)){lo=0;hi=1;}
  if(hi===lo){hi=lo+(Math.abs(lo)||1)*0.05;lo=lo-(Math.abs(lo)||1)*0.05;}
  const raw=(hi-lo)/n, mag=Math.pow(10,Math.floor(Math.log10(raw))), nm=raw/mag;
  const step=(nm<=1?1:nm<=2?2:nm<=5?5:10)*mag;
  const s=Math.floor(lo/step)*step, e=Math.ceil(hi/step)*step, ticks=[];
  for(let v=s;v<=e+step*1e-6;v+=step) ticks.push(+v.toFixed(10));
  return {lo:s,hi:e,ticks,step};
}
const fmtNum=(v,step)=>{const d=step<0.1?2:step<1?1:0;return v.toFixed(d);};
function saveSel(key,set){try{localStorage.setItem('pool.'+key,JSON.stringify([...set]));}catch(e){}}
function loadSel(key){try{const r=localStorage.getItem('pool.'+key);return r?new Set(JSON.parse(r)):null;}catch(e){return null;}}
"""


HTML_DAY = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>__TITLE__</title>
<style>__CSS__</style>
</head>
<body>
<div class="wrap">
  <h1>__TITLE__</h1>
  <p class="sub">__SUBTITLE__</p>
  <div class="nav" id="nav"></div>
  <div class="tiles" id="tiles"></div>
  <div id="panels"></div>

  <div class="card">
    <h2>Digital signals</h2>
    <p class="note">One lane per signal. A block means the signal was on during that
      interval; partial intervals are drawn lighter. Untick a signal to hide its lane.</p>
    <div class="legend" id="dlegend"></div>
    <div class="chartbox" id="dbox"><svg id="dsvg"></svg><div class="tt" id="dtt"></div></div>
  </div>

  <div class="card" id="evtcard">
    <h2>Status changes</h2>
    <div id="events"></div>
  </div>

  <div class="card">
    <h2>Run time per hour</h2>
    <p class="note">Seconds each output was on, per clock hour.</p>
    <div class="scroll"><table id="hourly"></table></div>
    <details><summary>Show individual on/off episodes</summary>
      <div class="scroll"><table id="eps"></table></div></details>
    <details><summary>Show logging coverage</summary>
      <div class="scroll"><table id="cov"></table></div></details>
  </div>
</div>

<script id="data" type="application/json">__DATA__</script>
<script>
__JS__
const D=JSON.parse(document.getElementById('data').textContent);

/* ---- day navigation (built from sibling files; degrades silently) ---- */
(function(){
  const d=new Date(D.date+'T12:00:00');
  const shift=n=>{const x=new Date(d);x.setDate(x.getDate()+n);
    return x.getFullYear()+'-'+p2(x.getMonth()+1)+'-'+p2(x.getDate());};
  const nav=document.getElementById('nav');
  const mk=(href,txt,cur)=>{const a=document.createElement('a');a.href=href;a.textContent=txt;
    if(cur)a.className='cur';nav.appendChild(a);return a;};
  mk('index.html','← All days');
  mk('day-'+shift(-1)+'.html','‹ '+shift(-1));
  mk('day-'+D.date+'.html',D.date,true);
  mk('day-'+shift(1)+'.html',shift(1)+' ›');
})();

/* ---- summary tiles ---- */
(function(){
  const t=document.getElementById('tiles');
  D.runtimeLabels.forEach(r=>{
    const s=D.runtime[r.k]||0, eps=(D.episodes[r.k]||[]).length;
    const div=document.createElement('div');div.className='tile';
    div.innerHTML=`<div class="lbl">${r.label}</div>
      <div class="val">${s>=3600?(s/3600).toFixed(1):Math.round(s/60)}<small>${s>=3600?' h':' min'}</small></div>
      <div class="sfx">${eps} ${eps===1?'episode':'episodes'} · ${(s/864).toFixed(1)}% of day</div>`;
    t.appendChild(div);
  });
})();

/* ---- analog panels ---- */
function analogPanel(p){
  const card=document.createElement('div');card.className='card';
  card.innerHTML=`<h2>${p.title}</h2><p class="note">${p.unit?'Unit: '+p.unit+'. ':''}`+
    `Averaged over ${D.step} s. Untick a series to hide it — the axis rescales to what is shown.</p>`;
  const leg=document.createElement('div');leg.className='legend';
  const box=document.createElement('div');box.className='chartbox';
  const svg=el('svg',{}); const tt=document.createElement('div');tt.className='tt';
  box.appendChild(svg);box.appendChild(tt);
  card.appendChild(leg);card.appendChild(box);
  document.getElementById('panels').appendChild(card);

  const colors={}; p.fields.forEach((f,i)=>colors[f.k]=cvar(PAL[i%8]));
  let vis=loadSel('vis.'+p.key)||new Set(p.fields.map(f=>f.k));
  if(![...vis].some(k=>p.fields.find(f=>f.k===k))) vis=new Set(p.fields.map(f=>f.k));

  p.fields.forEach(f=>{
    const lab=document.createElement('label');
    lab.innerHTML=`<input type="checkbox"><i class="sw" style="background:${colors[f.k]}"></i>${f.label}`;
    const cb=lab.querySelector('input');cb.checked=vis.has(f.k);
    if(!cb.checked)lab.classList.add('off');
    cb.addEventListener('change',()=>{
      cb.checked?vis.add(f.k):vis.delete(f.k);
      lab.classList.toggle('off',!cb.checked);
      saveSel('vis.'+p.key,vis);draw();
    });
    leg.appendChild(lab);
  });
  const tools=document.createElement('div');tools.className='tools';
  tools.innerHTML='<button data-a="all">All</button><button data-a="none">None</button>';
  tools.addEventListener('click',e=>{
    const a=e.target.dataset.a;if(!a)return;
    vis=a==='all'?new Set(p.fields.map(f=>f.k)):new Set();
    leg.querySelectorAll('label').forEach((lab,i)=>{
      const on=vis.has(p.fields[i].k);
      lab.querySelector('input').checked=on;lab.classList.toggle('off',!on);
    });
    saveSel('vis.'+p.key,vis);draw();
  });
  leg.appendChild(tools);

  const W=960,H=300,M={t:26,r:16,b:40,l:56};
  const pw=W-M.l-M.r, ph=H-M.t-M.b;
  svg.setAttribute('viewBox',`0 0 ${W} ${H}`);
  const xs=D.t[0], xe=D.t[D.t.length-1]||86400;
  const X=s=>M.l+(xe===xs?0:(s-xs)/(xe-xs))*pw;
  let sc=null;

  function draw(){
    while(svg.firstChild)svg.removeChild(svg.firstChild);
    const shown=p.fields.filter(f=>vis.has(f.k));
    if(!shown.length){
      const t=el('text',{x:W/2,y:H/2,'text-anchor':'middle',fill:'var(--muted)','font-size':13});
      t.textContent='No series selected';svg.appendChild(t);sc=null;return;
    }
    let lo=Infinity,hi=-Infinity;
    shown.forEach(f=>D.analog[f.k].forEach(v=>{if(v!=null){if(v<lo)lo=v;if(v>hi)hi=v;}}));
    sc=niceScale(lo,hi,5);
    const Y=v=>M.t+(sc.hi-v)/(sc.hi-sc.lo)*ph;

    sc.ticks.forEach(v=>{
      svg.appendChild(el('line',{x1:M.l,x2:W-M.r,y1:Y(v),y2:Y(v),stroke:'var(--grid)','stroke-width':1}));
      const t=el('text',{x:M.l-9,y:Y(v)+4,'text-anchor':'end',fill:'var(--muted)','font-size':11});
      t.textContent=fmtNum(v,sc.step);svg.appendChild(t);
    });
    if(p.unit){const u=el('text',{x:M.l-9,y:M.t-12,'text-anchor':'end',fill:'var(--muted)','font-size':10});
      u.textContent=p.unit;svg.appendChild(u);}
    for(let s=0;s<=86400;s+=10800){
      if(s<xs-1800||s>xe+1800)continue;
      svg.appendChild(el('line',{x1:X(s),x2:X(s),y1:M.t+ph,y2:M.t+ph+5,stroke:'var(--axis)','stroke-width':1}));
      const t=el('text',{x:X(s),y:M.t+ph+19,'text-anchor':'middle',fill:'var(--muted)','font-size':11});
      t.textContent=hhmm(s);svg.appendChild(t);
    }
    svg.appendChild(el('line',{x1:M.l,x2:W-M.r,y1:M.t+ph,y2:M.t+ph,stroke:'var(--axis)','stroke-width':1}));

    shown.forEach(f=>{
      const col=D.analog[f.k];let d='',pen=false;
      for(let i=0;i<col.length;i++){
        if(col[i]==null){pen=false;continue;}
        d+=(pen?'L':'M')+X(D.t[i]).toFixed(1)+' '+Y(col[i]).toFixed(1)+' ';pen=true;
      }
      svg.appendChild(el('path',{d,fill:'none',stroke:colors[f.k],'stroke-width':2,
        'stroke-linejoin':'round','stroke-linecap':'round'}));
    });

    const cross=el('line',{x1:0,x2:0,y1:M.t,y2:M.t+ph,stroke:'var(--axis)','stroke-width':1,opacity:0});
    svg.appendChild(cross);
    const dots=shown.map(f=>{const c=el('circle',{r:4.5,fill:colors[f.k],stroke:'var(--surface-1)',
      'stroke-width':2,opacity:0});svg.appendChild(c);return c;});

    svg.onpointermove=ev=>{
      const r=svg.getBoundingClientRect(), sx=(ev.clientX-r.left)/r.width*W;
      const s=xs+(sx-M.l)/pw*(xe-xs);
      let bi=0,bd=Infinity;
      for(let i=0;i<D.t.length;i++){const dd=Math.abs(D.t[i]-s);if(dd<bd){bd=dd;bi=i;}}
      if(sx<M.l-6||sx>W-M.r+6){svg.onpointerleave();return;}
      cross.setAttribute('opacity',1);cross.setAttribute('x1',X(D.t[bi]));cross.setAttribute('x2',X(D.t[bi]));
      let html=`<b>${hhmm(D.t[bi])}</b>`;
      shown.forEach((f,i)=>{
        const v=D.analog[f.k][bi];
        if(v==null){dots[i].setAttribute('opacity',0);return;}
        dots[i].setAttribute('opacity',1);dots[i].setAttribute('cx',X(D.t[bi]));dots[i].setAttribute('cy',Y(v));
        html+=`<br><span class="dotc" style="background:${colors[f.k]}"></span>`+
              `<span class="k">${f.label}</span> <b>${v}${p.unit==='pH'?'':' '+p.unit}</b>`;
      });
      tt.innerHTML=html;tt.style.opacity=1;
      const px=X(D.t[bi])/W*r.width;
      tt.style.left=Math.min(Math.max(px-tt.offsetWidth/2,4),r.width-tt.offsetWidth-4)+'px';
      tt.style.top='4px';
    };
    svg.onpointerleave=()=>{cross.setAttribute('opacity',0);dots.forEach(d=>d.setAttribute('opacity',0));
      tt.style.opacity=0;};
  }
  draw();
}
D.panels.forEach(analogPanel);

/* ---- digital lane strip ---- */
(function(){
  const leg=document.getElementById('dlegend'), svg=document.getElementById('dsvg'),
        tt=document.getElementById('dtt');
  const items=D.digitalLabels;
  if(!items.length){document.getElementById('dbox').innerHTML='<p class="empty">No digital signal changed state today.</p>';return;}
  const colors={};items.forEach((f,i)=>colors[f.k]=cvar(PAL[i%8]));
  let vis=loadSel('vis.digital')||new Set(items.map(f=>f.k));
  if(![...vis].some(k=>items.find(f=>f.k===k))) vis=new Set(items.map(f=>f.k));

  items.forEach(f=>{
    const lab=document.createElement('label');
    lab.innerHTML=`<input type="checkbox"><i class="sw" style="background:${colors[f.k]};height:11px;width:11px;border-radius:3px"></i>${f.label}`;
    const cb=lab.querySelector('input');cb.checked=vis.has(f.k);
    if(!cb.checked)lab.classList.add('off');
    cb.addEventListener('change',()=>{cb.checked?vis.add(f.k):vis.delete(f.k);
      lab.classList.toggle('off',!cb.checked);saveSel('vis.digital',vis);draw();});
    leg.appendChild(lab);
  });
  const tools=document.createElement('div');tools.className='tools';
  tools.innerHTML='<button data-a="all">All</button><button data-a="none">None</button>';
  tools.addEventListener('click',e=>{const a=e.target.dataset.a;if(!a)return;
    vis=a==='all'?new Set(items.map(f=>f.k)):new Set();
    leg.querySelectorAll('label').forEach((lab,i)=>{const on=vis.has(items[i].k);
      lab.querySelector('input').checked=on;lab.classList.toggle('off',!on);});
    saveSel('vis.digital',vis);draw();});
  leg.appendChild(tools);

  const W=960,M={t:12,r:16,b:36,l:150},LH=26;
  const pw=W-M.l-M.r;
  const xs=D.t[0], xe=D.t[D.t.length-1]||86400;
  const X=s=>M.l+(xe===xs?0:(s-xs)/(xe-xs))*pw;

  function draw(){
    while(svg.firstChild)svg.removeChild(svg.firstChild);
    const shown=items.filter(f=>vis.has(f.k));
    const H=M.t+M.b+Math.max(LH,shown.length*LH);
    svg.setAttribute('viewBox',`0 0 ${W} ${H}`);
    if(!shown.length){
      const t=el('text',{x:W/2,y:H/2,'text-anchor':'middle',fill:'var(--muted)','font-size':13});
      t.textContent='No signals selected';svg.appendChild(t);return;
    }
    const bh=Math.min(18,LH-8);
    shown.forEach((f,li)=>{
      const y=M.t+li*LH;
      svg.appendChild(el('rect',{x:M.l,y:y+(LH-bh)/2,width:pw,height:bh,fill:'var(--wash)',rx:3}));
      const t=el('text',{x:M.l-10,y:y+LH/2+4,'text-anchor':'end',fill:'var(--text-secondary)','font-size':11});
      t.textContent=f.label;svg.appendChild(t);
      const col=D.digital[f.k];
      for(let i=0;i<col.length;i++){
        const v=col[i];if(!v)continue;
        const x0=X(D.t[i]), x1=i+1<D.t.length?X(D.t[i+1]):X(D.t[i])+pw/Math.max(col.length,1);
        svg.appendChild(el('rect',{x:x0,y:y+(LH-bh)/2,width:Math.max(1.5,x1-x0),height:bh,
          fill:colors[f.k],'fill-opacity':(0.4+0.6*v).toFixed(2),rx:2}));
      }
    });
    const by=M.t+shown.length*LH;
    for(let s=0;s<=86400;s+=10800){
      if(s<xs-1800||s>xe+1800)continue;
      svg.appendChild(el('line',{x1:X(s),x2:X(s),y1:M.t,y2:by+5,stroke:'var(--grid)','stroke-width':1}));
      const t=el('text',{x:X(s),y:by+19,'text-anchor':'middle',fill:'var(--muted)','font-size':11});
      t.textContent=hhmm(s);svg.appendChild(t);
    }
    const cross=el('line',{x1:0,x2:0,y1:M.t,y2:by,stroke:'var(--axis)','stroke-width':1,opacity:0});
    svg.appendChild(cross);
    svg.onpointermove=ev=>{
      const r=svg.getBoundingClientRect(), sx=(ev.clientX-r.left)/r.width*W;
      if(sx<M.l-6||sx>W-M.r+6){svg.onpointerleave();return;}
      const s=xs+(sx-M.l)/pw*(xe-xs);
      let bi=0,bd=Infinity;
      for(let i=0;i<D.t.length;i++){const dd=Math.abs(D.t[i]-s);if(dd<bd){bd=dd;bi=i;}}
      cross.setAttribute('opacity',1);cross.setAttribute('x1',X(D.t[bi]));cross.setAttribute('x2',X(D.t[bi]));
      let html=`<b>${hhmm(D.t[bi])}</b>`;
      shown.forEach(f=>{const v=D.digital[f.k][bi];
        html+=`<br><span class="dotc" style="background:${v?colors[f.k]:'var(--axis)'}"></span>`+
              `<span class="k">${f.label}</span> <b>${v==null?'no data':v>=1?'on':v>0?'on '+Math.round(v*100)+'% of interval':'off'}</b>`;});
      tt.innerHTML=html;tt.style.opacity=1;
      const px=X(D.t[bi])/W*r.width;
      tt.style.left=Math.min(Math.max(px-tt.offsetWidth/2,4),r.width-tt.offsetWidth-4)+'px';
      tt.style.top='4px';
    };
    svg.onpointerleave=()=>{cross.setAttribute('opacity',0);tt.style.opacity=0;};
  }
  draw();
})();

/* ---- events, tables ---- */
(function(){
  const box=document.getElementById('events');
  if(!D.events.length){box.innerHTML='<p class="empty">No error or warning text changed today.</p>';return;}
  box.innerHTML=D.events.map(e=>
    `<div class="evt"><span class="badge ${e.kind}">${e.kind}</span>`+
    `<time>${hhmmss(e.t)}</time><span>${e.text||'(cleared)'}</span></div>`).join('');
})();

(function(){
  const rows=D.runtimeLabels.filter(r=>(D.runtime[r.k]||0)>0);
  const t=document.getElementById('hourly');
  if(!rows.length){t.outerHTML='<p class="empty">Nothing ran today.</p>';return;}
  let h='<thead><tr><th>Output</th>';
  for(let i=0;i<24;i++)h+=`<th class="num">${p2(i)}</th>`;
  h+='<th class="num">Total</th></tr></thead><tbody>';
  rows.forEach(r=>{
    h+=`<tr><td>${r.label}</td>`;
    for(let i=0;i<24;i++){const v=D.hourly[r.k][i]||0;
      h+=`<td class="num" style="color:${v?'var(--text-primary)':'var(--muted)'}">${v||'–'}</td>`;}
    h+=`<td class="num"><b>${dur(D.runtime[r.k])}</b></td></tr>`;
  });
  t.innerHTML=h+'</tbody>';
})();

(function(){
  let h='<thead><tr><th>Output</th><th>Start</th><th>End</th><th class="num">Duration</th></tr></thead><tbody>';
  let any=false;
  D.runtimeLabels.forEach(r=>(D.episodes[r.k]||[]).forEach(([a,b])=>{
    any=true;h+=`<tr><td>${r.label}</td><td>${hhmmss(a)}</td><td>${hhmmss(b)}</td>`+
      `<td class="num">${dur(b-a)}</td></tr>`;}));
  document.getElementById('eps').innerHTML=any?h+'</tbody>':
    '<tbody><tr><td class="empty">No episodes.</td></tr></tbody>';
})();

(function(){
  const c=D.coverage;
  document.getElementById('cov').innerHTML=
    '<tbody>'+
    `<tr><td>Samples</td><td class="num">${c.samples.toLocaleString()}</td></tr>`+
    `<tr><td>First / last sample</td><td class="num">${hhmmss(c.first)} – ${hhmmss(c.last)}</td></tr>`+
    `<tr><td>Median sample interval</td><td class="num">${c.median_gap} s</td></tr>`+
    `<tr><td>Longest gap</td><td class="num">${c.max_gap} s</td></tr>`+
    `<tr><td>Dropouts (gap &gt; 30 s)</td><td class="num">${c.dropouts}</td></tr>`+
    `<tr><td>Chart resolution</td><td class="num">${D.step} s per point</td></tr>`+
    '</tbody>';
})();
</script>
</body>
</html>
"""


HTML_INDEX = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>__TITLE__</title>
<style>__CSS__</style>
</head>
<body>
<div class="wrap">
  <h1>__TITLE__</h1>
  <p class="sub">__SUBTITLE__</p>

  <div class="card">
    <h2>Daily run time</h2>
    <p class="note">Minutes each output ran per day. Untick an output to hide it.</p>
    <div class="legend" id="legend"></div>
    <div class="chartbox" id="box"><svg id="svg"></svg><div class="tt" id="tt"></div></div>
  </div>

  <div class="card">
    <h2>Days</h2>
    <div class="scroll"><table id="tbl"></table></div>
  </div>
</div>

<script id="data" type="application/json">__DATA__</script>
<script>
__JS__
const D=JSON.parse(document.getElementById('data').textContent);
const days=D.days;

(function(){
  const leg=document.getElementById('legend'), svg=document.getElementById('svg'),
        tt=document.getElementById('tt');
  if(!days.length){document.getElementById('box').innerHTML='<p class="empty">No days yet.</p>';return;}
  const items=D.runtimeLabels.filter(r=>days.some(d=>(d.runtime[r.k]||0)>0));
  const colors={};items.forEach((f,i)=>colors[f.k]=cvar(PAL[i%8]));
  let vis=loadSel('vis.index')||new Set(items.map(f=>f.k));
  if(![...vis].some(k=>items.find(f=>f.k===k))) vis=new Set(items.map(f=>f.k));

  items.forEach(f=>{
    const lab=document.createElement('label');
    lab.innerHTML=`<input type="checkbox"><i class="sw" style="background:${colors[f.k]};height:11px;width:11px;border-radius:3px"></i>${f.label}`;
    const cb=lab.querySelector('input');cb.checked=vis.has(f.k);
    if(!cb.checked)lab.classList.add('off');
    cb.addEventListener('change',()=>{cb.checked?vis.add(f.k):vis.delete(f.k);
      lab.classList.toggle('off',!cb.checked);saveSel('vis.index',vis);draw();});
    leg.appendChild(lab);
  });
  const tools=document.createElement('div');tools.className='tools';
  tools.innerHTML='<button data-a="all">All</button><button data-a="none">None</button>';
  tools.addEventListener('click',e=>{const a=e.target.dataset.a;if(!a)return;
    vis=a==='all'?new Set(items.map(f=>f.k)):new Set();
    leg.querySelectorAll('label').forEach((lab,i)=>{const on=vis.has(items[i].k);
      lab.querySelector('input').checked=on;lab.classList.toggle('off',!on);});
    saveSel('vis.index',vis);draw();});
  leg.appendChild(tools);

  const W=960,H=300,M={t:26,r:16,b:52,l:56};
  const pw=W-M.l-M.r, ph=H-M.t-M.b;
  svg.setAttribute('viewBox',`0 0 ${W} ${H}`);

  function draw(){
    while(svg.firstChild)svg.removeChild(svg.firstChild);
    const shown=items.filter(f=>vis.has(f.k));
    if(!shown.length){
      const t=el('text',{x:W/2,y:H/2,'text-anchor':'middle',fill:'var(--muted)','font-size':13});
      t.textContent='No outputs selected';svg.appendChild(t);return;}
    let hi=0;days.forEach(d=>shown.forEach(f=>{const v=(d.runtime[f.k]||0)/60;if(v>hi)hi=v;}));
    const sc=niceScale(0,hi||1,4), Y=v=>M.t+(1-v/sc.hi)*ph;
    sc.ticks.forEach(v=>{
      svg.appendChild(el('line',{x1:M.l,x2:W-M.r,y1:Y(v),y2:Y(v),
        stroke:v?'var(--grid)':'var(--axis)','stroke-width':1}));
      const t=el('text',{x:M.l-9,y:Y(v)+4,'text-anchor':'end',fill:'var(--muted)','font-size':11});
      t.textContent=fmtNum(v,sc.step);svg.appendChild(t);
    });
    const u=el('text',{x:M.l-9,y:M.t-12,'text-anchor':'end',fill:'var(--muted)','font-size':10});
    u.textContent='min';svg.appendChild(u);

    const gw=pw/days.length;
    // Cap bar width so a two-day chart does not render as giant slabs.
    const bw=Math.max(2,Math.min(34,(gw-6)/shown.length-2));
    const grpW=shown.length*(bw+2)-2;
    days.forEach((d,di)=>{
      const gx=M.l+di*gw+(gw-grpW)/2;
      shown.forEach((f,si)=>{
        const v=(d.runtime[f.k]||0)/60;if(v<=0)return;
        const x=gx+si*(bw+2), hgt=Math.max(2,ph-(Y(v)-M.t));
        svg.appendChild(el('rect',{x,y:Y(v),width:bw,height:hgt,fill:colors[f.k],rx:Math.min(4,bw/2)}));
        if(hgt>4)svg.appendChild(el('rect',{x,y:Y(v)+4,width:bw,height:hgt-4,fill:colors[f.k]}));
      });
      const hit=el('rect',{x:gx,y:M.t,width:gw,height:ph,fill:'transparent'});
      hit.addEventListener('pointerenter',()=>{
        tt.innerHTML=`<b>${d.date}</b>`+shown.map(f=>
          `<br><span class="dotc" style="background:${colors[f.k]}"></span>`+
          `<span class="k">${f.label}</span> <b>${dur(d.runtime[f.k]||0)}</b>`).join('');
        tt.style.opacity=1;
        const r=svg.getBoundingClientRect(), px=(gx+gw/2)/W*r.width;
        tt.style.left=Math.min(Math.max(px-tt.offsetWidth/2,4),r.width-tt.offsetWidth-4)+'px';
        tt.style.top='4px';
      });
      svg.appendChild(hit);
      if(days.length<=16||di%Math.ceil(days.length/16)===0){
        const t=el('text',{x:gx+gw/2,y:M.t+ph+17,'text-anchor':'middle',fill:'var(--muted)','font-size':10});
        t.textContent=d.date.slice(5);svg.appendChild(t);
      }
    });
    svg.appendChild(el('line',{x1:M.l,x2:W-M.r,y1:M.t+ph,y2:M.t+ph,stroke:'var(--axis)','stroke-width':1}));
    svg.onpointerleave=()=>{tt.style.opacity=0;};
  }
  draw();
})();

(function(){
  const keys=D.runtimeLabels;
  let h='<thead><tr><th>Day</th>'+keys.map(k=>`<th class="num">${k.label}</th>`).join('')+
        '<th class="num">Redox avg</th><th class="num">pH avg</th><th class="num">Water °C</th>'+
        '<th class="num">Samples</th><th class="num">Dropouts</th></tr></thead><tbody>';
  [...days].reverse().forEach(d=>{
    const g=(o,k)=>d[o]&&d[o][k]!=null?d[o][k]:'–';
    h+=`<tr><td><a href="day-${d.date}.html">${d.date}</a></td>`+
       keys.map(k=>`<td class="num">${d.runtime[k.k]?dur(d.runtime[k.k]):'–'}</td>`).join('')+
       `<td class="num">${g('avg','redoxmedian')}</td><td class="num">${g('avg','phmedian')}</td>`+
       `<td class="num">${g('avg','temperature')}</td>`+
       `<td class="num">${d.samples.toLocaleString()}</td>`+
       `<td class="num" style="color:${d.dropouts?'var(--critical)':'var(--muted)'}">${d.dropouts||'–'}</td></tr>`;
  });
  document.getElementById('tbl').innerHTML=h+'</tbody>';
})();
</script>
</body>
</html>
"""

HTML_DAY = HTML_DAY.replace("__CSS__", SHARED_CSS).replace("__JS__", SHARED_JS)
HTML_INDEX = HTML_INDEX.replace("__CSS__", SHARED_CSS).replace("__JS__", SHARED_JS)


if __name__ == "__main__":
    main()

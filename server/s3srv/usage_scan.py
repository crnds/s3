"""Claude Code usage: tail ~/.claude/projects/*/*.jsonl into 5-minute
buckets, roll them up for the report, and the per-model price table."""

import glob
import json
import os
import time
from datetime import datetime, timedelta, timezone

from s3srv.common import log_err
from s3srv.state import STATE, activity_gated_loop

LOG_GLOB = os.path.expanduser("~/.claude/projects/*/*.jsonl")
# 30s on AC: token pages update from board polls of cached aggregates, so the
# ingest lag is invisible on screen, and this halves the scan wakeups + disk
# glob/stat over ~200 jsonl files vs. the old 15s — the biggest wakeup source.
SCAN_INTERVAL_SEC = 30
RETENTION = timedelta(days=8)

# Raw per-message events are aggregated into fixed-width time buckets rather
# than kept individually — a bucket's key space is bounded by RETENTION (~2300
# buckets at 8 days), whereas the old per-event list grew without bound as
# usage volume grew, and build_report() re-scanned the whole thing on every
# board poll. 5 minutes is far finer than anything displayed (day/hour
# granularity), so bucket-boundary rounding is invisible on screen.
BUCKET_SEC = 300

# Approximate $/M tokens by model family. Not exact billing — cache_creation
# and cache_read are rough multipliers of the input rate.
PRICING = {
    "opus": {"input": 15.0, "output": 75.0},
    "sonnet": {"input": 3.0, "output": 15.0},
    "haiku": {"input": 0.8, "output": 4.0},
    "fable": {"input": 10.0, "output": 50.0},
}
DEFAULT_PRICING = PRICING["sonnet"]


def pricing_for_model(model):
    model = (model or "").lower()
    for key, rates in PRICING.items():
        if key in model:
            return rates
    return DEFAULT_PRICING


def estimate_cost(usage, model):
    rates = pricing_for_model(model)
    input_tok = usage.get("input_tokens", 0)
    output_tok = usage.get("output_tokens", 0)
    cache_creation = usage.get("cache_creation_input_tokens", 0)
    cache_read = usage.get("cache_read_input_tokens", 0)
    cost = (
        input_tok * rates["input"]
        + output_tok * rates["output"]
        + cache_creation * rates["input"] * 1.25
        + cache_read * rates["input"] * 0.1
    ) / 1_000_000
    return cost


def model_family(model):
    # "claude-opus-4-8" -> "opus", "claude-haiku-4-5-20251001" -> "haiku"
    if not model:
        return "other"
    m = model.lower()
    if m.startswith("claude-"):
        m = m[len("claude-"):]
    return m.split("-")[0] or "other"


def total_tokens(usage):
    return (
        usage.get("input_tokens", 0)
        + usage.get("output_tokens", 0)
        + usage.get("cache_creation_input_tokens", 0)
        + usage.get("cache_read_input_tokens", 0)
    )


def project_name(cwd):
    if not cwd:
        return "unknown"
    return os.path.basename(cwd.rstrip("/")) or cwd


def bucket_epoch_for(ts):
    return int(ts.timestamp() // BUCKET_SEC) * BUCKET_SEC


def parse_line(line):
    """One jsonl line (bytes or str) -> a usage event, or None.

    None for blank or malformed lines and for entries without usage (user
    turns, tool results). The event:
      ts          aware datetime (a naive stamp is taken as UTC)
      key         (message.id, requestId) for dedupe, or None without an id
      project, model, tokens, cost
      ctx_tokens  the prompt size (input + both cache tiers, no output)
    """
    line = line.strip()
    if not line:
        return None
    try:
        entry = json.loads(line)
    except ValueError:
        return None
    if not isinstance(entry, dict):
        return None
    message = entry.get("message") or {}
    if not isinstance(message, dict):
        return None
    usage = message.get("usage")
    if not usage or not isinstance(usage, dict):
        return None
    ts_raw = entry.get("timestamp")
    if not ts_raw or not isinstance(ts_raw, str):
        return None
    try:
        ts = datetime.fromisoformat(ts_raw.replace("Z", "+00:00"))
    except ValueError:
        return None
    if ts.tzinfo is None:
        # A naive ts here would later raise TypeError when compared
        # against the aware `cutoff`/`now` in commit_batch -- and by then this
        # file's offset has already advanced, permanently dropping
        # these events instead of retrying them next scan.
        ts = ts.replace(tzinfo=timezone.utc)
    msg_id = message.get("id")
    return {
        "ts": ts,
        "key": (msg_id, entry.get("requestId")) if msg_id else None,
        "project": project_name(entry.get("cwd")),
        "model": model_family(message.get("model")),
        "tokens": total_tokens(usage),
        "cost": estimate_cost(usage, message.get("model")),
        # Context window of the latest session = the newest assistant
        # event's prompt size (input + both cache tiers, no output).
        "ctx_tokens": ((usage.get("input_tokens") or 0)
                       + (usage.get("cache_read_input_tokens") or 0)
                       + (usage.get("cache_creation_input_tokens") or 0)),
    }


def new_batch():
    # buckets: bucket_epoch -> {(project, model): {tokens, cost}}. Bucketed
    # at ingest time (rather than keeping a growing list of raw events) so
    # memory and the per-request aggregation work are bounded by
    # RETENTION, not by how much has been used.
    # newest_ctx: {tokens, ts} of the newest event in this batch.
    # newest_ts: exact ts of the newest event (bucketing would blur
    # active_now's 180s window otherwise).
    return {"buckets": {}, "newest_ctx": None, "newest_ts": None}


def add_event(batch, ev):
    dims = batch["buckets"].setdefault(bucket_epoch_for(ev["ts"]), {})
    d = dims.setdefault((ev["project"], ev["model"]), {"tokens": 0, "cost": 0.0})
    d["tokens"] += ev["tokens"]
    d["cost"] += ev["cost"]
    if batch["newest_ctx"] is None or ev["ts"] > batch["newest_ctx"]["ts"]:
        batch["newest_ctx"] = {"tokens": ev["ctx_tokens"], "ts": ev["ts"]}
    if batch["newest_ts"] is None or ev["ts"] > batch["newest_ts"]:
        batch["newest_ts"] = ev["ts"]


def ingest_lines(lines, seen, batch):
    """Parse, dedupe and bucket lines into batch.

    The same assistant message is often written several times while
    streaming, with identical usage, so events are deduped on
    (message.id, requestId), keep-first, against `seen` (key -> ts, kept
    across scans for RETENTION). Events without an id are never deduped."""
    for raw in lines:
        ev = parse_line(raw)
        if ev is None:
            continue
        if ev["key"] is not None:
            if ev["key"] in seen:
                continue
            seen[ev["key"]] = ev["ts"]
        add_event(batch, ev)


def merge_buckets(target, new_buckets):
    """Bucket insert: add new_buckets' sums into target, in place."""
    for bucket_epoch, dims in new_buckets.items():
        slot = target.setdefault(bucket_epoch, {})
        for key, v in dims.items():
            d = slot.setdefault(key, {"tokens": 0, "cost": 0.0})
            d["tokens"] += v["tokens"]
            d["cost"] += v["cost"]
    return target


def commit_batch(batch, now=None):
    """Merge a scan's batch into STATE under the lock and drop everything
    older than RETENTION."""
    cutoff = (now or datetime.now(timezone.utc)) - RETENTION
    cutoff_epoch = bucket_epoch_for(cutoff)
    newest_ctx, newest_ts = batch["newest_ctx"], batch["newest_ts"]
    with STATE["lock"]:
        merge_buckets(STATE["buckets"], batch["buckets"])
        STATE["buckets"] = {b: v for b, v in STATE["buckets"].items() if b >= cutoff_epoch}
        STATE["seen"] = {k: ts for k, ts in STATE["seen"].items() if ts >= cutoff}
        prev = STATE["context"]
        if newest_ctx and (prev is None or newest_ctx["ts"] > prev["ts"]):
            STATE["context"] = newest_ctx
        if newest_ts and (STATE["last_ts"] is None or newest_ts > STATE["last_ts"]):
            STATE["last_ts"] = newest_ts


def scan_once():
    """Tail every project log from its saved byte offset. Returns True when
    anything new was ingested (scan_loop skips the rollup otherwise)."""
    batch = new_batch()
    # offsets and seen are touched only by the scan thread (and main() before
    # it starts), so they are read and advanced here without the lock.
    offsets = STATE["offsets"]
    current_paths = set(glob.glob(LOG_GLOB))
    for path in current_paths:
        offset = offsets.get(path)
        if offset is None:
            try:
                mtime = os.path.getmtime(path)
                size = os.path.getsize(path)
            except OSError:
                continue
            if mtime < time.time() - RETENTION.total_seconds():
                offsets[path] = size
                continue
            offset = 0
        else:
            try:
                size = os.path.getsize(path)
            except OSError:
                continue
            if size < offset:
                offset = 0  # file was truncated/replaced; re-read from the start

        if size == offset:
            continue
        try:
            with open(path, "rb") as f:
                f.seek(offset)
                data = f.read()
        except OSError:
            continue
        # Only advance past lines that end in a newline — a line still being
        # written when we read it would otherwise be skipped forever, since
        # the offset would jump past the partial bytes before they're complete.
        consumable = data[:data.rfind(b"\n") + 1]
        ingest_lines(consumable.split(b"\n"), STATE["seen"], batch)
        offsets[path] = offset + len(consumable)

    # Drop offsets for paths that no longer exist (deleted/rotated project
    # logs) -- otherwise this dict grows for the life of the process. Safe to
    # re-read from scratch if a path reappears: "seen" dedup (keyed on
    # message id + requestId, retained for RETENTION) still catches re-ingested
    # duplicates.
    for path in set(offsets) - current_paths:
        del offsets[path]

    # Returned to scan_loop so it can skip the compute_aggregates() rollup on
    # scans that ingested nothing (most scans, when no Claude session is
    # active) -- see its day-rollover note.
    if not batch["buckets"]:
        return False
    commit_batch(batch)
    return True


# Logged hourly from scan_loop: a lightweight heartbeat of the bucketed
# state's size, so a runaway (e.g. buckets not pruning correctly) shows up in
# /tmp/s3usage.err rather than silently growing the process's RSS.
STATS_LOG_INTERVAL_SEC = 3600
_last_stats_log = 0.0


def maybe_log_stats():
    global _last_stats_log
    now = time.time()
    if now - _last_stats_log < STATS_LOG_INTERVAL_SEC:
        return
    _last_stats_log = now
    with STATE["lock"]:
        n_buckets = len(STATE["buckets"])
        n_seen = len(STATE["seen"])
        n_clients = len(STATE["clients"])
    log_err(f"stats: buckets={n_buckets} seen={n_seen} clients={n_clients}")


def compute_aggregates():
    # Rolls the bucketed state up into everything build_report() needs that
    # doesn't have to be fresh on every single request (today/week totals,
    # top projects, 7-day trend). Called once per scan cycle (~30s) instead
    # of once per HTTP request. last5h / models[] were dropped — no board,
    # simulator, or control-panel consumer.
    now = datetime.now(timezone.utc)
    local_today = datetime.now().astimezone().date()

    with STATE["lock"]:
        buckets = list(STATE["buckets"].items())

    today_tokens = today_cost = 0
    week_tokens = week_cost = 0
    project_totals = {}
    day_totals = {}

    seven_d_ago = now - timedelta(days=7)

    for bucket_epoch, dims in buckets:
        bucket_dt = datetime.fromtimestamp(bucket_epoch, tz=timezone.utc)
        bucket_tokens = sum(v["tokens"] for v in dims.values())
        bucket_cost = sum(v["cost"] for v in dims.values())
        local_date = bucket_dt.astimezone().date()

        if local_date == local_today:
            today_tokens += bucket_tokens
            today_cost += bucket_cost

        if bucket_dt >= seven_d_ago:
            week_tokens += bucket_tokens
            week_cost += bucket_cost
            for (project, _model), v in dims.items():
                project_totals[project] = project_totals.get(project, 0) + v["tokens"]
            day_totals[local_date] = day_totals.get(local_date, 0) + bucket_tokens

    top_projects = sorted(project_totals.items(), key=lambda kv: kv[1], reverse=True)[:5]

    trend = []
    for i in range(6, -1, -1):
        d = local_today - timedelta(days=i)
        trend.append(day_totals.get(d, 0))

    aggregates = {
        "today": {"tokens": today_tokens, "cost": round(today_cost, 4)},
        "week": {"tokens": week_tokens, "cost": round(week_cost, 4)},
        "projects": [{"name": name, "tokens": tokens} for name, tokens in top_projects],
        "trend": trend,
    }
    with STATE["lock"]:
        STATE["aggregates"] = aggregates


def scan_loop():
    # Recompute the rollup only when scan_once() ingested new data, OR when the
    # local calendar day has rolled over (so today/trend advance at midnight even
    # through a long idle stretch with no new events -- a plain "new data only"
    # gate would otherwise freeze "today" at yesterday's total until the next
    # event). This skips the every-scan compute_aggregates() during idle, which
    # was pure repeated work re-deriving identical output.
    last_agg_date = datetime.now().astimezone().date()

    def body():
        nonlocal last_agg_date
        try:
            changed = scan_once()
            today = datetime.now().astimezone().date()
            if changed or today != last_agg_date:
                compute_aggregates()
                last_agg_date = today
            maybe_log_stats()
        except Exception as e:
            # never let a transient error (e.g. during sleep/wake) kill the thread
            log_err(f"scan_loop: {type(e).__name__}: {e}")
        time.sleep(SCAN_INTERVAL_SEC)

    activity_gated_loop(body)

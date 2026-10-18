#!/usr/bin/env python3
"""GlideMix Trainer: retrains the GlideMix Phrase Detector overnight on a
library that rekordbox has already phrase-analysed, and installs the new
model only if it beats the current one on tracks it never saw.

    glidemix_trainer.py phrases --folders DIR [DIR ...] --level low|med|high
    glidemix_trainer.py fingerprints --folders DIR [DIR ...] --level low|med|high

Steps of `phrases` (each resumable; a stopped run continues where it left off):
  1. labels   rekordbox's phrases for the tracks in the folders, read from its
              analysis files (~/Library/Pioneer/rekordbox/share/PIONEER/USBANLZ).
              Read-only: nothing of rekordbox's, and no music file, is changed.
  2. collect  the app's own analysis of each labelled track (glidemix-analyze),
              so the model learns exactly the numbers GlideMix computes.
  3. train    candidate models on all folders but the test and check folders;
              the check folders pick the best candidate.
  4. test     the best candidate against the installed model on the test
              folders (about one folder in ten, never trained on).
  5. install  only if it wins: ~/Library/Application Support/Glidemix/models/
              glidemix_phrases.onnx (the app looks there first). The previous
              file is kept beside it.

Everything else lives in ~/Library/Application Support/GlideMix Trainer/.
"""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import random
import shutil
import subprocess
import sys
import time
import unicodedata

import numpy as np

WORK = pathlib.Path.home() / "Library/Application Support/GlideMix Trainer"
APP_MODELS = pathlib.Path.home() / "Library/Application Support/Glidemix/models"
ANLZ = pathlib.Path.home() / "Library/Pioneer/rekordbox/share/PIONEER/USBANLZ"
REPO = pathlib.Path(__file__).resolve().parents[2]
ANALYZE = REPO / "build/src/mixhouse/glidemix-analyze"
AUDIO = (".mp3", ".m4a", ".mp4", ".aac", ".wav", ".flac", ".aif", ".aiff", ".ogg", ".opus")

# Resource levels: analysis threads, training threads, process priority.
LEVELS = {
    "low": dict(analyze=2, train=2, nice=15),
    "med": dict(analyze=4, train=4, nice=10),
    "high": dict(analyze=8, train=8, nice=5),
}

# Keep in step with the app (mixhousephrasemodel.cpp): 4 beats x 3 bands, their
# mean, the built-in detector's phrase (5), position (2), tempo (1).
FEATURES = 12 + 3 + 5 + 2 + 1
ROLE = {"Intro": 0, "Up": 1, "Verse": 1, "Down": 2, "Bridge": 2, "Chorus": 3, "Outro": 4}
BUILTIN = {"Intro": 0, "Up": 1, "Down": 2, "Chorus": 3, "Outro": 4}

nfc = lambda s: unicodedata.normalize("NFC", s)


def log(msg):
    line = f"{datetime.datetime.now():%H:%M:%S}  {msg}"
    print(line, flush=True)
    with open(WORK / "trainer.log", "a", encoding="utf-8") as f:
        f.write(line + "\n")


def split_of(folder):
    """Fixed per folder, so a folder's tracks are never in two splits: about
    1 in 10 folders test, 1 in 20 check, the rest train."""
    h = int(hashlib.sha1(folder.encode()).hexdigest(), 16) % 20
    return "test" if h < 2 else "check" if h == 2 else "train"


def folder_of(path, roots):
    for r in roots:
        if path.startswith(r.rstrip("/") + "/"):
            rest = path[len(r.rstrip("/")) + 1:]
            return r + "/" + rest.split("/")[0] if "/" in rest else r
    return os.path.dirname(path)


# ---------------------------------------------------------------- 1. labels

def rekordbox_label(mood, kind):
    if mood == 1:
        return {1: "Intro", 2: "Up", 3: "Down", 5: "Chorus", 6: "Outro"}.get(kind)
    return {1: "Intro", 8: "Bridge", 9: "Chorus", 10: "Outro"}.get(kind, "Verse" if 2 <= kind <= 7 else None)


def parse_anlz(dat):
    """One rekordbox analysis: the track's path, mood and phrases (or None)."""
    from pyrekordbox.anlz import AnlzFile

    ext = dat[:-3] + "EXT"
    try:
        d = AnlzFile.parse_file(dat)
        path = nfc(d.get_tag("PPTH").content.path)
        e = AnlzFile.parse_file(ext)
        beats = [(b.time / 1000.0, b.beat) for b in d.get_tag("PQTZ").content.entries]
        pssi = e.get_tag("PSSI").content
    except Exception:
        return None
    if len(beats) < 8:
        return None

    def beat_time(n):
        if n < 1:
            return beats[0][0]
        if n <= len(beats):
            return beats[n - 1][0]
        return beats[-1][0] + (n - len(beats)) * (beats[-1][0] - beats[-2][0])

    ents = pssi.entries
    phrases = []
    for i, en in enumerate(ents):
        nxt = ents[i + 1].beat if i + 1 < len(ents) else pssi.end_beat
        lab = rekordbox_label(pssi.mood, en.kind)
        if lab and nxt > en.beat:
            phrases.append([beat_time(en.beat), beat_time(nxt), lab])
    return {"path": path, "mood": pssi.mood, "phrases": phrases} if phrases else None


def step_labels(roots, level):
    """rekordbox's phrases for the tracks in the folders. The parsed analyses
    are cached by file date, so only new or re-analysed tracks cost time."""
    import multiprocessing as mp

    tracks = {}
    for root in roots:
        for d, _, files in os.walk(root):
            for f in files:
                if f.lower().endswith(AUDIO) and not f.startswith("._"):
                    tracks[nfc(os.path.join(d, f))] = None
    log(f"labels: {len(tracks)} audio files in the chosen folders")
    by_name = {}
    for p in tracks:
        by_name.setdefault(os.path.basename(p), []).append(p)

    cache_file = WORK / "anlz-cache.json"
    cache = json.loads(cache_file.read_text()) if cache_file.exists() else {}
    dats = {}
    for dat in ANLZ.glob("*/*/ANLZ0000.DAT"):
        if dat.with_suffix(".EXT").exists():
            dats[str(dat)] = max(dat.stat().st_mtime, dat.with_suffix(".EXT").stat().st_mtime)
    todo = [d for d, m in dats.items() if cache.get(d, {}).get("mtime") != m]
    log(f"labels: {len(dats)} rekordbox analyses, {len(todo)} new or changed to read")
    if todo:
        with mp.get_context("spawn").Pool(LEVELS[level]["analyze"]) as pool:
            for i, (d, r) in enumerate(zip(todo, pool.imap(parse_anlz, todo, chunksize=64)), 1):
                cache[d] = {"mtime": dats[d], "label": r}
                if i % 5000 == 0:
                    log(f"labels: read {i}/{len(todo)}")
        cache = {d: v for d, v in cache.items() if d in dats}
        cache_file.write_text(json.dumps(cache))

    labels = {}
    for d, v in cache.items():
        r = v["label"]
        if not r:
            continue
        target = r["path"] if r["path"] in tracks else None
        if target is None:
            cands = by_name.get(os.path.basename(r["path"]), [])
            target = cands[0] if len(cands) == 1 else None
        if target is None:
            continue
        old = labels.get(target)
        if old is None or v["mtime"] > old["mtime"]:
            labels[target] = {"mtime": v["mtime"], "mood": r["mood"], "phrases": r["phrases"]}
    (WORK / "labels.json").write_text(json.dumps(labels))
    log(f"labels: {len(labels)} tracks with rekordbox phrases")
    return labels


# --------------------------------------------------------------- 2. collect

def step_collect(labels, level):
    feats = WORK / "features.jsonl"
    todo = WORK / "collect-list.txt"
    todo.write_text("\n".join(sorted(labels)) + "\n", encoding="utf-8")
    if not ANALYZE.exists():
        sys.exit(f"missing {ANALYZE}: build the dev app first (ninja -C build glidemix-analyze)")
    log(f"collect: GlideMix's analysis of {len(labels)} tracks ({LEVELS[level]['analyze']} at a time); "
        "tracks done in an earlier run are skipped")
    cmd = ["nice", "-n", str(LEVELS[level]["nice"]), str(ANALYZE), str(todo), str(feats),
           str(LEVELS[level]["analyze"]), "--no-key"]
    start = time.time()
    with subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True) as p:
        last = 0
        for line in p.stdout:
            done, _, total = line.strip().partition("/")
            if not (done.isdigit() and total.isdigit()):
                continue   # the analyser's own log lines
            if time.time() - last > 60 or done == total:
                last = time.time()
                rate = int(done) / max(1, time.time() - start) * 3600
                left = (int(total) - int(done)) / max(1e-6, rate)
                log(f"collect: {done}/{total}  ({rate:.0f} tracks/hour, about {left:.1f} h left)")
    if p.returncode:
        sys.exit(f"collect stopped (exit {p.returncode}); run again to continue")


def load_features(labels, roots):
    data = []
    with open(WORK / "features.jsonl", encoding="utf-8") as f:
        for line in f:
            t = json.loads(line)
            path = nfc(t["path"])
            tr = labels.get(path)
            if not tr or not t.get("valid") or t["bpm"] <= 0:
                continue
            X, T = bar_features(t)
            if X is None:
                continue
            period = 60.0 / t["bpm"]
            ph = tr["phrases"]
            Y = np.array([ROLE.get(next((p[2] for p in ph if p[0] <= tm + 2 * period < p[1]), None), -1) for tm in T])
            B = np.array([int(any(abs(p[0] - tm) < 2 * period for p in ph[1:])) for tm in T])
            folder = os.path.dirname(path)   # the track's own folder: an album, a playlist
            data.append(dict(path=path, X=X, Y=Y, B=B, T=T, mood=tr["mood"] - 1, folder=folder,
                             split=None, bpm=t["bpm"], downbeat=t["downbeat"],
                             duration=t["duration"], phrases=ph))
    assign_splits(data)
    return data


def assign_splits(data):
    """About 1 track in 10 to test on and 1 in 20 to choose with, never trained
    on. By folder when there are enough folders (so a test never hears a close
    relative of a training track), else track by track -- a few chosen folders
    must still give a real test."""
    folders = {t["folder"] for t in data}
    by_folder = len(folders) >= 40
    for t in data:
        t["split"] = split_of(t["folder"] if by_folder else t["path"])
    counts = {k: sum(t["split"] == k for t in data) for k in ("train", "check", "test")}
    if by_folder and (counts["test"] < 0.05 * len(data) or counts["check"] == 0):
        for t in data:
            t["split"] = split_of(t["path"])
        by_folder = False
    log(f"split {'by folder' if by_folder else 'by track'} ({len(folders)} folders): "
        + ", ".join(f"{k} {sum(t['split'] == k for t in data)}" for k in ("train", "check", "test")))


def bar_features(t):
    """The app's inputs for one track: a row per bar, and the bar start times
    (exactly as phraseModelInput() in mixhousephrasemodel.cpp builds them)."""
    bpm = t["bpm"]
    e = np.array(t["energy"], np.float32).reshape(-1, 3)
    if bpm <= 0 or len(e) < 64:
        return None, None
    period = 60.0 / bpm
    t0 = t["firstBeat"] - np.floor(t["firstBeat"] / period) * period
    phase = int(round((t["downbeat"] - t0) / period)) % 4
    starts = list(range(phase, len(e) - 3, 4))
    ph = [(a, b, BUILTIN.get(l.split(" ")[0], -1)) for a, b, l in t["phrases"]]
    rows, times = [], []
    n = len(starts)
    for i, s in enumerate(starts):
        eb = e[s:s + 4]
        tm = t0 + s * period
        today = np.zeros(5, np.float32)
        r = next((r for a, b, r in ph if a <= tm + 2 * period < b), -1)
        if r >= 0:
            today[r] = 1
        pos = i / max(1, n - 1)
        rows.append(np.concatenate([eb.ravel(), eb.mean(0), today, [pos, 1 - pos], [(bpm - 98.0) / 98.0]]))
        times.append(tm)
    if len(rows) < 16:
        return None, None
    return np.array(rows, np.float32), np.array(times)


# ----------------------------------------------------------------- 3. train

def make_net(d, hidden):
    import torch.nn as nn

    class Net(nn.Module):
        def __init__(s):
            super().__init__()
            s.inp = nn.Sequential(nn.Linear(d, 128), nn.GELU(), nn.Dropout(0.2))
            s.rnn = nn.GRU(128, hidden, num_layers=2, batch_first=True, bidirectional=True, dropout=0.2)
            s.role = nn.Linear(2 * hidden, 5)
            s.bound = nn.Linear(2 * hidden, 1)
            s.mood = nn.Linear(2 * hidden, 3)

        def forward(s, x):
            h, _ = s.rnn(s.inp(x))
            return s.role(h), s.bound(h).squeeze(-1), s.mood(h.mean(1))

    return Net()


def train(tr, hidden, epochs, threads):
    import torch
    import torch.nn as nn

    torch.manual_seed(0)
    random.seed(0)
    torch.set_num_threads(threads)
    net = make_net(FEATURES, hidden)
    opt = torch.optim.AdamW(net.parameters(), 2e-3, weight_decay=1e-2)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, epochs)
    ce = nn.CrossEntropyLoss(ignore_index=-1)
    for ep in range(epochs):
        tr = sorted(tr, key=lambda t: len(t["X"]) + random.random() * 40)
        order = [tr[i:i + 16] for i in range(0, len(tr), 16)]
        random.shuffle(order)
        net.train()
        for chunk in order:
            L = max(len(t["X"]) for t in chunk)
            X = np.zeros((len(chunk), L, FEATURES), np.float32)
            Y = np.full((len(chunk), L), -1)
            B = np.zeros((len(chunk), L), np.float32)
            M = np.zeros((len(chunk), L), np.float32)
            for j, t in enumerate(chunk):
                n = len(t["X"])
                X[j, :n], Y[j, :n], B[j, :n], M[j, :n] = t["X"], t["Y"], t["B"], 1
            X, Y, B, M = map(torch.tensor, (X, Y, B, M))
            mood = torch.tensor([t["mood"] for t in chunk])
            r, b, m = net(X)
            lb = nn.functional.binary_cross_entropy_with_logits(b, B, pos_weight=torch.tensor(3.0), reduction="none")
            loss = ce(r.reshape(-1, 5), Y.reshape(-1)) + (lb * M).sum() / M.sum() + 0.3 * ce(m, mood)
            opt.zero_grad()
            loss.backward()
            opt.step()
        sched.step()
        if (ep + 1) % 5 == 0:
            log(f"train: hidden {hidden}, epoch {ep + 1}/{epochs}")
    net.eval()
    return net


# ----------------------------------------------------- 4. test (as the app)

def torch_runner(net):
    import torch

    def run(X):
        with torch.no_grad():
            r, b, m = net(torch.tensor(X)[None])
        return r[0].numpy(), b[0].numpy(), m[0].numpy()
    return run


def onnx_runner(path):
    import onnxruntime as ort

    s = ort.InferenceSession(str(path))

    def run(X):
        r, b, m = s.run(None, {"bars": X[None]})
        return r[0], b[0], m[0]
    return run


def installed_model():
    """The model the app uses now: the user's copy first, as the app looks."""
    user = APP_MODELS / "glidemix_phrases.onnx"
    return user if user.exists() else REPO / "src/mixhouse/models/glidemix_phrases.onnx"


def model_phrases(run, t):
    """A model's phrases for one track, cut and named exactly as the app does
    it (modelPhrases() in mixhousephrasemodel.cpp)."""
    logits, bound, m = run(t["X"])
    mood = int(np.argmax(m))
    n = len(bound)
    cuts = [0]
    for i in range(1, n):
        p = 1 / (1 + np.exp(-bound[i]))
        peak = bound[i] >= bound[i - 1] and (i + 1 >= n or bound[i] >= bound[i + 1])
        if p > 0.5 and peak:
            cuts.append(i)
    cuts.append(n)
    high = ["Intro", "Up", "Down", "Chorus", "Outro"]
    other = ["Intro", "Verse", "Bridge", "Chorus", "Outro"]
    probs = np.exp(logits - logits.max(1, keepdims=True))
    probs /= probs.sum(1, keepdims=True)
    out, counts = [], [0] * 5
    for a, c in zip(cuts, cuts[1:]):
        role = int(probs[a:c].sum(0).argmax())
        start = 0.0 if a == 0 else float(t["T"][a])
        end = float(t["T"][c]) if c < n else t["duration"]
        top = (3 if role == 1 else 2 if role == 3 else 0 if role == 2 else 1) if mood == 0 else (2 if role == 1 else 0)
        counts[role] += 1
        name = (high if mood == 0 else other)[role]
        out.append([start, end, f"{name} {min(counts[role], top)}" if top else name])
    return out


def f1(ref, est, tol):
    used, hit = set(), 0
    for r in ref:
        c = min(((abs(e - r), i) for i, e in enumerate(est) if i not in used), default=None)
        if c and c[0] <= tol:
            used.add(c[1])
            hit += 1
    p = hit / len(est) if est else 0
    q = hit / len(ref) if ref else 0
    return 2 * p * q / (p + q) if p + q else 0


def score(tracks, phrases_of):
    """Names agree (per bar) and phrase starts within 0.5 s / 3 s, against
    rekordbox -- the same measure as every number reported so far."""
    res = []
    for t in tracks:
        ph = phrases_of(t)
        if not ph:
            continue
        bar = 240.0 / t["bpm"]
        dur = t["phrases"][-1][1]
        times = np.arange(t["downbeat"] % bar, dur, bar)
        ref_role = [ROLE.get(next((p[2] for p in t["phrases"] if p[0] <= tm + bar / 2 < p[1]), None), -1) for tm in times]
        est = [ROLE.get(next((l.split(" ")[0] for a, b, l in ph if a <= tm + bar / 2 < b), ""), -1) for tm in times]
        ok = [e == r for e, r in zip(est, ref_role) if r >= 0]
        ref_b = [p[0] for p in t["phrases"] if 1 < p[0] < dur - 1]
        est_b = [a for a, b, l in ph if 1 < a < dur - 1]
        res.append((np.mean(ok) if ok else 0, f1(ref_b, est_b, 0.5), f1(ref_b, est_b, 3.0)))
    v = np.array(res) if res else np.zeros((1, 3))
    return dict(tracks=len(res), names=float(v[:, 0].mean()), starts05=float(v[:, 1].mean()),
                starts3=float(v[:, 2].mean()))


def fmt(s):
    return (f"{s['tracks']} tracks | names agree {s['names']:.1%} | phrase starts "
            f"{s['starts05']:.1%} (0.5 s), {s['starts3']:.1%} (3 s)")


def better(a, b):
    """a beats b: phrase starts (0.5 s) and names together, as one number."""
    return a["starts05"] + a["names"] > b["starts05"] + b["names"] + 0.005


def export(net, path):
    import onnxruntime as ort
    import torch

    x = torch.zeros(1, 64, FEATURES)
    torch.onnx.export(net, (x,), str(path), input_names=["bars"], output_names=["role", "boundary", "mood"],
                      dynamic_axes={"bars": {1: "n"}, "role": {1: "n"}, "boundary": {1: "n"}},
                      opset_version=17, dynamo=False)
    s = ort.InferenceSession(str(path))
    y = np.random.default_rng(0).normal(size=(1, 80, FEATURES)).astype(np.float32)
    ok = np.allclose(s.run(None, {"bars": y})[0], net(torch.tensor(y))[0].detach().numpy(), atol=1e-4)
    if not ok:
        sys.exit("exported model does not match: not installing")


def step_train_test_install(data, level, epochs, install=True):
    tr = [t for t in data if t["split"] == "train"]
    ck = [t for t in data if t["split"] == "check"]
    te = [t for t in data if t["split"] == "test"]
    log(f"train: {len(tr)} tracks to learn from, {len(ck)} to choose with, {len(te)} to test on")
    best = None
    for hidden in (96, 160):
        net = train(tr, hidden, epochs, LEVELS[level]["train"])
        s = score(ck, lambda t, run=torch_runner(net): model_phrases(run, t))
        log(f"check, hidden {hidden}: {fmt(s)}")
        if best is None or better(s, best[1]):
            best = (net, s, hidden)
    net, _, hidden = best
    new_run, old_run = torch_runner(net), onnx_runner(installed_model())
    new = score(te, lambda t: model_phrases(new_run, t))
    old = score(te, lambda t: model_phrases(old_run, t))
    log(f"test, installed model ({installed_model()}): {fmt(old)}")
    log(f"test, new model:       {fmt(new)}")
    stamp = f"{datetime.datetime.now():%Y-%m-%d-%H%M}"
    out = WORK / "models" / f"glidemix_phrases-{stamp}.onnx"
    out.parent.mkdir(parents=True, exist_ok=True)
    export(net, out)
    report = dict(date=stamp, hidden=hidden, tracks=len(data), installed=old, new=new, installed_new=False)
    if not install:
        log(f"install: skipped (--no-install); new model kept in {out.parent}")
    elif better(new, old):
        APP_MODELS.mkdir(parents=True, exist_ok=True)
        cur = APP_MODELS / "glidemix_phrases.onnx"
        if cur.exists():
            shutil.copy2(cur, APP_MODELS / f"glidemix_phrases-before-{stamp}.onnx")
        shutil.copy2(out, cur)
        report["installed_new"] = True
        log(f"install: the new model wins and is installed in {APP_MODELS}. Tracks get the new "
            "phrases when they are analysed again.")
    else:
        log(f"install: the new model is not better; the app keeps its model (new one kept in {out.parent})")
    (WORK / "reports").mkdir(exist_ok=True)
    (WORK / "reports" / f"phrases-{stamp}.json").write_text(json.dumps(report, indent=1))
    return report


# ------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("task", choices=["phrases", "fingerprints"])
    ap.add_argument("--folders", nargs="+", required=True)
    ap.add_argument("--level", choices=LEVELS, default="med")
    ap.add_argument("--epochs", type=int, default=30)
    ap.add_argument("--limit", type=int, default=0, help="only this many labelled tracks (a quick trial)")
    ap.add_argument("--no-install", action="store_true", help="test only; never touch the app's model")
    a = ap.parse_args()
    WORK.mkdir(parents=True, exist_ok=True)
    roots = [nfc(str(pathlib.Path(f).expanduser()).rstrip("/")) for f in a.folders]
    os.nice(LEVELS[a.level]["nice"])
    log(f"=== GlideMix Trainer: {a.task}, level {a.level}, folders: {', '.join(roots)}")
    if a.task == "phrases":
        labels = step_labels(roots, a.level)
        if a.limit:
            keep = sorted(labels)
            random.Random(0).shuffle(keep)
            labels = {p: labels[p] for p in keep[:a.limit]}
        step_collect(labels, a.level)
        data = load_features(labels, roots)
        log(f"collect: {len(data)} tracks usable for training")
        r = step_train_test_install(data, a.level, a.epochs, install=not a.no_install)
        log("=== done" + (": new model installed" if r["installed_new"] else ": app model unchanged"))
    else:
        import glidemix_fingerprints
        glidemix_fingerprints.run(roots, a.level, log, WORK)


if __name__ == "__main__":
    main()

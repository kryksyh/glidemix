"""GlideMix Trainer, fingerprints: a "sounds like" fingerprint of every track,
and of every few seconds of it, for similarity search, the map and smart
playlists later.

Model: Myna-Hybrid (MIT; Yonay et al. 2025, github.com/ghost-signal/myna,
huggingface.co/oriyonay/myna-hybrid), a 22M-parameter vision transformer on
mel spectrograms -- the model family Music Mapper uses. Each 3-second chunk of
a track gives 768 numbers (two 384-wide passes: square patches for timbre and
rhythm, full-height ones for harmony and key).

Stored in fingerprints.sqlite (resumable, one row per track):
  mean    float32[768]   the whole track
  blocks  float16[n,768] every BLOCK chunks (~6 s), so a phrase's fingerprint
                         is the mean of the blocks it covers
"""
import math
import multiprocessing as mp
import os
import pathlib
import sqlite3
import time
import unicodedata

import numpy as np

SR = 16000
N_FRAMES = 96          # Myna's chunk: 96 mel frames at hop 512 = 3.07 s
CHUNK_SEC = N_FRAMES * 512 / SR
BLOCK = 2              # chunks per stored block (~6 s)
AUDIO = (".mp3", ".m4a", ".mp4", ".aac", ".wav", ".flac", ".aif", ".aiff", ".ogg", ".opus")
WORKERS = {"low": 2, "med": 4, "high": 7}

nfc = lambda s: unicodedata.normalize("NFC", s)


def build_model(weights):
    """Myna-Hybrid, as myna.py on the Hub builds it (without transformers)."""
    import torch
    from einops import rearrange
    from einops.layers.torch import Rearrange
    from safetensors.torch import load_file
    from torch import nn

    def posemb(h, w, dim, temperature=10000):
        y, x = torch.meshgrid(torch.arange(h), torch.arange(w), indexing="ij")
        omega = 1.0 / (temperature ** (torch.arange(dim // 4) / (dim // 4 - 1)))
        y = y.flatten()[:, None] * omega[None, :]
        x = x.flatten()[:, None] * omega[None, :]
        return torch.cat((x.sin(), x.cos(), y.sin(), y.cos()), dim=1).float()

    class Attention(nn.Module):
        def __init__(s, dim, heads=6, dim_head=64):
            super().__init__()
            s.heads, s.scale = heads, dim_head ** -0.5
            s.norm = nn.LayerNorm(dim)
            s.to_qkv = nn.Linear(dim, heads * dim_head * 3, bias=False)
            s.to_out = nn.Linear(heads * dim_head, dim, bias=False)

        def forward(s, x):
            q, k, v = (rearrange(t, "b n (h d) -> b h n d", h=s.heads) for t in s.to_qkv(s.norm(x)).chunk(3, -1))
            a = torch.softmax(q @ k.transpose(-1, -2) * s.scale, -1)
            return s.to_out(rearrange(a @ v, "b h n d -> b n (h d)"))

    class FeedForward(nn.Module):
        def __init__(s, dim, hidden):
            super().__init__()
            s.net = nn.Sequential(nn.LayerNorm(dim), nn.Linear(dim, hidden), nn.GELU(), nn.Linear(hidden, dim))

        def forward(s, x):
            return s.net(x)

    class Transformer(nn.Module):
        def __init__(s, dim=384, depth=12):
            super().__init__()
            s.norm = nn.LayerNorm(dim)
            s.layers = nn.ModuleList([nn.ModuleList([Attention(dim), FeedForward(dim, 1536)]) for _ in range(depth)])

        def forward(s, x):
            for attn, ff in s.layers:
                x = attn(x) + x
                x = ff(x) + x
            return s.norm(x)

    def embedding(ph, pw, dim=384):
        return nn.Sequential(Rearrange("b c (h p1) (w p2) -> b (h w) (p1 p2 c)", p1=ph, p2=pw),
                             nn.LayerNorm(ph * pw), nn.Linear(ph * pw, dim), nn.LayerNorm(dim))

    class Myna(nn.Module):
        def __init__(s):
            super().__init__()
            s.to_patch_embedding = embedding(16, 16)
            s.to_patch_embedding_b = embedding(128, 2)
            s.pos = posemb(128 // 16, 4096 // 16, 384)
            s.pos_b = posemb(1, 4096 // 2, 384)
            s.transformer = Transformer()

        def one(s, spec, emb, pos):
            x = emb(spec)
            x = x + pos[:x.shape[1]]
            return s.transformer(x).mean(1)

        def forward(s, spec):   # (B, 128, 96) -> (B, 768)
            spec = spec.unsqueeze(1)
            return torch.cat((s.one(spec, s.to_patch_embedding, s.pos),
                              s.one(spec, s.to_patch_embedding_b, s.pos_b)), -1)

    net = Myna()
    missing, unexpected = net.load_state_dict(load_file(str(weights)), strict=False)
    if missing or unexpected:
        raise RuntimeError(f"Myna weights do not fit: missing {missing[:3]}, unexpected {unexpected[:3]}")
    return net.eval()


def decode(path):
    """Mono float32 at 16 kHz (PyAV, so every format GlideMix reads works)."""
    import av

    out = []
    with av.open(path) as c:
        s = c.streams.audio[0]
        rs = av.AudioResampler(format="flt", layout="mono", rate=SR)
        for frame in c.decode(s):
            for f in rs.resample(frame):
                out.append(f.to_ndarray().reshape(-1))
        for f in rs.resample(None):
            out.append(f.to_ndarray().reshape(-1))
    return np.concatenate(out) if out else np.zeros(0, np.float32)


_net = None
_mel = None


def _init(weights, threads):
    global _net, _mel
    import torch
    from nnAudio.features.mel import MelSpectrogram

    torch.set_num_threads(threads)
    try:
        os.nice(5)
    except OSError:
        pass   # already as low as it goes
    _net = build_model(weights)
    _mel = MelSpectrogram(sr=SR, n_mels=128, verbose=False)


def _fingerprint(path):
    import torch

    try:
        audio = decode(path)
        if len(audio) < SR * 10:
            return path, None, "shorter than 10 s"
        with torch.no_grad():
            ms = _mel(torch.from_numpy(audio)[None])            # (1, 128, frames)
            n = ms.shape[-1] // N_FRAMES
            chunks = ms[0, :, :n * N_FRAMES].reshape(128, n, N_FRAMES).permute(1, 0, 2)
            emb = torch.cat([_net(chunks[i:i + 32]) for i in range(0, n, 32)]).numpy()
        nb = math.ceil(n / BLOCK)
        blocks = np.stack([emb[i * BLOCK:(i + 1) * BLOCK].mean(0) for i in range(nb)])
        return path, (emb.mean(0).astype(np.float32), blocks.astype(np.float16), len(audio) / SR), None
    except Exception as e:  # a broken file must not stop the night
        return path, None, str(e)[:200]


def run(roots, level, log, work):
    weights = work / "myna-hybrid" / "model.safetensors"
    if not weights.exists():
        from huggingface_hub import snapshot_download
        log("fingerprints: downloading Myna-Hybrid (86 MB, MIT licence)")
        snapshot_download("oriyonay/myna-hybrid", local_dir=str(work / "myna-hybrid"))
    db = sqlite3.connect(work / "fingerprints.sqlite")
    db.execute("CREATE TABLE IF NOT EXISTS tracks (path TEXT PRIMARY KEY, mtime REAL, duration REAL, "
               "chunk_sec REAL, block INTEGER, mean BLOB, blocks BLOB, error TEXT)")
    have = dict(db.execute("SELECT path, mtime FROM tracks"))
    files = []
    for root in roots:
        for d, _, names in os.walk(root):
            for f in names:
                if f.lower().endswith(AUDIO) and not f.startswith("._"):
                    p = nfc(os.path.join(d, f))
                    if have.get(p) != os.path.getmtime(p):
                        files.append(p)
    files.sort()
    workers = WORKERS[level]
    log(f"fingerprints: {len(have)} done before, {len(files)} to go, {workers} at a time")
    start, done, failed = time.time(), 0, 0
    ctx = mp.get_context("spawn")
    with ctx.Pool(workers, initializer=_init, initargs=(str(weights), 1)) as pool:
        for path, res, err in pool.imap_unordered(_fingerprint, files, chunksize=4):
            done += 1
            if res is None:
                failed += 1
                db.execute("INSERT OR REPLACE INTO tracks (path, mtime, error) VALUES (?, ?, ?)",
                           (path, os.path.getmtime(path), err))
            else:
                mean, blocks, dur = res
                db.execute("INSERT OR REPLACE INTO tracks VALUES (?, ?, ?, ?, ?, ?, ?, NULL)",
                           (path, os.path.getmtime(path), dur, CHUNK_SEC, BLOCK, mean.tobytes(), blocks.tobytes()))
            if done % 50 == 0 or done == len(files):
                db.commit()
                rate = done / max(1, time.time() - start) * 3600
                log(f"fingerprints: {done}/{len(files)}  ({rate:.0f} tracks/hour, about "
                    f"{(len(files) - done) / max(1e-6, rate):.1f} h left, {failed} unreadable)")
    db.commit()
    neighbours_sample(db, log)


def neighbours_sample(db, log, n=5):
    """A sanity check in the log: for a few tracks, the five that sound most alike."""
    rows = db.execute("SELECT path, mean FROM tracks WHERE mean IS NOT NULL").fetchall()
    if len(rows) < 20:
        return
    paths = [r[0] for r in rows]
    M = np.stack([np.frombuffer(r[1], np.float32) for r in rows])
    M = M - M.mean(0)
    M /= np.linalg.norm(M, axis=1, keepdims=True) + 1e-9
    rng = np.random.default_rng(int(time.time()))
    for i in rng.choice(len(paths), size=min(n, len(paths)), replace=False):
        sims = M @ M[i]
        near = [j for j in np.argsort(-sims) if j != i][:5]
        log(f"sounds like {os.path.basename(paths[i])}:")
        for j in near:
            log(f"    {sims[j]:.2f}  {os.path.basename(paths[j])}")

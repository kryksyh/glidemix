# Fixed-size rotary tables: computed once in eager mode, then used as
# constants when tracing (Core ML can't convert the on-the-fly version).
import torch
import rotary_embedding_torch.rotary_embedding_torch as R
_cache = {}
def rotate_queries_or_keys(self, t, seq_dim=None, offset=0, scale=None):
    seq_dim = self.default_seq_dim if seq_dim is None else seq_dim
    n = int(t.shape[seq_dim])
    key = (id(self), n)
    if key not in _cache:
        with torch.no_grad():
            seq = torch.arange(n, dtype=torch.float32) + offset
            f = self.forward(seq, seq_len=n).detach().clone()
            _cache[key] = (f.cos(), f.sin(), int(f.shape[-1]))
    cos, sin, rot = _cache[key]
    d = int(t.shape[-1])
    mid = t[..., :rot]
    right = t[..., rot:]
    pairs = mid.reshape(*[int(v) for v in mid.shape[:-1]], rot // 2, 2)
    x1, x2 = pairs[..., 0], pairs[..., 1]
    half = torch.stack((-x2, x1), dim=-1).reshape(*[int(v) for v in mid.shape])
    out = mid * cos + half * sin
    return torch.cat((out, right), dim=-1) if rot < d else out
R.RotaryEmbedding.rotate_queries_or_keys = rotate_queries_or_keys

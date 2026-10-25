# Plain torch versions of the einops patterns Beat This! uses, so the model
# traces to fixed reshapes/permutes Core ML can convert. Same maths.
import torch
def _norm(p): return " ".join(p.replace("->", " -> ").split())
def rearrange(x, pattern, **kw):
    p = _norm(pattern)
    s = [int(v) for v in x.shape]
    if p == "b c f t -> b t (c f)":
        return x.permute(0, 3, 1, 2).reshape(s[0], s[3], s[1] * s[2])
    if p == "b t f -> b f t":
        return x.transpose(1, 2)
    if p == "b f t -> b 1 f t":
        return x.unsqueeze(1)
    if p == "b c f t -> (b t) f c":
        return x.permute(0, 3, 2, 1).reshape(s[0] * s[3], s[2], s[1])
    if p == "b c f t -> (b f) t c":
        return x.permute(0, 2, 3, 1).reshape(s[0] * s[2], s[3], s[1])
    if p == "(b t) f c -> b c f t":
        b = kw["b"]; return x.reshape(b, s[0] // b, s[1], s[2]).permute(0, 3, 2, 1)
    if p == "(b f) t c -> b c f t":
        b = kw["b"]; return x.reshape(b, s[0] // b, s[1], s[2]).permute(0, 3, 1, 2)
    if p == "(b t) f c -> (b f) t c":
        b = kw["b"]; t = s[0] // b
        return x.reshape(b, t, s[1], s[2]).permute(0, 2, 1, 3).reshape(b * s[1], t, s[2])
    if p == "b t c -> c b t":
        return x.permute(2, 0, 1)
    if p == "b n (qkv h d) -> qkv b h n d":
        qkv, h = kw["qkv"], kw["h"]; d = s[2] // (qkv * h)
        return x.reshape(s[0], s[1], qkv, h, d).permute(2, 0, 3, 1, 4)
    if p == "b n h -> b h n 1":
        return x.permute(0, 2, 1).unsqueeze(-1)
    if p == "b h n d -> b n (h d)":
        return x.permute(0, 2, 1, 3).reshape(s[0], s[2], s[1] * s[3])
    raise NotImplementedError(pattern)
class Rearrange(torch.nn.Module):
    def __init__(self, pattern, **kw): super().__init__(); self.pattern = pattern; self.kw = kw
    def forward(self, x): return rearrange(x, self.pattern, **self.kw)

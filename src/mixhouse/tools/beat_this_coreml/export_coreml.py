"""GlideMix: Beat This! (CPJKU, MIT) as a Core ML model for the Mac.

The full model (final0), 32-bit floats (16-bit moved beats), one 30-second
piece at a time: spect (1, 1500, 128) -> beat, downbeat (1, 1500) logits.
Run from a folder holding beat_this_src (github.com/CPJKU/beat_this),
einops_shim.py, rotary_patch.py and ckpt/final0.ckpt:
    python export_coreml.py final0 out/beat_this.mlpackage
then: xcrun coremlcompiler compile out/beat_this.mlpackage out/
"""
import sys
sys.path.insert(0, ".")
import rotary_patch  # noqa: F401  fixed-size rotary tables
sys.path.insert(0, "beat_this_src")
import torch
import coremltools as ct
from beat_this.inference import load_model

name, out = sys.argv[1], sys.argv[2]
model = load_model(f"ckpt/{name}.ckpt", device="cpu").eval()

class Wrap(torch.nn.Module):
    def __init__(self, m):
        super().__init__()
        self.m = m
    def forward(self, x):
        o = self.m(x)
        return o["beat"], o["downbeat"]

x = torch.randn(1, 1500, 128)
w = Wrap(model).eval()
with torch.no_grad():
    w(x)  # fills the rotary tables
    traced = torch.jit.trace(w, x)
ml = ct.convert(traced, inputs=[ct.TensorType(name="spect", shape=(1, 1500, 128))],
                outputs=[ct.TensorType(name="beat"), ct.TensorType(name="downbeat")],
                convert_to="mlprogram", compute_precision=ct.precision.FLOAT32,
                minimum_deployment_target=ct.target.macOS13)
ml.short_description = "Beat This! (CPJKU, MIT licence) beat and downbeat tracker, " + name
ml.save(out)

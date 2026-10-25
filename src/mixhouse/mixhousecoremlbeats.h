/*
 * GlideMix: Beat This! on the Mac's GPU through Core ML.
 *
 * The full model (CPJKU final0, 32-bit floats) converted with
 * tools/beat_this_coreml/export_coreml.py. Measured on an M1 Max: a 30 s
 * piece in 0.07 s (ONNX Runtime, one core: 2.1 s), beats identical to the
 * PyTorch model on every test song. 16-bit floats were faster but moved
 * beats, so they are not used.
 */
#pragma once

#include <QString>

namespace au::mixhouse {
//! Loads the compiled model (beat_this.mlmodelc) and hands Beat This! its
//! pieces from now on. False when Core ML or the model isn't there: ONNX
//! Runtime keeps doing it.
bool installCoreMlBeatThis(const QString& compiledModelPath);
}

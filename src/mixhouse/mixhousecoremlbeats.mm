/*
 * GlideMix
 */
#include "mixhousecoremlbeats.h"

#import <CoreML/CoreML.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <mutex>
#include <vector>

#include "beat_this_api.h"

#include "log.h"

namespace {
constexpr int kFrames = 1500;   // the model's fixed piece: 30 s at 50 frames/s
constexpr int kBins = 128;

MLModel* s_model = nil;
// One GPU: pieces from several songs take turns.
std::mutex s_mutex;

bool copyOut(MLMultiArray* array, int frames, float* out)
{
    if (!array || array.count < frames) {
        return false;
    }
    const NSInteger stride = array.strides.lastObject.integerValue;
    if (array.dataType == MLMultiArrayDataTypeFloat32) {
        const float* data = static_cast<const float*>(array.dataPointer);
        for (int i = 0; i < frames; ++i) {
            out[i] = data[i * stride];
        }
        return true;
    }
    for (int i = 0; i < frames; ++i) {
        out[i] = [array objectAtIndexedSubscript:i * stride].floatValue;
    }
    return true;
}

bool runPiece(const float* spect, int frames, int bins, float* beat, float* downbeat)
{
    if (bins != kBins || frames <= 0 || frames > kFrames) {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    @autoreleasepool {
        // Shorter pieces (a short song) are padded with silence; only their
        // own frames are read back.
        std::vector<float> input(static_cast<size_t>(kFrames) * kBins, 0.0f);
        std::copy(spect, spect + static_cast<size_t>(frames) * kBins, input.begin());
        NSError* error = nil;
        MLMultiArray* array = [[MLMultiArray alloc] initWithDataPointer:input.data()
                                                                  shape:@[ @1, @(kFrames), @(kBins) ]
                                                               dataType:MLMultiArrayDataTypeFloat32
                                                                strides:@[ @(kFrames * kBins), @(kBins), @1 ]
                                                            deallocator:nil
                                                                  error:&error];
        if (!array) {
            return false;
        }
        MLDictionaryFeatureProvider* features =
            [[MLDictionaryFeatureProvider alloc] initWithDictionary:@{ @"spect": [MLFeatureValue featureValueWithMultiArray:array] }
                                                              error:&error];
        id<MLFeatureProvider> result = features ? [s_model predictionFromFeatures:features error:&error] : nil;
        if (!result) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                LOGE() << "Beat This! Core ML prediction failed: "
                       << (error ? error.localizedDescription.UTF8String : "unknown") << " -- ONNX Runtime takes over";
            }
            return false;
        }
        return copyOut([result featureValueForName:@"beat"].multiArrayValue, frames, beat)
               && copyOut([result featureValueForName:@"downbeat"].multiArrayValue, frames, downbeat);
    }
}
}

bool au::mixhouse::installCoreMlBeatThis(const QString& compiledModelPath)
{
    if (s_model) {
        return true;
    }
    if (qEnvironmentVariableIsSet("GLIDEMIX_NO_COREML")) {
        return false;
    }
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:compiledModelPath.toNSString()];
        MLModelConfiguration* config = [[MLModelConfiguration alloc] init];
        // 32-bit floats on the GPU: the Neural Engine would compute in 16 bits.
        config.computeUnits = MLComputeUnitsCPUAndGPU;
        NSError* error = nil;
        MLModel* model = [MLModel modelWithContentsOfURL:url configuration:config error:&error];
        if (!model) {
            LOGW() << "Beat This! Core ML model not loaded (" << compiledModelPath.toStdString() << "): "
                   << (error ? error.localizedDescription.UTF8String : "unknown") << " -- ONNX Runtime is used";
            return false;
        }
        s_model = model;
    }
    BeatThis::setChunkRunner(runPiece);
    LOGI() << "Beat This!: Core ML (GPU), " << compiledModelPath.toStdString();
    return true;
}

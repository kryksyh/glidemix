// GlideMix: Mixxx's EngineObject bases, without QObject.
#pragma once

#include <cstddef>

#include "util/types.h"

class EngineObjectConstIn {
public:
    virtual ~EngineObjectConstIn() = default;
    virtual void process(const CSAMPLE* pIn, CSAMPLE* pOut, const std::size_t bufferSize) = 0;
};

/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

//! Non-owning view of the project times belonging to one processing block.
//! The explicit array takes precedence over a uniform start/step mapping.
struct ParameterAutomationTimes {
    const double* samples{};
    double start{};
    double step{};

    double At(size_t index) const noexcept
    {
        return samples ? samples[index] : start + static_cast<double>(index) * step;
    }
};

//! Immutable, plugin-independent input to realtime and offline automation.
//! Construct on the control thread. Evaluation performs no allocation, locking,
//! plugin calls, or access to mutable project objects.
class COMPONENTS_API ParameterAutomationSnapshot final
{
public:
    struct Point {
        double time{}; // Seconds relative to the untrimmed clip sequence start.
        double value{}; // Normalized [0, 1], never an amplitude envelope value.
    };

    struct Region {
        std::string parameterId;
        double start{}; // Visible project interval [start, end).
        double end{};
        double offset{}; // Project time of untrimmed sequence start.
        double defaultValue{};
        std::vector<Point> points;
        bool stepped{};   // hold each point's value until the next one
    };

    enum class Status {
        Inactive,
        Active,
        Conflict,
        InvalidTime
    };

    struct Value {
        Status status{Status::Inactive};
        double normalized{};
    };

    //! Throws invalid_argument for malformed data. Parameter IDs remain opaque;
    //! the plugin adapter resolves them once, before audio processing begins.
    explicit ParameterAutomationSnapshot(std::vector<Region> regions = {});

    size_t ParameterCount() const noexcept { return mParameters.size(); }
    const std::string& ParameterId(size_t index) const { return mParameters.at(index).id; }
    Value Evaluate(size_t parameter, double projectTime) const noexcept;

    //! Explicit times support loops, seeks, reverse playback and time warps.
    //! Each array must have at least count entries; null is allowed for count=0.
    void EvaluateBlock(size_t parameter, const double* projectTimes,
                       size_t count, Value* output) const noexcept;
    //! Constant-rate special case, also used by ordinary offline rendering.
    void EvaluateUniformBlock(size_t parameter, double start, double secondsPerSample,
                              size_t count, Value* output) const noexcept;

private:
    struct Parameter {
        std::string id;
        std::vector<Region> regions;
    };
    std::vector<Parameter> mParameters;
};

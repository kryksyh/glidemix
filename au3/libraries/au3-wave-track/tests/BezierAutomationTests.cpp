/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "au3-components/BezierAutomation.h"
#include "au3-components/ParameterAutomationSnapshot.h"
#include <cassert>
#include <iostream>
#include <thread>

using namespace clipautomation;
void near(double a, double b, double epsilon = 1e-6) { assert(std::abs(a - b) <= epsilon); }

int main()
{
    Curve curve;
    curve.segments.push_back({{0, .1}, {8, .9}, .8f});
    assert(curve.Valid());
    near(curve.Evaluate(0), .1); near(curve.Evaluate(8), .9);
    assert(curve.Evaluate(4) > .5);
    auto concave = curve; concave.segments[0].curvature = -.8f;
    assert(concave.Evaluate(4) < .5);
    auto linear = curve; linear.segments[0].curvature = 0;
    near(linear.Evaluate(4), .5);

    const auto split = curve.Slice(2, 6, true);
    for (int i = 0; i < 400; ++i) { near(split.Evaluate(i / 100.0), curve.Evaluate(2 + i / 100.0)); }
    auto stretch = curve; stretch.Stretch(2);
    near(stretch.Evaluate(8), curve.Evaluate(4));

    auto shelf = curve;
    shelf.BoxStep(2, 6, .05);
    assert(shelf.Valid());
    near(shelf.Evaluate(2), .05); near(shelf.Evaluate(6 - 1e-9), .05);
    near(shelf.Evaluate(6), curve.Evaluate(6));
    near(shelf.Evaluate(1), curve.Evaluate(1));
    near(shelf.Evaluate(7), curve.Evaluate(7));
    shelf.BoxStep(2, 6, .15);
    assert(shelf.Valid()); near(shelf.Evaluate(2), .15);

    Curve hold; hold.segments.push_back({{0, curve.Evaluate(4)}, {2, curve.Evaluate(4)}, 0});
    auto insert = curve; insert.Splice(4, 4, hold, 2);
    assert(insert.Valid()); near(insert.Evaluate(5), curve.Evaluate(4)); near(insert.Evaluate(9), curve.Evaluate(7));
    insert.Splice(4, 6, {}, 0);
    for (int i = 0; i < 800; ++i) { near(insert.Evaluate(i / 100.0), curve.Evaluate(i / 100.0)); }

    ParameterAutomationSnapshot::Region region;
    region.parameterId = "cutoff"; region.start = 10; region.end = 18; region.offset = 10; region.defaultValue = .1;
    for (const auto p : curve.Linearize()) { region.points.push_back({p.timeOffset, p.value}); }
    const ParameterAutomationSnapshot snapshot({region});
    for (int i = 0; i < 80000; ++i) { near(snapshot.Evaluate(0, 10 + i / 10000.0).normalized, curve.Evaluate(i / 10000.0), 1.0001e-5); }
    std::thread reader([&] { for (int i = 0; i < 10000; ++i) { assert(snapshot.Evaluate(0, 14).status == ParameterAutomationSnapshot::Status::Active); } });
    for (int i = 0; i < 10000; ++i) { assert(snapshot.Evaluate(0, 14).status == ParameterAutomationSnapshot::Status::Active); }
    reader.join();
    region.points.clear();
    for (const auto p : shelf.Linearize()) { region.points.push_back({p.timeOffset, p.value}); }
    const ParameterAutomationSnapshot steps({region});
    near(steps.Evaluate(0, 12).normalized, .15); near(steps.Evaluate(0, 16).normalized, curve.Evaluate(6));
    auto bad = curve; bad.segments[0].curvature = 2;
    assert(!bad.Valid());
    bool rejected = false; try { bad.Linearize(); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    std::cout << "PASS: Bezier math, exact splits, stretch, shelf edges, splice, ramp error bound, concurrent snapshot reads\n";
}

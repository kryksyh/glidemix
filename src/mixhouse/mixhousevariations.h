/*
 * GlideMix: "Create variations" on a clip's right-click (an idea from
 * Vibseek's menu, our own code). Four new sounds from one: a loop keeps its
 * length (so its tempo still fits) -- reversed, chopped, stuttered, darker;
 * a one-shot gets reversed, pitched up, pitched down and darker.
 */
#pragma once

#include <QString>
#include <QStringList>

namespace au::mixhouse {
/// Writes the variations of `sourcePath` into `folder` as 24-bit WAVs named
/// "<baseName> - <what>.wav". Returns the files written (empty on failure).
QStringList makeSoundVariations(const QString& sourcePath, const QString& folder, const QString& baseName, bool loop);
}

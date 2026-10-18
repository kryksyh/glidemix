/*
 * GlideMix
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "playback/iperformance.h"

namespace au::preferences {
/// The headphone cue output, on the Audio settings page.
///
/// It is also on the Perform button's own menu, which is where it belongs
/// during a set -- but somebody setting the machine up looks for outputs in
/// Preferences, so it lives in both places and reads the same setting either
/// way. Index 0 is outputs 3-4 of the audience device; the rest are separate
/// output devices.
class CueOutputPreferencesModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QStringList outputs READ outputs NOTIFY changed)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY changed)

    muse::ContextInject<playback::IPerformance> performance { this };

public:
    explicit CueOutputPreferencesModel(QObject* parent = nullptr);

    Q_INVOKABLE void init();

    QStringList outputs() const;
    int currentIndex() const;
    void setCurrentIndex(int index);

signals:
    void changed();

private:
    //! The devices behind `outputs`, index-aligned with it; the first is empty,
    //! meaning outputs 3-4 rather than a device of its own.
    std::vector<std::string> devices() const;
};
}

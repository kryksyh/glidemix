/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"

#include "uicomponents/qml/Muse/UiComponents/toolbaritem.h"

namespace au::playback {
class PlaybackToolBarBPMItem : public muse::uicomponents::ToolBarItem
{
    Q_OBJECT

    Q_PROPERTY(double currentValue READ currentValue WRITE setCurrentValue NOTIFY currentValueChanged FINAL)

    muse::ContextInject<context::IGlobalContext> globalContext{ this };

public:
    explicit PlaybackToolBarBPMItem(const muse::ui::UiAction& action, muse::uicomponents::ToolBarItemType::Type type,
                                    QObject* parent = nullptr);

    double currentValue() const;
    void setCurrentValue(double value);

signals:
    void currentValueChanged();

protected:
    //! GlideMix: virtual so the DJ toolbar can show the tempo line's BPM.
    virtual void updateValues();
    void setDisplayedValue(double value);

    context::IPlaybackStatePtr playbackState() const;

private:
    void onProjectChanged();

    double m_currentValue = 0.0;
};
}

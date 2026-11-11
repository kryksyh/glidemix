/*
 * GlideMix: the LED meter above the clock (plan A3) -- a 40 x 7 dot matrix
 * of the mix the audience hears, in a colour sweep across the spectrum
 * (red lows to violet highs, the track header colours). Unlit dots stay faintly visible; dark when stopped.
 */
#pragma once

#include <QElapsedTimer>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QtQmlIntegration>
#include <memory>

namespace au::mixhouse {
class LedSpectrum;

class MixHouseLedMeter : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    /// Dots shown (right-click Hide / Show, saved).
    Q_PROPERTY(bool shown READ shown WRITE setShown NOTIFY shownChanged)

public:
    explicit MixHouseLedMeter(QQuickItem* parent = nullptr);
    ~MixHouseLedMeter() override;

    void paint(QPainter* painter) override;

    bool shown() const { return m_shown; }
    void setShown(bool shown);

signals:
    void shownChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    void onTick();
    void updateTimer();

    std::unique_ptr<LedSpectrum> m_spectrum;
    QTimer m_timer;
    QElapsedTimer m_sinceTick;
    bool m_shown = true;
};
}

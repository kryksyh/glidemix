/*
 * GlideMix: see mixhouseledmeter.h.
 */
#include "mixhouseledmeter.h"

#include <QPainter>
#include <QSettings>
#include <algorithm>
#include <cmath>

#include "mixhouse/mixhouseledspectrum.h"
#include "log.h"

namespace au::mixhouse {
namespace {
const char* kShownKey = "GlideMix/ledMeterShown";
constexpr qreal kDot = 3.0;
constexpr qreal kGap = 1.0;
constexpr qreal kOffAlpha = 0.12;
// A sweep across the spectrum through the track header's colours (LOW red,
// MID orange, HIGH yellow, COLOR violet) with green and blue between (Chris:
// "more colors, it is a wide spectrum").
const QColor kStops[] = {
    QColor(0xF0, 0x50, 0x6E),   // lows: red
    QColor(0xF5, 0xA0, 0x4A),   // orange
    QColor(0xED, 0xD8, 0x5A),   // yellow
    QColor(0x6F, 0xD6, 0x6F),   // green
    QColor(0x4F, 0xC3, 0xE8),   // blue
    QColor(0xA0, 0x7C, 0xF5),   // highs: violet
};

QColor bandColor(int band)
{
    constexpr int stops = sizeof(kStops) / sizeof(kStops[0]);
    const qreal t = qreal(band) / (LedSpectrum::kBands - 1) * (stops - 1);
    const int i = std::min(stops - 2, int(t));
    const qreal f = t - i;
    const QColor& a = kStops[i];
    const QColor& b = kStops[i + 1];
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * f, a.greenF() + (b.greenF() - a.greenF()) * f,
                            a.blueF() + (b.blueF() - a.blueF()) * f);
}
}

MixHouseLedMeter::MixHouseLedMeter(QQuickItem* parent)
    : QQuickPaintedItem(parent), m_spectrum(std::make_unique<LedSpectrum>())
{
    setAntialiasing(true);
    m_shown = QSettings().value(QString::fromLatin1(kShownKey), true).toBool();
    setImplicitWidth(LedSpectrum::kBands * (kDot + kGap) - kGap);
    setImplicitHeight(LedSpectrum::kRows * (kDot + kGap) - kGap);
    m_timer.setInterval(33);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &MixHouseLedMeter::onTick);
}

MixHouseLedMeter::~MixHouseLedMeter() = default;

void MixHouseLedMeter::setShown(bool shown)
{
    if (m_shown == shown) {
        return;
    }
    m_shown = shown;
    QSettings().setValue(QString::fromLatin1(kShownKey), shown);
    updateTimer();
    update();
    emit shownChanged();
}

void MixHouseLedMeter::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickPaintedItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged || change == ItemSceneChange) {
        updateTimer();
    }
}

void MixHouseLedMeter::updateTimer()
{
    if (m_shown && isVisible() && window()) {
        if (!m_timer.isActive()) {
            m_sinceTick.start();
            m_timer.start();
            LOGI() << "GlideMix LED meter: running";
        }
    } else {
        m_timer.stop();
    }
}

void MixHouseLedMeter::onTick()
{
    const double seconds = std::min(0.2, m_sinceTick.isValid() ? m_sinceTick.restart() / 1000.0 : 0.033);
    if (m_spectrum->tick(seconds)) {
        update();
    }
}

void MixHouseLedMeter::paint(QPainter* painter)
{
    if (!m_shown) {
        return;
    }
    painter->setPen(Qt::NoPen);
    const auto& levels = m_spectrum->levels();
    const qreal step = kDot + kGap;
    const qreal x0 = (width() - (LedSpectrum::kBands * step - kGap)) / 2.0;
    const qreal y0 = (height() - (LedSpectrum::kRows * step - kGap)) / 2.0;
    for (int b = 0; b < LedSpectrum::kBands; ++b) {
        const QColor base = bandColor(b);
        for (int r = 0; r < LedSpectrum::kRows; ++r) {
            // Row 0 is the bottom one. A partly reached dot fades in.
            const qreal lit = std::clamp<qreal>(levels[b] - r, 0.0, 1.0);
            QColor c = base;
            c.setAlphaF(kOffAlpha + (1.0 - kOffAlpha) * lit);
            painter->setBrush(c);
            const qreal x = x0 + b * step;
            const qreal y = y0 + (LedSpectrum::kRows - 1 - r) * step;
            painter->drawEllipse(QRectF(x, y, kDot, kDot));
        }
    }
}
}

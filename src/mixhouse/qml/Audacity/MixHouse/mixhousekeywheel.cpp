/*
 * MixHouse DJ 2 -- Lancelot wheel. Ported from mixxx/src/mixxxmeister/keywheel.cpp.
 */
#include "mixhousekeywheel.h"

#include "mixhouse/imixhousekeymatch.h"
#include "mixhouse/mixhousesearchquery.h"

#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <algorithm>
#include <cmath>

using namespace au::mixhouse;

namespace {
// Ring geometry as a fraction of the wheel radius -- the fork's values. The
// hollow middle is not decoration: it stops the twelve inner sectors converging
// into an unclickable point.
constexpr double kOuterR = 1.00;   // outer edge of the B (major) ring
constexpr double kMidR = 0.70;     // B ring inner edge / A ring outer edge
constexpr double kInnerR = 0.38;   // inner edge of the A (minor) ring

constexpr int kSectors = 12;
constexpr double kSectorDeg = 360.0 / kSectors;

// Qt measures angles counter-clockwise from 3 o'clock; the wheel is numbered
// clockwise from 12 o'clock. Getting this wrong is how a Camelot wheel ends up
// mirrored.
double sectorCenterDegFromTop(int number)
{
    return (number % kSectors) * kSectorDeg;
}

// Mixed In Key's key colours: one shared copy, IMixHouseKeyMatch::openKeyColor.
QColor mikKeyColor(int openKeyNumber) { return au::mixhouse::IMixHouseKeyMatch::openKeyColor(openKeyNumber); }

// mixxx's real ChromaticKey enum values (proto/keys.proto: INVALID=0,
// C_MAJOR=1..B_MAJOR=12, C_MINOR=13..B_MINOR=24) mapped to Open Key number
// (1-12), copied case-for-case from KeyUtils::keyToOpenKeyNumber
// (track/keyutils.cpp) rather than derived -- the chromatic-to-circle-of-
// fifths relationship is real but a hand-written formula here is exactly the
// kind of thing worth getting from the source instead of re-deriving.
// Index 0 (INVALID) is unused; a lookup there is guarded against below.
constexpr int kOpenKeyNumberByChromaticKey[25] = {
    0,                          // 0  INVALID
    1, 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6,   // 1-12  major keys
    10, 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3    // 13-24 minor keys (relative to the row above)
};

QColor greyedOut(const QColor& base)
{
    // Unselected keys must read as "excluded", not "a different colour".
    // Keeping a trace of the hue is what lets the eye still find 6A when dimmed.
    const QColor c = base.toHsv();
    return QColor::fromHsv(c.hue(),
                           std::max(0, c.saturation() / 6),
                           std::max(56, c.value() / 3));
}
}  // namespace

MixHouseKeyWheel::MixHouseKeyWheel(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setAntialiasing(true);
}

QString MixHouseKeyWheel::labelFor(int number, int ring)
{
    return QString::number(number) + (ring == RingMinor ? QStringLiteral("A") : QStringLiteral("B"));
}

QString MixHouseKeyWheel::displayLabelFor(int number, int ring) const
{
    const QString lancelot = labelFor(number, ring);
    const int keyId = keyIdFromText(lancelot);
    switch (static_cast<MixHouseKeyNotation>(m_notation)) {
    case MixHouseKeyNotation::OpenKey:
    case MixHouseKeyNotation::OpenKeyTraditional:
        return IMixHouseKeyMatch::openKey(keyId);
    case MixHouseKeyNotation::Traditional:
        return IMixHouseKeyMatch::traditional(keyId);
    default:
        return lancelot;
    }
}

void MixHouseKeyWheel::setNotation(int notation)
{
    if (m_notation == notation) {
        return;
    }
    m_notation = notation;
    update();
    emit notationChanged();
    emit selectedLabelsChanged();
}

int MixHouseKeyWheel::openKeyNumber(int camelotNumber)
{
    // Camelot 8 is C major / A minor, which is OpenKey 1; the two scales run in
    // the same direction, so the whole mapping is one rotation.
    return ((camelotNumber + 4) % kSectors) + 1;
}

QColor MixHouseKeyWheel::keyColor(int camelotNumber)
{
    return mikKeyColor(openKeyNumber(camelotNumber));
}

QVariant MixHouseKeyWheel::colorForKey(const QString& lancelotKey) const
{
    // Same parse mixxx's own key text always takes: a Camelot number (1-12)
    // followed by A (minor) or B (major). Anything else -- empty, a musical
    // name like "Am" if the notation preference ever changes, garbage --
    // returns null, on purpose, so QML can tell "no real color" apart from a
    // real one (`kc || fallback` in QML/JS treats null the same as undefined).
    const QString k = lancelotKey.trimmed().toUpper();
    if (k.length() < 2) {
        return QVariant();
    }
    const QChar ring = k.back();
    if (ring != QLatin1Char('A') && ring != QLatin1Char('B')) {
        return QVariant();
    }
    bool ok = false;
    const int number = k.left(k.length() - 1).toInt(&ok);
    if (!ok || number < 1 || number > kSectors) {
        return QVariant();
    }
    QColor c = keyColor(number);
    if (ring == QLatin1Char('A')) {
        c = c.lighter(112);   // exactly the wheel's own A-ring treatment, paint()
    }
    return QVariant(c);
}

QVariant MixHouseKeyWheel::colorForKeyId(int keyId) const
{
    if (keyId < 1 || keyId > 24) {   // 0 = INVALID, anything else isn't a real key
        return QVariant();
    }
    const int openKeyNum = kOpenKeyNumberByChromaticKey[keyId];
    QColor c = mikKeyColor(openKeyNum);
    if (keyId >= 13) {   // 13-24 are the minor keys -- same lift the wheel gives its A ring
        c = c.lighter(112);
    }
    return QVariant(c);
}

QStringList MixHouseKeyWheel::selectedKeys() const
{
    // Ascending by sector id, so an identical selection always produces
    // byte-identical text and the search box can tell our writes from typing.
    QList<int> ids = m_selected.values();
    std::sort(ids.begin(), ids.end());
    QStringList out;
    for (int id : ids) {
        out << labelFor(id / 2 + 1, id % 2);
    }
    return out;
}

QStringList MixHouseKeyWheel::selectedLabels() const
{
    QList<int> ids = m_selected.values();
    std::sort(ids.begin(), ids.end());
    QStringList out;
    for (int id : ids) {
        out << displayLabelFor(id / 2 + 1, id % 2);
    }
    return out;
}

void MixHouseKeyWheel::clearSelection()
{
    if (m_selected.isEmpty()) {
        return;
    }
    m_selected.clear();
    update();
    emit selectionChanged(QStringList());
    emit selectedLabelsChanged();
}

void MixHouseKeyWheel::selectKeys(const QStringList& lancelotKeys)
{
    m_selected.clear();
    for (int number = 1; number <= kSectors; ++number) {
        for (int ring : { RingMajor, RingMinor }) {
            const QString name = labelFor(number, ring);
            for (const QString& want : lancelotKeys) {
                if (name.compare(want.trimmed(), Qt::CaseInsensitive) == 0) {
                    m_selected.insert(sectorId(number, ring));
                }
            }
        }
    }
    update();
    emit selectionChanged(selectedKeys());
    emit selectedLabelsChanged();
}

QRectF MixHouseKeyWheel::wheelRect() const
{
    const double d = std::min(width(), height()) - 4.0;
    return QRectF((width() - d) / 2.0, (height() - d) / 2.0, d, d);
}

int MixHouseKeyWheel::sectorAt(const QPointF& pos) const
{
    const QRectF r = wheelRect();
    const QPointF c = r.center();
    const double radius = r.width() / 2.0;
    if (radius <= 0.0) {
        return -1;
    }
    const double dx = pos.x() - c.x();
    const double dy = pos.y() - c.y();
    const double dist = std::sqrt(dx * dx + dy * dy) / radius;

    int ring;
    if (dist <= kInnerR || dist > kOuterR) {
        return -1;   // hollow middle, or outside the wheel
    } else if (dist <= kMidR) {
        ring = RingMinor;
    } else {
        ring = RingMajor;
    }

    // Angle clockwise from 12 o'clock, to match the numbering.
    double deg = qRadiansToDegrees(std::atan2(dx, -dy));
    if (deg < 0.0) {
        deg += 360.0;
    }
    // Sectors are CENTRED on their angle, so shift by half a sector before
    // dividing -- otherwise every sector is off by 15 degrees and clicks near a
    // boundary select the neighbour.
    const int idx = static_cast<int>(std::floor((deg + kSectorDeg / 2.0) / kSectorDeg)) % kSectors;
    const int number = (idx == 0) ? kSectors : idx;
    return sectorId(number, ring);
}

void MixHouseKeyWheel::mousePressEvent(QMouseEvent* e)
{
    const int id = sectorAt(e->position());
    if (id < 0) {
        e->ignore();
        return;
    }
    if (m_selected.contains(id)) {
        m_selected.remove(id);
    } else {
        m_selected.insert(id);
    }
    update();
    emit selectionChanged(selectedKeys());
    emit selectedLabelsChanged();
    e->accept();
}

void MixHouseKeyWheel::hoverMoveEvent(QHoverEvent* e)
{
    const int id = sectorAt(e->position());
    if (id != m_hover) {
        m_hover = id;
        update();
    }
}

void MixHouseKeyWheel::hoverLeaveEvent(QHoverEvent*)
{
    if (m_hover != -1) {
        m_hover = -1;
        update();
    }
}

void MixHouseKeyWheel::paint(QPainter* painter)
{
    QPainter& p = *painter;
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = wheelRect();
    const QPointF c = r.center();
    const double radius = r.width() / 2.0;
    const bool filtering = !m_selected.isEmpty();

    QFont f = p.font();
    f.setPixelSize(std::max(8, static_cast<int>(radius * 0.115)));
    f.setBold(true);
    p.setFont(f);

    for (int number = 1; number <= kSectors; ++number) {
        for (int ring : { RingMajor, RingMinor }) {
            const int id = sectorId(number, ring);
            const bool selected = m_selected.contains(id);

            const double rOuter = (ring == RingMajor) ? kOuterR : kMidR;
            const double rInner = (ring == RingMajor) ? kMidR : kInnerR;

            // Same hue for a number's A and B -- relative minor and major belong
            // together at a glance -- but the inner ring is lifted so the two
            // rings stay separable.
            QColor fill = keyColor(number);
            if (ring == RingMinor) {
                fill = fill.lighter(112);
            }
            if (filtering && !selected) {
                fill = greyedOut(fill);
            }
            if (id == m_hover && !selected) {
                fill = fill.lighter(122);
            }

            // An explicit annulus wedge: drawPie() would fill to the centre and
            // paint the inner ring over the outer.
            const double startDeg = sectorCenterDegFromTop(number) - kSectorDeg / 2.0;
            const double qtStart = 90.0 - startDeg;   // to Qt's CCW-from-3-o'clock
            const double qtSpan = -kSectorDeg;

            const QRectF outerBox(c.x() - radius * rOuter, c.y() - radius * rOuter,
                                  2 * radius * rOuter, 2 * radius * rOuter);
            const QRectF innerBox(c.x() - radius * rInner, c.y() - radius * rInner,
                                  2 * radius * rInner, 2 * radius * rInner);

            QPainterPath path;
            path.arcMoveTo(outerBox, qtStart);
            path.arcTo(outerBox, qtStart, qtSpan);
            path.arcTo(innerBox, qtStart + qtSpan, -qtSpan);
            path.closeSubpath();

            p.setBrush(fill);
            p.setPen(QPen(QColor(0x1a, 0x1a, 0x1a), 1.0));
            p.drawPath(path);

            if (selected) {
                // Colour alone is not enough once the others are only dimmed;
                // the outline is what survives at 190px.
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(QColor(0xff, 0xff, 0xff), 1.8));
                p.drawPath(path);
            }

            const double labelR = (rOuter + rInner) / 2.0;
            const double midDeg = sectorCenterDegFromTop(number);
            const QPointF lp(c.x() + radius * labelR * std::sin(qDegreesToRadians(midDeg)),
                             c.y() - radius * labelR * std::cos(qDegreesToRadians(midDeg)));
            // The palette is deliberately light, so labels are dark on a live
            // sector and dimmed on a greyed one.
            p.setPen((filtering && !selected) ? QColor(0x8a, 0x8a, 0x8a) : QColor(0x14, 0x14, 0x14));
            p.drawText(QRectF(lp.x() - 22, lp.y() - 9, 44, 18), Qt::AlignCenter,
                       displayLabelFor(number, ring));
        }
    }
}

/*
 * MixHouse DJ 2 -- the Lancelot (Camelot) wheel.
 *
 * A direct port of MxmKeyWheel from the Mixxx fork
 * (mixxx/src/mixxxmeister/keywheel.{h,cpp}), moved from QWidget::paintEvent onto
 * QQuickPaintedItem::paint so it can live in Audacity's QML scene. The drawing is
 * the same code: same geometry constants, same annulus-wedge construction, same
 * hit test, same greying rule. It is a port, not a lookalike.
 *
 * Two concentric rings of twelve sectors: OUTER = B (major), INNER = A (minor),
 * numbered 1..12 clockwise from the top. Clicking toggles a sector; while
 * anything is selected the rest grey out, so the wheel doubles as a readout of
 * what the library is filtered to.
 */
#pragma once

#include <QColor>
#include <QHash>
#include <QQuickPaintedItem>
#include <QSet>
#include <QStringList>
#include <QVariant>
#include <QtQmlIntegration>

namespace au::mixhouse {
class MixHouseKeyWheel : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QStringList selectedKeys READ selectedKeys NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    /// MixHouseKeyNotation the sectors are labelled in. Only the labels:
    /// selection and selectedKeys stay in Lancelot.
    Q_PROPERTY(int notation READ notation WRITE setNotation NOTIFY notationChanged)
    /// selectedKeys as the sectors are labelled, for showing to the user.
    Q_PROPERTY(QStringList selectedLabels READ selectedLabels NOTIFY selectedLabelsChanged)

public:
    explicit MixHouseKeyWheel(QQuickItem* parent = nullptr);

    void paint(QPainter* painter) override;

    QStringList selectedKeys() const;
    QStringList selectedLabels() const;
    bool hasSelection() const { return !m_selected.isEmpty(); }
    int notation() const { return m_notation; }
    void setNotation(int notation);

    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void selectKeys(const QStringList& lancelotKeys);

    /// The library's Key column text color -- same real palette as the wheel
    /// itself (kMikKeyColors, below), never a second copy of it. Takes a
    /// Camelot/Lancelot string ("7A", "12B"); an unparseable/empty key
    /// returns a null QVariant (not an invalid QColor -- that bridges to QML
    /// unpredictably), which is plain falsy in QML/JS, so callers can just
    /// write `keyWheel.colorForKey(k) || fallbackColor`.
    Q_INVOKABLE QVariant colorForKey(const QString& lancelotKey) const;

    /// The reliable path: library.key is TEXT in whatever notation mixxx's
    /// own Key Notation preference was set to when the track was analysed --
    /// for a real library that's Traditional ("Bb", "Ebm"), not
    /// Camelot, so colorForKey() above never matches a single row. key_id is
    /// mixxx's ChromaticKey enum value (proto/keys.proto), which is the same
    /// regardless of display notation -- use THIS for coloring, always.
    Q_INVOKABLE QVariant colorForKeyId(int keyId) const;

signals:
    void selectionChanged(const QStringList& lancelotKeys);
    void notationChanged();
    void selectedLabelsChanged();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void hoverMoveEvent(QHoverEvent* e) override;
    void hoverLeaveEvent(QHoverEvent* e) override;

private:
    // Ring index within a sector id, matching the fork's kRingMajor/kRingMinor.
    enum Ring { RingMajor = 0, RingMinor = 1 };

    static int sectorId(int number, int ring) { return (number - 1) * 2 + ring; }
    static QString labelFor(int number, int ring);
    /// labelFor() in the chosen notation; the combined ones show only their
    /// first half, as there is no room for "8A/Am" in a sector.
    QString displayLabelFor(int number, int ring) const;
    int m_notation = 0;
    /// Palette index for a Camelot number, via its OpenKey number. Verified
    /// against the fork's KeyUtils::keyToOpenKeyNumber and against the real
    /// key_id/key rows in the reference library: 8A->1, 9A->2, 12A->5.
    static int openKeyNumber(int camelotNumber);
    static QColor keyColor(int camelotNumber);

    QRectF wheelRect() const;
    int sectorAt(const QPointF& pos) const;

    QSet<int> m_selected;
    int m_hover = -1;
};
}

/*
 * GlideMix
 */
#pragma once

#include <map>

#include <QColor>
#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQmlIntegration>

#include "mixhousegrideditormodel.h"

namespace au::mixhouse {
/// The Beatgrid editor's overview: the whole track with its phrases, cue,
/// loop and playhead; click or drag to go there. (The big waveform is
/// MixHouseGridEditorWave.)
class MixHouseGridEditorView : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(au::mixhouse::MixHouseGridEditorModel* model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(bool overview READ overview WRITE setOverview NOTIFY modelChanged)
    Q_PROPERTY(double ribbonHeight MEMBER m_ribbonHeight NOTIFY modelChanged)

public:
    explicit MixHouseGridEditorView(QQuickItem* parent = nullptr);

    MixHouseGridEditorModel* model() const { return m_model; }
    void setModel(MixHouseGridEditorModel* model);
    bool overview() const { return m_overview; }
    void setOverview(bool overview);

    void paint(QPainter* painter) override;

signals:
    void modelChanged();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void paintOverview(QPainter& p);
    void paintPhrases(QPainter& p, const QRectF& area, double t0, double pxPerSec, bool labels);
    double timeAtX(double x) const;

    QPointer<MixHouseGridEditorModel> m_model;
    bool m_overview = false;
    double m_ribbonHeight = 20.0;

    QImage m_overviewImage;   // the overview's waveform; phrases and playhead go on top
    const MixHouseGridEditorModel::Waveform* m_overviewWaveform = nullptr;
};
}

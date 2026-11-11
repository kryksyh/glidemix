/*
* Audacity: A Digital Audio Editor
*/
#include "waveview.h"

#include "mixhouse/mixhousetransitionspec.h"

#include <QFontMetricsF>

#include <cmath>

#include "global/translation.h"

#include <QPainter>
#include <QPainterPath>
#include <QElapsedTimer>

#include "global/types/color.h"
#include "global/log.h"

#include "au3/wavepainterutils.h"
#include "au3/samplespainterutils.h"
#include "view/timeline/timelinecontext.h"

using namespace au::projectscene;

static const QColor BACKGROUND_COLOR = QColor(255, 255, 255);
static const QColor SAMPLES_BASE_COLOR = QColor(0, 0, 0);
static const QColor SAMPLES_HIGHLIGHT_COLOR = QColor(255, 255, 255);
static const QColor RMS_BASE_COLOR = QColor(255, 255, 255);
static const QColor RMS_SELECTED_COLOR = QColor(255, 255, 255); // TODO: This need update
static const QColor CLIPPING_SOLID_COLOR = QColor(239, 71, 111);
static const QColor CENTER_LINE_COLOR = QColor(0, 0, 0);
static const QColor SAMPLE_HEAD_COLOR = QColor(0, 0, 0);
static const QColor SAMPLE_STALK_COLOR = QColor(0, 0, 0);

// AU3 colors from au3/libraries/au3-theme-resources/light/Components/Colors.txt
static const QColor CLASSIC_BACKGROUND_COLOR = QColor(240, 243, 255);               // Unselected: #f0f3ff
static const QColor CLASSIC_BACKGROUND_SELECTED_COLOR = QColor(170, 195, 242);      // Selected: #aac3f2
static const QColor CLASSIC_SAMPLES_BASE_COLOR = QColor(100, 100, 211);             // Sample: #6464D3
static const QColor CLASSIC_SAMPLES_BASE_SELECTED_COLOR = QColor(103, 124, 228);    // SelSample: #677ce4
static const QColor CLASSIC_RMS_COLOR = QColor(151, 151, 253);                      // Rms: #9797FD
static const QColor CLASSIC_RMS_SELECTED_COLOR = QColor(151, 151, 253);             // Rms: #9797FD // TODO: This need update
static const QColor CLASSIC_CLIPPING_COLOR = QColor(239, 71, 111);                  // Clipped: #ef476f

static const float SAMPLE_HEAD_DEFAULT_ALPHA= 0.6;
static const float SAMPLE_HEAD_CLIP_SELECTED_ALPHA = 0.8;
static const float SAMPLE_HEAD_DATA_SELECTED_ALPHA = 0.9;
static const float SAMPLE_STALK_DEFAULT_ALPHA = 0.4;
static const float SAMPLE_STALK_CLIP_SELECTED_ALPHA = 0.6;
static const float SAMPLE_STALK_DATA_SELECTED_ALPHA = 0.7;

WaveView::WaveView(QQuickItem* parent)
    : QQuickPaintedItem(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    //! NOTE: Push history state after edit is completed to avoid multiple unecessary calls.
    connect(this, &WaveView::isIsolationModeChanged, [this]() {
        if (!m_isIsolationMode) {
            pushProjectHistorySampleEdit();
        }
    });

    connect(this, &WaveView::visibleChanged, [this]() {
        emit isNearSampleChanged();
    });

    connect(this, &WaveView::multiSampleEditChanged, [this]() {
        if (!m_multiSampleEdit) {
            pushProjectHistorySampleEdit();
        }
    });

    configuration()->isRMSInWaveformVisibleChanged().onReceive(this, [this](bool) {
        update();
    });

    configuration()->isClippingInWaveformVisibleChanged().onReceive(this, [this](bool) {
        update();
    });

    configuration()->clipStyleChanged().onReceive(this, [this](projectscene::ClipStyles::Style) {
        emit backgroundColorChanged();
        update();
    });
}

WaveView::~WaveView()
{
}

void WaveView::setClipKey(const ClipKey& newClipKey)
{
    // GlideMix: subscribe to beat grid changes here, on the GUI thread.
    // paint() runs on the render thread, and a receiver connected from there
    // was never removed when the view died -- the app then crashed on quit
    // ("mutex lock failed") when MixHouseBeatGrids released its notification.
    if (!m_beatGridsSubscribed) {
        if (const auto grids = beatGrids()) {
            m_beatGridsSubscribed = true;
            grids->beatGridsChanged().onNotify(this, [this]() {
                update();
                emit anchorsChanged();
            });
        }
        // Turning the colour waveform on or off repaints the clip; nothing
        // else about it has changed. Subscribed here for the same reason.
        if (const auto uiState = projectSceneUiState()) {
            uiState->clipWaveformRgbChanged().onNotify(this, [this]() {
                update();
            });
        }
        // The track's volume slider moved: the waveform follows it.
        if (const auto control = trackPlaybackControl()) {
            control->volumeChanged().onReceive(this, [this](long trackId) {
                if (trackId == m_clipKey.key.trackId) {
                    update();
                }
            });
        }
        // A song's HD waveform finished analysing in the background.
        if (const auto hd = hdWaveforms()) {
            hd->ready().onNotify(this, [this]() {
                update();
            });
        }
    }
    m_clipKey = newClipKey;
    emit clipKeyChanged();

    update();
}

IWavePainter::Params WaveView::getWavePainterParams() const
{
    IWavePainter::Params params;
    params.geometry.height = height();
    params.geometry.width = width();
    params.geometry.left = 0.0;

    params.zoom = m_context->zoom();
    params.fromTime = (m_clipTime.itemStartTime - m_clipTime.startTime);
    params.toTime = params.fromTime + (m_clipTime.itemEndTime - m_clipTime.itemStartTime);
    params.selectionStartTime = m_clipTime.selectionStartTime;
    params.selectionEndTime = m_clipTime.selectionEndTime;
    params.channelHeightRatio = m_channelHeightRatio;
    params.showRMS = configuration()->isRMSInWaveformVisible();
    params.showClipping = configuration()->isClippingInWaveformVisible();
    params.isLinear = m_isLinear;
    params.dbRange = m_dbRange;
    params.displayBounds = m_displayBounds;
    // GlideMix: the waveform shows the track's volume slider too, as a DAW's
    // does: the view range is widened so the audio is drawn that much smaller.
    const double gain = std::max(trackGain(), 0.004);
    if (gain < 0.999 || gain > 1.001) {
        params.displayBounds = { static_cast<float>(m_displayBounds.first / gain),
                                 static_cast<float>(m_displayBounds.second / gain) };
    }

    projectscene::ClipStyles::Style clipStyle = configuration()->clipStyle();
    if (clipStyle == projectscene::ClipStyles::Style::COLORFUL) {
        applyColorfulStyle(params, m_clipColor, m_clipSelectedColor, m_clipSelected);
    } else {
        applyClassicStyle(params, m_clipSelected);
    }

    return params;
}

void WaveView::applyColorfulStyle(IWavePainter::Params& params,
                                  const QColor& clipColor,
                                  const QColor& clipSelectedColor,
                                  bool selected) const
{
    // MixHouse DJ 2: the clip body is the clip colour itself. Stock Audacity
    // washes it 10-20% toward white (blankBrush 0.9, background 0.8), which is
    // what made every clip read pastel/faded (2026-09-13).
    // GlideMix: the parked MixHouse DJ app's deck-line colour intensity.
    // The body carries the clip colour at that strength over the dark canvas;
    // the waveform is a dark shade of the colour while the body is strong, and
    // takes the full colour once the body fades, so it stays readable at every
    // setting (ArrangementWindow::drawClip: bodyAlpha > 140 ? darker(260) : colour).
    const double intensity = std::clamp(m_colorIntensity, 0.0, 1.0);
    const bool strongBody = intensity * 255.0 > 140.0;
    const QColor body = muse::blendQColors(m_canvasColor, clipColor, intensity);
    const QColor selectedBody = muse::blendQColors(m_canvasColor, clipSelectedColor, std::max(intensity, 0.35));
    if (selected) {
        params.style.blankBrush = selectedBody;
        params.style.normalBackground = selectedBody;
        params.style.selectedBackground = selectedBody;
        params.style.envelopeBackground = selectedBody;
        params.style.selectedEnvelopeBackground = selectedBody;
    } else {
        params.style.blankBrush = body;
        params.style.normalBackground = body;
        params.style.selectedBackground = selectedBody;
        params.style.envelopeBackground = body;
        params.style.selectedEnvelopeBackground = selectedBody;
    }

    const QColor waveColor = strongBody ? clipColor.darker(260) : clipColor;
    params.style.samplePen = waveColor;
    params.style.selectedSamplePen = strongBody
                                     ? muse::blendQColors(params.style.blankBrush,
                                                          selected ? SAMPLES_HIGHLIGHT_COLOR : SAMPLES_BASE_COLOR, 0.75)
                                     : clipColor.lighter(130);
    params.style.rmsPen = muse::blendQColors(params.style.samplePen, RMS_BASE_COLOR, 0.25);
    params.style.rmsSelectedPen = muse::blendQColors(params.style.selectedSamplePen, RMS_BASE_COLOR, 0.6); // TODO: use RMS_SELECTED_COLOR
    params.style.clippedPen = CLIPPING_SOLID_COLOR;
    params.style.centerLine = muse::blendQColors(params.style.samplePen, CENTER_LINE_COLOR, 0.2);

    float headAlpha = selected ? SAMPLE_HEAD_CLIP_SELECTED_ALPHA : SAMPLE_HEAD_DEFAULT_ALPHA;
    float stalkAlpha = selected ? SAMPLE_STALK_CLIP_SELECTED_ALPHA : SAMPLE_STALK_DEFAULT_ALPHA;

    params.style.sampleHead = muse::blendQColors(params.style.samplePen, SAMPLE_HEAD_COLOR, headAlpha);
    params.style.sampleStalk = muse::blendQColors(params.style.samplePen, SAMPLE_STALK_COLOR, stalkAlpha);

    if (!selected) {
        params.style.sampleHeadSelection = muse::blendQColors(params.style.samplePen, SAMPLE_HEAD_COLOR,
                                                              SAMPLE_HEAD_DATA_SELECTED_ALPHA);
        params.style.sampleStalkSelection
            = muse::blendQColors(params.style.samplePen, SAMPLE_STALK_COLOR, SAMPLE_STALK_DATA_SELECTED_ALPHA);
    }
}

void WaveView::applyClassicStyle(IWavePainter::Params& params, bool selected) const
{
    params.style.blankBrush = selected ? CLASSIC_BACKGROUND_SELECTED_COLOR : CLASSIC_BACKGROUND_COLOR;
    params.style.normalBackground = params.style.blankBrush;
    params.style.selectedBackground = selected ? transformColor(CLASSIC_BACKGROUND_SELECTED_COLOR) : CLASSIC_BACKGROUND_SELECTED_COLOR;

    params.style.envelopeBackground = params.style.blankBrush;
    params.style.selectedEnvelopeBackground
        = selected ? transformColor(CLASSIC_BACKGROUND_SELECTED_COLOR) : CLASSIC_BACKGROUND_SELECTED_COLOR;

    QColor baseSampleColor = selected ? CLASSIC_SAMPLES_BASE_SELECTED_COLOR : CLASSIC_SAMPLES_BASE_COLOR;
    params.style.samplePen = baseSampleColor;
    params.style.selectedSamplePen = CLASSIC_SAMPLES_BASE_SELECTED_COLOR;
    params.style.rmsPen = CLASSIC_RMS_COLOR;
    params.style.rmsSelectedPen = muse::blendQColors(params.style.selectedSamplePen, CLASSIC_RMS_COLOR, 0.6); // TODO: use CLASSIC_RMS_SELECTED_COLOR
    params.style.clippedPen = CLASSIC_CLIPPING_COLOR;
    params.style.centerLine = baseSampleColor;
    params.style.sampleHead = baseSampleColor;
    params.style.sampleStalk = baseSampleColor;

    if (!selected) {
        params.style.sampleHeadSelection = baseSampleColor;
        params.style.sampleStalkSelection = baseSampleColor;
    }
}

namespace {
//! Past this many screen points per analysis column the HD picture is only
//! smoothed guesses between columns; the real audio is drawn instead, in
//! the HD colours, so a close-up stays sharp.
constexpr double kHdMaxPixelsPerColumn = 1.5;
}

void WaveView::paint(QPainter* painter)
{
    IWavePainter::Params params = getWavePainterParams();
    IWavePainter::PlotType pType = wavepainterutils::getPlotType(globalContext()->currentProject(), m_clipKey.key, params.zoom);

    bool isStemPlot = pType == IWavePainter::PlotType::Stem;

    setIsStemPlot(isStemPlot);
    setAntialiasing(isStemPlot);

    // GlideMix: a stem track ("Drums — <track>" ...) holds different audio
    // from the song its clip is named after, so the song's colour analysis
    // would show the wrong picture: it draws its own audio instead.
    bool stemTrack = false;
    if (const auto project = globalContext()->currentTrackeditProject()) {
        if (const auto track = project->track(m_clipKey.key.trackId)) {
            const QString title = track->title.toQString();
            // "Drums — <song>"; stems split before 1.0.4 end in " - Drums".
            for (const char* stem : { "Drums", "Bass", "Vocals", "Other" }) {
                const QString name = QString::fromLatin1(stem);
                stemTrack |= title.startsWith(name + QStringLiteral(" \u2014 "))
                             || title.endsWith(QStringLiteral(" - ") + name);
            }
        }
    }
    const bool rgb = !stemTrack && projectSceneUiState() && projectSceneUiState()->clipWaveformRgb();
    // Zoomed in past rekordbox's own 150 columns a second, its columns would be
    // drawn as wide blocks; there the audio itself is painted and only tinted
    // with rekordbox's colours, so the lane stays sharp all the way in.
    double columnRate = rgb && beatGrids() ? beatGrids()->clipWaveColumnRate(m_clipKey.key) : 0.0;
    // GlideMix: the HD analysis, when there is one, sets where the close-up starts.
    mixhouse::IMixHouseBeatGrids::ClipFile hdFile;
    mixhouse::MixHouseHdWavePtr hdWave;
    if (rgb && beatGrids() && hdWaveforms() && beatGrids()->clipFile(m_clipKey.key, hdFile)) {
        hdWave = hdWaveforms()->wave(hdFile.path);
        if (hdWave) {
            columnRate = hdWave->rate / hdFile.ratio;
        }
    }
    const bool tint = rgb && !hdWave && columnRate > 0.0 && params.zoom > columnRate * 1.5;

    // The phrase ribbon is drawn over the bottom of the clip, so the waveform
    // is given the height above it to itself instead of having its lower peaks
    // covered.
    // A couple of pixels at the top as well, so a loud passage does not come out
    // as a flat line against the clip's title bar.
    constexpr double kTopInset = 2.0;
    const double ribbon = phraseRibbonHeight(params);
    IWavePainter::Params waveParams = params;
    waveParams.geometry.height = std::max(8.0, params.geometry.height - ribbon - kTopInset);

    if (tint) {
        paintTintedStyle(waveParams);
    }
    // The whole clip is black under the colour waveform, including the strips
    // the wave leaves free at the top and under the phrase ribbon.
    if (rgb) {
        painter->fillRect(QRectF(0.0, 0.0, params.geometry.width, params.geometry.height), rgbBackground());
    }
    painter->save();
    painter->translate(0.0, kTopInset);
    if (rgb && !tint && paintHdLane(*painter, waveParams)) {
        // GlideMix: the HD colour waveform, djay style.
    } else if (tint || !rgb || !paintRgbWave(*painter, waveParams)) {
        wavePainter()->paint(*painter, m_clipKey.key, waveParams, pType);
        if (tint) {
            paintRgbTint(*painter, waveParams);
        }
    }
    paintSilentGhost(*painter, waveParams, rgb);
    painter->restore();
    paintPhrases(*painter, params);
    paintBarMarks(*painter, params, ribbon);
    paintCues(*painter, params);
    paintAnchors(*painter, params);

    if (globalContext()->isRecording()) {
        paintRecordingPlaceholder(*painter, params);
    }
}

//! How tall the phrase ribbon along the bottom of the clip is (0 when the
//! track has no phrases). The waveform reserves this, rather than being drawn
//! under it.
//! rekordbox's black, or the selection tint when the clip is selected.
QColor WaveView::rgbBackground() const
{
    return m_clipSelected ? QColor(0x1E, 0x24, 0x38) : QColor(0x08, 0x08, 0x0A);
}

double WaveView::phraseRibbonHeight(const IWavePainter::Params& params) const
{
    const auto grids = beatGrids();
    if (!grids || params.zoom <= 0.0) {
        return 0.0;
    }
    if (grids->clipPhrases(m_clipKey.key, m_clipTime.itemStartTime, m_clipTime.itemEndTime).empty()) {
        return 0.0;
    }
    return std::min(16.0, std::max(4.0, params.geometry.height * 0.14));
}

// GlideMix: the greys the tinted mode paints the audio in, so that
// multiplying rekordbox's colour over them lands on the colour itself (white)
// and a dimmed version of it (grey).
void WaveView::paintTintedStyle(IWavePainter::Params& params) const
{
    const QColor kBack = rgbBackground();
    static const QColor kBody(0xB4, 0xB4, 0xB4);
    static const QColor kCore(0xFF, 0xFF, 0xFF);

    params.style.blankBrush = kBack;
    params.style.normalBackground = kBack;
    params.style.selectedBackground = kBack;
    params.style.envelopeBackground = kBack;
    params.style.selectedEnvelopeBackground = kBack;
    params.style.samplePen = kBody;
    params.style.selectedSamplePen = kBody;
    params.style.sampleBrush = kCore;
    params.style.rmsPen = kCore;
    params.style.rmsSelectedPen = kCore;
    params.style.centerLine = QColor(0x3C, 0x3C, 0x3C);
    params.style.sampleHead = kCore;
    params.style.sampleStalk = kBody;
    params.style.sampleHeadSelection = kCore;
    params.style.sampleStalkSelection = kBody;
}

// Multiplies rekordbox's colour over the painted audio, a pixel column at a
// time: white samples come out in the colour, the greys in a darker version of
// it, and the black background stays black.
void WaveView::paintRgbTint(QPainter& painter, const IWavePainter::Params& params)
{
    const auto grids = beatGrids();
    if (!grids || params.zoom <= 0.0 || params.geometry.width < 1.0) {
        return;
    }
    const int columns = static_cast<int>(std::ceil(params.geometry.width));
    // GlideMix: the HD colours, when the song has been analysed.
    mixhouse::IMixHouseBeatGrids::ClipFile file;
    if (hdWaveforms() && grids->clipFile(m_clipKey.key, file)) {
        if (const mixhouse::MixHouseHdWavePtr hd = hdWaveforms()->wave(file.path)) {
            painter.save();
            painter.setCompositionMode(QPainter::CompositionMode_Multiply);
            const double h = params.geometry.height;
            // One colour per analysis column, not per pixel: close up a column
            // spans several pixels.
            const double pxPerColumn = params.zoom * file.ratio / hd->rate;
            const int step = std::max(1, static_cast<int>(std::floor(pxPerColumn)));
            for (int x = 0; x < columns; x += step) {
                const double t0 = m_clipTime.itemStartTime + x / params.zoom;
                const double s0 = (t0 - file.sequenceStart) / file.ratio;
                const uint32_t rgbColour = mixhouse::hdWaveColour(*hd, s0, s0 + step / params.zoom / file.ratio);
                painter.fillRect(QRectF(x, 0.0, step, h), QColor(static_cast<int>((rgbColour >> 16) & 0xFF),
                                                               static_cast<int>((rgbColour >> 8) & 0xFF),
                                                               static_cast<int>(rgbColour & 0xFF)));
            }
            painter.restore();
            return;
        }
    }
    const std::vector<mixhouse::IMixHouseBeatGrids::WaveColumn> wave
        = grids->clipWaveColumns(m_clipKey.key, m_clipTime.itemStartTime,
                                 m_clipTime.itemStartTime + columns / params.zoom, columns);
    if (wave.empty()) {
        return;
    }

    painter.save();
    painter.setCompositionMode(QPainter::CompositionMode_Multiply);
    const double h = params.geometry.height;
    for (int x = 0; x < columns && x < static_cast<int>(wave.size()); ++x) {
        const auto& column = wave[static_cast<size_t>(x)];
        if (!column.valid) {
            continue;
        }
        painter.fillRect(QRectF(x, 0.0, 1.0, h),
                         QColor(static_cast<int>((column.rgb >> 16) & 0xFF),
                                static_cast<int>((column.rgb >> 8) & 0xFF),
                                static_cast<int>(column.rgb & 0xFF)));
    }
    painter.restore();
}

// GlideMix: the clip drawn the way rekordbox draws a deck -- each pixel
// column in the colour of the loudest band there (blue bass, orange mids,
// white highs) on black, with a brighter core at the middle. The colours are
// rekordbox's own for tracks it analysed; this app's analysis otherwise.
bool WaveView::paintRgbWave(QPainter& painter, const IWavePainter::Params& params)
{
    const auto grids = beatGrids();
    if (!grids || params.geometry.width < 1.0 || params.geometry.height < 2.0) {
        return false;
    }
    const int columns = static_cast<int>(std::ceil(params.geometry.width));
    if (params.zoom <= 0.0) {
        return false;
    }
    // x = 0 is the item's own start (the same origin the bar marks use), and a
    // pixel is 1 / zoom seconds -- the item can run past the painted width.
    const std::vector<mixhouse::IMixHouseBeatGrids::WaveColumn> wave
        = grids->clipWaveColumns(m_clipKey.key, m_clipTime.itemStartTime,
                                 m_clipTime.itemStartTime + columns / params.zoom, columns);
    if (wave.empty()) {
        return false;
    }

    const double h = params.geometry.height;
    painter.fillRect(QRectF(0.0, 0.0, params.geometry.width, h), rgbBackground());

    // The clip's channels split the height the same way the wave painter
    // splits it, so a stereo clip is drawn as two lanes rather than one band
    // across the middle. rekordbox's stored waveform is a mono sum, so at this
    // zoom both lanes carry the same shape; zoomed in past its columns the
    // audio itself is painted and the two channels differ.
    std::vector<std::pair<double, double> > lanes;
    if (params.channelHeightRatio >= 0.999) {
        lanes.push_back({ 0.0, h });
    } else {
        const double first = h * params.channelHeightRatio;
        lanes.push_back({ 0.0, first });
        lanes.push_back({ first, h - first });
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    // rekordbox's stored heights are already normalised and sit near the top
    // for loud music; a gamma opens the quiet parts up again so the lane shows
    // the shape of the track rather than a solid block.
    const auto shape = [](uint8_t level) { return std::pow(level / 255.0, 2.2); };
    for (int x = 0; x < columns && x < static_cast<int>(wave.size()); ++x) {
        const auto& column = wave[static_cast<size_t>(x)];
        if (!column.valid || column.level == 0) {
            continue;
        }
        const QColor colour(static_cast<int>((column.rgb >> 16) & 0xFF),
                            static_cast<int>((column.rgb >> 8) & 0xFF),
                            static_cast<int>(column.rgb & 0xFF));
        const double px = x + 0.5;
        for (const auto& [top, laneHeight] : lanes) {
            const double mid = top + laneHeight / 2.0;
            const double half = std::max(1.0, laneHeight / 2.0 - 1.0);
            const double v = std::max(1.0, shape(column.level) * half);
            // The peaks in a dimmed colour, the body of the sound bright
            // inside them: rekordbox's two-tone waveform.
            painter.setPen(QPen(colour.darker(190), 1.0));
            painter.drawLine(QPointF(px, mid - v), QPointF(px, mid + v));
            const double core = std::max(1.0, shape(column.core) * half);
            painter.setPen(QPen(colour, 1.0));
            painter.drawLine(QPointF(px, mid - core), QPointF(px, mid + core));
        }
    }
    painter.restore();
    return true;
}

// GlideMix: the HD colour waveform (mixhousehdwaveform.h) of a library
// track: bass, mids and highs as three layered envelopes. Zoomed in further
// than a few pixels per analysis column the audio itself is drawn instead
// (false), so editing close up still shows the real samples.
double WaveView::trackGain() const
{
    const auto control = trackPlaybackControl();
    if (!control || !m_clipKey.isValid()) {
        return 1.0;
    }
    const double db = control->volume(m_clipKey.key.trackId);
    return db <= -60.0 ? 0.0 : std::pow(10.0, db / 20.0);
}

bool WaveView::paintHdLane(QPainter& painter, const IWavePainter::Params& params)
{
    const auto grids = beatGrids();
    const auto hd = hdWaveforms();
    mixhouse::IMixHouseBeatGrids::ClipFile file;
    if (!grids || !hd || params.zoom <= 0.0 || params.geometry.width < 1.0 || params.geometry.height < 2.0
        || !grids->clipFile(m_clipKey.key, file)) {
        return false;
    }
    // One lane over the whole height, as djay draws a deck (the analysis of
    // the mono sum); or, with the stereo colour waveform on and a stereo
    // clip, left and right in the clip's two channel lanes, each analysed apart.
    const double h = params.geometry.height;
    struct Lane {
        mixhouse::MixHouseHdWavePtr wave;
        int channel = -1;
        double top = 0.0;
        double height = 0.0;
    };
    std::vector<Lane> lanes;
    const bool stereo = projectSceneUiState() && projectSceneUiState()->clipWaveformStereo()
                        && params.channelHeightRatio < 0.999;
    if (stereo) {
        const auto sides = hd->stereoWave(file.path);
        if (!sides.first || !sides.second) {
            return false;
        }
        const double first = h * params.channelHeightRatio;
        lanes.push_back({ sides.first, 0, 0.0, first });
        lanes.push_back({ sides.second, 1, first, h - first });
    } else {
        const mixhouse::MixHouseHdWavePtr wave = hd->wave(file.path);
        if (!wave) {
            return false;
        }
        lanes.push_back({ wave, -1, 0.0, h });
    }
    const int columns = static_cast<int>(std::ceil(params.geometry.width));
    const double fromSec = (m_clipTime.itemStartTime - file.sequenceStart) / file.ratio;
    const double toSec = (m_clipTime.itemStartTime + columns / params.zoom - file.sequenceStart) / file.ratio;

    painter.fillRect(QRectF(0.0, 0.0, params.geometry.width, h), rgbBackground());

    // At the screen's own resolution: on a Retina screen a point is two
    // pixels, and an image made in points was stretched and came out soft.
    const qreal dpr = painter.device() ? std::max<qreal>(1.0, painter.device()->devicePixelRatioF()) : 1.0;
    const int iw = std::max(1, static_cast<int>(std::ceil(columns * dpr)));
    const bool closeUp = params.zoom > lanes.front().wave->rate / file.ratio * kHdMaxPixelsPerColumn;
    for (const Lane& lane : lanes) {
        const int ih = std::max(2, static_cast<int>(std::round(lane.height * dpr)));
        // Zoomed in past the stored columns: the same picture, measured from
        // the audio on screen one column per pixel, so it stays sharp all the way in.
        if (closeUp) {
            constexpr double kWarmupSec = 0.05;   // settles the band filters
            std::vector<float> samples;
            double rate = 0.0, startSec = 0.0;
            const double from = std::max(0.0, fromSec);
            if (!grids->clipMonoSamples(m_clipKey.key, from - kWarmupSec, toSec, samples, rate, startSec, lane.channel)) {
                return false;
            }
            // The clip's volume line, sample by sample, as the audio is heard.
            std::vector<double> gain;
            if (grids->clipVolume(m_clipKey.key, file.sequenceStart + startSec * file.ratio, file.ratio / rate,
                                  samples.size(), gain)) {
                const double track = trackGain();
                for (size_t i = 0; i < samples.size(); ++i) {
                    samples[i] = static_cast<float>(samples[i] * gain[i] * track);
                }
            }
            const size_t warmup = static_cast<size_t>(std::clamp((from - startSec) * rate, 0.0,
                                                                 static_cast<double>(samples.size())));
            // Where the audio starts after the clip's left edge (the image's left).
            const double leftGap = std::max(0.0, -fromSec) * file.ratio * params.zoom;
            const int imageLeft = static_cast<int>(std::round(leftGap * dpr));
            QImage part(std::max(1, iw - imageLeft), ih, QImage::Format_ARGB32_Premultiplied);
            mixhouse::paintHdCloseUp(part, *lane.wave, samples.data(), samples.size(), warmup, rate,
                                     static_cast<long long>(std::floor(from * 8000.0)));
            painter.drawImage(QRectF(imageLeft / dpr, lane.top, part.width() / dpr, lane.height), part);
            continue;
        }
        QImage image(iw, ih, QImage::Format_ARGB32_Premultiplied);
        // The clip's volume line, one value per image column.
        std::vector<double> gain;
        const double pixel = 1.0 / (params.zoom * dpr);
        grids->clipVolume(m_clipKey.key, m_clipTime.itemStartTime + pixel / 2.0, pixel, static_cast<size_t>(iw), gain);
        const double track = trackGain();
        if (!gain.empty() && (track < 0.999 || track > 1.001)) {
            for (double& g : gain) {
                g *= track;
            }
        }
        mixhouse::paintHdWave(image, *lane.wave, fromSec, toSec, gain.empty() ? nullptr : &gain);
        painter.drawImage(QRectF(0.0, lane.top, columns, lane.height), image);
    }
    return true;
}

void WaveView::paintSilentGhost(QPainter& painter, const IWavePainter::Params& params, bool rgb)
{
    const auto grids = beatGrids();
    const auto hd = hdWaveforms();
    mixhouse::IMixHouseBeatGrids::ClipFile file;
    if (!grids || !hd || params.zoom <= 0.0 || params.geometry.width < 1.0 || params.geometry.height < 4.0
        || !grids->clipFile(m_clipKey.key, file)) {
        return;
    }
    // The volume line, one value per column: the silent stretches.
    const int columns = static_cast<int>(std::ceil(params.geometry.width));
    std::vector<double> gain;
    if (!grids->clipVolume(m_clipKey.key, m_clipTime.itemStartTime + 0.5 / params.zoom, 1.0 / params.zoom,
                           static_cast<size_t>(columns), gain) || gain.size() < static_cast<size_t>(columns)) {
        return;
    }
    constexpr double kSilent = 0.01;   // -40 dB: nothing heard
    std::vector<std::pair<int, int> > spans;
    for (int x = 0; x < columns;) {
        if (gain[static_cast<size_t>(x)] >= kSilent) {
            ++x;
            continue;
        }
        const int from = x;
        while (x < columns && gain[static_cast<size_t>(x)] < kSilent) {
            ++x;
        }
        spans.push_back({ from, x });
    }
    if (spans.empty()) {
        return;
    }
    const mixhouse::MixHouseHdWavePtr wave = hd->wave(file.path);
    if (!wave) {
        return;
    }
    const qreal dpr = painter.device() ? std::max<qreal>(1.0, painter.device()->devicePixelRatioF()) : 1.0;
    const int iw = std::max(1, static_cast<int>(std::ceil(columns * dpr)));
    const int ih = std::max(2, static_cast<int>(std::round(params.geometry.height * dpr)));
    const double fromSec = (m_clipTime.itemStartTime - file.sequenceStart) / file.ratio;
    const double toSec = (m_clipTime.itemStartTime + columns / params.zoom - file.sequenceStart) / file.ratio;
    QImage image(iw, ih, QImage::Format_ARGB32_Premultiplied);
    mixhouse::paintHdWave(image, *wave, fromSec, toSec, nullptr);
    if (!rgb) {
        // In the clip's own waveform colour, like the audible part.
        QPainter tint(&image);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(image.rect(), getWavePainterParams().style.samplePen);
    }
    painter.save();
    painter.setOpacity(rgb ? 0.3 : 0.4);
    for (const auto& [from, to] : spans) {
        const QRectF target(from, 0.0, to - from, params.geometry.height);
        const QRectF source(from * dpr, 0.0, (to - from) * dpr, ih);
        painter.drawImage(target, image, source);
    }
    painter.restore();
}

// GlideMix: a library track's beat grid, marked the way rekordbox marks
// it (the reference screenshots): a small white tick at the top and bottom
// edge for every beat, and at each bar start a red triangle at both edges with
// a thin white line between them. Two tracks are in sync when their bar lines
// line up, not just their beat ticks. No full-height lines on plain beats: in
// the parked MixHouse DJ app those read as clutter over the waveform.
void WaveView::paintBarMarks(QPainter& painter, const IWavePainter::Params& params, double bottomInset)
{
    const auto grids = beatGrids();
    if (!grids || params.zoom <= 0.0) {
        return;
    }

    const std::vector<mixhouse::IMixHouseBeatGrids::Tick> ticks
        = grids->clipTicks(m_clipKey.key, m_clipTime.itemStartTime, m_clipTime.itemEndTime);
    if (ticks.size() < 2) {
        return;
    }

    static const QColor kBarColor(0xFF, 0x2A, 0x2A);
    static const QColor kBeatColor(0xFF, 0xFF, 0xFF, 230);
    static const QColor kBarLineColor(0xFF, 0xFF, 0xFF, 200);
    constexpr double kTriangleHalfWidth = 4.5;
    constexpr double kTriangleHeight = 6.0;
    constexpr double kBeatTickLength = 5.0;
    constexpr double kMinBeatSpacingPx = 6.0;   // below this, beat ticks become a blur
    // the full-height lines were noise at normal zoom. Bar triangles
    // keep a readable gap; the white lines only appear zoomed in to beats.
    constexpr double kMinBarSpacingPx = 36.0;
    constexpr double kBarLinesFromBeatPx = 22.0;

    const double beatSpacingPx = (ticks[1].time - ticks[0].time) * params.zoom;
    const bool drawBeats = beatSpacingPx >= kMinBeatSpacingPx;
    const bool drawBarLines = beatSpacingPx >= kBarLinesFromBeatPx;
    // The bottom marks sit on top of the phrase ribbon, not over its labels.
    const double h = params.geometry.height - bottomInset;

    painter.save();
    double lastBarX = -1e9;
    for (const auto& tick : ticks) {
        const double x = std::round((tick.time - m_clipTime.itemStartTime) * params.zoom) + 0.5;
        if (!tick.isBar) {
            if (drawBeats) {
                painter.setRenderHint(QPainter::Antialiasing, false);
                painter.setPen(QPen(kBeatColor, 1.0));
                painter.drawLine(QPointF(x, 0.0), QPointF(x, std::min(kBeatTickLength, h)));
                painter.drawLine(QPointF(x, std::max(0.0, h - kBeatTickLength)), QPointF(x, h));
            }
            continue;
        }
        if (x - lastBarX < kMinBarSpacingPx) {
            continue;
        }
        lastBarX = x;

        if (drawBarLines) {
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setPen(QPen(kBarLineColor, 1.0));
            painter.drawLine(QPointF(x, kTriangleHeight), QPointF(x, h - kTriangleHeight));
        }

        painter.setRenderHint(QPainter::Antialiasing, true);
        // A dark edge keeps the red triangle visible on red and orange clips.
        painter.setPen(QPen(QColor(0, 0, 0, 170), 1.0));
        painter.setBrush(kBarColor);
        const QPointF top[3] = { QPointF(x - kTriangleHalfWidth, 0.0), QPointF(x + kTriangleHalfWidth, 0.0), QPointF(x, kTriangleHeight) };
        const QPointF bottom[3] = { QPointF(x - kTriangleHalfWidth, h), QPointF(x + kTriangleHalfWidth, h), QPointF(x, h - kTriangleHeight) };
        painter.drawPolygon(top, 3);
        painter.drawPolygon(bottom, 3);
    }
    painter.restore();
}

// GlideMix: rekordbox's phrase analysis as a ribbon along the bottom
// of the clip -- one solid block per phrase in rekordbox's colours, named
// (upper case, as rekordbox does) when it's wide enough.
double WaveView::paintPhrases(QPainter& painter, const IWavePainter::Params& params)
{
    const auto grids = beatGrids();
    if (!grids || params.zoom <= 0.0) {
        return 0.0;
    }
    const auto phrases = grids->clipPhrases(m_clipKey.key, m_clipTime.itemStartTime, m_clipTime.itemEndTime);
    if (phrases.empty()) {
        return 0.0;
    }
    const double h = params.geometry.height;
    const double ribbon = phraseRibbonHeight(params);
    QFont font = painter.font();
    font.setPixelSize(10);
    font.setBold(true);
    painter.save();
    painter.setFont(font);
    const QFontMetricsF fm(font);
    for (const auto& phrase : phrases) {
        const double x1 = std::round((phrase.time - m_clipTime.itemStartTime) * params.zoom);
        const double x2 = std::round((phrase.endTime - m_clipTime.itemStartTime) * params.zoom);
        const QRectF block(x1, h - ribbon, std::max(1.0, x2 - x1), ribbon);
        // Solid, like rekordbox's ribbon, with a dark seam between phrases.
        painter.fillRect(block, phrase.color);
        painter.fillRect(QRectF(x1, h - ribbon, 1.0, ribbon), QColor(0, 0, 0, 200));
        if (phrase.fillTime >= 0.0) {
            // rekordbox's fill-in: a dotted line through the phrase's last beats.
            const double fx = std::round((phrase.fillTime - m_clipTime.itemStartTime) * params.zoom);
            const double y = h - ribbon / 2.0;
            const double line = std::max(2.0, std::round(ribbon * 0.3));
            painter.fillRect(QRectF(fx, y - line / 2.0, x2 - fx, line), QColor(0, 0, 0, 220));
            const double dot = std::max(1.0, line - 2.0);
            for (double x = fx + 1.0; x + dot <= x2 - 1.0; x += dot * 2.0) {
                painter.fillRect(QRectF(x, y - dot / 2.0, dot, dot), QColor(255, 255, 255));
            }
        }
        if (ribbon >= 10.0) {
            const QString label = fm.elidedText(phrase.label.toUpper(), Qt::ElideRight, block.width() - 6.0);
            if (!label.isEmpty() && label != QStringLiteral("…")) {
                painter.setPen(QColor(255, 255, 255));
                painter.drawText(block.adjusted(4.0, 0.0, -2.0, 0.0), Qt::AlignVCenter | Qt::AlignLeft, label);
            }
        }
    }
    painter.restore();
    return ribbon;
}

// GlideMix: the track's cue points (Lexicon DJ), marked the way DJ
// software marks them: a coloured line with a tag at the top -- a letter for
// hot cues (A, B, ...), a small flag for memory cues -- the cue's name beside
// it when there's room, and loops as a translucent band.
void WaveView::paintCues(QPainter& painter, const IWavePainter::Params& params)
{
    const auto grids = beatGrids();
    if (!grids || params.zoom <= 0.0) {
        return;
    }
    const auto cues = grids->clipCues(m_clipKey.key, m_clipTime.itemStartTime, m_clipTime.itemEndTime);
    if (cues.empty()) {
        return;
    }
    const double h = params.geometry.height;
    constexpr double kTagHeight = 13.0;
    QFont font = painter.font();
    font.setPixelSize(10);
    font.setBold(true);
    painter.save();
    painter.setFont(font);
    const QFontMetricsF fm(font);
    for (const auto& cue : cues) {
        const double x = std::round((cue.time - m_clipTime.itemStartTime) * params.zoom) + 0.5;
        if (cue.endTime > cue.time) {
            const double x2 = (cue.endTime - m_clipTime.itemStartTime) * params.zoom;
            QColor band = cue.color;
            band.setAlpha(60);
            painter.fillRect(QRectF(x, 0.0, std::max(1.0, x2 - x), h), band);
        }
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen(cue.color, 1.0));
        painter.drawLine(QPointF(x, 0.0), QPointF(x, h));

        painter.setRenderHint(QPainter::Antialiasing, true);
        const QString letter = cue.hotCueIndex >= 0 && cue.hotCueIndex < 26
                               ? QString(QChar(u'A' + cue.hotCueIndex)) : QString();
        const double tagWidth = letter.isEmpty() ? 8.0 : std::max(kTagHeight, fm.horizontalAdvance(letter) + 6.0);
        const QRectF tag(x, 0.0, tagWidth, std::min(kTagHeight, h));
        painter.setPen(Qt::NoPen);
        painter.setBrush(cue.color);
        painter.drawRoundedRect(tag, 2.0, 2.0);
        if (!letter.isEmpty()) {
            painter.setPen(QColor(0, 0, 0, 220));
            painter.drawText(tag, Qt::AlignCenter, letter);
        }
        if (!cue.name.isEmpty() && h >= 2 * kTagHeight) {
            const double room = cue.endTime > cue.time ? (cue.endTime - cue.time) * params.zoom : 160.0;
            const QString name = fm.elidedText(cue.name, Qt::ElideRight, std::max(0.0, room - tagWidth - 4.0));
            if (!name.isEmpty()) {
                const QRectF textRect(tag.right() + 3.0, 0.0, fm.horizontalAdvance(name) + 4.0, kTagHeight);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 0, 0, 140));
                painter.drawRoundedRect(textRect, 2.0, 2.0);
                painter.setPen(QColor(255, 255, 255, 235));
                painter.drawText(textRect, Qt::AlignCenter, name);
            }
        }
    }
    painter.restore();
}

// GlideMix: MixMeister's Intro and Outro anchors -- an orange line with
// a flag at the top pointing into the mix (IN right, OUT left). Drag a flag to
// move the anchor (ClipItem); an Outro meeting the next track's Intro is a
// transition.
void WaveView::paintAnchors(QPainter& painter, const IWavePainter::Params& params)
{
    const auto grids = beatGrids();
    if (!grids || params.zoom <= 0.0) {
        return;
    }
    const auto anchors = grids->clipAnchors(m_clipKey.key);
    if (!anchors.valid) {
        return;
    }
    const QColor orange(0xFF, 0x8A, 0x3A);
    const double h = params.geometry.height;
    QFont font = painter.font();
    font.setPixelSize(9);
    font.setBold(true);
    painter.save();
    painter.setFont(font);
    const auto draw = [&](double time, bool intro) {
        if (time < m_clipTime.itemStartTime - 1.0 || time > m_clipTime.itemEndTime + 1.0) {
            return;
        }
        const double x = std::round((time - m_clipTime.itemStartTime) * params.zoom) + 0.5;
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen(orange, 1.6));
        painter.drawLine(QPointF(x, 0.0), QPointF(x, h));
        painter.setRenderHint(QPainter::Antialiasing, true);
        constexpr double kFlagTop = 14.0;
        constexpr double kFlagHeight = 13.0;
        constexpr double kFlagWidth = 24.0;
        QPainterPath flag;
        if (intro) {
            flag.moveTo(x, kFlagTop);
            flag.lineTo(x + kFlagWidth, kFlagTop);
            flag.lineTo(x + kFlagWidth - 4.0, kFlagTop + kFlagHeight / 2.0);
            flag.lineTo(x + kFlagWidth, kFlagTop + kFlagHeight);
            flag.lineTo(x, kFlagTop + kFlagHeight);
        } else {
            flag.moveTo(x, kFlagTop);
            flag.lineTo(x - kFlagWidth, kFlagTop);
            flag.lineTo(x - kFlagWidth + 4.0, kFlagTop + kFlagHeight / 2.0);
            flag.lineTo(x - kFlagWidth, kFlagTop + kFlagHeight);
            flag.lineTo(x, kFlagTop + kFlagHeight);
        }
        flag.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(orange);
        painter.drawPath(flag);
        painter.setPen(QColor(0x14, 0x0C, 0x06));
        const QRectF label = intro ? QRectF(x + 1.0, kFlagTop, kFlagWidth - 6.0, kFlagHeight)
                                   : QRectF(x - kFlagWidth + 5.0, kFlagTop, kFlagWidth - 6.0, kFlagHeight);
        painter.drawText(label, Qt::AlignCenter, intro ? QStringLiteral("IN") : QStringLiteral("OUT"));
    };
    draw(anchors.intro, true);
    draw(anchors.outro, false);
    painter.restore();
}

QString WaveView::keyText() const
{
    if (!keyMatch()) {
        return QString();
    }
    const auto info = keyMatch()->keyInfo(m_clipKey.key);
    return info.effectiveKeyId > 0 ? au::mixhouse::IMixHouseKeyMatch::camelot(info.effectiveKeyId) : QString();
}

QVariantList WaveView::anchorPositions() const
{
    const auto grids = beatGrids();
    const auto anchors = grids ? grids->clipAnchors(m_clipKey.key) : mixhouse::IMixHouseBeatGrids::Anchors {};
    const double zoom = m_context ? m_context->zoom() : 0.0;
    if (!anchors.valid || zoom <= 0.0) {
        return { -1.0, -1.0 };
    }
    return { (anchors.intro - m_clipTime.itemStartTime) * zoom, (anchors.outro - m_clipTime.itemStartTime) * zoom };
}

void WaveView::dragAnchor(int which, double x, bool completed)
{
    const auto grids = beatGrids();
    const double zoom = m_context ? m_context->zoom() : 0.0;
    if (grids && zoom > 0.0) {
        grids->moveClipAnchor(m_clipKey.key, which, m_clipTime.itemStartTime + x / zoom, completed);
    }
}

namespace {
//! The transition an anchor belongs to: the OUT flag's pair has this clip
//! going out, the IN flag's has it coming in.
std::optional<au::mixhouse::IMixHouseTransitions::Zone> anchorZone(const au::mixhouse::IMixHouseTransitions* transitions,
                                                                   const au::trackedit::ClipKey& clip, int which)
{
    if (!transitions) {
        return std::nullopt;
    }
    for (const auto& zone : transitions->zones()) {
        if ((which == 1 ? zone.outgoing : zone.incoming) == clip) {
            return zone;
        }
    }
    return std::nullopt;
}
}

QVariantList WaveView::anchorTransitionMenu(int which) const
{
    const auto zone = anchorZone(transitions().get(), m_clipKey.key, which);
    if (!zone) {
        return { QVariantMap { { "id", "none" }, { "title", muse::qtrc("projectscene", "No transition at this anchor") },
                               { "enabled", false } } };
    }
    const std::string current = zone->crossfaded && !zone->disabled ? zone->preset : std::string();
    const auto item = [&](const char* name, const QString& title) {
        return QVariantMap { { "id", QString("transition:") + name }, { "title", title }, { "enabled", true },
                             { "checkable", true }, { "checked", current == name } };
    };
    QVariantList menu;
    menu << item(au::mixhouse::kVolumeFadeTransition, muse::qtrc("projectscene", "Volume Fade (MixMeister)"));
    menu << item(au::mixhouse::kGlideMixTransition,
                 muse::qtrc("projectscene", "GlideMix transition (crossfade + bass swap)"));
    menu << QVariantMap {};
    if (zone->disabled) {
        menu << QVariantMap { { "id", "enable" }, { "title", muse::qtrc("projectscene", "Turn transition on") },
                              { "enabled", true } };
    } else {
        menu << QVariantMap { { "id", "disable" }, { "title", muse::qtrc("projectscene", "Turn transition off") },
                              { "enabled", true } };
    }
    return menu;
}

void WaveView::handleAnchorTransitionMenu(int which, const QString& itemId)
{
    const auto zone = anchorZone(transitions().get(), m_clipKey.key, which);
    if (!zone) {
        return;
    }
    if (itemId.startsWith("transition:")) {
        transitions()->applyTransition(zone->outgoing, zone->incoming, itemId.mid(11).toStdString());
    } else if (itemId == "disable" || itemId == "enable") {
        transitions()->setTransitionEnabled(zone->outgoing, zone->incoming, itemId == "enable");
    }
}

void WaveView::resetAnchors()
{
    if (const auto grids = beatGrids()) {
        grids->resetClipAnchors(m_clipKey.key);
    }
}

void WaveView::paintRecordingPlaceholder(QPainter& painter, const IWavePainter::Params& params)
{
    trackedit::ITrackeditProjectPtr prj = globalContext()->currentTrackeditProject();
    if (!prj) {
        return;
    }

    trackedit::Clip clip = prj->clip(m_clipKey.key);
    if (!clip.isValid() || m_clipTime.itemEndTime <= clip.endTime) {
        return;
    }

    const double x0 = std::max(0.0, (clip.endTime - m_clipTime.itemStartTime) * params.zoom);
    const double x1 = std::min(width(), (m_clipTime.itemEndTime - m_clipTime.itemStartTime) * params.zoom);
    if (x1 - x0 < 1.0) {
        return;
    }

    const double bandsHeight = params.geometry.height;
    std::vector<std::pair<double, double> > channelBands;
    if (muse::is_equal(m_channelHeightRatio, 1.0)) {
        channelBands.emplace_back(0.0, bandsHeight);
    } else {
        channelBands.emplace_back(0.0, bandsHeight * m_channelHeightRatio);
        channelBands.emplace_back(bandsHeight * m_channelHeightRatio, bandsHeight * (1.0 - m_channelHeightRatio));
    }

    for (const auto& [top, bandHeight] : channelBands) {
        const double centerY = top + bandHeight / 2.0;

        painter.setPen(params.style.samplePen);
        painter.drawLine(QPointF(x0, centerY), QPointF(x1, centerY));
    }
}

ClipKey WaveView::clipKey() const
{
    return m_clipKey;
}

TimelineContext* WaveView::timelineContext() const
{
    return m_context;
}

void WaveView::setTimelineContext(TimelineContext* newContext)
{
    if (m_context == newContext) {
        return;
    }

    if (m_context) {
        disconnect(m_context, nullptr, this, nullptr);
    }

    m_context = newContext;

    if (m_context) {
        connect(m_context, &TimelineContext::frameTimeChanged, this, &WaveView::updateView);
        connect(m_context, &TimelineContext::selectionStartTimeChanged, this, &WaveView::updateView);
        connect(m_context, &TimelineContext::selectionEndTimeChanged, this, &WaveView::updateView);
        connect(m_context, &TimelineContext::zoomChanged, this, &WaveView::onWaveZoomChanged);

        onWaveZoomChanged();
    }

    emit timelineContextChanged();
}

void WaveView::updateView()
{
    update();
}

QColor WaveView::clipColor() const
{
    return m_clipColor;
}

double WaveView::colorIntensity() const
{
    return m_colorIntensity;
}

void WaveView::setColorIntensity(double intensity)
{
    if (muse::RealIsEqual(m_colorIntensity, intensity)) {
        return;
    }
    m_colorIntensity = intensity;
    emit colorIntensityChanged();
    emit backgroundColorChanged();
    update();
}

QColor WaveView::canvasColor() const
{
    return m_canvasColor;
}

void WaveView::setCanvasColor(const QColor& color)
{
    if (m_canvasColor == color) {
        return;
    }
    m_canvasColor = color;
    emit canvasColorChanged();
    emit backgroundColorChanged();
    update();
}

void WaveView::setClipColor(const QColor& newClipColor)
{
    if (m_clipColor == newClipColor) {
        return;
    }
    m_clipColor = newClipColor;
    emit clipColorChanged();
    emit backgroundColorChanged();

    update();
}

QColor WaveView::clipSelectedColor() const
{
    return m_clipSelectedColor;
}

void WaveView::setClipSelectedColor(const QColor& newClipSelectedColor)
{
    if (m_clipSelectedColor == newClipSelectedColor) {
        return;
    }
    m_clipSelectedColor = newClipSelectedColor;
    emit clipSelectedColorChanged();
    emit backgroundColorChanged();

    update();
}

bool WaveView::clipSelected() const
{
    return m_clipSelected;
}

void WaveView::setClipSelected(bool newClipSelected)
{
    if (m_clipSelected == newClipSelected) {
        return;
    }
    m_clipSelected = newClipSelected;
    emit clipSelectedChanged();
    emit backgroundColorChanged();

    update();
}

QColor WaveView::backgroundColor() const
{
    const IWavePainter::Params params = getWavePainterParams();

    const bool endWithinSelection = m_clipTime.selectionStartTime < m_clipTime.selectionEndTime
                                    && m_clipTime.selectionStartTime <= m_clipTime.endTime
                                    && m_clipTime.selectionEndTime >= m_clipTime.endTime;

    return endWithinSelection ? params.style.selectedBackground : params.style.normalBackground;
}

ClipTime WaveView::clipTime() const
{
    return m_clipTime;
}

void WaveView::setClipTime(const ClipTime& newClipTime)
{
    if (m_clipTime == newClipTime) {
        return;
    }
    m_clipTime = newClipTime;
    emit clipTimeChanged();
    emit backgroundColorChanged();

    update();
}

double WaveView::channelHeightRatio() const
{
    return m_channelHeightRatio;
}

void WaveView::setChannelHeightRatio(double channelHeightRatio)
{
    m_channelHeightRatio = channelHeightRatio;
    emit channelHeightRatioChanged();
    update();
}

bool WaveView::isNearSample() const
{
    return isVisible() && m_isNearSample;
}

void WaveView::setIsNearSample(bool isNearSample)
{
    if (m_isNearSample == isNearSample) {
        return;
    }

    m_isNearSample = isNearSample;
    emit isNearSampleChanged();
}

bool WaveView::isStemPlot() const
{
    return m_isStemPlot;
}

void WaveView::setIsStemPlot(bool isStemPlot)
{
    if (m_isStemPlot == isStemPlot) {
        return;
    }

    m_isStemPlot = isStemPlot;
    emit isStemPlotChanged();
}

int WaveView::currentChannel() const
{
    return m_currentChannel.value_or(0);
}

void WaveView::setCurrentChannel(int currentChannel)
{
    m_currentChannel = currentChannel;
}

bool WaveView::isIsolationMode() const
{
    return m_isIsolationMode;
}

void WaveView::setIsIsolationMode(bool isIsolationMode)
{
    if (m_isIsolationMode == isIsolationMode) {
        return;
    }

    m_isIsolationMode = isIsolationMode;
    emit isIsolationModeChanged();
}

void WaveView::setMultiSampleEdit(bool multiSampleEdit)
{
    if (m_multiSampleEdit == multiSampleEdit) {
        return;
    }

    m_multiSampleEdit = multiSampleEdit;
    emit multiSampleEditChanged();
}

bool WaveView::multiSampleEdit() const
{
    return m_multiSampleEdit;
}

void WaveView::setIsBrush(bool isBrush)
{
    if (m_isBrush == isBrush) {
        return;
    }

    m_isBrush = isBrush;
    emit isBrushChanged();
}

bool WaveView::isBrush() const
{
    return m_isBrush;
}

bool WaveView::isLinear() const
{
    return m_isLinear;
}

void WaveView::setIsLinear(bool isLinear)
{
    if (m_isLinear == isLinear) {
        return;
    }

    m_isLinear = isLinear;
    update();
}

double WaveView::dbRange() const
{
    return m_dbRange;
}

void WaveView::setDbRange(double dbRange)
{
    if (m_dbRange == dbRange) {
        return;
    }

    m_dbRange = dbRange;
    update();
}

QVariant WaveView::displayBounds() const
{
    QMap<QString, float> bounds;
    bounds["min"] = m_displayBounds.first;
    bounds["max"] = m_displayBounds.second;
    return QVariant::fromValue(bounds);
}

void WaveView::setDisplayBounds(const QVariant& displayBounds)
{
    float minBound = displayBounds.toMap().value("min", -1.0f).toFloat();
    float maxBound = displayBounds.toMap().value("max", 1.0f).toFloat();

    if (m_displayBounds.first == minBound && m_displayBounds.second == maxBound) {
        return;
    }

    m_displayBounds.first = minBound;
    m_displayBounds.second = maxBound;

    update();
}

QColor WaveView::transformColor(const QColor& originalColor) const
{
    int r = originalColor.red();
    int g = originalColor.green();
    int b = originalColor.blue();

    int deltaRed = (r < 240) ? 51 : (255 - r);
    int deltaGreen = (g < 240) ? 69 : (255 - g);
    int deltaBlue = 77;

    int newRed = qBound(0, r + deltaRed, 255);
    int newGreen = qBound(0, g + deltaGreen, 255);
    int newBlue = qBound(0, b + deltaBlue, 255);

    return QColor(newRed, newGreen, newBlue);
}

void WaveView::setLastMousePos(const unsigned int x, const unsigned int y)
{
    if (wavepainterutils::getPlotType(globalContext()->currentProject(), m_clipKey.key,
                                      m_context->zoom()) != IWavePainter::PlotType::Stem) {
        return;
    }

    const auto params = getWavePainterParams();
    m_currentChannel =  samplespainterutils::hitNearestSampleChannelIndex(globalContext()->currentProject(), m_clipKey.key, QPoint(x,
                                                                                                                                   y),
                                                                          params);
    setIsNearSample(m_currentChannel.has_value());
}

void WaveView::setLastClickPos(const unsigned lastX, const unsigned lastY, const unsigned int x, const unsigned int y)
{
    if (wavepainterutils::getPlotType(globalContext()->currentProject(), m_clipKey.key,
                                      m_context->zoom()) != IWavePainter::PlotType::Stem) {
        return;
    }

    // Prevent sample editing during playback
    if (playbackState()->isPlaying()) {
        return;
    }

    const auto currentPosition = QPoint(x, y);
    const auto lastPosition = QPoint(lastX, lastY);

    const auto params = getWavePainterParams();

    if (!m_currentChannel.has_value()) {
        m_currentChannel = samplespainterutils::hitNearestSampleChannelIndex(
            globalContext()->currentProject(), m_clipKey.key, currentPosition, params);
        return;
    }

    samplespainterutils::setLastClickPos(
        m_currentChannel.value(),
        globalContext()->currentProject(), m_clipKey.key, lastPosition, currentPosition, params);

    m_lastClickedPoint = currentPosition;
}

void WaveView::smoothLastClickPos(unsigned int x, const unsigned int y)
{
    if (!m_isStemPlot) {
        return;
    }

    // Prevent sample editing during playback
    if (playbackState()->isPlaying()) {
        return;
    }

    const auto currentPosition = QPoint(x, y);
    const auto params = getWavePainterParams();

    auto channel = samplespainterutils::hitChannelIndex(globalContext()->currentProject(), m_clipKey.key, currentPosition, params);

    if (!channel) {
        return;
    }

    samplespainterutils::smoothLastClickPos(
        channel.value(),
        globalContext()->currentProject(), m_clipKey.key, currentPosition, params);

    //! NOTE: History state is only pushed when data is actually changed.
    // For smooth edition there is no data change on button press or release
    // just on mouse click.
    pushProjectHistorySampleEdit();
}

void WaveView::setIsolatedPoint(const unsigned int x, const unsigned int y)
{
    if (!m_isStemPlot) {
        return;
    }

    if (!m_isIsolationMode) {
        return;
    }

    // Prevent sample editing during playback
    if (playbackState()->isPlaying()) {
        return;
    }

    if (!m_lastClickedPoint.has_value()) {
        return;
    }

    const auto currentPosition = QPoint(x, y);
    const auto params = getWavePainterParams();

    if (!m_currentChannel.has_value()) {
        m_currentChannel = samplespainterutils::hitNearestSampleChannelIndex(
            globalContext()->currentProject(), m_clipKey.key, currentPosition, params);
        return;
    }

    samplespainterutils::setIsolatedPoint(
        m_currentChannel.value(),
        m_clipKey.key, globalContext()->currentProject(), m_lastClickedPoint.value(), currentPosition, params);
}

void WaveView::onWaveZoomChanged()
{
    const IWavePainter::PlotType currentPlotType = wavepainterutils::getPlotType(globalContext()->currentProject(), m_clipKey.key,
                                                                                 m_context->zoom());
    const bool wasStemPlot = m_isStemPlot;
    const bool isStemPlot = currentPlotType == IWavePainter::PlotType::Stem;

    if (wasStemPlot != isStemPlot) {
        setIsStemPlot(isStemPlot);
        if (!isStemPlot && m_isNearSample) {
            // force isNearSample to false when transitioning away from stem plot mode
            setIsNearSample(false);
        }
        // Note: When transitioning TO stem plot mode, ClipItem.qml onIsStemPlotChanged
        // will trigger mouse position update to force isNearSample to be set correctly
    }

    update();
}

void WaveView::pushProjectHistorySampleEdit()
{
    projectHistory()->pushHistoryState(muse::trc("projectscene", "Moved Samples"), muse::trc("projectscene", "Sample Edit"),
                                       trackedit::UndoPushType::CONSOLIDATE);
}

au::context::IPlaybackStatePtr WaveView::playbackState() const
{
    return globalContext()->playbackState();
}

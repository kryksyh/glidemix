/*
 * GlideMix Samples tab (plan B1): the user's samples as a flat list --
 * filtered by category tile, pack and search, sorted by any column. The
 * list comes from IMixHouseSamples (all in memory) and is filtered here.
 */
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QTimer>
#include <atomic>
#include <memory>
#include <QQmlParserStatus>
#include <QVariantList>
#include <QtQmlIntegration>
#include <vector>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "context/iglobalcontext.h"
#include "mixhouse/imixhouselibraryconfiguration.h"
#include "mixhouse/imixhousesamples.h"
#include "mixhouse/imixhousetempomap.h"

namespace au::mixhouse {
class MixHousePreviewPlayer;

class MixHouseSamplesModel : public QAbstractListModel, public QQmlParserStatus, public muse::Injectable,
    public muse::async::Asyncable
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY filterChanged)
    Q_PROPERTY(QString pack READ pack WRITE setPack NOTIFY filterChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY filterChanged)
    //! "Show similar sounds" from a clip: the sample the list is ranked against ("" = off).
    Q_PROPERTY(QString similarName READ similarName NOTIFY filterChanged)
    Q_PROPERTY(QString sortKey READ sortKey NOTIFY sortChanged)
    Q_PROPERTY(bool ascending READ ascending NOTIFY sortChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY samplesChanged)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY samplesChanged)
    Q_PROPERTY(QStringList packs READ packs NOTIFY packsChanged)
    Q_PROPERTY(QStringList folders READ folders NOTIFY samplesChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanChanged)
    Q_PROPERTY(QString scanText READ scanText NOTIFY scanChanged)
    //! The sample playing in the preview (its path), empty when none.
    Q_PROPERTY(QString previewPath READ previewPath NOTIFY previewChanged)
    //! The same preview level as the library's slider (one setting).
    Q_PROPERTY(double previewVolume READ previewVolume WRITE setPreviewVolume NOTIFY previewChanged)
    //! Where the preview is in its sample, 0..1 (the playhead on its waveform).
    Q_PROPERTY(double previewPosition READ previewPosition NOTIFY previewPositionChanged)
    //! Counts up as waveforms arrive (the selected-sample strip re-reads).
    Q_PROPERTY(int overviewsReady READ overviewsReady NOTIFY overviewsChanged)

    muse::GlobalInject<IMixHouseSamples> samples;
    muse::GlobalInject<IMixHouseLibraryConfiguration> libraryConfiguration;
    muse::Inject<context::IGlobalContext> globalContext = { this };
    muse::Inject<IMixHouseTempoMap> tempoMap = { this };

public:
    enum Roles {
        NameRole = Qt::UserRole + 1, CategoryRole, PackRole, TypeRole, BpmRole, KeyRole, KeyIdRole, BarsRole,
        LengthRole, FormatRole, LocationRole, DateAddedRole, MissingRole, PathRole, OverviewRole
    };

    explicit MixHouseSamplesModel(QObject* parent = nullptr);
    ~MixHouseSamplesModel() override;

    //! One preview at a time across song lists and the Samples tab.
    static void stopAllPreviews();

    void classBegin() override {}
    void componentComplete() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString category() const { return m_category; }
    void setCategory(const QString& category);
    QString pack() const { return m_pack; }
    void setPack(const QString& pack);
    QString search() const { return m_search; }
    QString similarName() const;
    Q_INVOKABLE void clearSimilar();
    void setSearch(const QString& search);
    QString sortKey() const { return m_sortKey; }
    bool ascending() const { return m_ascending; }
    int totalCount() const;
    QVariantList categories() const;   ///< [{name, count}] in name order, "All" first
    QStringList packs() const { return m_packs; }
    QStringList folders() const;
    bool scanning() const;
    QString scanText() const { return m_scanText; }

    //! Same column: flip; new column: start ascending (name, pack...) or
    //! descending (date added).
    Q_INVOKABLE void sortBy(const QString& key);
    Q_INVOKABLE void addFolder(const QString& folder);
    Q_INVOKABLE void removeFolder(const QString& folder);
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void revealInFileBrowser(int row) const;
    Q_INVOKABLE QString pathAt(int row) const;
    //! Drags the sample out (onto a song's sample line, or another app).
    Q_INVOKABLE void startDrag(int row);
    //! Play button: plays the sample (a loop at the mix's tempo, over and
    //! over; a one-shot once, at its own speed), or stops it.
    Q_INVOKABLE void togglePreview(int row);
    //! Click on a sample's waveform: plays it from there (like the library).
    Q_INVOKABLE void previewAt(int row, double fraction);
    double previewPosition() const { return m_previewPosition; }
    int overviewsReady() const { return m_overviewsReady; }
    //! Everything about one row (the selected-sample strip).
    Q_INVOKABLE QVariantMap sampleInfo(int row) const;
    Q_INVOKABLE void stopPreview();
    QString previewPath() const { return m_previewPath; }
    double previewVolume() const;
    void setPreviewVolume(double volume);
    //! The row of the sample playing, or -1.
    Q_INVOKABLE int previewRow() const;

signals:
    //! A clip asked for this tab: the panel brings its tab to the front.
    void raiseRequested();
    void filterChanged();
    void sortChanged();
    void samplesChanged();
    void packsChanged();
    void scanChanged();
    void previewChanged();
    void previewPositionChanged();
    void overviewsChanged();

private:
    void rebuild();
    void updatePacks();
    void takeShowRequest();
    bool applyShow();
    QString m_similarTo;     ///< path; the list holds the sounds most like it
    QString m_showPending;   ///< asked for before the scan found the file
    bool m_showPendingSimilar = false;

    std::vector<int> m_rows;   ///< indexes into samples()->samples(), filtered and sorted
    QString m_category;
    QString m_pack;
    QString m_search;
    QString m_sortKey = QStringLiteral("name");
    bool m_ascending = true;
    QStringList m_packs;
    QString m_scanText;

    double previewRate(const MixHouseSample& s) const;
    std::unique_ptr<MixHousePreviewPlayer> m_player;
    QTimer m_previewPoll;
    QString m_previewPath;
    bool m_previewLoop = false;
    double m_previewPosition = 0.0;
    void startPreview(int row, double fraction);

    // Waveforms, made on demand for the rows on screen (one worker thread).
    void requestOverview(const QString& path) const;
    mutable QHash<QString, QVariantList> m_overviews;
    mutable QStringList m_overviewQueue;
    mutable bool m_overviewBusy = false;
    int m_overviewsReady = 0;
    std::shared_ptr<std::atomic<bool> > m_alive = std::make_shared<std::atomic<bool> >(true);
};
}

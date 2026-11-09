/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <QHash>

#include "framework/uicomponents/qml/Muse/UiComponents/selectableitemlistmodel.h"
#include "framework/global/async/asyncable.h"
#include "framework/global/modularity/ioc.h"
#include "framework/ui/iuiconfiguration.h"
#include "framework/ui/iuistate.h"
#include "framework/ui/iuiactionsregister.h"

#include "playback/iplaybackconfiguration.h"
#include "record/irecordconfiguration.h"

class QItemSelectionModel;

#if (defined(_MSCVER) || defined(_MSC_VER))
// unreferenced function with internal linkage has been removed
#pragma warning(disable: 4505)
#endif

namespace au::projectscene {
class PlaybackToolBarCustomiseItem;
class PlaybackToolBarCustomiseModel : public muse::uicomponents::SelectableItemListModel, public muse::Contextable,
    public muse::async::Asyncable
{
    Q_OBJECT

    muse::GlobalInject<muse::ui::IUiConfiguration> uiConfiguration;
    muse::GlobalInject<au::playback::IPlaybackConfiguration> configuration;
    muse::GlobalInject<au::record::IRecordConfiguration> recordConfiguration;

    muse::ContextInject<muse::ui::IUiActionsRegister> actionsRegister{ this };
    muse::ContextInject<muse::ui::IUiState> uiState { this };

    Q_PROPERTY(QItemSelectionModel * selectionModel READ selectionModel NOTIFY selectionChanged)
    Q_PROPERTY(bool isAddSeparatorAvailable READ isAddSeparatorAvailable NOTIFY isAddSeparatorAvailableChanged)

public:
    explicit PlaybackToolBarCustomiseModel(QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QItemSelectionModel* selectionModel() const;
    bool isAddSeparatorAvailable() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void addSeparatorLine();

    //! GlideMix: the Safari-style Customize Toolbar sheet works on plain
    //! lists: [{ id, title, icon, shown, separator }] in toolbar order
    //! (hidden items keep their place). applyEntries saves at once, so the
    //! toolbar changes while the sheet is open.
    Q_INVOKABLE QVariantList entries() const;
    Q_INVOKABLE QVariantList defaultEntries() const;
    Q_INVOKABLE void applyEntries(const QVariantList& entries);
    Q_INVOKABLE void restoreDefault();
    //! 0 icon and text, 1 icon only, 2 text only.
    Q_INVOKABLE int displayStyle() const;
    Q_INVOKABLE void setDisplayStyle(int style);

signals:
    void isAddSeparatorAvailableChanged(bool isAddSeparatorAvailable);

private:
    enum Roles {
        ItemRole = SelectableItemListModel::UserRole + 1
    };

    PlaybackToolBarCustomiseItem* modelIndexToItem(const QModelIndex& index) const;

    void onUpdateOperationsAvailability() override;
    void onRowsMoved() override;
    void onRowsRemoved() override;

    void updateRemovingAvailability();
    void updateAddSeparatorAvailability();
    void setIsAddSeparatorAvailable(bool isAddSeparatorAvailable);

    PlaybackToolBarCustomiseItem* makeItem(const muse::ui::UiAction& action, bool checked);
    PlaybackToolBarCustomiseItem* makeSeparatorItem();

    QColor iconColor(const muse::ui::UiAction& action) const;

    void saveActions();
    QVariantList entriesOf(const muse::ui::ToolConfig& config) const;

    bool m_isAddSeparatorAvailable = false;
};
}

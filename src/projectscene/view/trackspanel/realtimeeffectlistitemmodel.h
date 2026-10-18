/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include "mixhouse/imixhouselibraryconfiguration.h"
#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "effects/effects_base/ieffectsprovider.h"
#include "effects/effects_base/irealtimeeffectservice.h"
#include "effects/effects_base/ieffectviewcontroller.h"

#include <QObject>
#include <QVariantList>
#include <QTimer>

#include <chrono>
#include "context/iglobalcontext.h"
#include "effects/effects_base/iparameterextractorregistry.h"
#include "automation/iclipgaininteraction.h"
#include "trackedit/iprojecthistory.h"

namespace au::projectscene {
class RealtimeEffectListItemModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    Q_PROPERTY(bool isAvailable READ prop_isAvailable CONSTANT)
    Q_PROPERTY(bool isActive READ prop_isActive WRITE prop_setIsActive NOTIFY isActiveChanged)
    Q_PROPERTY(bool isMasterEffect READ prop_isMasterEffect CONSTANT)
    Q_PROPERTY(QString automationColor READ prop_automationColor CONSTANT)
    Q_PROPERTY(QString automationMode READ prop_automationMode WRITE prop_setAutomationMode NOTIFY automationModeChanged)
    Q_PROPERTY(bool supportsAutomation READ supportsAutomation CONSTANT)
    //! The parameter this effect shows in the automation lane, or empty.
    Q_PROPERTY(QString focusedParameterId READ focusedParameterId NOTIFY automationFocusChanged)
    //! Ableton: the lane's parameter was overridden by moving its control.
    Q_PROPERTY(bool automationOverridden READ automationOverridden NOTIFY automationFocusChanged)
    //! The lane's parameter has a line on this track (Delete automation applies).
    Q_PROPERTY(bool automationExists READ automationExists NOTIFY automationFocusChanged)

    muse::GlobalInject<effects::IEffectsProvider> effectsProvider;
    muse::ContextInject<effects::IRealtimeEffectService> realtimeEffectService{ this };
    muse::ContextInject<effects::IEffectViewController> effectViewController{ this };
    muse::ContextInject<context::IGlobalContext> globalContext{this};
    muse::GlobalInject<effects::IParameterExtractorRegistry> parameterExtractors;
    muse::ContextInject<automation::IClipGainInteraction> clipGain{ this };
    muse::ContextInject<trackedit::IProjectHistory> projectHistory{ this };
    muse::GlobalInject<mixhouse::IMixHouseLibraryConfiguration> libraryConfiguration;

public:
    RealtimeEffectListItemModel(QObject* parent, effects::RealtimeEffectStatePtr effectState);
    ~RealtimeEffectListItemModel();

    Q_INVOKABLE QString effectState() const;
    effects::RealtimeEffectStatePtr effectStatePtr() const;
    Q_INVOKABLE QString effectName() const;
    Q_INVOKABLE void showEffectDialog();
    Q_INVOKABLE QVariantList automationParameters() const;
    QVariantList rawAutomationParameters() const;
    bool supportsAutomation() const;
    Q_INVOKABLE bool focusAutomation(const QString& parameterId);
    Q_INVOKABLE void hideAutomation();
    QString focusedParameterId() const;
    bool automationOverridden() const;
    static QString parameterColor(const std::string& instanceId, const std::string& parameterId);
    bool automationExists() const;
    Q_INVOKABLE void reenableAutomation();
    Q_INVOKABLE void deleteAutomation();

    bool prop_isAvailable() const;
    bool prop_isActive() const;
    void prop_setIsActive(bool isActive);
    bool prop_isMasterEffect() const;
    QString prop_automationColor() const;
    QString prop_automationMode() const;
    void prop_setAutomationMode(const QString& mode);

signals:
    void isActiveChanged();
    void automationModeChanged();
    void automationFocusChanged();

private:
    std::weak_ptr<RealtimeEffectState> m_effectState;
    QTimer m_gestureTimer;
    int m_gestureTicks = 0;
    void followParameterValue();
    void overrideAutomation(const std::string& parameterId);
    void recordTouch(const std::string& parameterId);
    void continueRecording();
    void finishRecording();
    std::string m_recordParameter;
    double m_recordPrevious = -1.0;
    bool m_recordWrote = false;
    std::chrono::steady_clock::time_point m_recordLastTouch;
};

using RealtimeEffectListItemModelPtr = std::shared_ptr<RealtimeEffectListItemModel>;
}

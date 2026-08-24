#pragma once

#include <QObject>
#include <QProcess>
#include <QMetaObject>

#include <optional>

#include "platform/displayinputpolicy.h"

// Moonlight-owned monitor input switching for the local G2724D setup.
// Requests are sent directly to m1ddc without a shell and are serialized.
class DisplayInputController : public QObject
{
    Q_OBJECT

public:
    static constexpr int InputDp1 = DisplayInputPolicy::InputDp1;
    static constexpr int InputHdmi1 = DisplayInputPolicy::InputHdmi1;

    explicit DisplayInputController(QObject* parent = nullptr);
    ~DisplayInputController() override;

    static DisplayInputController* instance();
    static bool isRemoteInputAudioApplication(const QString& applicationName);

    void scheduleAutomaticSwitch(const QString& applicationName);

public slots:
    void switchToDp1();
    void switchToHdmi1();

private slots:
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessError(QProcess::ProcessError error);
    void handleSessionFinished(int portTestResult);

private:
    void registerGlobalHotkeys();
    void unregisterGlobalHotkeys();
    void requestInput(int inputValue, const char* reason);
    void startInputRequest(int inputValue, const char* reason);
    void startPendingRequest();
    void activateStreamingWindow();
    void restoreMoonlightUi();
    QString locateBetterDisplay() const;
    QString locateM1ddc() const;

    QProcess m_Process;
    std::optional<int> m_PendingInput;
    QString m_PendingReason;
    QString m_ActiveBackend;
    QMetaObject::Connection m_SessionFinishedConnection;
    void* m_EventHandler = nullptr;
    void* m_Dp1Hotkey = nullptr;
    void* m_HdmiHotkey = nullptr;

    static DisplayInputController* s_Instance;
};

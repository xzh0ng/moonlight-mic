#include "displayinputcontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QWindow>

#include <Carbon/Carbon.h>
#include <AppKit/AppKit.h>

#include "SDL_compat.h"
#include "streaming/session.h"

namespace {

constexpr EventHotKeyID kDp1HotkeyId = { 'MLDD', 1 };
constexpr EventHotKeyID kHdmiHotkeyId = { 'MLDD', 2 };

OSStatus globalHotkeyHandler(EventHandlerCallRef, EventRef event, void* userData)
{
    EventHotKeyID hotkeyId = {};
    OSStatus status = GetEventParameter(event,
                                        kEventParamDirectObject,
                                        typeEventHotKeyID,
                                        nullptr,
                                        sizeof(hotkeyId),
                                        nullptr,
                                        &hotkeyId);
    if (status != noErr || hotkeyId.signature != kDp1HotkeyId.signature) {
        return eventNotHandledErr;
    }

    auto* controller = static_cast<DisplayInputController*>(userData);
    if (hotkeyId.id == kDp1HotkeyId.id) {
        QMetaObject::invokeMethod(controller, "switchToDp1", Qt::QueuedConnection);
        return noErr;
    }
    if (hotkeyId.id == kHdmiHotkeyId.id) {
        QMetaObject::invokeMethod(controller, "switchToHdmi1", Qt::QueuedConnection);
        return noErr;
    }
    return eventNotHandledErr;
}

} // namespace

DisplayInputController* DisplayInputController::s_Instance = nullptr;

DisplayInputController::DisplayInputController(QObject* parent) :
    QObject(parent)
{
    Q_ASSERT(s_Instance == nullptr);
    s_Instance = this;

    connect(&m_Process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            &DisplayInputController::handleProcessFinished);
    connect(&m_Process,
            &QProcess::errorOccurred,
            this,
            &DisplayInputController::handleProcessError);

    registerGlobalHotkeys();
}

DisplayInputController::~DisplayInputController()
{
    unregisterGlobalHotkeys();
    if (m_Process.state() != QProcess::NotRunning) {
        m_Process.kill();
        m_Process.waitForFinished(1000);
    }
    s_Instance = nullptr;
}

DisplayInputController* DisplayInputController::instance()
{
    return s_Instance;
}

bool DisplayInputController::isRemoteInputAudioApplication(const QString& applicationName)
{
    return DisplayInputPolicy::isRemoteInputAudioApplication(applicationName);
}

void DisplayInputController::scheduleAutomaticSwitch(const QString& applicationName)
{
    if (!isRemoteInputAudioApplication(applicationName)) {
        return;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Display input: Remote Input + Audio connected; scheduling DP1 switch");
    QTimer::singleShot(500, this, [this]() {
        activateStreamingWindow();
        QTimer::singleShot(150, this, [this]() {
            requestInput(InputDp1, "Remote Input + Audio connected");
        });
    });
}

void DisplayInputController::switchToDp1()
{
    activateStreamingWindow();
    QTimer::singleShot(150, this, [this]() {
        requestInput(InputDp1, "Command-Control-G hotkey");
    });
}

void DisplayInputController::switchToHdmi1()
{
    requestInput(InputHdmi1, "Option-Control-G hotkey");

    // Return to the Mac input, then cleanly tear down only the compatibility
    // stream. Moonlight stays open at its normal UI for a quick return to DP1.
    // The Apollo host application also remains running.
    QTimer::singleShot(250, this, []() {
        Session* session = Session::get();
        if (session != nullptr) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Display input: HDMI return requested; ending stream and keeping Moonlight open");
            DisplayInputController* controller = DisplayInputController::instance();
            if (controller != nullptr) {
                QObject::disconnect(controller->m_SessionFinishedConnection);
                controller->m_SessionFinishedConnection = QObject::connect(
                    session,
                    &Session::sessionFinished,
                    controller,
                    &DisplayInputController::handleSessionFinished,
                    Qt::QueuedConnection);
            }
            session->endSession(false);
        }
        else {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Display input: HDMI return requested without active session; Moonlight remains open");
        }
    });
}

void DisplayInputController::handleSessionFinished(int portTestResult)
{
    Q_UNUSED(portTestResult);
    QObject::disconnect(m_SessionFinishedConnection);
    m_SessionFinishedConnection = {};

    // The G2724D topology changes when its active input changes. Wait for
    // macOS to publish the newly online screen before moving and raising the
    // Moonlight UI.
    QTimer::singleShot(350, this, &DisplayInputController::restoreMoonlightUi);
}

void DisplayInputController::requestInput(int inputValue, const char* reason)
{
    if (m_Process.state() != QProcess::NotRunning) {
        m_PendingInput = inputValue;
        m_PendingReason = QString::fromUtf8(reason);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Display input: queued input %d while %s is active",
                    inputValue,
                    qPrintable(m_ActiveBackend));
        return;
    }
    startInputRequest(inputValue, reason);
}

void DisplayInputController::startInputRequest(int inputValue, const char* reason)
{
    QString executable = locateBetterDisplay();
    QStringList arguments;
    if (!executable.isEmpty()) {
        m_ActiveBackend = QStringLiteral("BetterDisplay");
        arguments = DisplayInputPolicy::betterDisplayArguments(inputValue);
    }
    else {
        executable = locateM1ddc();
        m_ActiveBackend = QStringLiteral("m1ddc");
        arguments = DisplayInputPolicy::m1ddcArguments(inputValue);
    }

    if (executable.isEmpty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Display input: no DDC backend found; cannot switch to input %d",
                    inputValue);
        return;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Display input: switching DELL G2724D to input %d via %s (%s)",
                inputValue,
                qPrintable(m_ActiveBackend),
                reason);
    m_Process.setProgram(executable);
    m_Process.setArguments(arguments);
    m_Process.start(QIODevice::ReadOnly);
}

void DisplayInputController::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Display input: %s completed successfully",
                    qPrintable(m_ActiveBackend));
    }
    else {
        const QByteArray errorText = m_Process.readAllStandardError().trimmed();
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Display input: %s failed (exitCode=%d status=%d): %s",
                    qPrintable(m_ActiveBackend),
                    exitCode,
                    static_cast<int>(exitStatus),
                    errorText.constData());
    }
    startPendingRequest();
}

void DisplayInputController::handleProcessError(QProcess::ProcessError error)
{
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Display input: failed to execute %s (error=%d): %s",
                qPrintable(m_ActiveBackend),
                static_cast<int>(error),
                qPrintable(m_Process.errorString()));
    if (m_Process.state() == QProcess::NotRunning) {
        startPendingRequest();
    }
}

void DisplayInputController::startPendingRequest()
{
    if (!m_PendingInput.has_value() || m_Process.state() != QProcess::NotRunning) {
        return;
    }

    const int inputValue = *m_PendingInput;
    const QByteArray reason = m_PendingReason.toUtf8();
    m_PendingInput.reset();
    m_PendingReason.clear();
    startInputRequest(inputValue, reason.constData());
}

void DisplayInputController::activateStreamingWindow()
{
    [NSApp activateIgnoringOtherApps:YES];

    Session* session = Session::get();
    if (session != nullptr) {
        session->raiseStreamingWindow();
    }
    else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Display input: DP1 requested without an active streaming window");
    }
}

void DisplayInputController::restoreMoonlightUi()
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    QScreen* targetScreen = QGuiApplication::primaryScreen();
    if (targetScreen == nullptr || screens.isEmpty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Display input: no online Mac screen available for Moonlight UI");
        return;
    }

    for (QWindow* window : QGuiApplication::topLevelWindows()) {
        if (window == nullptr || !window->isVisible()) {
            continue;
        }

        if (!screens.contains(window->screen())) {
            window->setScreen(targetScreen);
        }

        bool intersectsOnlineScreen = false;
        for (QScreen* screen : screens) {
            if (screen->availableGeometry().intersects(window->geometry())) {
                intersectsOnlineScreen = true;
                break;
            }
        }
        if (!intersectsOnlineScreen) {
            const QRect available = targetScreen->availableGeometry();
            const QSize size = window->size().boundedTo(available.size());
            window->resize(size);
            window->setPosition(available.center() - QPoint(size.width() / 2,
                                                            size.height() / 2));
        }

        window->show();
        window->raise();
        window->requestActivate();
    }

    [NSApp activateIgnoringOtherApps:YES];
    for (NSWindow* window in [NSApp windows]) {
        if ([window isVisible]) {
            [window makeKeyAndOrderFront:nil];
        }
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Display input: restored Moonlight UI on an online Mac screen");
}

QString DisplayInputController::locateM1ddc() const
{
    const QString bundledPath = QDir(QCoreApplication::applicationDirPath())
                                    .absoluteFilePath(QStringLiteral("../Resources/m1ddc"));
    const QStringList candidates = {
        bundledPath,
        QStringLiteral("/opt/homebrew/bin/m1ddc"),
        QStringLiteral("/usr/local/bin/m1ddc"),
        QStandardPaths::findExecutable(QStringLiteral("m1ddc")),
    };
    for (const QString& candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo(candidate).isExecutable()) {
            return candidate;
        }
    }
    return {};
}

QString DisplayInputController::locateBetterDisplay() const
{
    const QString executable = QStringLiteral(
        "/Applications/BetterDisplay.app/Contents/MacOS/BetterDisplay");
    return QFileInfo(executable).isExecutable() ? executable : QString();
}

void DisplayInputController::registerGlobalHotkeys()
{
    const EventTypeSpec eventType = { kEventClassKeyboard, kEventHotKeyPressed };
    EventHandlerRef handler = nullptr;
    OSStatus status = InstallEventHandler(GetApplicationEventTarget(),
                                          globalHotkeyHandler,
                                          1,
                                          &eventType,
                                          this,
                                          &handler);
    if (status != noErr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Display input: failed to install global hotkey handler (%d)",
                    static_cast<int>(status));
        return;
    }
    m_EventHandler = handler;

    EventHotKeyRef dp1Hotkey = nullptr;
    status = RegisterEventHotKey(kVK_ANSI_G,
                                 cmdKey | controlKey,
                                 kDp1HotkeyId,
                                 GetApplicationEventTarget(),
                                 0,
                                 &dp1Hotkey);
    if (status == noErr) {
        m_Dp1Hotkey = dp1Hotkey;
    }
    else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Display input: could not register Command-Control-G (%d); "
                    "quit the standalone G2724D Input Switcher if it is running",
                    static_cast<int>(status));
    }

    EventHotKeyRef hdmiHotkey = nullptr;
    status = RegisterEventHotKey(kVK_ANSI_G,
                                 optionKey | controlKey,
                                 kHdmiHotkeyId,
                                 GetApplicationEventTarget(),
                                 0,
                                 &hdmiHotkey);
    if (status == noErr) {
        m_HdmiHotkey = hdmiHotkey;
    }
    else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Display input: could not register Option-Control-G (%d); "
                    "quit the standalone G2724D Input Switcher if it is running",
                    static_cast<int>(status));
    }
}

void DisplayInputController::unregisterGlobalHotkeys()
{
    if (m_Dp1Hotkey != nullptr) {
        UnregisterEventHotKey(static_cast<EventHotKeyRef>(m_Dp1Hotkey));
        m_Dp1Hotkey = nullptr;
    }
    if (m_HdmiHotkey != nullptr) {
        UnregisterEventHotKey(static_cast<EventHotKeyRef>(m_HdmiHotkey));
        m_HdmiHotkey = nullptr;
    }
    if (m_EventHandler != nullptr) {
        RemoveEventHandler(static_cast<EventHandlerRef>(m_EventHandler));
        m_EventHandler = nullptr;
    }
}

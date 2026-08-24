#pragma once

#include <QString>
#include <QStringList>

namespace DisplayInputPolicy {

constexpr int InputDp1 = 15;
constexpr int InputHdmi1 = 17;
constexpr int M1ddcFallbackDisplayIndex = 1;

inline bool isSupportedInput(int inputValue)
{
    return inputValue == 15 || inputValue == 16 || inputValue == 17 ||
           inputValue == 18 || inputValue == 27;
}

inline bool isRemoteInputAudioApplication(const QString& applicationName)
{
    return applicationName.trimmed().compare(QStringLiteral("Remote Input + Audio"),
                                              Qt::CaseInsensitive) == 0;
}

inline bool useAbsoluteMouseMode(const QString& applicationName,
                                 bool savedAbsoluteMouseMode)
{
    // The dedicated mode is used for both desktop control and games. Relative
    // input is the only mode that permits unrestricted FPS camera movement.
    return isRemoteInputAudioApplication(applicationName) ? false : savedAbsoluteMouseMode;
}

inline QStringList m1ddcArguments(int inputValue)
{
    return {
        QStringLiteral("display"),
        QString::number(M1ddcFallbackDisplayIndex),
        QStringLiteral("set"),
        QStringLiteral("input"),
        QString::number(inputValue),
    };
}

inline QStringList betterDisplayArguments(int inputValue)
{
    return {
        QStringLiteral("set"),
        QStringLiteral("-namelike=G2724D"),
        QStringLiteral("-ddc=%1").arg(inputValue),
        QStringLiteral("-vcp=inputSelect"),
    };
}

} // namespace DisplayInputPolicy

#pragma once

#include <QString>
#include <QStringList>

namespace DisplayInputPolicy {

constexpr int InputDp1 = 15;
constexpr int InputHdmi1 = 17;
// Address the physical monitor by its stable CoreGraphics UUID. Numeric m1ddc
// positions change when BetterDisplay adds or removes virtual screens.
constexpr char DisplayUuid[] = "425C0628-3B6A-481D-95FC-695F1E5EFB71";

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
        QString::fromLatin1(DisplayUuid),
        QStringLiteral("set"),
        QStringLiteral("input"),
        QString::number(inputValue),
    };
}

} // namespace DisplayInputPolicy

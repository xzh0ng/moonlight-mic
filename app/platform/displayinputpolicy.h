#pragma once

#include <QString>
#include <QStringList>

namespace DisplayInputPolicy {

constexpr int InputDp1 = 15;
constexpr int InputHdmi1 = 17;
constexpr int DisplayIndex = 1;

inline bool isRemoteInputAudioApplication(const QString& applicationName)
{
    return applicationName.trimmed().compare(QStringLiteral("Remote Input + Audio"),
                                              Qt::CaseInsensitive) == 0;
}

inline QStringList m1ddcArguments(int inputValue)
{
    return {
        QStringLiteral("display"),
        QString::number(DisplayIndex),
        QStringLiteral("set"),
        QStringLiteral("input"),
        QString::number(inputValue),
    };
}

} // namespace DisplayInputPolicy

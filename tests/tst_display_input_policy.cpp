#include <QtTest>

#include "../app/platform/displayinputpolicy.h"

class TstDisplayInputPolicy : public QObject
{
    Q_OBJECT

private slots:
    void testAutomaticSwitchApplicationGate()
    {
        QVERIFY(DisplayInputPolicy::isRemoteInputAudioApplication(
            QStringLiteral("Remote Input + Audio")));
        QVERIFY(DisplayInputPolicy::isRemoteInputAudioApplication(
            QStringLiteral("  remote input + audio  ")));
        QVERIFY(!DisplayInputPolicy::isRemoteInputAudioApplication(
            QStringLiteral("Desktop")));
        QVERIFY(!DisplayInputPolicy::isRemoteInputAudioApplication(QString()));
    }

    void testM1ddcPacketArguments()
    {
        QCOMPARE(DisplayInputPolicy::m1ddcArguments(DisplayInputPolicy::InputDp1),
                 QStringList({"display", "1", "set", "input", "15"}));
        QCOMPARE(DisplayInputPolicy::m1ddcArguments(DisplayInputPolicy::InputHdmi1),
                 QStringList({"display", "1", "set", "input", "17"}));
    }
};

int runDisplayInputPolicyTests(int argc, char** argv)
{
    TstDisplayInputPolicy test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_display_input_policy.moc"

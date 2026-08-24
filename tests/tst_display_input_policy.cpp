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

        QCOMPARE(DisplayInputPolicy::betterDisplayArguments(DisplayInputPolicy::InputDp1),
                 QStringList({"set", "-namelike=G2724D", "-ddc=15",
                              "-vcp=inputSelect"}));
        QCOMPARE(DisplayInputPolicy::betterDisplayArguments(DisplayInputPolicy::InputHdmi1),
                 QStringList({"set", "-namelike=G2724D", "-ddc=17",
                              "-vcp=inputSelect"}));

        QCOMPARE(DisplayInputPolicy::betterDisplayArguments(16),
                 QStringList({"set", "-namelike=G2724D", "-ddc=16",
                              "-vcp=inputSelect"}));
        QCOMPARE(DisplayInputPolicy::betterDisplayArguments(18),
                 QStringList({"set", "-namelike=G2724D", "-ddc=18",
                              "-vcp=inputSelect"}));
        QCOMPARE(DisplayInputPolicy::betterDisplayArguments(27),
                 QStringList({"set", "-namelike=G2724D", "-ddc=27",
                              "-vcp=inputSelect"}));
    }

    void testSupportedDisplayInputs()
    {
        QVERIFY(DisplayInputPolicy::isSupportedInput(15));
        QVERIFY(DisplayInputPolicy::isSupportedInput(16));
        QVERIFY(DisplayInputPolicy::isSupportedInput(17));
        QVERIFY(DisplayInputPolicy::isSupportedInput(18));
        QVERIFY(DisplayInputPolicy::isSupportedInput(27));
        QVERIFY(!DisplayInputPolicy::isSupportedInput(0));
        QVERIFY(!DisplayInputPolicy::isSupportedInput(19));
    }

    void testRemoteInputAudioForcesRelativeMouseMode()
    {
        QVERIFY(!DisplayInputPolicy::useAbsoluteMouseMode(
            QStringLiteral("Remote Input + Audio"), true));
        QVERIFY(!DisplayInputPolicy::useAbsoluteMouseMode(
            QStringLiteral("Remote Input + Audio"), false));
        QVERIFY(DisplayInputPolicy::useAbsoluteMouseMode(
            QStringLiteral("Desktop"), true));
        QVERIFY(!DisplayInputPolicy::useAbsoluteMouseMode(
            QStringLiteral("Desktop"), false));
    }
};

int runDisplayInputPolicyTests(int argc, char** argv)
{
    TstDisplayInputPolicy test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_display_input_policy.moc"

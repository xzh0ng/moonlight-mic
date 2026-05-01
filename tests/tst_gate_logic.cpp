// C4 — Test B: capability-gated session-start logic.
//
// Tests the four (streamMicToHost toggle, SS_FF_MIC_INPUT in hostFlags)
// combinations via the shouldStartMicSender(bool, uint32_t) overload extracted
// from Session::startConnectionAsync().
//
// This overload was added to decouple the gate logic from StreamingPreferences
// (which depends on QQmlEngine / Qt6Qml). No live streaming session is needed.

#include <QtTest>
#include <cstdint>

#include "../app/streaming/session_gate_helper.h"
#include <Limelight.h>

class TstGateLogic : public QObject
{
    Q_OBJECT

private slots:
    void test_gateDecision_data()
    {
        QTest::addColumn<bool>("toggle");
        QTest::addColumn<uint32_t>("hostFlags");
        QTest::addColumn<bool>("expected");

        QTest::newRow("toggle=false, flag=0")               << false << (uint32_t)0               << false;
        QTest::newRow("toggle=false, flag=SS_FF_MIC_INPUT") << false << (uint32_t)SS_FF_MIC_INPUT << false;
        QTest::newRow("toggle=true,  flag=0")               << true  << (uint32_t)0               << false;
        QTest::newRow("toggle=true,  flag=SS_FF_MIC_INPUT") << true  << (uint32_t)SS_FF_MIC_INPUT << true;
    }

    void test_gateDecision()
    {
        QFETCH(bool, toggle);
        QFETCH(uint32_t, hostFlags);
        QFETCH(bool, expected);

        bool result = shouldStartMicSender(toggle, hostFlags);
        QCOMPARE(result, expected);
    }
};

int runGateLogicTests(int argc, char** argv)
{
    TstGateLogic test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_gate_logic.moc"

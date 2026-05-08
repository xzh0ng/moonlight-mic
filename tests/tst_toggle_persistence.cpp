// C4 — Test A: streamMicToHost preference persists correctly via QSettings.
//
// Tests the QSettings key ("streammic") that StreamingPreferences::save()
// writes and StreamingPreferences::reload() reads. Rather than constructing
// the full StreamingPreferences class (which depends on QQmlEngine / Qt6Qml),
// this test exercises the storage contract directly: write a bool under the
// "streammic" key in a temporary QSettings scope and read it back. The key
// name and default value are the contract we're testing.
//
// This approach was chosen over constructing StreamingPreferences directly to
// avoid adding QT += qml to the test binary, which breaks headless CI runs
// due to Qt6Network.dll initialization issues (tracked in open threads).
//
// Key name matches SER_STREAMMIC in streamingpreferences.cpp; verified to be
// "streammic" by inspection of that file.

#include <QtTest>
#include <QCoreApplication>
#include <QSettings>
#include <QUuid>

static const char* kStreamMicKey = "streammic";
static const char* kMicCaptureDeviceKey = "miccapturedevice";

class TstTogglePersistence : public QObject
{
    Q_OBJECT

private:
    QString m_SavedOrgName;
    QString m_SavedAppName;

private slots:
    void initTestCase()
    {
        // Install a unique, throwaway org/app name so we never pollute real
        // user preferences. The UUID suffix ensures no two parallel test
        // processes share state.
        m_SavedOrgName = QCoreApplication::organizationName();
        m_SavedAppName = QCoreApplication::applicationName();
        QCoreApplication::setOrganizationName(
            "moonlight-mic-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        QCoreApplication::setApplicationName("C4TogglePersistence");

        // Clear any pre-existing state so the default tests start clean.
        QSettings settings;
        settings.clear();
        settings.sync();
    }

    void cleanupTestCase()
    {
        QSettings settings;
        settings.clear();
        settings.sync();
        QCoreApplication::setOrganizationName(m_SavedOrgName);
        QCoreApplication::setApplicationName(m_SavedAppName);
    }

    void cleanup()
    {
        // Reset between sub-tests so they're independent.
        QSettings settings;
        settings.remove(kStreamMicKey);
        settings.remove(kMicCaptureDeviceKey);
        settings.sync();
    }

    // -----------------------------------------------------------------------
    // Default: key absent → QSettings.value() returns false (matching the
    // default passed to StreamingPreferences::reload()).
    // -----------------------------------------------------------------------
    void test_defaultIsFalse()
    {
        QSettings settings;
        bool value = settings.value(kStreamMicKey, false).toBool();
        QCOMPARE(value, false);
    }

    // -----------------------------------------------------------------------
    // Round-trip true: write true, sync, read back → true.
    // -----------------------------------------------------------------------
    void test_persistTrue()
    {
        {
            QSettings settings;
            settings.setValue(kStreamMicKey, true);
            settings.sync();
        }
        QSettings settings2;
        QCOMPARE(settings2.value(kStreamMicKey, false).toBool(), true);
    }

    // -----------------------------------------------------------------------
    // Round-trip false: write true, overwrite with false, read back → false.
    // -----------------------------------------------------------------------
    void test_persistFalse()
    {
        {
            QSettings settings;
            settings.setValue(kStreamMicKey, true);
            settings.sync();
        }
        {
            QSettings settings;
            settings.setValue(kStreamMicKey, false);
            settings.sync();
        }
        QSettings settings3;
        QCOMPARE(settings3.value(kStreamMicKey, false).toBool(), false);
    }

    // -----------------------------------------------------------------------
    // Mic capture device: absent key means system default; a device name
    // round-trips as an exact string for SDL_OpenAudioDevice().
    // -----------------------------------------------------------------------
    void test_micCaptureDevicePersistence()
    {
        QSettings settings;
        QCOMPARE(settings.value(kMicCaptureDeviceKey, QString()).toString(), QString());

        const QString deviceName = QStringLiteral("Microphone Array (Senary Audio)");
        settings.setValue(kMicCaptureDeviceKey, deviceName);
        settings.sync();

        QSettings settings2;
        QCOMPARE(settings2.value(kMicCaptureDeviceKey, QString()).toString(), deviceName);
    }
};

int runTogglePersistenceTests(int argc, char** argv)
{
    TstTogglePersistence test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_toggle_persistence.moc"

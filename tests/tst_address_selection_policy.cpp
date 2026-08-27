#include <QtTest>

#include "backend/addressselectionpolicy.h"

class TstAddressSelectionPolicy : public QObject
{
    Q_OBJECT

private slots:
    void testManualAddressIsPreferred()
    {
        const QVector<NvAddress> addresses = AddressSelectionPolicy::orderedUniqueAddresses(
            NvAddress(QStringLiteral("10.147.17.2"), 47989),
            NvAddress(QStringLiteral("192.168.4.10"), 47989),
            NvAddress(QStringLiteral("192.168.4.10"), 47989),
            NvAddress(QStringLiteral("142.188.162.137"), 47989),
            NvAddress());

        QCOMPARE(addresses, QVector<NvAddress>({
            NvAddress(QStringLiteral("10.147.17.2"), 47989),
            NvAddress(QStringLiteral("192.168.4.10"), 47989),
            NvAddress(QStringLiteral("142.188.162.137"), 47989),
        }));
    }

    void testNullManualPreservesExistingFallbackOrder()
    {
        const QVector<NvAddress> addresses = AddressSelectionPolicy::orderedUniqueAddresses(
            NvAddress(),
            NvAddress(QStringLiteral("192.168.4.10"), 47989),
            NvAddress(QStringLiteral("192.168.4.11"), 47989),
            NvAddress(QStringLiteral("142.188.162.137"), 47989),
            NvAddress(QStringLiteral("2001:db8::10"), 47989));

        QCOMPARE(addresses, QVector<NvAddress>({
            NvAddress(QStringLiteral("192.168.4.10"), 47989),
            NvAddress(QStringLiteral("192.168.4.11"), 47989),
            NvAddress(QStringLiteral("142.188.162.137"), 47989),
            NvAddress(QStringLiteral("2001:db8::10"), 47989),
        }));
    }

    void testManualKeepsFallbackCandidates()
    {
        const NvAddress manual(QStringLiteral("10.147.17.2"), 47989);
        const NvAddress local(QStringLiteral("192.168.4.10"), 47989);
        const QVector<NvAddress> addresses = AddressSelectionPolicy::orderedUniqueAddresses(
            manual, local, local, NvAddress(), NvAddress());

        QCOMPARE(addresses.size(), 2);
        QCOMPARE(addresses[0], manual);
        QCOMPARE(addresses[1], local);
    }
};

int runAddressSelectionPolicyTests(int argc, char** argv)
{
    TstAddressSelectionPolicy test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_address_selection_policy.moc"

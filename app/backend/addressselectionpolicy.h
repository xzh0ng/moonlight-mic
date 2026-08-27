#pragma once

#include "nvaddress.h"

#include <QVector>

namespace AddressSelectionPolicy {

inline QVector<NvAddress> orderedUniqueAddresses(const NvAddress& manualAddress,
                                                  const NvAddress& activeAddress,
                                                  const NvAddress& localAddress,
                                                  const NvAddress& remoteAddress,
                                                  const NvAddress& ipv6Address)
{
    QVector<NvAddress> addresses = {
        manualAddress,
        activeAddress,
        localAddress,
        remoteAddress,
        ipv6Address,
    };

    // Prune nulls and duplicates, always preserving the first occurrence.
    for (int i = 0; i < addresses.count(); i++) {
        if (addresses[i].isNull()) {
            addresses.remove(i);
            i--;
            continue;
        }
        for (int j = i + 1; j < addresses.count(); j++) {
            if (addresses[i] == addresses[j]) {
                addresses.remove(j);
                j--;
            }
        }
    }

    return addresses;
}

} // namespace AddressSelectionPolicy

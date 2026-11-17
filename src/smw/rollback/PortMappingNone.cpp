// No router port opening on the original Xbox: system link is the local
// network, where the others reach the console directly (see PortMapping.h)
#include "PortMapping.h"

namespace rollback::upnp {

void openPort(uint16_t /*port*/) {}

void closePort() {}

Status status()
{
    return {};
}

} // namespace rollback::upnp

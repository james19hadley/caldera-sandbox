#ifndef CALDERA_BACKEND_TRANSPORT_ITRANSPORT_SERVER_H
#define CALDERA_BACKEND_TRANSPORT_ITRANSPORT_SERVER_H

#include <memory>

#include "common/DataTypes.h"

namespace spdlog { class logger; }

namespace caldera::backend::transport {

class ITransportServer {
public:
    virtual ~ITransportServer() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void sendWorldFrame(const caldera::backend::common::WorldFrame& frame) = 0;
};

} // namespace caldera::backend::transport

#endif // CALDERA_BACKEND_TRANSPORT_ITRANSPORT_SERVER_H

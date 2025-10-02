// HandshakeServer.h
// Abstraction of a backend -> client out-of-band handshake channel.
// Current implementation uses a POSIX FIFO; future variants may implement
// TCP or Unix domain sockets while preserving the same interface.

#ifndef CALDERA_BACKEND_TRANSPORT_HANDSHAKE_SERVER_H
#define CALDERA_BACKEND_TRANSPORT_HANDSHAKE_SERVER_H

#include <memory>
#include <string>
#include <cstdint>
#include <atomic>
#include <vector>
#include <functional>

namespace spdlog { class logger; }

namespace caldera::backend::transport {

struct HandshakeConfig {
    std::string pipe_path = "/tmp/caldera_handshake"; // for FIFO impl
    int timeout_ms = 5000;        // per session timeout waiting for HELLO
    int max_sessions = 3;         // sequential sessions served before shutdown
};

// Result payload returned to a client after successful HELLO.
struct HandshakePayloadField {
    std::string key; std::string value; // value already JSON-escaped/raw per use
};

class IHandshakeServer {
public:
    virtual ~IHandshakeServer() = default;
    virtual void setStaticFields(std::vector<HandshakePayloadField> fields) = 0; // constant fields inserted each session
    virtual void setDynamicJsonBuilder(std::function<std::string()> builder) = 0; // optional override builder: if set, ignores static fields
    virtual void start() = 0;
    virtual void stop() = 0;
};

// FIFO implementation. Accepts a single-line HELLO string and responds with JSON (multi-line OK).
class FifoHandshakeServer : public IHandshakeServer {
public:
    FifoHandshakeServer(std::shared_ptr<spdlog::logger> log,
                        std::shared_ptr<spdlog::logger> trace,
                        HandshakeConfig cfg);
    ~FifoHandshakeServer() override;

    void setStaticFields(std::vector<HandshakePayloadField> fields) override;
    void setDynamicJsonBuilder(std::function<std::string()> builder) override;

    void start() override; // blocking loop (could be run on its own thread)
    void stop() override;  // idempotent

    // Provide access for server to configure expected HELLO value
    void setHelloString(std::string hello) { client_hello_ = std::move(hello); }

private:
    std::string buildJson() const; // builds response body

    std::shared_ptr<spdlog::logger> log_;
    std::shared_ptr<spdlog::logger> trace_;
    HandshakeConfig cfg_;
    std::vector<HandshakePayloadField> static_fields_;
    std::function<std::string()> dynamic_builder_;
    std::atomic<bool> running_{false};
    std::string client_hello_ = "HELLO_CALDERA_CLIENT_V1";
};

} // namespace caldera::backend::transport

#endif

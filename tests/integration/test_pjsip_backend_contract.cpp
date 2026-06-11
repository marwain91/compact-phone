// Instantiates the backend-agnostic contract suite against the real
// PJSIP adapter. Runs in the integration environment where a live
// pjsua endpoint can start, bind a UDP transport, and complete the
// full libCreate/libInit/libStart/libDestroy lifecycle — once per test
// (18 cycles total — slow but legal for contract verification).
//
// Init-order note: OwningPjsipBackend inherits PjsipBackend and holds
// a SipEngine member. Base classes initialise before members in C++,
// so PjsipBackend(&m_engine) in the init-list passes a pointer to a
// not-yet-constructed SipEngine. This is safe here because
// PjsipBackend's constructor only stores the pointer (m_engine(engine)
// in the .cpp) — it does NOT dereference it. The engine is fully
// constructed by the time start() is called, which is the first actual
// dereference.
//
// Port strategy: EngineConfig{} uses port 5060 (the contract default).
// Tests run sequentially in-process; stop() clears the PJSUA state so
// the UDP socket is released before the next test binds. UDP has no
// TIME_WAIT, so the port is immediately reusable. Asterisk lives in a
// separate container and does not compete for the dev-container's
// 0.0.0.0:5060 bind.

#include "unit/sip_backend_contract.h"

#include "core/SipEngine.h"
#include "core/sipbackend/pjsip/PjsipBackend.h"

#include <QCoreApplication>

namespace compactphone::sipbackend::testing {

// OwningPjsipBackend: a self-contained backend that owns its SipEngine.
// The contract fixture drives lifecycle entirely (start/stop per test);
// the engine is NOT started in the constructor.
struct OwningPjsipBackend : PjsipBackend {
    // Member declared before usage in start() / after base ctor stores ptr.
    // See the init-order note in the file header.
    sip::SipEngine m_engine;

    OwningPjsipBackend()
        // PjsipBackend only stores the pointer — safe to pass &m_engine
        // before m_engine is fully constructed (see file header).
        : PjsipBackend(&m_engine)
    {}

    ~OwningPjsipBackend()
    {
        // Defensive stop: TearDown calls backend->stop() if isRunning(), but
        // the contract fixture may skip TearDown on ASSERT failures in SetUp.
        // Calling stop() a second time when already stopped is safe (SipEngine
        // guard: "if (!m_running) return").
        if (m_engine.isRunning())
            PjsipBackend::stop();
    }
};

// QCoreApplication must exist before any PjsipBackend is constructed
// (EventDispatch creates a QObject internally which requires a QCoreApp).
// Mirror the ensureApp() singleton pattern from the fake contract TU.
static QCoreApplication *ensureApp()
{
    static int argc = 1;
    static char arg0[] = "test_pjsip_backend_contract";
    static char *argv[] = {arg0, nullptr};
    static QCoreApplication *app = QCoreApplication::instance()
        ? QCoreApplication::instance()
        : new QCoreApplication(argc, argv);
    return app;
}

INSTANTIATE_TEST_SUITE_P(
    Pjsip, SipBackendContract,
    ::testing::Values(BackendFactory{[]() -> std::unique_ptr<ISipBackend> {
        ensureApp();
        return std::make_unique<OwningPjsipBackend>();
    }}));

} // namespace compactphone::sipbackend::testing

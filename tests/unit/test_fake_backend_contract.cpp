// Instantiates the backend-agnostic contract suite against the fake.
// Phases 2/3 add a PJSIP instantiation in the integration environment.
#include "sip_backend_contract.h"

#include "core/sipbackend/fake/FakeSipBackend.h"

#include <QCoreApplication>

namespace compactphone::sipbackend::testing {

// QCoreApplication must exist before any FakeSipBackend is constructed
// (it creates a QObject internally). Mirror the ensureApp() pattern from
// test_fake_sip_backend.cpp so the contract fixture has a Qt event loop.
static QCoreApplication *ensureApp()
{
    static int argc = 1;
    static char arg0[] = "test_fake_backend_contract";
    static char *argv[] = {arg0, nullptr};
    static QCoreApplication *app = QCoreApplication::instance()
        ? QCoreApplication::instance()
        : new QCoreApplication(argc, argv);
    return app;
}

// Force app construction at static-init time so it precedes any test.
static QCoreApplication *s_app = ensureApp();

INSTANTIATE_TEST_SUITE_P(
    Fake, SipBackendContract,
    ::testing::Values(BackendFactory{
        [] { return std::make_unique<FakeSipBackend>(); }}));

} // namespace compactphone::sipbackend::testing

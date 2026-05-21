#include "AccountsController.h"

#include "AccountsManager.h"
#include "SipEngine.h"
#include "models/AccountsModel.h"

#include <QMetaObject>

#include <set>
#include <sstream>

namespace compactphone {

namespace {

void applyParams(sip::Account &a, const QVariantMap &p)
{
    auto str = [&](const char *k, const std::string &fallback = "") {
        return p.contains(k) ? p.value(k).toString().toStdString() : fallback;
    };
    auto boolean = [&](const char *k, bool fallback) {
        return p.contains(k) ? p.value(k).toBool() : fallback;
    };
    auto integer = [&](const char *k, int fallback) {
        return p.contains(k) ? p.value(k).toInt() : fallback;
    };
    if (p.contains("label"))               a.label = str("label");
    if (p.contains("displayName"))         a.displayName = str("displayName");
    if (p.contains("username"))            a.username = str("username");
    if (p.contains("domain"))              a.domain = str("domain");
    if (p.contains("authUser"))            a.authUser = str("authUser");
    if (a.authUser.empty())                a.authUser = a.username;
    if (p.contains("transport"))           a.transport = sip::transportFromString(str("transport"));
    if (p.contains("proxy"))               a.proxy = str("proxy");
    if (p.contains("stunServer"))          a.stunServer = str("stunServer");
    if (p.contains("publicAddress"))       a.publicAddress = str("publicAddress");
    if (p.contains("codecs"))              a.codecs = str("codecs");
    if (p.contains("voicemailNumber"))     a.voicemailNumber = str("voicemailNumber");
    if (p.contains("registerOnStartup"))   a.registerOnStartup = boolean("registerOnStartup", true);
    if (p.contains("registerIntervalSec")) a.registerIntervalSec = integer("registerIntervalSec", 0);
    if (p.contains("keepaliveIntervalSec")) a.keepaliveIntervalSec = integer("keepaliveIntervalSec", 0);
    if (p.contains("sessionTimersEnabled")) a.sessionTimersEnabled = boolean("sessionTimersEnabled", true);
    if (p.contains("publishPresenceEnabled")) a.publishPresenceEnabled = boolean("publishPresenceEnabled", false);
    if (p.contains("iceEnabled"))          a.iceEnabled = boolean("iceEnabled", false);
    if (p.contains("hideCallerId"))        a.hideCallerId = boolean("hideCallerId", false);
    if (p.contains("zrtpEnabled"))         a.zrtpEnabled = boolean("zrtpEnabled", false);
    if (p.contains("srtpMode"))            a.srtpMode = sip::srtpModeFromString(str("srtpMode"));
    if (p.contains("allowUntrustedCert"))  a.allowUntrustedCert = boolean("allowUntrustedCert", false);
    if (p.contains("dtmfMethod"))          a.dtmfMethod = sip::dtmfMethodFromString(str("dtmfMethod"));
}

} // namespace

AccountsController::AccountsController(sip::AccountsManager *accounts,
                                       models::AccountsModel *model,
                                       sip::SipEngine *engine,
                                       QObject *parent)
    : QObject(parent), m_accounts(accounts), m_model(model), m_engine(engine)
{
    refreshRegisteredAccountCount();
    pushNetworkAndCodecSettings();
    if (!m_accounts) return;
    m_accounts->setOnRegistrationStateChanged(
        [this](sip::AccountId id, sip::RegistrationState s) {
            // PJSIP thread — marshal everything to the Qt main thread,
            // including the previous-state lookup, so m_lastState only
            // mutates from one thread.
            QMetaObject::invokeMethod(this, [this, id, s] {
                const auto it = m_lastState.find(static_cast<int>(id));
                const bool isNewFailure =
                    s == sip::RegistrationState::Failed
                    && (it == m_lastState.end() || it->second != s);
                m_lastState[static_cast<int>(id)] = s;
                refreshModel();
                refreshRegisteredAccountCount();
                emit activeAccountIdChanged();
                if (isNewFailure && m_accounts) {
                    const auto err = m_accounts->lastRegErrorOf(id);
                    auto acc = m_accounts->find(id);
                    QString label;
                    if (acc) {
                        label = QString::fromStdString(
                            acc->label.empty() ? acc->displayName : acc->label);
                    }
                    QString detail;
                    if (err.code > 0 && !err.reason.empty()) {
                        detail = QStringLiteral("%1 %2").arg(err.code)
                                     .arg(QString::fromStdString(err.reason));
                    } else if (err.code > 0) {
                        detail = QString::number(err.code);
                    } else if (!err.reason.empty()) {
                        detail = QString::fromStdString(err.reason);
                    } else {
                        detail = tr("transport error");
                    }
                    const QString msg = label.isEmpty()
                        ? tr("Registration failed: %1").arg(detail)
                        : tr("%1 — registration failed: %2").arg(label, detail);
                    emit registrationFailed(msg);
                }
            }, Qt::QueuedConnection);
        });
}

void AccountsController::pushNetworkAndCodecSettings()
{
    if (!m_accounts || !m_engine) return;
    // Union of STUN servers across all enabled accounts.
    std::vector<std::string> stuns;
    std::set<std::string> seen;
    for (const auto &a : m_accounts->list()) {
        if (!a.enabled || a.stunServer.empty()) continue;
        if (seen.insert(a.stunServer).second) stuns.push_back(a.stunServer);
    }
    m_engine->applyStunServers(stuns);
    // Codec priority comes from the default account (or first enabled).
    std::string codecs;
    for (const auto &a : m_accounts->list()) {
        if (a.enabled && a.isDefault) { codecs = a.codecs; break; }
    }
    if (codecs.empty()) {
        for (const auto &a : m_accounts->list()) {
            if (a.enabled && !a.codecs.empty()) { codecs = a.codecs; break; }
        }
    }
    if (codecs.empty()) return;
    std::vector<std::string> order;
    std::stringstream ss(codecs);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // trim spaces
        auto start = token.find_first_not_of(" \t");
        auto end = token.find_last_not_of(" \t");
        if (start == std::string::npos) continue;
        order.push_back(token.substr(start, end - start + 1));
    }
    m_engine->applyCodecPriority(order);
}

AccountsController::~AccountsController()
{
    if (m_accounts) m_accounts->setOnRegistrationStateChanged({});
}

QAbstractListModel *AccountsController::model() const
{
    return m_model;
}

int AccountsController::addAccount(const QVariantMap &params)
{
    if (!m_accounts) return sip::kInvalidAccountId;
    sip::Account a;
    applyParams(a, params);
    a.isDefault = m_accounts->list().empty();
    a.enabled = true;
    if (a.authUser.empty()) a.authUser = a.username;
    const auto id = m_accounts->add(a, params.value("password").toString().toStdString());
    refreshModel();
    pushNetworkAndCodecSettings();
    return id;
}

QVariantMap AccountsController::accountSnapshot(int accountId) const
{
    if (!m_accounts) return {};
    auto opt = m_accounts->find(static_cast<sip::AccountId>(accountId));
    if (!opt) return {};
    const auto &a = *opt;
    QVariantMap m;
    m["accountId"] = a.id;
    m["label"] = QString::fromStdString(a.label);
    m["displayName"] = QString::fromStdString(a.displayName);
    m["username"] = QString::fromStdString(a.username);
    m["domain"] = QString::fromStdString(a.domain);
    m["authUser"] = QString::fromStdString(a.authUser);
    m["transport"] = QString::fromUtf8(sip::transportToString(a.transport));
    m["proxy"] = QString::fromStdString(a.proxy);
    m["stunServer"] = QString::fromStdString(a.stunServer);
    m["publicAddress"] = QString::fromStdString(a.publicAddress);
    m["codecs"] = QString::fromStdString(a.codecs);
    m["voicemailNumber"] = QString::fromStdString(a.voicemailNumber);
    m["registerOnStartup"] = a.registerOnStartup;
    m["registerIntervalSec"] = a.registerIntervalSec;
    m["keepaliveIntervalSec"] = a.keepaliveIntervalSec;
    m["sessionTimersEnabled"] = a.sessionTimersEnabled;
    m["publishPresenceEnabled"] = a.publishPresenceEnabled;
    m["iceEnabled"] = a.iceEnabled;
    m["hideCallerId"] = a.hideCallerId;
    m["zrtpEnabled"] = a.zrtpEnabled;
    m["srtpMode"] = QString::fromUtf8(sip::srtpModeToString(a.srtpMode));
    m["allowUntrustedCert"] = a.allowUntrustedCert;
    m["dtmfMethod"] = QString::fromUtf8(sip::dtmfMethodToString(a.dtmfMethod));
    return m;
}

bool AccountsController::removeAccount(int accountId)
{
    if (!m_accounts) return false;
    const bool ok = m_accounts->remove(static_cast<sip::AccountId>(accountId));
    if (ok) {
        refreshModel();
        refreshRegisteredAccountCount();
        pushNetworkAndCodecSettings();
        emit activeAccountIdChanged();
    }
    return ok;
}

bool AccountsController::updateAccount(int accountId, const QVariantMap &params)
{
    if (!m_accounts) return false;
    auto current = m_accounts->find(static_cast<sip::AccountId>(accountId));
    if (!current) return false;
    sip::Account edited = *current;
    applyParams(edited, params);
    if (edited.authUser.empty()) edited.authUser = edited.username;
    const bool ok = m_accounts->update(edited);
    if (!ok) return false;
    // Password is sent only when the user actually retyped it in the
    // dialog. The update() call above already re-registered the account;
    // setPassword reruns that cycle with the new credentials so the next
    // REGISTER goes out with them.
    const QString newPassword = params.value(QStringLiteral("password")).toString();
    if (!newPassword.isEmpty()) {
        m_accounts->setPassword(static_cast<sip::AccountId>(accountId),
                                newPassword.toStdString());
    }
    refreshModel();
    refreshRegisteredAccountCount();
    pushNetworkAndCodecSettings();
    emit activeAccountIdChanged();
    return true;
}

bool AccountsController::setDefaultAccount(int accountId)
{
    if (!m_accounts) return false;
    const bool ok = m_accounts->setDefault(static_cast<sip::AccountId>(accountId));
    if (ok) {
        refreshModel();
        pushNetworkAndCodecSettings();
        emit activeAccountIdChanged();
    }
    return ok;
}

bool AccountsController::setAccountEnabled(int accountId, bool enabled)
{
    if (!m_accounts) return false;
    const bool ok = m_accounts->setEnabled(static_cast<sip::AccountId>(accountId),
                                           enabled);
    if (ok) {
        refreshModel();
        refreshRegisteredAccountCount();
        pushNetworkAndCodecSettings();
        emit activeAccountIdChanged();
    }
    return ok;
}

int AccountsController::activeAccountId() const
{
    if (!m_accounts) return -1;
    if (m_activeAccountId > 0) {
        for (const auto &a : m_accounts->list()) {
            if (a.id == m_activeAccountId && a.enabled
                && m_accounts->stateOf(a.id) == sip::RegistrationState::Registered) {
                return m_activeAccountId;
            }
        }
    }
    for (const auto &a : m_accounts->list()) {
        if (a.enabled && a.isDefault
            && m_accounts->stateOf(a.id) == sip::RegistrationState::Registered) {
            return static_cast<int>(a.id);
        }
    }
    for (const auto &a : m_accounts->list()) {
        if (a.enabled
            && m_accounts->stateOf(a.id) == sip::RegistrationState::Registered) {
            return static_cast<int>(a.id);
        }
    }
    return -1;
}

void AccountsController::setActiveAccountId(int id)
{
    if (m_activeAccountId == id) return;
    m_activeAccountId = id;
    emit activeAccountIdChanged();
}

void AccountsController::refreshModel()
{
    if (m_model) m_model->refresh();
}

void AccountsController::refreshRegisteredAccountCount()
{
    if (!m_accounts) return;
    int n = 0;
    for (const auto &a : m_accounts->list()) {
        if (m_accounts->stateOf(a.id) == sip::RegistrationState::Registered) ++n;
    }
    if (n == m_registeredAccountCount) return;
    m_registeredAccountCount = n;
    emit registeredAccountCountChanged();
}

} // namespace compactphone

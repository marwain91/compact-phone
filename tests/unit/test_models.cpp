#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/Contact.h"
#include "core/ContactsManager.h"
#include "core/HistoryEntry.h"
#include "core/HistoryManager.h"
#include "core/LinesManager.h"
#include "core/Message.h"
#include "core/MessagesManager.h"
#include "core/platform/Keychain_memory.h"
#include "models/AccountsModel.h"
#include "models/ConversationsModel.h"
#include "models/ContactsModel.h"
#include "models/HistoryModel.h"
#include "models/LinesModel.h"
#include "models/MessagesModel.h"
#include "persistence/Database.h"

#include <sqlite3.h>

namespace {

void seedHistoryAccount(compactphone::persistence::Database &db)
{
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(db.handle(),
        "INSERT INTO accounts (display_name, username, domain, "
        "password_ref, transport, srtp_mode, dtmf_method) "
        "VALUES ('seed','seed','example.com','ref','udp','optional','rfc2833')",
        -1, &stmt, nullptr);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void appendMessage(compactphone::sip::MessagesManager &manager,
                   const std::string &peer,
                   compactphone::sip::MessageDirection direction,
                   const std::string &body,
                   std::int64_t createdAtMs,
                   bool read)
{
    compactphone::sip::Message message;
    message.accountId = 1;
    message.peerUri = peer;
    message.direction = direction;
    message.body = body;
    message.createdAtMs = createdAtMs;
    message.read = read;
    ASSERT_TRUE(manager.append(message));
}

} // namespace

TEST(AccountsModel, ExposesAccountRowsRolesAndFallbackLabel)
{
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    compactphone::platform::MemoryKeychain kc;
    compactphone::sip::AccountsManager manager(nullptr, &db, &kc);

    compactphone::sip::Account account;
    account.displayName = "Work Line";
    account.username = "1001";
    account.domain = "example.com";
    account.transport = compactphone::sip::Transport::Tcp;
    account.enabled = false;
    account.isDefault = true;
    const auto id = manager.add(account, "secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    compactphone::models::AccountsModel model(&manager);
    ASSERT_EQ(model.rowCount(), 1);
    const auto index = model.index(0, 0);

    EXPECT_EQ(model.data(index, compactphone::models::AccountsModel::IdRole).toInt(), id);
    EXPECT_EQ(model.data(index, compactphone::models::AccountsModel::DisplayNameRole).toString(),
              QStringLiteral("Work Line"));
    EXPECT_EQ(model.data(index, compactphone::models::AccountsModel::UsernameRole).toString(),
              QStringLiteral("1001"));
    EXPECT_EQ(model.data(index, compactphone::models::AccountsModel::DomainRole).toString(),
              QStringLiteral("example.com"));
    EXPECT_EQ(model.data(index, compactphone::models::AccountsModel::TransportRole).toString(),
              QStringLiteral("tcp"));
    EXPECT_TRUE(model.data(index, compactphone::models::AccountsModel::IsDefaultRole).toBool());
    EXPECT_FALSE(model.data(index, compactphone::models::AccountsModel::EnabledRole).toBool());
    EXPECT_EQ(model.data(index, compactphone::models::AccountsModel::RegistrationStateRole).toString(),
              QStringLiteral("unregistered"));
    EXPECT_EQ(model.data(index, compactphone::models::AccountsModel::LabelRole).toString(),
              QStringLiteral("Work Line"));
    EXPECT_FALSE(model.data(model.index(5, 0), compactphone::models::AccountsModel::IdRole).isValid());
    EXPECT_EQ(model.rowCount(model.index(0, 0)), 0);
}

TEST(ContactsModel, RefreshesRowsAndExposesContactRoles)
{
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    compactphone::sip::ContactsManager manager(&db);
    compactphone::models::ContactsModel model(&manager);
    EXPECT_EQ(model.rowCount(), 0);

    compactphone::sip::Contact contact;
    contact.displayName = "Alice";
    contact.sipUri = "sip:alice@example.com";
    contact.phone = "+420123456";
    contact.favorite = true;
    const auto id = manager.add(contact);
    ASSERT_NE(id, compactphone::sip::kInvalidContactId);

    model.refresh();
    ASSERT_EQ(model.rowCount(), 1);
    const auto index = model.index(0, 0);
    EXPECT_EQ(model.data(index, compactphone::models::ContactsModel::IdRole).toInt(), id);
    EXPECT_EQ(model.data(index, compactphone::models::ContactsModel::DisplayNameRole).toString(),
              QStringLiteral("Alice"));
    EXPECT_EQ(model.data(index, compactphone::models::ContactsModel::SipUriRole).toString(),
              QStringLiteral("sip:alice@example.com"));
    EXPECT_EQ(model.data(index, compactphone::models::ContactsModel::PhoneRole).toString(),
              QStringLiteral("+420123456"));
    EXPECT_TRUE(model.data(index, compactphone::models::ContactsModel::FavoriteRole).toBool());
    EXPECT_FALSE(model.data(QModelIndex{}, compactphone::models::ContactsModel::IdRole).isValid());
}

TEST(HistoryModel, ExposesNewestHistoryRowsAndRoles)
{
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    seedHistoryAccount(db);

    compactphone::sip::HistoryManager manager(&db);
    compactphone::sip::HistoryEntry entry;
    entry.accountId = 1;
    entry.direction = compactphone::sip::CallDirection::Inbound;
    entry.remoteUri = "sip:caller@example.com";
    entry.remoteDisplayName = "Caller";
    entry.startedAt = 1700000000000LL;
    entry.durationMs = 1234;
    entry.endReason = "normal";
    const auto id = manager.append(entry);
    ASSERT_NE(id, compactphone::sip::kInvalidHistoryId);

    compactphone::models::HistoryModel model(&manager);
    ASSERT_EQ(model.rowCount(), 1);
    const auto index = model.index(0, 0);
    EXPECT_EQ(model.data(index, compactphone::models::HistoryModel::IdRole).toInt(), id);
    EXPECT_EQ(model.data(index, compactphone::models::HistoryModel::AccountIdRole).toInt(), 1);
    EXPECT_EQ(model.data(index, compactphone::models::HistoryModel::DirectionRole).toString(),
              QStringLiteral("in"));
    EXPECT_EQ(model.data(index, compactphone::models::HistoryModel::RemoteUriRole).toString(),
              QStringLiteral("sip:caller@example.com"));
    EXPECT_EQ(model.data(index, compactphone::models::HistoryModel::RemoteDisplayNameRole).toString(),
              QStringLiteral("Caller"));
    EXPECT_EQ(model.data(index, compactphone::models::HistoryModel::StartedAtRole).toLongLong(),
              1700000000000LL);
    EXPECT_EQ(model.data(index, compactphone::models::HistoryModel::DurationMsRole).toLongLong(),
              1234);
    EXPECT_EQ(model.data(index, compactphone::models::HistoryModel::EndReasonRole).toString(),
              QStringLiteral("normal"));
}

TEST(MessagesModel, ShowsOnlySelectedPeerInThreadOrder)
{
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    compactphone::sip::MessagesManager manager(&db);

    appendMessage(manager, "sip:bob@example.com",
                  compactphone::sip::MessageDirection::Incoming,
                  "oldest bob", 1000, false);
    appendMessage(manager, "sip:alice@example.com",
                  compactphone::sip::MessageDirection::Outgoing,
                  "alice", 2000, true);
    appendMessage(manager, "sip:bob@example.com",
                  compactphone::sip::MessageDirection::Outgoing,
                  "newest bob", 3000, true);

    compactphone::models::MessagesModel model(&manager);
    EXPECT_EQ(model.rowCount(), 0);

    int peerSignals = 0;
    QObject::connect(&model, &compactphone::models::MessagesModel::peerChanged,
                     [&] { ++peerSignals; });

    model.setPeer(QStringLiteral("sip:bob@example.com"));
    EXPECT_EQ(peerSignals, 1);
    ASSERT_EQ(model.rowCount(), 2);

    auto first = model.index(0, 0);
    EXPECT_EQ(model.data(first, compactphone::models::MessagesModel::DirectionRole).toString(),
              QStringLiteral("in"));
    EXPECT_EQ(model.data(first, compactphone::models::MessagesModel::BodyRole).toString(),
              QStringLiteral("oldest bob"));
    EXPECT_EQ(model.data(first, compactphone::models::MessagesModel::CreatedAtMsRole).toLongLong(),
              1000);
    EXPECT_FALSE(model.data(first, compactphone::models::MessagesModel::ReadRole).toBool());

    auto second = model.index(1, 0);
    EXPECT_EQ(model.data(second, compactphone::models::MessagesModel::DirectionRole).toString(),
              QStringLiteral("out"));
    EXPECT_EQ(model.data(second, compactphone::models::MessagesModel::BodyRole).toString(),
              QStringLiteral("newest bob"));
    EXPECT_FALSE(model.data(model.index(5, 0),
                            compactphone::models::MessagesModel::BodyRole).isValid());

    model.setPeer(QStringLiteral("sip:bob@example.com"));
    EXPECT_EQ(peerSignals, 1);
}

TEST(ConversationsModel, ExposesPeersNewestFirstWithUnreadFlag)
{
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    compactphone::sip::MessagesManager manager(&db);

    appendMessage(manager, "sip:bob@example.com",
                  compactphone::sip::MessageDirection::Incoming,
                  "unread bob", 1000, false);
    appendMessage(manager, "sip:alice@example.com",
                  compactphone::sip::MessageDirection::Outgoing,
                  "latest alice", 3000, true);
    appendMessage(manager, "sip:bob@example.com",
                  compactphone::sip::MessageDirection::Incoming,
                  "latest bob", 2000, true);

    compactphone::models::ConversationsModel model(&manager);
    ASSERT_EQ(model.rowCount(), 2);

    auto first = model.index(0, 0);
    EXPECT_EQ(model.data(first, compactphone::models::ConversationsModel::PeerRole).toString(),
              QStringLiteral("sip:alice@example.com"));
    EXPECT_EQ(model.data(first, compactphone::models::ConversationsModel::LastBodyRole).toString(),
              QStringLiteral("latest alice"));
    EXPECT_EQ(model.data(first, compactphone::models::ConversationsModel::LastDirectionRole).toString(),
              QStringLiteral("out"));
    EXPECT_FALSE(model.data(first, compactphone::models::ConversationsModel::UnreadRole).toBool());

    auto second = model.index(1, 0);
    EXPECT_EQ(model.data(second, compactphone::models::ConversationsModel::PeerRole).toString(),
              QStringLiteral("sip:bob@example.com"));
    EXPECT_EQ(model.data(second, compactphone::models::ConversationsModel::LastBodyRole).toString(),
              QStringLiteral("latest bob"));
    EXPECT_EQ(model.data(second, compactphone::models::ConversationsModel::LastCreatedAtMsRole).toLongLong(),
              2000);
    EXPECT_TRUE(model.data(second, compactphone::models::ConversationsModel::UnreadRole).toBool());
}

// ---- Empty-state coverage ---------------------------------------------

TEST(EmptyModels, AllModelsHaveRoleNamesAndReturnInvalidForOutOfRange)
{
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    compactphone::platform::MemoryKeychain kc;

    compactphone::sip::AccountsManager am(nullptr, &db, &kc);
    compactphone::sip::ContactsManager cm(&db);
    compactphone::sip::HistoryManager hm(&db);
    compactphone::sip::MessagesManager mm(&db);
    compactphone::sip::LinesManager lm(&db, nullptr);

    compactphone::models::AccountsModel am_model(&am);
    compactphone::models::ContactsModel cm_model(&cm);
    compactphone::models::HistoryModel hm_model(&hm);
    compactphone::models::MessagesModel m_model(&mm);
    compactphone::models::ConversationsModel c_model(&mm);
    compactphone::models::LinesModel l_model(&lm);

    for (QAbstractListModel *m : {
            static_cast<QAbstractListModel *>(&am_model),
            static_cast<QAbstractListModel *>(&cm_model),
            static_cast<QAbstractListModel *>(&hm_model),
            static_cast<QAbstractListModel *>(&m_model),
            static_cast<QAbstractListModel *>(&c_model),
            static_cast<QAbstractListModel *>(&l_model),
        }) {
        EXPECT_EQ(m->rowCount(), 0);
        EXPECT_FALSE(m->roleNames().isEmpty());
        // Out-of-range index returns an invalid QVariant for any role.
        EXPECT_FALSE(m->data(m->index(0, 0), Qt::UserRole + 1).isValid());
        EXPECT_FALSE(m->data(m->index(100, 0), Qt::DisplayRole).isValid());
    }
}

TEST(LinesModel, RefreshesRowsAndExposesLineRoles)
{
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    compactphone::sip::LinesManager manager(&db, nullptr);
    compactphone::models::LinesModel model(&manager);
    EXPECT_EQ(model.rowCount(), 0);

    const auto id = manager.add(42, "sip:1001@example.com", "Support");
    ASSERT_NE(id, compactphone::sip::kInvalidWatchedLineId);

    ASSERT_EQ(model.rowCount(), 1);
    const auto index = model.index(0, 0);
    EXPECT_EQ(model.data(index, compactphone::models::LinesModel::IdRole).toInt(), id);
    EXPECT_EQ(model.data(index, compactphone::models::LinesModel::AccountIdRole).toInt(), 42);
    EXPECT_EQ(model.data(index, compactphone::models::LinesModel::UriRole).toString(),
              QStringLiteral("sip:1001@example.com"));
    EXPECT_EQ(model.data(index, compactphone::models::LinesModel::LabelRole).toString(),
              QStringLiteral("Support"));
    EXPECT_EQ(model.data(index, compactphone::models::LinesModel::StateRole).toString(),
              QStringLiteral("unknown"));
    EXPECT_FALSE(model.data(model.index(5, 0),
                            compactphone::models::LinesModel::UriRole).isValid());
}

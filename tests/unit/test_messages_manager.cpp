#include <gtest/gtest.h>

#include "core/Message.h"
#include "core/MessagesManager.h"
#include "persistence/Database.h"

TEST(MessagesManagerTest, AppendListPeersUnreadAndMarkRead)
{
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    compactphone::sip::MessagesManager manager(&db);

    int changedSignals = 0;
    QObject::connect(&manager, &compactphone::sip::MessagesManager::messagesChanged,
                     [&] { ++changedSignals; });

    compactphone::sip::Message first;
    first.accountId = 1;
    first.peerUri = "sip:bob@example.com";
    first.direction = compactphone::sip::MessageDirection::Incoming;
    first.body = "first";
    first.createdAtMs = 1000;
    first.read = false;
    EXPECT_TRUE(manager.append(first));
    EXPECT_GT(first.id, 0);

    compactphone::sip::Message second;
    second.accountId = 1;
    second.peerUri = "sip:alice@example.com";
    second.direction = compactphone::sip::MessageDirection::Outgoing;
    second.body = "second";
    second.createdAtMs = 2000;
    second.read = true;
    EXPECT_TRUE(manager.append(second));

    compactphone::sip::Message third;
    third.accountId = 1;
    third.peerUri = "sip:bob@example.com";
    third.direction = compactphone::sip::MessageDirection::Incoming;
    third.body = "third";
    third.createdAtMs = 3000;
    third.read = true;
    EXPECT_TRUE(manager.append(third));
    EXPECT_EQ(changedSignals, 3);

    const auto newestFirst = manager.list();
    ASSERT_EQ(newestFirst.size(), 3u);
    EXPECT_EQ(newestFirst[0].body, "third");
    EXPECT_EQ(newestFirst[1].body, "second");
    EXPECT_EQ(newestFirst[2].body, "first");

    const auto peers = manager.peers();
    ASSERT_EQ(peers.size(), 2u);
    EXPECT_EQ(peers[0], "sip:bob@example.com");
    EXPECT_EQ(peers[1], "sip:alice@example.com");

    const auto bobThread = manager.listByPeer("sip:bob@example.com");
    ASSERT_EQ(bobThread.size(), 2u);
    EXPECT_EQ(bobThread[0].body, "first");
    EXPECT_EQ(bobThread[1].body, "third");

    EXPECT_EQ(manager.unreadCount(), 1);
    EXPECT_TRUE(manager.markPeerRead("sip:bob@example.com"));
    EXPECT_EQ(manager.unreadCount(), 0);
    EXPECT_EQ(changedSignals, 4);

    EXPECT_TRUE(manager.markPeerRead("sip:missing@example.com"));
    EXPECT_EQ(changedSignals, 4);
}

TEST(MessagesManagerTest, HandlesMissingDatabaseGracefully)
{
    compactphone::sip::MessagesManager manager(nullptr);
    compactphone::sip::Message message;

    EXPECT_FALSE(manager.append(message));
    EXPECT_TRUE(manager.list().empty());
    EXPECT_TRUE(manager.peers().empty());
    EXPECT_TRUE(manager.listByPeer("sip:any@example.com").empty());
    EXPECT_FALSE(manager.markPeerRead("sip:any@example.com"));
    EXPECT_EQ(manager.unreadCount(), 0);
}

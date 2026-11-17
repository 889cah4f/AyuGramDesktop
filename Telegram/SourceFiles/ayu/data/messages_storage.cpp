// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/data/messages_storage.h"

#include "ayu/data/ayu_database.h"
#include "ayu/data/deleted_media.h"
#include "ayu/utils/ayu_mapper.h"
#include "ayu/utils/telegram_helpers.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_forum_topic.h"
#include "data/data_session.h"
#include "data/data_types.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"

namespace AyuMessages {

template<typename DerivedMessage>
std::vector<AyuMessageBase> convertToBase(const std::vector<DerivedMessage> &messages) {
	std::vector<AyuMessageBase> based;
	based.reserve(messages.size());
	for (const auto &msg : messages) {
		based.push_back(static_cast<AyuMessageBase>(msg));
	}
	return based;
}

void map(not_null<HistoryItem*> item, AyuMessageBase &message) {
	const ID userId = item->history()->owner().session().userId().bare & PeerId::kChatTypeMask;

	message.userId = userId;
	message.dialogId = getDialogIdFromPeer(item->history()->peer);
	message.groupedId = item->groupId().raw();
	message.peerId = item->history()->peer->id.value & PeerId::kChatTypeMask;
	message.fromId = item->from()->id.value & PeerId::kChatTypeMask;
	if (item->topic()) {
		message.topicId = item->topicRootId().bare;
	} else {
		message.topicId = 0;
	}
	message.messageId = item->id.bare;
	message.date = item->date();
	message.flags = AyuMapper::mapItemFlagsToMTPFlags(item);

	if (const auto edited = item->Get<HistoryMessageEdited>()) {
		message.editDate = edited->date;
	} else {
		message.editDate = base::unixtime::now();
	}

	message.views = item->viewsCount();
	message.fwdFlags = 0;
	message.fwdFromId = 0;
	// message.fwdName
	message.fwdDate = 0;
	// message.fwdPostAuthor
	if (const auto msgsigned = item->Get<HistoryMessageSigned>()) {
		message.postAuthor = msgsigned->author.toStdString();
	}
	message.replyFlags = 0;
	message.replyMessageId = 0;
	message.replyPeerId = 0;
	message.replyTopId = 0;
	message.replyForumTopic = false;
	// message.replySerialized
	// message.replyMarkupSerialized
	message.entityCreateDate = base::unixtime::now();

	auto serializedText = AyuMapper::serializeTextWithEntities(item);
	message.text = serializedText.first;
	message.textEntities = serializedText.second;

	saveMediaForMessage(item, message);
}

void addEditedMessage(not_null<HistoryItem *> item) {
	EditedMessage message;
	map(item, message);

	if (message.text.empty()) {
		return;
	}

	AyuDatabase::addEditedMessage(message);
}

std::vector<AyuMessageBase> getEditedMessages(not_null<HistoryItem*> item, ID minId, ID maxId, int totalLimit) {
	const ID userId = item->history()->owner().session().userId().bare & PeerId::kChatTypeMask;
	const auto dialogId = getDialogIdFromPeer(item->history()->peer);
	const auto msgId = item->id.bare;

	return convertToBase(AyuDatabase::getEditedMessages(userId, dialogId, msgId, minId, maxId, totalLimit));
}

bool hasRevisions(not_null<HistoryItem*> item) {
	const ID userId = item->history()->owner().session().userId().bare & PeerId::kChatTypeMask;
	const auto dialogId = getDialogIdFromPeer(item->history()->peer);
	const auto msgId = item->id.bare;

	return AyuDatabase::hasRevisions(userId, dialogId, msgId);
}

void addDeletedMessage(not_null<HistoryItem*> item) {
	DeletedMessage message;
	map(item, message);

	const auto hasMedia = message.documentType != 0;
	if (message.text.empty() && !hasMedia) {
		return;
	}

	AyuDatabase::addDeletedMessage(message);
	fetchDeletedMedia(item, message);
}

std::vector<AyuMessageBase>
getDeletedMessages(not_null<PeerData*> peer, ID topicId, ID minId, ID maxId, int totalLimit, const QString &searchQuery) {
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	return convertToBase(
		AyuDatabase::getDeletedMessages(userId, getDialogIdFromPeer(peer), topicId, minId, maxId, totalLimit, searchQuery.toStdString()));
}

namespace {

constexpr auto kFilterBatch = 400;
constexpr auto kFilterMaxPasses = 120;
constexpr auto kSerializedVersionTag = qint32(0x7FFFFFFF);

[[nodiscard]] int SerializedDocumentType(const std::vector<char> &serialized) {
	if (serialized.empty()) {
		return -1;
	}
	auto bytes = QByteArray::fromRawData(
		serialized.data(),
		int(serialized.size()));
	auto stream = QDataStream(&bytes, QIODevice::ReadOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	quint64 id = 0, access = 0;
	qint32 date = 0, versionTag = 0, version = 0;
	qint32 dc = 0, size = 0, width = 0, height = 0, type = -1;
	QByteArray fileReference;
	QString name, mime;
	stream >> id >> access >> date >> fileReference >> versionTag;
	if (versionTag == kSerializedVersionTag) {
		stream >> version;
	}
	stream >> name >> mime >> dc >> size >> width >> height >> type;
	if (stream.status() != QDataStream::Ok
		|| type < FileDocument
		|| type > WallPaperDocument) {
		return -1;
	}
	return type;
}

[[nodiscard]] bool MatchesFilter(
		const AyuMessageBase &message,
		DeletedFilter filter) {
	if (filter == DeletedFilter::All) {
		return true;
	} else if (message.documentType == int(SavedMediaType::Photo)) {
		return (filter == DeletedFilter::Media);
	} else if (message.documentType != int(SavedMediaType::Document)) {
		return false;
	}
	const auto mime = QString::fromStdString(message.mimeType).toLower();
	auto type = SerializedDocumentType(message.documentSerialized);
	if (type < 0) {
		type = mime.startsWith(u"video/"_q)
			? VideoDocument
			: (mime == u"audio/ogg"_q)
			? VoiceDocument
			: FileDocument;
	}
	switch (filter) {
	case DeletedFilter::Media: return (type == VideoDocument);
	case DeletedFilter::Gifs: return (type == AnimatedDocument);
	case DeletedFilter::Voice: return (type == VoiceDocument);
	case DeletedFilter::Rounds: return (type == RoundVideoDocument);
	case DeletedFilter::Files:
		return (type == FileDocument) || (type == SongDocument);
	default: return false;
	}
}

} // namespace

std::vector<AyuMessageBase> getDeletedMessages(
		not_null<PeerData*> peer,
		ID topicId,
		ID minId,
		ID maxId,
		int totalLimit,
		const QString &searchQuery,
		DeletedFilter filter) {
	if (filter == DeletedFilter::All) {
		return getDeletedMessages(
			peer,
			topicId,
			minId,
			maxId,
			totalLimit,
			searchQuery);
	}
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	const auto dialogId = getDialogIdFromPeer(peer);
	const auto query = searchQuery.toStdString();
	const auto down = (minId != 0 && maxId == 0);
	auto result = std::vector<AyuMessageBase>();
	auto from = minId;
	auto till = maxId;
	for (auto pass = 0; pass != kFilterMaxPasses; ++pass) {
		const auto batch = AyuDatabase::getDeletedMessages(
			userId,
			dialogId,
			topicId,
			from,
			till,
			kFilterBatch,
			query,
			true);
		if (batch.empty()) {
			break;
		}
		for (const auto &message : batch) {
			if (MatchesFilter(message, filter)) {
				result.push_back(message);
			}
		}
		if (int(batch.size()) < kFilterBatch
			|| int(result.size()) >= totalLimit) {
			break;
		}
		if (down) {
			from = batch.front().messageId;
		} else {
			till = batch.back().messageId;
		}
	}
	std::sort(result.begin(), result.end(), [](
			const AyuMessageBase &a,
			const AyuMessageBase &b) {
		return a.messageId > b.messageId;
	});
	if (int(result.size()) > totalLimit) {
		if (down) {
			result.erase(result.begin(), result.end() - totalLimit);
		} else {
			result.resize(totalLimit);
		}
	}
	return result;
}

bool hasDeletedMessages(not_null<PeerData*> peer, ID topicId) {
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	return AyuDatabase::hasDeletedMessages(userId, getDialogIdFromPeer(peer), topicId);
}

void removeDeletedMessage(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	const auto dialogId = getDialogIdFromPeer(peer);
	for (const auto &stored : AyuDatabase::getDeletedMessages(userId, dialogId, 0, item->id.bare - 1, item->id.bare + 1, 4, {})) {
		if (stored.messageId == item->id.bare) {
			removeSavedMedia(stored);
		}
	}
	AyuDatabase::removeDeletedMessage(userId, dialogId, item->id.bare);
}

void clearDeletedMessages(not_null<PeerData*> peer, ID topicId) {
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	const auto dialogId = getDialogIdFromPeer(peer);
	if (topicId == 0) {
		removeSavedMediaForDialog(userId, dialogId);
	}
	AyuDatabase::clearDeletedMessages(userId, dialogId, topicId);
}

std::vector<AyuMessageBase> getDeletedMessagesByDate(
		not_null<PeerData*> peer,
		ID topicId,
		int dateFrom,
		int dateTill,
		int totalLimit) {
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	return convertToBase(AyuDatabase::getDeletedMessagesByDate(
		userId,
		getDialogIdFromPeer(peer),
		topicId,
		dateFrom,
		dateTill,
		totalLimit));
}

int clearDeletedMessagesInChannels(not_null<Main::Session*> session) {
	const ID userId = session->userId().bare & PeerId::kChatTypeMask;
	auto cleared = 0;
	for (const auto dialogId : AyuDatabase::getDialogsWithDeletedMessages(userId)) {
		if (dialogId >= 0) {
			continue;
		}
		const auto channel = session->data().channelLoaded(ChannelId(-dialogId));
		if (!channel || !channel->isBroadcast()) {
			continue;
		}
		removeSavedMediaForDialog(userId, dialogId);
		AyuDatabase::clearDeletedMessages(userId, dialogId, 0);
		++cleared;
	}
	return cleared;
}

int countDeletedMessages(not_null<PeerData*> peer) {
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	return AyuDatabase::countDeletedMessages(userId, getDialogIdFromPeer(peer));
}

}

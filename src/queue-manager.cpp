#include "queue-manager.hpp"
#include "twitch-api.hpp"

#include <obs-module.h>
#include <QDateTime>
#include <QTimer>

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

QueueManager &QueueManager::instance()
{
	static QueueManager inst;
	return inst;
}

QueueManager::QueueManager(QObject *parent) : QObject(parent) {}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void QueueManager::setBroadcasterInfo(const QString &id,
				      const QString &username)
{
	_broadcasterId = id;
	_broadcasterUsername = username;
}

// ---------------------------------------------------------------------------
// Public queue API
// ---------------------------------------------------------------------------

bool QueueManager::enqueue(const QString &username)
{
	const QString lower = username.toLower();

	// Reject duplicates already pending
	for (const auto &e : qAsConst(_queue)) {
		if (e.username.toLower() == lower)
			return false;
	}

	ShoutoutEntry entry;
	entry.username = username;
	_queue.enqueue(entry);
	emit queueChanged();

	// Kick off processing if nothing is running
	if (!_processing)
		QTimer::singleShot(0, this, &QueueManager::processNext);

	return true;
}

void QueueManager::clearQueue()
{
	_queue.clear();
	emit queueChanged();
}

QList<ShoutoutEntry> QueueManager::pendingEntries() const
{
	return _queue.toList();
}

// ---------------------------------------------------------------------------
// Queue processor — called recursively via QTimer so Qt's event loop
// stays alive and UI remains responsive.
// ---------------------------------------------------------------------------

void QueueManager::processNext()
{
	if (_queue.isEmpty() || _processing || _broadcasterId.isEmpty())
		return;

	_processing = true;

	// ── 1. Live check ────────────────────────────────────────────────────
	TwitchApi::instance().isLive(
		_broadcasterId, [this](bool live) {
			if (!live) {
				_processing = false;
				emit statusMessage(
					"Stream is offline — queue paused. "
					"Will retry when you go live.");
				// Poll every 60 s while offline
				QTimer::singleShot(60'000, this,
						   &QueueManager::processNext);
				return;
			}
			resolveNextUser();
		});
}

void QueueManager::resolveNextUser()
{
	// Peek at the front entry
	ShoutoutEntry &front = _queue.head();

	// ── 2. Resolve username → ID ─────────────────────────────────────────
	TwitchApi::instance().getUserId(
		front.username,
		[this](QString userId) {
			if (userId.isEmpty()) {
				const QString name = _queue.dequeue().username;
				_processing = false;
				emit queueChanged();
				emit shoutoutSkipped(
					name,
					"Twitch user not found");
				QTimer::singleShot(0, this,
						   &QueueManager::processNext);
				return;
			}

			_queue.head().userId = userId;
			checkCooldowns(userId);
		});
}

void QueueManager::checkCooldowns(const QString &userId)
{
	const qint64 now = QDateTime::currentSecsSinceEpoch();
	ShoutoutEntry entry = _queue.head();

	// ── 3a. Per-user cooldown ────────────────────────────────────────────
	if (_userCooldowns.contains(userId)) {
		const qint64 elapsed = now - _userCooldowns[userId];
		if (elapsed < USER_COOLDOWN_SECS) {
			_queue.dequeue();
			_processing = false;
			emit queueChanged();

			const int minsLeft =
				static_cast<int>((USER_COOLDOWN_SECS - elapsed) /
						 60) + 1;
			emit shoutoutSkipped(
				entry.username,
				QString("On cooldown — %1 min remaining")
					.arg(minsLeft));

			QTimer::singleShot(0, this,
					   &QueueManager::processNext);
			return;
		}
	}

	// ── 3b. Global cooldown ──────────────────────────────────────────────
	if (_lastGlobalShoutoutAt > 0) {
		const qint64 globalElapsed = now - _lastGlobalShoutoutAt;
		if (globalElapsed < GLOBAL_COOLDOWN_SECS) {
			const qint64 waitMs =
				(GLOBAL_COOLDOWN_SECS - globalElapsed) * 1000;

			emit statusMessage(
				QString("Global cooldown — next shoutout in %1 s")
					.arg((GLOBAL_COOLDOWN_SECS -
					      globalElapsed)));

			// Stay _processing = true to block re-entry
			QTimer::singleShot(static_cast<int>(waitMs), this,
					   [this]() { sendShoutout(); });
			return;
		}
	}

	sendShoutout();
}

void QueueManager::sendShoutout()
{
	if (_queue.isEmpty()) {
		_processing = false;
		return;
	}

	const ShoutoutEntry entry = _queue.dequeue();
	emit queueChanged();

	TwitchApi::instance().sendShoutout(
		_broadcasterId, entry.userId, _broadcasterId,
		[this, entry](bool ok, QString errMsg) {
			const qint64 now =
				QDateTime::currentSecsSinceEpoch();

			if (ok) {
				_userCooldowns[entry.userId] = now;
				_lastGlobalShoutoutAt = now;
				emit shoutoutSent(entry.username);

				// Send a chat confirmation
				TwitchApi::instance().sendChatMessage(
					_broadcasterId, _broadcasterId,
					QString("Shoutout sent to @%1!")
						.arg(entry.username));
			} else {
				emit shoutoutSkipped(entry.username,
						     errMsg);
			}

			// Whether it succeeded or failed, wait for global
			// cooldown before processing the next entry.
			_processing = true;
			QTimer::singleShot(
				GLOBAL_COOLDOWN_SECS * 1000, this,
				[this]() {
					_processing = false;
					processNext();
				});
		});
}

// ---------------------------------------------------------------------------
// Called by checkCooldowns — needs to be a method for the timer slot
// ---------------------------------------------------------------------------

// (sendShoutout is defined above; no extra method needed)

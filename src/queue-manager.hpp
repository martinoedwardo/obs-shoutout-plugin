#pragma once

#include <QObject>
#include <QQueue>
#include <QMap>
#include <QString>

struct ShoutoutEntry {
	QString username; // display name as typed
	QString userId;   // resolved numeric Twitch ID (filled in during processing)
};

// Manages the shoutout queue and enforces Twitch's cooldown rules:
//   - Global:      125 s between any two shoutouts (Twitch enforces 2 min; we
//                  add a 5 s buffer).
//   - Per-streamer: 1 hour before the same channel can be shouted out again.
//
// Processing is automatic — adding an entry triggers the pipeline.
// Singleton — access via QueueManager::instance().
class QueueManager : public QObject {
	Q_OBJECT
public:
	static QueueManager &instance();

	// Must be called once after authentication succeeds.
	void setBroadcasterInfo(const QString &id, const QString &username);

	// Adds username to the back of the queue.
	// Returns false if the same username is already pending.
	bool enqueue(const QString &username);

	// Removes all pending entries (does NOT cancel an in-flight shoutout).
	void clearQueue();

	// Snapshot of the current pending queue (index 0 = next up).
	QList<ShoutoutEntry> pendingEntries() const;

	bool isBusy() const { return _processing; }

signals:
	// Emitted after any queue or state change — dock should refresh its list.
	void queueChanged();

	// A shoutout was successfully sent.
	void shoutoutSent(const QString &username);

	// A queued entry was skipped (cooldown, user not found, etc.).
	void shoutoutSkipped(const QString &username, const QString &reason);

	// Non-fatal status message for the dock log.
	void statusMessage(const QString &message);

	// Fatal-ish error (e.g. auth failure).
	void errorOccurred(const QString &message);

private slots:
	void processNext();

private:
	void resolveNextUser();
	void checkCooldowns(const QString &userId);
	void sendShoutout();


	explicit QueueManager(QObject *parent = nullptr);

	QQueue<ShoutoutEntry> _queue;
	QMap<QString, qint64> _userCooldowns; // userId → Unix timestamp of last SO
	qint64 _lastGlobalShoutoutAt = 0;
	bool _processing = false;

	QString _broadcasterId;
	QString _broadcasterUsername;

	// Twitch allows one shoutout per 2 min globally; we add 5 s buffer.
	static constexpr qint64 GLOBAL_COOLDOWN_SECS = 125;
	// Twitch disallows re-shouting the same channel within 1 hour.
	static constexpr qint64 USER_COOLDOWN_SECS = 3600;
};

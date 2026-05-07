#pragma once

#include <QDockWidget>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QGroupBox>

class ShoutoutDock : public QDockWidget {
	Q_OBJECT
public:
	explicit ShoutoutDock(QWidget *parent = nullptr);

private slots:
	// Auth panel
	void onConnectClicked();
	void onDisconnectClicked();
	void onAuthSuccess(const QString &username, const QString &userId);
	void onAuthFailed(const QString &error);

	// Queue panel
	void onAddClicked();
	void onClearClicked();
	void onInputReturnPressed();

	// QueueManager signals
	void onQueueChanged();
	void onShoutoutSent(const QString &username);
	void onShoutoutSkipped(const QString &username, const QString &reason);
	void onStatusMessage(const QString &message);
	void onErrorOccurred(const QString &message);

	// TwitchChat signals
	void onChatConnected();
	void onChatDisconnected();
	void onBsoCommand(const QStringList &targets);
	void onChatError(const QString &message);

private:
	void buildUi();
	void restoreSession();
	void setConnectedState(const QString &username);
	void setDisconnectedState();
	void refreshQueueList();
	void appendLog(const QString &text, const QString &color = "#cccccc");

	// ── Auth widgets ──────────────────────────────────────────────────────
	QLabel *_authStatusLabel;
	QPushButton *_connectBtn;
	QPushButton *_disconnectBtn;

	// ── Queue widgets ─────────────────────────────────────────────────────
	QListWidget *_queueList;
	QLineEdit *_usernameInput;
	QPushButton *_addBtn;
	QPushButton *_clearBtn;

	// ── Activity log ──────────────────────────────────────────────────────
	QListWidget *_logList;

	// ── Chat status ───────────────────────────────────────────────────────
	QLabel *_chatStatusLabel;

	// State
	QString _broadcasterId;
	QString _broadcasterName;
};

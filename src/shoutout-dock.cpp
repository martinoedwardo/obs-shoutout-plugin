#include "shoutout-dock.hpp"
#include "twitch-auth.hpp"
#include "twitch-chat.hpp"
#include "queue-manager.hpp"
#include "token-store.hpp"
#include "twitch-api.hpp"

#include <obs-module.h>
#include <QWidget>
#include <QHBoxLayout>
#include <QSplitter>
#include <QDateTime>
#include <QListWidgetItem>
#include <QFont>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ShoutoutDock::ShoutoutDock(QWidget *parent) : QDockWidget(parent)
{
	setObjectName("ShoutoutManagerDock");
	setWindowTitle(obs_module_text("ShoutoutManager.Title"));
	setFeatures(QDockWidget::DockWidgetMovable |
		    QDockWidget::DockWidgetFloatable);

	buildUi();

	// ── Wire QueueManager signals ─────────────────────────────────────────
	auto &qm = QueueManager::instance();
	connect(&qm, &QueueManager::queueChanged, this,
		&ShoutoutDock::onQueueChanged);
	connect(&qm, &QueueManager::shoutoutSent, this,
		&ShoutoutDock::onShoutoutSent);
	connect(&qm, &QueueManager::shoutoutSkipped, this,
		&ShoutoutDock::onShoutoutSkipped);
	connect(&qm, &QueueManager::statusMessage, this,
		&ShoutoutDock::onStatusMessage);
	connect(&qm, &QueueManager::errorOccurred, this,
		&ShoutoutDock::onErrorOccurred);

	// ── Wire TwitchChat signals ───────────────────────────────────────────
	auto &chat = TwitchChat::instance();
	connect(&chat, &TwitchChat::chatConnected, this,
		&ShoutoutDock::onChatConnected);
	connect(&chat, &TwitchChat::chatDisconnected, this,
		&ShoutoutDock::onChatDisconnected);
	connect(&chat, &TwitchChat::bsoCommand, this,
		&ShoutoutDock::onBsoCommand);
	connect(&chat, &TwitchChat::chatError, this,
		&ShoutoutDock::onChatError);

	// ── Restore session if tokens already exist ───────────────────────────
	if (TokenStore::instance().hasTokens())
		restoreSession();
	else
		setDisconnectedState();
}

// ---------------------------------------------------------------------------
// UI construction — all done in code, no .ui file required
// ---------------------------------------------------------------------------

void ShoutoutDock::buildUi()
{
	auto *root = new QWidget(this);
	setWidget(root);

	auto *rootLayout = new QVBoxLayout(root);
	rootLayout->setContentsMargins(6, 6, 6, 6);
	rootLayout->setSpacing(6);

	// ── Auth group ────────────────────────────────────────────────────────
	auto *authGroup = new QGroupBox(obs_module_text("ShoutoutManager.Auth"),
					root);
	auto *authLayout = new QVBoxLayout(authGroup);
	authLayout->setSpacing(4);

	_authStatusLabel = new QLabel(
		obs_module_text("ShoutoutManager.NotConnected"), authGroup);
	_authStatusLabel->setWordWrap(true);

	_connectBtn = new QPushButton(
		obs_module_text("ShoutoutManager.ConnectTwitch"), authGroup);
	_connectBtn->setStyleSheet(
		"QPushButton { background-color: #9146ff; color: white; "
		"font-weight: bold; border-radius: 4px; padding: 4px 10px; }"
		"QPushButton:hover { background-color: #772ce8; }");

	_disconnectBtn = new QPushButton(
		obs_module_text("ShoutoutManager.Disconnect"), authGroup);

	_chatStatusLabel = new QLabel(authGroup);
	_chatStatusLabel->setWordWrap(true);
	_chatStatusLabel->hide();

	authLayout->addWidget(_authStatusLabel);
	authLayout->addWidget(_connectBtn);
	authLayout->addWidget(_disconnectBtn);
	authLayout->addWidget(_chatStatusLabel);
	rootLayout->addWidget(authGroup);

	connect(_connectBtn, &QPushButton::clicked, this,
		&ShoutoutDock::onConnectClicked);
	connect(_disconnectBtn, &QPushButton::clicked, this,
		&ShoutoutDock::onDisconnectClicked);

	// ── Queue group ───────────────────────────────────────────────────────
	auto *queueGroup = new QGroupBox(
		obs_module_text("ShoutoutManager.Queue"), root);
	auto *queueLayout = new QVBoxLayout(queueGroup);
	queueLayout->setSpacing(4);

	_queueList = new QListWidget(queueGroup);
	_queueList->setFixedHeight(120);
	_queueList->setAlternatingRowColors(true);

	auto *inputRow = new QHBoxLayout;
	_usernameInput = new QLineEdit(queueGroup);
	_usernameInput->setPlaceholderText(
		obs_module_text("ShoutoutManager.UsernamePlaceholder"));
	_addBtn = new QPushButton(obs_module_text("ShoutoutManager.Add"),
				  queueGroup);
	_addBtn->setDefault(true);
	inputRow->addWidget(_usernameInput, 1);
	inputRow->addWidget(_addBtn);

	_clearBtn = new QPushButton(obs_module_text("ShoutoutManager.ClearQueue"),
				    queueGroup);

	queueLayout->addWidget(_queueList);
	queueLayout->addLayout(inputRow);
	queueLayout->addWidget(_clearBtn);
	rootLayout->addWidget(queueGroup);

	connect(_addBtn, &QPushButton::clicked, this,
		&ShoutoutDock::onAddClicked);
	connect(_usernameInput, &QLineEdit::returnPressed, this,
		&ShoutoutDock::onInputReturnPressed);
	connect(_clearBtn, &QPushButton::clicked, this,
		&ShoutoutDock::onClearClicked);

	// ── Activity log ──────────────────────────────────────────────────────
	auto *logGroup = new QGroupBox(
		obs_module_text("ShoutoutManager.Log"), root);
	auto *logLayout = new QVBoxLayout(logGroup);

	_logList = new QListWidget(logGroup);
	_logList->setFixedHeight(130);
	_logList->setFont(QFont("Monospace", 8));
	logLayout->addWidget(_logList);
	rootLayout->addWidget(logGroup);

	rootLayout->addStretch();
}

// ---------------------------------------------------------------------------
// Restore persisted session
// ---------------------------------------------------------------------------

void ShoutoutDock::restoreSession()
{
	// Try to validate the stored token by calling /helix/users
	QNetworkRequest req(QUrl("https://api.twitch.tv/helix/users"));
	req.setRawHeader("Client-Id", TWITCH_CLIENT_ID);
	req.setRawHeader(
		"Authorization",
		QStringLiteral("Bearer %1")
			.arg(TokenStore::instance().accessToken())
			.toUtf8());

	auto *nam = new QNetworkAccessManager(this);
	auto *reply = nam->get(req);

	connect(reply, &QNetworkReply::finished,
		[this, reply, nam]() {
			reply->deleteLater();
			nam->deleteLater();

			const int status =
				reply->attribute(
					QNetworkRequest::HttpStatusCodeAttribute)
					.toInt();

			if (status == 200) {
				const auto data =
					QJsonDocument::fromJson(reply->readAll())
						.object()["data"]
						.toArray();

				if (!data.isEmpty()) {
					const auto user =
						data[0].toObject();
					onAuthSuccess(
						user["display_name"]
							.toString(),
						user["id"].toString());
					return;
				}
			}

			// Token stale — try refresh
			TwitchApi::instance().refreshToken(
				[this](bool ok) {
					if (ok)
						restoreSession();
					else
						setDisconnectedState();
				});
		});
}

// ---------------------------------------------------------------------------
// Auth slots
// ---------------------------------------------------------------------------

void ShoutoutDock::onConnectClicked()
{
	auto *auth = new TwitchAuth(this);
	connect(auth, &TwitchAuth::authSuccess, this,
		&ShoutoutDock::onAuthSuccess);
	connect(auth, &TwitchAuth::authFailed, this,
		&ShoutoutDock::onAuthFailed);
	connect(auth, &TwitchAuth::authSuccess, auth, &QObject::deleteLater);
	connect(auth, &TwitchAuth::authFailed, auth, &QObject::deleteLater);

	_connectBtn->setEnabled(false);
	_connectBtn->setText(
		obs_module_text("ShoutoutManager.WaitingForBrowser"));
	auth->startOAuthFlow();
}

void ShoutoutDock::onDisconnectClicked()
{
	TwitchChat::instance().disconnectFromChat();
	TokenStore::instance().clear();
	QueueManager::instance().clearQueue();
	setDisconnectedState();
	appendLog("Disconnected from Twitch", "#ff9900");
}

void ShoutoutDock::onAuthSuccess(const QString &username, const QString &userId)
{
	_broadcasterId = userId;
	_broadcasterName = username;
	QueueManager::instance().setBroadcasterInfo(userId, username);
	setConnectedState(username);
	appendLog(QString("Connected as %1").arg(username), "#00cc66");

	// Start listening for !bso in chat
	TwitchChat::instance().connectToChat(userId,
					     TokenStore::instance().accessToken());
}

void ShoutoutDock::onAuthFailed(const QString &error)
{
	setDisconnectedState();
	appendLog(QString("Auth failed: %1").arg(error), "#ff4444");
}

// ---------------------------------------------------------------------------
// Queue slots
// ---------------------------------------------------------------------------

void ShoutoutDock::onAddClicked()
{
	const QString name = _usernameInput->text().trimmed();
	if (name.isEmpty())
		return;

	if (_broadcasterId.isEmpty()) {
		appendLog("Connect to Twitch first", "#ff9900");
		return;
	}

	if (QueueManager::instance().enqueue(name)) {
		_usernameInput->clear();
		appendLog(QString("Queued: %1").arg(name), "#aaaaff");
	} else {
		appendLog(QString("%1 is already in the queue").arg(name),
			  "#ff9900");
	}
}

void ShoutoutDock::onInputReturnPressed()
{
	onAddClicked();
}

void ShoutoutDock::onClearClicked()
{
	QueueManager::instance().clearQueue();
	appendLog("Queue cleared", "#888888");
}

void ShoutoutDock::onQueueChanged()
{
	refreshQueueList();
}

void ShoutoutDock::onShoutoutSent(const QString &username)
{
	appendLog(QString("✓ Shoutout sent to %1").arg(username), "#00cc66");
	refreshQueueList();
}

void ShoutoutDock::onShoutoutSkipped(const QString &username,
				     const QString &reason)
{
	appendLog(QString("⚠ Skipped %1 — %2").arg(username, reason),
		  "#ff9900");
}

void ShoutoutDock::onStatusMessage(const QString &message)
{
	appendLog(message, "#888888");
}

void ShoutoutDock::onErrorOccurred(const QString &message)
{
	appendLog(QString("✗ %1").arg(message), "#ff4444");
}

// ---------------------------------------------------------------------------
// Chat slots
// ---------------------------------------------------------------------------

void ShoutoutDock::onChatConnected()
{
	_chatStatusLabel->setText(
		"&#x1F7E2; Chat connected — listening for <b>!bso</b>");
	_chatStatusLabel->show();
	appendLog("Chat connected — listening for !bso", "#00cc66");
}

void ShoutoutDock::onChatDisconnected()
{
	_chatStatusLabel->setText(
		"&#x1F534; Chat disconnected — reconnecting...");
	_chatStatusLabel->show();
	appendLog("Chat disconnected", "#ff9900");
}

void ShoutoutDock::onBsoCommand(const QStringList &targets)
{
	for (const QString &name : targets) {
		if (QueueManager::instance().enqueue(name))
			appendLog(
				QString("!bso → queued: %1").arg(name),
				"#aaaaff");
		else
			appendLog(
				QString("!bso → %1 already in queue").arg(name),
				"#ff9900");
	}
}

void ShoutoutDock::onChatError(const QString &message)
{
	appendLog(QString("Chat error: %1").arg(message), "#ff4444");
}

// ---------------------------------------------------------------------------
// UI helpers
// ---------------------------------------------------------------------------

void ShoutoutDock::setConnectedState(const QString &username)
{
	_authStatusLabel->setText(
		QString("%1 <b>%2</b>")
			.arg(obs_module_text("ShoutoutManager.ConnectedAs"),
			     username));
	_connectBtn->hide();
	_disconnectBtn->show();
	_addBtn->setEnabled(true);
	_usernameInput->setEnabled(true);
	_clearBtn->setEnabled(true);
}

void ShoutoutDock::setDisconnectedState()
{
	_broadcasterId.clear();
	_broadcasterName.clear();

	_authStatusLabel->setText(
		obs_module_text("ShoutoutManager.NotConnected"));
	_connectBtn->setText(
		obs_module_text("ShoutoutManager.ConnectTwitch"));
	_connectBtn->setEnabled(true);
	_connectBtn->show();
	_disconnectBtn->hide();
	_chatStatusLabel->hide();
	_addBtn->setEnabled(false);
	_usernameInput->setEnabled(false);
	_clearBtn->setEnabled(false);

	_queueList->clear();
}

void ShoutoutDock::refreshQueueList()
{
	_queueList->clear();
	const auto entries = QueueManager::instance().pendingEntries();
	for (int i = 0; i < entries.size(); ++i) {
		const auto &e = entries[i];
		QString label = (i == 0 && QueueManager::instance().isBusy())
					? QString("▶ %1").arg(e.username)
					: QString("  %1").arg(e.username);
		_queueList->addItem(label);
	}

	if (entries.isEmpty())
		_queueList->addItem(
			obs_module_text("ShoutoutManager.QueueEmpty"));
}

void ShoutoutDock::appendLog(const QString &text, const QString &color)
{
	const QString time =
		QDateTime::currentDateTime().toString("HH:mm:ss");
	const QString html =
		QString("<span style='color:%1'>[%2] %3</span>")
			.arg(color, time, text.toHtmlEscaped());

	auto *item = new QListWidgetItem();
	item->setText(QString("[%1] %2").arg(time, text));
	item->setForeground(QColor(color));
	_logList->addItem(item);

	// Keep log from growing unbounded
	while (_logList->count() > 200)
		delete _logList->takeItem(0);

	_logList->scrollToBottom();
}

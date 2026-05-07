#include "token-store.hpp"

#include <obs-module.h>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

TokenStore &TokenStore::instance()
{
	static TokenStore inst;
	return inst;
}

TokenStore::TokenStore()
{
	// obs_module_get_config_path returns e.g.:
	//   Windows: %APPDATA%/obs-studio/plugin_config/obs-shoutout-manager/
	//   macOS:   ~/Library/Application Support/obs-studio/plugin_config/...
	//   Linux:   ~/.config/obs-studio/plugin_config/...
	char *configDir = obs_module_get_config_path(obs_current_module(), "");
	QDir dir(QString::fromUtf8(configDir));
	bfree(configDir);

	if (!dir.exists())
		dir.mkpath(".");

	_filePath = dir.filePath("tokens.json");
	load();
}

bool TokenStore::hasTokens() const
{
	return !_accessToken.isEmpty() && !_refreshToken.isEmpty();
}

QString TokenStore::accessToken() const
{
	return _accessToken;
}

QString TokenStore::refreshToken() const
{
	return _refreshToken;
}

void TokenStore::setTokens(const QString &access, const QString &refresh)
{
	_accessToken = access;
	_refreshToken = refresh;
	save();
}

void TokenStore::setAccessToken(const QString &access)
{
	_accessToken = access;
	save();
}

void TokenStore::clear()
{
	_accessToken.clear();
	_refreshToken.clear();
	QFile::remove(_filePath);
}

void TokenStore::load()
{
	QFile file(_filePath);
	if (!file.open(QIODevice::ReadOnly))
		return;

	auto doc = QJsonDocument::fromJson(file.readAll());
	if (doc.isNull() || !doc.isObject())
		return;

	auto obj = doc.object();
	_accessToken = obj["access_token"].toString();
	_refreshToken = obj["refresh_token"].toString();
}

void TokenStore::save() const
{
	QFile file(_filePath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return;

	QJsonObject obj;
	obj["access_token"] = _accessToken;
	obj["refresh_token"] = _refreshToken;

	file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

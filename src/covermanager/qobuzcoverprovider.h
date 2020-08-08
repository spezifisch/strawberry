/*
 * Strawberry Music Player
 * Copyright 2020, Jonas Kvinge <jonas@jkvinge.net>
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QOBUZCOVERPROVIDER_H
#define QOBUZCOVERPROVIDER_H

#include "config.h"

#include <QObject>
#include <QList>
#include <QVariant>
#include <QByteArray>
#include <QString>
#include <QJsonObject>
#include <QSslError>

#include "jsoncoverprovider.h"

class QNetworkAccessManager;
class QNetworkReply;
class Application;

class QobuzCoverProvider : public JsonCoverProvider {
  Q_OBJECT

 public:
  explicit QobuzCoverProvider(Application *app, QObject *parent = nullptr);
  ~QobuzCoverProvider() override;

  bool StartSearch(const QString &artist, const QString &album, const QString &title, const int id) override;
  void CancelSearch(const int id) override;

  void Authenticate() override;
  void Deauthenticate() override;
  bool IsAuthenticated() const override { return !user_auth_token_.isEmpty(); }

 private slots:
  void HandleLoginSSLErrors(QList<QSslError> ssl_errors);
  void HandleAuthReply(QNetworkReply *reply);

 private slots:
  void HandleSearchReply(QNetworkReply *reply, const int id);

 private:
  QByteArray GetReplyData(QNetworkReply *reply);
  void AuthError(const QString &error = QString(), const QVariant &debug = QVariant());
  void Error(const QString &error, const QVariant &debug = QVariant()) override;

 private:
  typedef QPair<QString, QString> Param;
  typedef QList<Param> ParamList;

  static const char *kSettingsGroup;
  static const char *kAuthUrl;
  static const char *kApiUrl;
  static const char *kAppID;
  static const int kLimit;

  QNetworkAccessManager *network_;
  QList<QNetworkReply*> replies_;

  QString username_;
  QString password_;
  qint64 user_id_;
  QString user_auth_token_;
  QString device_id_;
  qint64 credential_id_;
  QStringList login_errors_;

};

#endif  // QOBUZCOVERPROVIDER_H

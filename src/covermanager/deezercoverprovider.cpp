/*
 * Strawberry Music Player
 * Copyright 2018, Jonas Kvinge <jonas@jkvinge.net>
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

#include "config.h"

#include <algorithm>

#include <QtGlobal>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QList>
#include <QMap>
#include <QVariant>
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonArray>
#include <QtDebug>

#include "core/application.h"
#include "core/network.h"
#include "core/logging.h"
#include "core/song.h"
#include "albumcoverfetcher.h"
#include "albumcoverfetchersearch.h"
#include "jsoncoverprovider.h"
#include "deezercoverprovider.h"

const char *DeezerCoverProvider::kApiUrl = "https://api.deezer.com";
const int DeezerCoverProvider::kLimit = 10;

DeezerCoverProvider::DeezerCoverProvider(Application *app, QObject *parent): JsonCoverProvider("Deezer", true, false, 2.0, true, true, app, parent), network_(new NetworkAccessManager(this)) {}

DeezerCoverProvider::~DeezerCoverProvider() {

  while (!replies_.isEmpty()) {
    QNetworkReply *reply = replies_.takeFirst();
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
  }

}

bool DeezerCoverProvider::StartSearch(const QString &artist, const QString &album, const QString &title, const int id) {

  typedef QPair<QString, QString> Param;
  typedef QList<Param> Params;

  if (artist.isEmpty() && album.isEmpty() && title.isEmpty()) return false;

  QString resource;
  QString query = artist;
  if (album.isEmpty() && !title.isEmpty()) {
    resource = "search/track";
    if (!query.isEmpty()) query.append(" ");
    query.append(title);
  }
  else {
    resource = "search/album";
    if (!album.isEmpty()) {
      if (!query.isEmpty()) query.append(" ");
      query.append(album);
    }
  }

  const Params params = Params() << Param("output", "json")
                                 << Param("q", query)
                                 << Param("limit", QString::number(kLimit));

  QUrlQuery url_query;
  for (const Param &param : params) {
    url_query.addQueryItem(QUrl::toPercentEncoding(param.first), QUrl::toPercentEncoding(param.second));
  }

  QUrl url(kApiUrl + QString("/") + resource);
  url.setQuery(url_query);
  QNetworkRequest req(url);
  req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
  QNetworkReply *reply = network_->get(req);
  replies_ << reply;
  connect(reply, &QNetworkReply::finished, [=] { HandleSearchReply(reply, id); });

  return true;

}

void DeezerCoverProvider::CancelSearch(const int id) { Q_UNUSED(id); }

QByteArray DeezerCoverProvider::GetReplyData(QNetworkReply *reply) {

  QByteArray data;

  if (reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
    data = reply->readAll();
  }
  else {
    if (reply->error() != QNetworkReply::NoError && reply->error() < 200) {
      // This is a network error, there is nothing more to do.
      QString error = QString("%1 (%2)").arg(reply->errorString()).arg(reply->error());
      Error(error);
    }
    else {
      // See if there is Json data containing "error" object - then use that instead.
      data = reply->readAll();
      QJsonParseError json_error;
      QJsonDocument json_doc = QJsonDocument::fromJson(data, &json_error);
      QString error;
      if (json_error.error == QJsonParseError::NoError && !json_doc.isEmpty() && json_doc.isObject()) {
        QJsonObject json_obj = json_doc.object();
        if (json_obj.contains("error")) {
          QJsonValue value_error = json_obj["error"];
          if (value_error.isObject()) {
            QJsonObject obj_error = value_error.toObject();
            int code = obj_error["code"].toInt();
            QString message = obj_error["message"].toString();
            QString type = obj_error["type"].toString();
            error = QString("%1 (%2)").arg(message).arg(code);
          }
        }
      }
      if (error.isEmpty()) {
        if (reply->error() != QNetworkReply::NoError) {
          error = QString("%1 (%2)").arg(reply->errorString()).arg(reply->error());
        }
        else {
          error = QString("Received HTTP code %1").arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
        }
      }
      Error(error);
    }
    return QByteArray();
  }

  return data;
  
}

QJsonValue DeezerCoverProvider::ExtractData(const QByteArray &data) {

  QJsonObject json_obj = ExtractJsonObj(data);
  if (json_obj.isEmpty()) return QJsonObject();

  if (json_obj.contains("error")) {
    QJsonValue value_error = json_obj["error"];
    if (!value_error.isObject()) {
      Error("Error missing object", json_obj);
      return QJsonValue();
    }
    QJsonObject obj_error = value_error.toObject();
    const int code = obj_error["code"].toInt();
    QString message = obj_error["message"].toString();
    QString type = obj_error["type"].toString();
    Error(QString("%1 (%2)").arg(message).arg(code));
    return QJsonValue();
  }

  if (!json_obj.contains("data") && !json_obj.contains("DATA")) {
    Error("Json reply object is missing data.", json_obj);
    return QJsonValue();
  }

  QJsonValue value_data;
  if (json_obj.contains("data")) value_data = json_obj["data"];
  else value_data = json_obj["DATA"];

  return value_data;

}

void DeezerCoverProvider::HandleSearchReply(QNetworkReply *reply, const int id) {

  if (!replies_.contains(reply)) return;
  replies_.removeAll(reply);
  disconnect(reply, nullptr, this, nullptr);
  reply->deleteLater();

  QByteArray data = GetReplyData(reply);
  if (data.isEmpty()) {
    emit SearchFinished(id, CoverSearchResults());
    return;
  }

  QJsonValue value_data = ExtractData(data);
  if (!value_data.isArray()) {
    emit SearchFinished(id, CoverSearchResults());
    return;
  }

  QJsonArray array_data = value_data.toArray();
  if (array_data.isEmpty()) {
    emit SearchFinished(id, CoverSearchResults());
    return;
  }

  QMap<QUrl, CoverSearchResult> results;
  int i = 0;
  for (const QJsonValue &json_value : array_data) {

    if (!json_value.isObject()) {
      Error("Invalid Json reply, data array value is not a object.", json_value);
      continue;
    }
    QJsonObject json_obj = json_value.toObject();
    QJsonObject obj_album;
    if (json_obj.contains("album") && json_obj["album"].isObject()) { // Song search, so extract the album.
      obj_album = json_obj["album"].toObject();
    }
    else {
      obj_album = json_obj;
    }

    if (!json_obj.contains("id") || !obj_album.contains("id")) {
      Error("Invalid Json reply, data array value object is missing ID.", json_obj);
      continue;
    }

    if (!obj_album.contains("type")) {
      Error("Invalid Json reply, data array value album object is missing type.", obj_album);
      continue;
    }
    QString type = obj_album["type"].toString();
    if (type != "album") {
      Error("Invalid Json reply, data array value album object has incorrect type returned", obj_album);
      continue;
    }

    if (!json_obj.contains("artist")) {
      Error("Invalid Json reply, data array value object is missing artist.", json_obj);
      continue;
    }
    QJsonValue value_artist = json_obj["artist"];
    if (!value_artist.isObject()) {
      Error("Invalid Json reply, data array value artist is not a object.", value_artist);
      continue;
    }
    QJsonObject obj_artist = value_artist.toObject();

    if (!obj_artist.contains("name")) {
      Error("Invalid Json reply, data array value artist object is missing name.", obj_artist);
      continue;
    }
    QString artist = obj_artist["name"].toString();

    if (!obj_album.contains("title")) {
      Error("Invalid Json reply, data array value album object is missing title.", obj_album);
      continue;
    }
    QString album = obj_album["title"].toString();

    album = album.remove(Song::kAlbumRemoveDisc);
    album = album.remove(Song::kAlbumRemoveMisc);

    CoverSearchResult cover_result;
    cover_result.artist = artist;
    cover_result.album = album;

    bool have_cover = false;
    QList<QPair<QString, QSize>> cover_sizes = QList<QPair<QString, QSize>>() << qMakePair(QString("cover_xl"), QSize(1000, 1000))
                                                                              << qMakePair(QString("cover_big"), QSize(500, 500));
    for (const QPair<QString, QSize> &cover_size : cover_sizes) {
      if (!obj_album.contains(cover_size.first)) continue;
      QString cover = obj_album[cover_size.first].toString();
      if (!have_cover) {
        have_cover = true;
        ++i;
      }
      QUrl url(cover);
      if (!results.contains(url)) {
        cover_result.image_url = url;
        cover_result.image_size = cover_size.second;
        cover_result.number = i;
        results.insert(url, cover_result);
      }
    }

    if (!have_cover) {
      Error("Invalid Json reply, data array value album object is missing cover.", obj_album);
    }

  }

  if (results.isEmpty()) {
    emit SearchFinished(id, CoverSearchResults());
  }
  else {
    CoverSearchResults cover_results = results.values();
    std::stable_sort(cover_results.begin(), cover_results.end(), AlbumCoverFetcherSearch::CoverSearchResultCompareNumber);
    emit SearchFinished(id, cover_results);
  }

}

void DeezerCoverProvider::Error(const QString &error, const QVariant &debug) {
  qLog(Error) << "Deezer:" << error;
  if (debug.isValid()) qLog(Debug) << debug;
}

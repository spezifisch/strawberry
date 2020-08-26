/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
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
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <QtAlgorithms>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonParseError>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "acoustidclient.h"
#include "core/network.h"
#include "core/networktimeouts.h"
#include "core/timeconstants.h"
#include "core/logging.h"

const char *AcoustidClient::kClientId = "0qjUoxbowg";
const char *AcoustidClient::kUrl = "https://api.acoustid.org/v2/lookup";
const int AcoustidClient::kDefaultTimeout = 5000;  // msec

AcoustidClient::AcoustidClient(QObject *parent)
    : QObject(parent),
      network_(new NetworkAccessManager(this)),
      timeouts_(new NetworkTimeouts(kDefaultTimeout, this)) {}

AcoustidClient::~AcoustidClient() {

  CancelAll();

}

void AcoustidClient::SetTimeout(const int msec) { timeouts_->SetTimeout(msec); }

void AcoustidClient::Start(const int id, const QString &fingerprint, int duration_msec) {

  typedef QPair<QString, QString> Param;
  typedef QList<Param> ParamList;

  const ParamList params = ParamList () << Param("format", "json")
                                        << Param("client", kClientId)
                                        << Param("duration", QString::number(duration_msec / kMsecPerSec))
                                        << Param("meta", "recordingids+sources")
                                        << Param("fingerprint", fingerprint);

  QUrlQuery url_query;
  url_query.setQueryItems(params);
  QUrl url(kUrl);
  url.setQuery(url_query);

  QNetworkRequest req(url);
#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#else
  req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
#endif
  QNetworkReply *reply = network_->get(req);
  connect(reply, &QNetworkReply::finished, [=] { RequestFinished(reply, id); });
  requests_[id] = reply;

  timeouts_->AddReply(reply);

}

void AcoustidClient::Cancel(const int id) {

  if (requests_.contains(id)) delete requests_.take(id);

}

void AcoustidClient::CancelAll() {

  qDeleteAll(requests_.values());
  requests_.clear();

}

namespace {
// Struct used when extracting results in RequestFinished
struct IdSource {
  IdSource(const QString& id, int source)
    : id_(id), nb_sources_(source) {}

  bool operator<(const IdSource& other) const {
    // We want the items with more sources to be at the beginning of the list
    return nb_sources_ > other.nb_sources_;
  }

  QString id_;
  int nb_sources_;
};
}

void AcoustidClient::RequestFinished(QNetworkReply *reply, const int request_id) {

  disconnect(reply, nullptr, this, nullptr);
  reply->deleteLater();
  requests_.remove(request_id);

  if (reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
    if (reply->error() != QNetworkReply::NoError) {
      qLog(Error) << QString("Acoustid: %1 (%2)").arg(reply->errorString()).arg(reply->error());
    }
    else {
      qLog(Error) << QString("Acoustid: Received HTTP code %1").arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
    }
    emit Finished(request_id, QStringList());
    return;
  }

  QJsonParseError error;
  QJsonDocument json_document = QJsonDocument::fromJson(reply->readAll(), &error);

  if (error.error != QJsonParseError::NoError) {
    emit Finished(request_id, QStringList());
    return;
  }

  QJsonObject json_object = json_document.object();

  QString status = json_object["status"].toString();
  if (status != "ok") {
    emit Finished(request_id, QStringList(), status);
    return;
  }

  // Get the results:
  // -in a first step, gather ids and their corresponding number of sources
  // -then sort results by number of sources (the results are originally
  //  unsorted but results with more sources are likely to be more accurate)
  // -keep only the ids, as sources where useful only to sort the results
  QJsonArray json_results = json_object["results"].toArray();

  // List of <id, nb of sources> pairs
  QList<IdSource> id_source_list;

  for (const QJsonValue& v : json_results) {
    QJsonObject r = v.toObject();
    if (!r["recordings"].isUndefined()) {
      QJsonArray json_recordings = r["recordings"].toArray();
      for (const QJsonValue& recording : json_recordings) {
        QJsonObject o = recording.toObject();
        if (!o["id"].isUndefined()) {
          id_source_list << IdSource(o["id"].toString(), o["sources"].toInt());
        }
      }
    }
  }

  std::stable_sort(id_source_list.begin(), id_source_list.end());

  QList<QString> id_list;
  for (const IdSource& is : id_source_list) {
    id_list << is.id_;
  }

  emit Finished(request_id, id_list);

}


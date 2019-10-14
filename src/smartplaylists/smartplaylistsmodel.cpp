/*
 * Strawberry Music Player
 * This code was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2019, Jonas Kvinge <jonas@jkvinge.net>
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

#include <QAbstractListModel>
#include <QVariant>
#include <QStringList>
#include <QUrl>
#include <QMimeData>
#include <QSettings>

#include "core/application.h"
#include "core/database.h"
#include "core/logging.h"
#include "core/iconloader.h"
#include "core/simpletreemodel.h"
#include "collection/collectionbackend.h"
#include "playlist/songmimedata.h"

#include "smartplaylistsitem.h"
#include "smartplaylistsmodel.h"
#include "smartplaylistsview.h"
#include "smartplaylistsearch.h"
#include "playlistgenerator.h"
#include "playlistgeneratormimedata.h"
#include "playlistquerygenerator.h"

const char *SmartPlaylistsModel::kSmartPlaylistsMimeType = "application/x-strawberry-smart-playlist-generator";
const char *SmartPlaylistsModel::kSmartPlaylistsSettingsGroup = "SerialisedSmartPlaylists";
const int SmartPlaylistsModel::kSmartPlaylistsVersion = 1;

SmartPlaylistsModel::SmartPlaylistsModel(CollectionBackend *backend, Application *app, QObject *parent)
    : SimpleTreeModel<SmartPlaylistsItem>(new SmartPlaylistsItem(this), parent),
      backend_(backend),
      app_(app),
      icon_(IconLoader::Load("view-media-playlist")) {

  root_->lazy_loaded = true;

}

SmartPlaylistsModel::~SmartPlaylistsModel() {
  delete root_;
}

void SmartPlaylistsModel::Init() {

  QSettings s;
  s.beginGroup(kSmartPlaylistsSettingsGroup);
  int version = s.value(backend_->songs_table() + "_version", 0).toInt();

  // How many defaults do we have to write?
  int unwritten_defaults = 0;
  for (int i = version; i < default_smart_playlists_.count(); ++i) {
    unwritten_defaults += default_smart_playlists_[i].count();
  }

  // Save the defaults if there are any unwritten ones
  if (unwritten_defaults) {
    // How many items are stored already?
    int playlist_index = s.beginReadArray(backend_->songs_table());
    s.endArray();

    // Append the new ones
    s.beginWriteArray(backend_->songs_table(), playlist_index + unwritten_defaults);
    for (; version < default_smart_playlists_.count(); ++version) {
      for (PlaylistGeneratorPtr gen : default_smart_playlists_[version]) {
        SaveGenerator(&s, playlist_index++, gen);
      }
    }
    s.endArray();
  }

  s.setValue(backend_->songs_table() + "_version", version);

  const int count = s.beginReadArray(backend_->songs_table());
  for (int i = 0; i < count; ++i) {
    s.setArrayIndex(i);
    ItemFromSmartPlaylist(s, false);
  }

}

void SmartPlaylistsModel::ItemFromSmartPlaylist(const QSettings &s, bool notify) {

  SmartPlaylistsItem *item = new SmartPlaylistsItem(SmartPlaylistsItem::Type_SmartPlaylist, notify ? nullptr : root_);
  item->display_text = tr(qPrintable(s.value("name").toString()));
  item->sort_text = item->display_text;
  item->key = s.value("type").toString();
  item->smart_playlist_data = s.value("data").toByteArray();
  item->lazy_loaded = true;

  if (notify) item->InsertNotify(root_);

}

void SmartPlaylistsModel::AddGenerator(PlaylistGeneratorPtr gen) {

  QSettings s;
  s.beginGroup(kSmartPlaylistsSettingsGroup);

  // Count the existing items
  const int count = s.beginReadArray(backend_->songs_table());
  s.endArray();

  // Add this one to the end
  s.beginWriteArray(backend_->songs_table(), count + 1);
  SaveGenerator(&s, count, gen);

  // Add it to the model
  ItemFromSmartPlaylist(s, true);

  s.endArray();

}

void SmartPlaylistsModel::UpdateGenerator(const QModelIndex &idx, PlaylistGeneratorPtr gen) {

  if (idx.parent() != ItemToIndex(root_)) return;
  SmartPlaylistsItem *item = IndexToItem(idx);
  if (!item) return;

  // Update the config
  QSettings s;
  s.beginGroup(kSmartPlaylistsSettingsGroup);

  // Count the existing items
  const int count = s.beginReadArray(backend_->songs_table());
  s.endArray();

  s.beginWriteArray(backend_->songs_table(), count);
  SaveGenerator(&s, idx.row(), gen);

  // Update the text of the item
  item->display_text = gen->name();
  item->sort_text = item->display_text;
  item->key = gen->type();
  item->smart_playlist_data = gen->Save();
  item->ChangedNotify();

}

void SmartPlaylistsModel::DeleteGenerator(const QModelIndex &idx) {

  if (idx.parent() != ItemToIndex(root_)) return;

  // Remove the item from the tree
  root_->DeleteNotify(idx.row());

  QSettings s;
  s.beginGroup(kSmartPlaylistsSettingsGroup);

  // Rewrite all the items to the settings
  s.beginWriteArray(backend_->songs_table(), root_->children.count());
  int i = 0;
  for (SmartPlaylistsItem *item : root_->children) {
    s.setArrayIndex(i++);
    s.setValue("name", item->display_text);
    s.setValue("type", item->key);
    s.setValue("data", item->smart_playlist_data);
  }
  s.endArray();

}

void SmartPlaylistsModel::SaveGenerator(QSettings *s, int i, PlaylistGeneratorPtr generator) const {

  s->setArrayIndex(i);
  s->setValue("name", generator->name());
  s->setValue("type", generator->type());
  s->setValue("data", generator->Save());

}

PlaylistGeneratorPtr SmartPlaylistsModel::CreateGenerator(const QModelIndex &idx) const {

  PlaylistGeneratorPtr ret;

  const SmartPlaylistsItem *item = IndexToItem(idx);
  if (!item || item->type != SmartPlaylistsItem::Type_SmartPlaylist) return ret;

  ret = PlaylistGenerator::Create(item->key);
  if (!ret) return ret;

  ret->set_name(item->display_text);
  ret->set_collection(backend_);
  ret->Load(item->smart_playlist_data);

  return ret;

}

QVariant SmartPlaylistsModel::data(const QModelIndex &idx, int role) const {

  if (!idx.isValid()) return QVariant();
  const SmartPlaylistsItem *item = IndexToItem(idx);
  if (!item) return QVariant();

  switch (role) {
    case Qt::DecorationRole:
      return icon_;
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
      return item->DisplayText();
  }

  return QVariant();

}

void SmartPlaylistsModel::LazyPopulate(SmartPlaylistsItem *parent, bool signal) {}

QStringList SmartPlaylistsModel::mimeTypes() const {
  return QStringList() << "text/uri-list";
}

QMimeData *SmartPlaylistsModel::mimeData(const QModelIndexList &indexes) const {

  if (indexes.isEmpty()) return nullptr;

  PlaylistGeneratorPtr generator = CreateGenerator(indexes.first());
  if (!generator) return nullptr;

  PlaylistGeneratorMimeData *data = new PlaylistGeneratorMimeData(generator);
  data->setData(kSmartPlaylistsMimeType, QByteArray());
  data->name_for_new_playlist_ = this->data(indexes.first()).toString();
  return data;

}

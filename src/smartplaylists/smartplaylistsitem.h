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

#ifndef SMARTPLAYLISTSITEM_H
#define SMARTPLAYLISTSITEM_H

#include "config.h"

#include <QByteArray>

#include "core/simpletreeitem.h"

class SmartPlaylistsItem : public SimpleTreeItem<SmartPlaylistsItem> {
 public:
  enum Type {
    Type_Root,
    Type_SmartPlaylist,
  };
  SmartPlaylistsItem(SimpleTreeModel<SmartPlaylistsItem> *model) : SimpleTreeItem<SmartPlaylistsItem>(Type_Root, model) {}
  SmartPlaylistsItem(Type type, SmartPlaylistsItem *parent = nullptr) : SimpleTreeItem<SmartPlaylistsItem>(type, parent) {}
  QByteArray smart_playlist_data;
};

#endif  // SMARTPLAYLISTSITEM_H

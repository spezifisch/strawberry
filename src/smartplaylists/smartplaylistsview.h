/*
 * Strawberry Music Player
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

#ifndef SMARTPLAYLISTSVIEW_H
#define SMARTPLAYLISTSVIEW_H

#include "config.h"

#include <QListView>

class Application;
class SmartPlaylistsModel;

class QWidget;
class QMimeData;
class QMenu;
class QAction;
class QModelIndex;
class QContextMenuEvent;

class SmartPlaylistsView : public QListView {
  Q_OBJECT

 public:
  SmartPlaylistsView(QWidget* parent = nullptr);
  ~SmartPlaylistsView();

  void SetApplication(Application* app);
  void SetModel(SmartPlaylistsModel *model);

 protected:
  void contextMenuEvent(QContextMenuEvent* e);

 private slots:
  void ItemDoubleClicked(const QModelIndex &index);
  void Load();
  void AddToPlaylist();
  void AddToPlaylistEnqueue();
  void AddToPlaylistEnqueueNext();
  void OpenInNewPlaylist();

  void NewSmartPlaylist();
  void EditSmartPlaylist();
  void DeleteSmartPlaylist();

  void NewSmartPlaylistFinished();
  void EditSmartPlaylistFinished();

 signals:
  void AddToPlaylistSignal(QMimeData *data);

 private:
  Application* app_;
  SmartPlaylistsModel *model_;

  QMenu *context_menu_;
  QMenu *context_menu_selected_;
  QModelIndex context_menu_index_;
  QAction *load_;
  QAction *add_to_playlist_;
  QAction *add_to_playlist_enqueue_;
  QAction *add_to_playlist_enqueue_next_;
  QAction *open_in_new_playlist_;
  QAction *new_smart_playlist_;
  QAction *edit_smart_playlist_;
  QAction *delete_smart_playlist_;

};

#endif  // SMARTPLAYLISTSVIEW_H

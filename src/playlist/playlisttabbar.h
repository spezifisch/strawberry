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

#ifndef PLAYLISTTABBAR_H
#define PLAYLISTTABBAR_H

#include "config.h"

#include <QObject>
#include <QBasicTimer>
#include <QList>
#include <QString>
#include <QIcon>
#include <QTabBar>

class QWidget;
class QMenu;
class QAction;
class QEvent;
class QContextMenuEvent;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QMouseEvent;
class QTimerEvent;

class PlaylistManager;
class RenameTabLineEdit;

class PlaylistTabBar : public QTabBar {
  Q_OBJECT

 public:
  explicit PlaylistTabBar(QWidget *parent = nullptr);

  static const int kDragHoverTimeout = 500;
  static const char *kSettingsGroup;

  void SetActions(QAction *new_playlist, QAction *load_playlist);
  void SetManager(PlaylistManager *manager);

  // We use IDs to refer to tabs so the tabs can be moved around (and their indexes change).
  int index_of(int id) const;
  int current_id() const;
  int id_of(int index) const;

  // Utility functions that use IDs rather than indexes
  void set_current_id(int id);
  void set_icon_by_id(int id, const QIcon &icon);
  void set_text_by_id(int id, const QString &text);

  void RemoveTab(int id);
  void InsertTab(int id, int index, const QString &text, bool favorite);

 signals:
  void CurrentIdChanged(int id);
  void Rename(int id, const QString &name);
  void Close(int id);
  void Save(int id);
  void PlaylistOrderChanged(const QList<int> &ids);
  void PlaylistFavorited(int id, bool favorite);

 protected:
  void contextMenuEvent(QContextMenuEvent *e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;
  void mouseDoubleClickEvent(QMouseEvent *e) override;
  void dragEnterEvent(QDragEnterEvent *e) override;
  void dragMoveEvent(QDragMoveEvent *e) override;
  void dragLeaveEvent(QDragLeaveEvent *e) override;
  void dropEvent(QDropEvent *e) override;
  void timerEvent(QTimerEvent *e) override;
  bool event(QEvent *e) override;

 private slots:
  void CurrentIndexChanged(int index);
  void Rename();
  void RenameInline();
  void HideEditor();
  void Close();
  void CloseFromTabIndex(int index);
  // Used when playlist's favorite flag isn't changed from the favorite widget (e.g. from the playlistlistcontainer): will update the favorite widget
  void PlaylistFavoritedSlot(int id, bool favorite);
  // Used to signal that the playlist manager is done starting up
  void PlaylistManagerInitialized();
  void TabMoved();
  void Save();

 private:
  PlaylistManager *manager_;

  QMenu *menu_;
  int menu_index_;
  QAction *new_;
  QAction *rename_;
  QAction *close_;
  QAction *save_;

  QBasicTimer drag_hover_timer_;
  int drag_hover_tab_;

  bool suppress_current_changed_;
  bool initialized_;

  // Editor for inline renaming
  RenameTabLineEdit *rename_editor_;
};

#endif  // PLAYLISTTABBAR_H

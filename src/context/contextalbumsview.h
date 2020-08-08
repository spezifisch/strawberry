/*
 * Strawberry Music Player
 * This code was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2013, Jonas Kvinge <jonas@strawbs.net>
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

#ifndef CONTEXTALBUMSVIEW_H
#define CONTEXTALBUMSVIEW_H

#include "config.h"

#include <memory>

#include <QObject>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QStyledItemDelegate>
#include <QStyleOption>
#include <QSet>
#include <QString>

#include "core/song.h"
#include "widgets/autoexpandingtreeview.h"

class QWidget;
class QMenu;
class QAction;
class QContextMenuEvent;
class QHelpEvent;
class QMouseEvent;
class QPaintEvent;

class Application;
class ContextAlbumsModel;
class EditTagDialog;
class OrganizeDialog;

class ContextItemDelegate : public QStyledItemDelegate {
  Q_OBJECT

 public:
  explicit ContextItemDelegate(QObject *parent);

 public slots:
  bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option, const QModelIndex &index) override;
};

class ContextAlbumsView : public AutoExpandingTreeView {
  Q_OBJECT

 public:
  explicit ContextAlbumsView(QWidget *parent = nullptr);
  ~ContextAlbumsView() override;

  // Returns Songs currently selected in the collection view.
  // Please note that the selection is recursive meaning that if for example an album is selected this will return all of it's songs.
  SongList GetSelectedSongs() const;

  void Init(Application *app);

  // QTreeView
  void scrollTo(const QModelIndex &index, ScrollHint hint = EnsureVisible) override;

  ContextAlbumsModel *albums_model() { return model_; }

 public slots:
  void SaveFocus();
  void RestoreFocus();

 protected:
  // QWidget
  void paintEvent(QPaintEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *e) override;
  void contextMenuEvent(QContextMenuEvent *e) override;

 private slots:
  void Load();
  void AddToPlaylist();
  void AddToPlaylistEnqueue();
  void OpenInNewPlaylist();
  void Organize();
  void CopyToDevice();
  void EditTracks();
  void ShowInBrowser();

 private:
  void RecheckIsEmpty();
  bool RestoreLevelFocus(const QModelIndex &parent = QModelIndex());
  void SaveContainerPath(const QModelIndex &child);

 private:
  Application *app_;

  QMenu *context_menu_;
  QModelIndex context_menu_index_;
  QAction *load_;
  QAction *add_to_playlist_;
  QAction *add_to_playlist_enqueue_;
  QAction *open_in_new_playlist_;
  QAction *organize_;
#ifndef Q_OS_WIN
  QAction *copy_to_device_;
#endif
  QAction *edit_track_;
  QAction *edit_tracks_;
  QAction *show_in_browser_;

  std::unique_ptr<OrganizeDialog> organize_dialog_;
  std::unique_ptr<EditTagDialog> edit_tag_dialog_;

  bool is_in_keyboard_search_;

  // Save focus
  Song last_selected_song_;
  QString last_selected_container_;
  QSet<QString> last_selected_path_;

  ContextAlbumsModel *model_;

};

#endif  // CONTEXTALBUMSVIEW_H


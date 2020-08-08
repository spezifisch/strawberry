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

#include <QWidget>
#include <QAbstractItemModel>
#include <QItemSelectionModel>
#include <QTreeView>
#include <QList>
#include <QTimer>
#include <QSettings>
#include <QKeySequence>
#include <QLabel>
#include <QToolButton>

#include "core/iconloader.h"
#include "playlist/playlist.h"
#include "playlist/playlistdelegates.h"
#include "playlist/playlistmanager.h"
#include "queue.h"
#include "queueview.h"
#include "ui_queueview.h"
#include "settings/appearancesettingspage.h"

QueueView::QueueView(QWidget *parent)
    : QWidget(parent),
      ui_(new Ui_QueueView),
      playlists_(nullptr),
      current_playlist_(nullptr) {

  ui_->setupUi(this);
  ui_->list->setItemDelegate(new QueuedItemDelegate(this, 0));

  // Set icons on buttons
  ui_->move_down->setIcon(IconLoader::Load("go-down"));
  ui_->move_up->setIcon(IconLoader::Load("go-up"));
  ui_->remove->setIcon(IconLoader::Load("edit-delete"));
  ui_->clear->setIcon(IconLoader::Load("edit-clear-list"));

  // Set a standard shortcut
  ui_->remove->setShortcut(QKeySequence::Delete);

  // Button connections
  connect(ui_->move_down, SIGNAL(clicked()), SLOT(MoveDown()));
  connect(ui_->move_up, SIGNAL(clicked()), SLOT(MoveUp()));
  connect(ui_->remove, SIGNAL(clicked()), SLOT(Remove()));
  connect(ui_->clear, SIGNAL(clicked()), SLOT(Clear()));

  ReloadSettings();

}

QueueView::~QueueView() {
  delete ui_;
}

void QueueView::SetPlaylistManager(PlaylistManager *manager) {

  playlists_ = manager;

  connect(playlists_, SIGNAL(CurrentChanged(Playlist*)), SLOT(CurrentPlaylistChanged(Playlist*)));
  CurrentPlaylistChanged(playlists_->current());

}

void QueueView::ReloadSettings() {

  QSettings s;
  s.beginGroup(AppearanceSettingsPage::kSettingsGroup);
  int iconsize = s.value(AppearanceSettingsPage::kIconSizeLeftPanelButtons, 22).toInt();
  s.endGroup();

  ui_->move_down->setIconSize(QSize(iconsize, iconsize));
  ui_->move_up->setIconSize(QSize(iconsize, iconsize));
  ui_->remove->setIconSize(QSize(iconsize, iconsize));
  ui_->clear->setIconSize(QSize(iconsize, iconsize));

}

void QueueView::CurrentPlaylistChanged(Playlist *playlist) {

  if (current_playlist_) {
    disconnect(current_playlist_->queue(), SIGNAL(rowsInserted(QModelIndex, int, int)), this, SLOT(UpdateButtonState()));
    disconnect(current_playlist_->queue(), SIGNAL(rowsRemoved(QModelIndex, int, int)), this, SLOT(UpdateButtonState()));
    disconnect(current_playlist_->queue(), SIGNAL(layoutChanged()), this, SLOT(UpdateButtonState()));
    disconnect(current_playlist_->queue(), SIGNAL(SummaryTextChanged(QString)), ui_->summary, SLOT(setText(QString)));
    disconnect(current_playlist_, SIGNAL(destroyed()), this, SLOT(PlaylistDestroyed()));
  }

  current_playlist_ = playlist;

  connect(current_playlist_->queue(), SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(UpdateButtonState()));
  connect(current_playlist_->queue(), SIGNAL(rowsRemoved(QModelIndex,int,int)), this, SLOT(UpdateButtonState()));
  connect(current_playlist_->queue(), SIGNAL(layoutChanged()), this, SLOT(UpdateButtonState()));
  connect(current_playlist_->queue(), SIGNAL(SummaryTextChanged(QString)), ui_->summary, SLOT(setText(QString)));
  connect(current_playlist_, SIGNAL(destroyed()), this, SLOT(PlaylistDestroyed()));

  ui_->list->setModel(current_playlist_->queue());

  connect(ui_->list->selectionModel(), SIGNAL(currentChanged(QModelIndex,QModelIndex)), SLOT(UpdateButtonState()));
  connect(ui_->list->selectionModel(), SIGNAL(selectionChanged(QItemSelection, QItemSelection)), SLOT(UpdateButtonState()));

  QTimer::singleShot(0, current_playlist_->queue(), SLOT(UpdateSummaryText()));

}

void QueueView::MoveUp() {

  QModelIndexList indexes = ui_->list->selectionModel()->selectedRows();
  std::stable_sort(indexes.begin(), indexes.end());

  if (indexes.isEmpty() || indexes.first().row() == 0) return;

  for (const QModelIndex &index : indexes) {
    current_playlist_->queue()->MoveUp(index.row());
  }

}

void QueueView::MoveDown() {

  QModelIndexList indexes = ui_->list->selectionModel()->selectedRows();
  std::stable_sort(indexes.begin(), indexes.end());

  if (indexes.isEmpty() || indexes.last().row() == current_playlist_->queue()->rowCount()-1)
    return;

  for (int i = indexes.count() - 1; i >= 0; --i) {
    current_playlist_->queue()->MoveDown(indexes[i].row());
  }

}

void QueueView::Clear() {
  current_playlist_->queue()->Clear();
}

void QueueView::Remove() {

  // collect the rows to be removed
  QList<int> row_list;
  for (const QModelIndex &index : ui_->list->selectionModel()->selectedRows()) {
    if (index.isValid()) row_list << index.row();
  }

  current_playlist_->queue()->Remove(row_list);

}

void QueueView::UpdateButtonState() {

  if (ui_->list->selectionModel()->selectedRows().count() > 0) {
    ui_->remove->setEnabled(true);
    QModelIndex index_top = ui_->list->model()->index(0, 0);
    QModelIndex index_bottom = ui_->list->model()->index(ui_->list->model()->rowCount() - 1, 0);
    const QModelIndexList selected = ui_->list->selectionModel()->selectedIndexes();
    bool all_selected = ui_->list->selectionModel()->selectedRows().count() == ui_->list->model()->rowCount();
    ui_->move_up->setEnabled(!all_selected && !selected.contains(index_top));
    ui_->move_down->setEnabled(!all_selected && !selected.contains(index_bottom));
  }
  else {
    ui_->move_up->setEnabled(false);
    ui_->move_down->setEnabled(false);
    ui_->remove->setEnabled(false);
  }

  ui_->clear->setEnabled(!current_playlist_->queue()->is_empty());

}

void QueueView::PlaylistDestroyed() {
  current_playlist_ = nullptr;
  // We'll get another CurrentPlaylistChanged() soon
}


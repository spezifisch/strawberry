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

#include "config.h"

#include <QListView>
#include <QMenu>
#include <QContextMenuEvent>

#include "core/application.h"
#include "core/logging.h"
#include "core/mimedata.h"
#include "core/iconloader.h"
#include "smartplaylistsmodel.h"
#include "smartplaylistsview.h"
#include "smartplaylistwizard.h"

SmartPlaylistsView::SmartPlaylistsView(QWidget *parent)
      : QListView(parent),
      app_(nullptr),
      context_menu_(nullptr),
      context_menu_selected_(nullptr) {

  setAttribute(Qt::WA_MacShowFocusRect, false);
  setDragEnabled(true);
  setDragDropMode(QAbstractItemView::DragOnly);
  setSelectionMode(QAbstractItemView::ExtendedSelection);

  connect(this, SIGNAL(doubleClicked(QModelIndex)), SLOT(ItemDoubleClicked(QModelIndex)));

}

SmartPlaylistsView::~SmartPlaylistsView() {}

void SmartPlaylistsView::SetModel(SmartPlaylistsModel *model) {

  model_ = model;
  setModel(model);

}

void SmartPlaylistsView::SetApplication(Application *app) {
  app_ = app;
}

void SmartPlaylistsView::contextMenuEvent(QContextMenuEvent *e) {

  if (!context_menu_) {
    context_menu_ = new QMenu(this);
    new_smart_playlist_ = context_menu_->addAction(IconLoader::Load("document-new"), tr("New smart playlist..."), this, SLOT(NewSmartPlaylist()));
    new_smart_playlist_->setVisible(true);
  }

  if (!context_menu_selected_) {
    context_menu_selected_ = new QMenu(this);
    add_to_playlist_ = context_menu_selected_->addAction(IconLoader::Load("media-play"), tr("Append to current playlist"), this, SLOT(AddToPlaylist()));
    load_ = context_menu_selected_->addAction(IconLoader::Load("media-play"), tr("Replace current playlist"), this, SLOT(Load()));
    open_in_new_playlist_ = context_menu_selected_->addAction(IconLoader::Load("document-new"), tr("Open in new playlist"), this, SLOT(OpenInNewPlaylist()));

    context_menu_selected_->addSeparator();
    add_to_playlist_enqueue_ = context_menu_selected_->addAction(IconLoader::Load("go-next"), tr("Queue track"), this, SLOT(AddToPlaylistEnqueue()));
    add_to_playlist_enqueue_next_ = context_menu_selected_->addAction(IconLoader::Load("go-next"), tr("Play next"), this, SLOT(AddToPlaylistEnqueueNext()));
    context_menu_selected_->addSeparator();

    context_menu_selected_->addSeparator();
    edit_smart_playlist_ = context_menu_selected_->addAction(IconLoader::Load("edit-rename"), tr("Edit smart playlist..."), this, SLOT(EditSmartPlaylist()));
    delete_smart_playlist_ = context_menu_selected_->addAction(IconLoader::Load("edit-delete"), tr("Delete smart playlist"), this, SLOT(DeleteSmartPlaylist()));

    context_menu_selected_->addSeparator();

  }

  context_menu_index_ = indexAt(e->pos());
  if (!context_menu_index_.isValid()) {
    context_menu_->popup(e->globalPos());
    return;
  }

  context_menu_selected_->popup(e->globalPos());

}

void SmartPlaylistsView::Load() {

  QMimeData *data = model()->mimeData(selectedIndexes());
  if (MimeData *mime_data = qobject_cast<MimeData*>(data)) {
    mime_data->clear_first_ = true;
  }
  emit AddToPlaylistSignal(data);

}

void SmartPlaylistsView::AddToPlaylist() {

  emit AddToPlaylistSignal(model()->mimeData(selectedIndexes()));

}

void SmartPlaylistsView::AddToPlaylistEnqueue() {

  QMimeData *data = model()->mimeData(selectedIndexes());
  if (MimeData *mime_data = qobject_cast<MimeData*>(data)) {
    mime_data->enqueue_now_ = true;
  }
  emit AddToPlaylistSignal(data);

}

void SmartPlaylistsView::AddToPlaylistEnqueueNext() {

  QMimeData *data = model()->mimeData(selectedIndexes());
  if (MimeData *mime_data = qobject_cast<MimeData*>(data)) {
    mime_data->enqueue_next_now_ = true;
  }
  emit AddToPlaylistSignal(data);

}

void SmartPlaylistsView::OpenInNewPlaylist() {

  QMimeData *data = model()->mimeData(selectedIndexes());
  if (MimeData *mime_data = qobject_cast<MimeData*>(data)) {
    mime_data->open_in_new_playlist_ = true;
  }
  emit AddToPlaylistSignal(data);

}

void SmartPlaylistsView::NewSmartPlaylist() {

  SmartPlaylistWizard *wizard = new SmartPlaylistWizard(app_, app_->collection_backend(), this);
  wizard->setAttribute(Qt::WA_DeleteOnClose);
  connect(wizard, SIGNAL(accepted()), SLOT(NewSmartPlaylistFinished()));

  wizard->show();

}

void SmartPlaylistsView::EditSmartPlaylist() {

  if (!context_menu_index_.isValid()) return;

  SmartPlaylistWizard *wizard = new SmartPlaylistWizard(app_, app_->collection_backend(), this);
  wizard->setAttribute(Qt::WA_DeleteOnClose);
  connect(wizard, SIGNAL(accepted()), SLOT(EditSmartPlaylistFinished()));

  wizard->show();
  wizard->SetGenerator(model_->CreateGenerator(context_menu_index_));

}

void SmartPlaylistsView::DeleteSmartPlaylist() {

  if (!context_menu_index_.isValid()) return;
  model_->DeleteGenerator(context_menu_index_);

}

void SmartPlaylistsView::NewSmartPlaylistFinished() {

  SmartPlaylistWizard *wizard = qobject_cast<SmartPlaylistWizard*>(sender());
  if (!wizard) return;
  disconnect(wizard, SIGNAL(accepted()), this, SLOT(NewSmartPlaylistFinished()));
  model_->AddGenerator(wizard->CreateGenerator());

}

void SmartPlaylistsView::EditSmartPlaylistFinished() {

  const SmartPlaylistWizard *wizard = qobject_cast<SmartPlaylistWizard*>(sender());
  if (!wizard) return;

  disconnect(wizard, SIGNAL(accepted()), this, SLOT(EditSmartPlaylistFinished()));

  if (!context_menu_index_.isValid()) return;
  model_->UpdateGenerator(context_menu_index_, wizard->CreateGenerator());

}

void SmartPlaylistsView::ItemDoubleClicked(const QModelIndex &index) {

  QMimeData *data = model()->mimeData(QModelIndexList() << index);
  if (MimeData *mime_data = qobject_cast<MimeData*>(data)) {
    mime_data->from_doubleclick_ = true;
  }
  emit AddToPlaylistSignal(data);

}

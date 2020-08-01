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
#include <QFileInfo>
#include <QFileSystemModel>
#include <QDir>
#include <QMenu>
#include <QUrl>
#include <QtEvents>

#include "core/iconloader.h"
#include "core/mimedata.h"
#include "core/utilities.h"
#include "fileviewlist.h"

FileViewList::FileViewList(QWidget *parent)
    : QListView(parent),
      menu_(new QMenu(this))
{

  menu_->addAction(IconLoader::Load("media-playback-start"), tr("Append to current playlist"), this, SLOT(AddToPlaylistSlot()));
  menu_->addAction(IconLoader::Load("media-playback-start"), tr("Replace current playlist"), this, SLOT(LoadSlot()));
  menu_->addAction(IconLoader::Load("document-new"), tr("Open in new playlist"), this, SLOT(OpenInNewPlaylistSlot()));
  menu_->addSeparator();
  menu_->addAction(IconLoader::Load("edit-copy"), tr("Copy to collection..."), this, SLOT(CopyToCollectionSlot()));
  menu_->addAction(IconLoader::Load("go-jump"), tr("Move to collection..."), this, SLOT(MoveToCollectionSlot()));
  menu_->addAction(IconLoader::Load("device"), tr("Copy to device..."), this, SLOT(CopyToDeviceSlot()));
  menu_->addAction(IconLoader::Load("edit-delete"), tr("Delete from disk..."), this, SLOT(DeleteSlot()));

  menu_->addSeparator();
  menu_->addAction(IconLoader::Load("edit-rename"), tr("Edit track information..."), this, SLOT(EditTagsSlot()));
  menu_->addAction(IconLoader::Load("document-open-folder"), tr("Show in file browser..."), this, SLOT(ShowInBrowser()));

  setAttribute(Qt::WA_MacShowFocusRect, false);

}

void FileViewList::contextMenuEvent(QContextMenuEvent *e) {

  menu_selection_ = selectionModel()->selection();

  menu_->popup(e->globalPos());
  e->accept();

}

QList<QUrl> FileViewList::UrlListFromSelection() const {

  QList<QUrl> urls;
  for (const QModelIndex& index : menu_selection_.indexes()) {
    if (index.column() == 0)
      urls << QUrl::fromLocalFile(qobject_cast<QFileSystemModel*>(model())->fileInfo(index).canonicalFilePath());
  }
  std::sort(urls.begin(), urls.end());

  return urls;

}

MimeData *FileViewList::MimeDataFromSelection() const {

  MimeData *mimedata = new MimeData;
  mimedata->setUrls(UrlListFromSelection());

  QList<QString> filenames = FilenamesFromSelection();

  // if just one folder selected - use it's path as the new playlist's name
  if (filenames.size() == 1 && QFileInfo(filenames.first()).isDir()) {
    if (filenames.first().length() > 20) {
      mimedata->name_for_new_playlist_ = QDir(filenames.first()).dirName();
    }
    else {
      mimedata->name_for_new_playlist_ = filenames.first();
    }
  }
  // otherwise, use the current root path
  else {
    QString path = qobject_cast<QFileSystemModel*>(model())->rootPath();
    if (path.length() > 20) {
      QFileInfo info(path);
      if (info.isDir()) {
        mimedata->name_for_new_playlist_ = QDir(info.filePath()).dirName();
      }
      else {
        mimedata->name_for_new_playlist_ = info.baseName();
      }
    }
    else {
      mimedata->name_for_new_playlist_ = path;
    }
  }

  return mimedata;

}

QStringList FileViewList::FilenamesFromSelection() const {

  QStringList filenames;
  for (const QModelIndex& index : menu_selection_.indexes()) {
    if (index.column() == 0)
      filenames << qobject_cast<QFileSystemModel*>(model())->filePath(index);
  }
  return filenames;

}

void FileViewList::LoadSlot() {

  MimeData *mimedata = MimeDataFromSelection();
  mimedata->clear_first_ = true;
  emit AddToPlaylist(mimedata);

}

void FileViewList::AddToPlaylistSlot() {
  emit AddToPlaylist(MimeDataFromSelection());
}

void FileViewList::OpenInNewPlaylistSlot() {

  MimeData *mimedata = MimeDataFromSelection();
  mimedata->open_in_new_playlist_ = true;
  emit AddToPlaylist(mimedata);

}

void FileViewList::CopyToCollectionSlot() {
  emit CopyToCollection(UrlListFromSelection());
}

void FileViewList::MoveToCollectionSlot() {
  emit MoveToCollection(UrlListFromSelection());
}

void FileViewList::CopyToDeviceSlot() {
  emit CopyToDevice(UrlListFromSelection());
}

void FileViewList::DeleteSlot() {
  emit Delete(FilenamesFromSelection());
}

void FileViewList::EditTagsSlot() {
  emit EditTags(UrlListFromSelection());
}

void FileViewList::mousePressEvent(QMouseEvent *e) {

  switch (e->button()) {
    case Qt::XButton1:
      emit Back();
      break;
    case Qt::XButton2:
      emit Forward();
      break;
    // enqueue to playlist with middleClick
    case Qt::MidButton: {
      QListView::mousePressEvent(e);

      // we need to update the menu selection
      menu_selection_ = selectionModel()->selection();

      MimeData *mimedata = new MimeData;
      mimedata->setUrls(UrlListFromSelection());
      mimedata->enqueue_now_ = true;
      emit AddToPlaylist(mimedata);
      break;
    }
    default:
      QListView::mousePressEvent(e);
      break;
  }

}

void FileViewList::ShowInBrowser() {
  Utilities::OpenInFileBrowser(UrlListFromSelection());
}

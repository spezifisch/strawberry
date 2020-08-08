/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2018-2019, Jonas Kvinge <jonas@jkvinge.net>
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

#ifndef ORGANISEDIALOG_H
#define ORGANISEDIALOG_H

#include "config.h"

#include <memory>

#include <QtGlobal>
#include <QObject>
#include <QDialog>
#include <QFuture>
#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtEvents>

#include "core/song.h"
#include "organize.h"
#include "organizeformat.h"

class QAbstractItemModel;
class QWidget;
class QResizeEvent;
class QShowEvent;
class QCloseEvent;

class TaskManager;
class CollectionBackend;
class OrganizeErrorDialog;
class Ui_OrganizeDialog;

class OrganizeDialog : public QDialog {
  Q_OBJECT

 public:
  explicit OrganizeDialog(TaskManager *task_manager, CollectionBackend *backend = nullptr, QWidget *parentwindow = nullptr, QWidget *parent = nullptr);
  ~OrganizeDialog() override;

  static const char *kDefaultFormat;

  QSize sizeHint() const override;

  void SetDestinationModel(QAbstractItemModel *model, bool devices = false);

  // These functions return true if any songs were actually added to the dialog.
  // SetSongs returns immediately, SetUrls and SetFilenames load the songs in the background.
  bool SetSongs(const SongList &songs);
  bool SetUrls(const QList<QUrl> &urls);
  bool SetFilenames(const QStringList &filenames);

  void SetCopy(bool copy);

  static Organize::NewSongInfoList ComputeNewSongsFilenames(const SongList &songs, const OrganizeFormat &format);
  
  void SetPlaylist(const QString &playlist);

 protected:
  void showEvent(QShowEvent*) override;
  void closeEvent(QCloseEvent*) override;

 private:
  void LoadGeometry();
  void SaveGeometry();
  void LoadSettings();

  SongList LoadSongsBlocking(const QStringList &filenames);
  void SetLoadingSongs(bool loading);

 signals:
  void FileCopied(int);

 public slots:
  void accept() override;
  void reject() override;

 private slots:
  void SaveSettings();
  void RestoreDefaults();

  void InsertTag(const QString &tag);
  void UpdatePreviews();

  void OrganizeFinished(const QStringList files_with_errors, const QStringList log);

  void AllowExtASCII(bool checked);

 private:
  static const char *kSettingsGroup;

  QWidget *parentwindow_;
  Ui_OrganizeDialog *ui_;
  TaskManager *task_manager_;
  CollectionBackend *backend_;

  OrganizeFormat format_;

  QFuture<SongList> songs_future_;
  SongList songs_;
  Organize::NewSongInfoList new_songs_info_;
  quint64 total_size_;
  QString playlist_;

  std::unique_ptr<OrganizeErrorDialog> error_dialog_;

};

#endif  // ORGANISEDIALOG_H

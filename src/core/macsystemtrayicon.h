/*
  *Strawberry Music Player
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

#ifndef MACSYSTEMTRAYICON_H
#define MACSYSTEMTRAYICON_H

#include "config.h"

#include <memory>

#include <QObject>
#include <QAction>
#include <QPixmap>

class MacSystemTrayIconPrivate;

class SystemTrayIcon : public QObject {
  Q_OBJECT

 public:
  SystemTrayIcon(QObject *parent = nullptr);
  ~SystemTrayIcon();

  bool IsAvailable const { return true; }

  void SetupMenu(QAction *previous, QAction *play, QAction *stop, QAction *stop_after, QAction *next, QAction *mute, QAction *love, QAction *quit);

  void SetNowPlaying(const Song& song, const QString& image_path);
  void ClearNowPlaying();

 signals:
  void ChangeVolume(int delta);
  void SeekForward();
  void SeekBackward();
  void NextTrack();
  void PreviousTrack();
  void ShowHide();
  void PlayPause();

 private:
  void SetupMenuItem(QAction *action);

 private slots:
  void SetProgress(int percentage);
  void ActionChanged();

 protected:
  QPixmap CreateIcon(const QPixmap &icon, const QPixmap &grey_icon);
  void UpdateIcon();

 private:
  int percentage_;
  QPixmap orange_icon_;
  QPixmap grey_icon_;
  QPixmap playing_icon_;
  QPixmap paused_icon_;
  QPixmap current_state_icon_;
  std::unique_ptr<MacSystemTrayIconPrivate> p_;
  Q_DISABLE_COPY(SystemTrayIcon);

};

#endif  // MACSYSTEMTRAYICON_H

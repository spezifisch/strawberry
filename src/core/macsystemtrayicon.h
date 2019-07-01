/*
  *Strawberry Music Player
 * This file was part of Clementine.
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

#ifndef MACSYSTEMTRAYICON_H
#define MACSYSTEMTRAYICON_H

#include "config.h"

#include <memory>

#include <QObject>
#include <QString>
#include <QIcon>
#include <QPixmap>
#include <QAction>

class MacSystemTrayIconPrivate;
class Song;

class SystemTrayIcon : public QObject {
  Q_OBJECT

 public:
  SystemTrayIcon(QObject *parent = nullptr);
  ~SystemTrayIcon();

  bool IsAvailable() const { return true; }

  void SetupMenu(QAction *previous, QAction *play, QAction *stop, QAction *stop_after, QAction *next, QAction *mute, QAction *love, QAction *quit);

  void SetNowPlaying(const Song &song, const QString &image_path);
  void ClearNowPlaying();

  void SetPlaying(bool enable_play_pause = false);
  void SetPaused();
  void SetStopped();

  bool MuteEnabled() { return action_mute_->isVisible(); }
  void SetMuteEnabled(bool enabled) { action_mute_->setVisible(enabled); }
  void MuteButtonStateChanged(bool value);
  void LoveVisibilityChanged(bool value) {}
  void LoveStateChanged(bool value) {}

 private:
  void SetupMenuItem(QAction *action);

 private slots:
  void ActionChanged();

 public slots:
  void SetProgress(int percentage);

 signals:
  void ChangeVolume(int delta);
  void SeekForward();
  void SeekBackward();
  void NextTrack();
  void PreviousTrack();
  void ShowHide();
  void PlayPause();

 protected:
  QPixmap CreateIcon(const QPixmap &icon, const QPixmap &grey_icon);
  void UpdateIcon();

 private:
  QIcon icon_;
  QPixmap normal_icon_;
  QPixmap grey_icon_;
  QPixmap playing_icon_;
  QPixmap paused_icon_;
  QPixmap current_state_icon_;
  int percentage_;

  std::unique_ptr<MacSystemTrayIconPrivate> p_;
  Q_DISABLE_COPY(SystemTrayIcon);

};

#endif  // MACSYSTEMTRAYICON_H

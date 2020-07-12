/*
 * Strawberry Music Player
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

#include "config.h"

#include <cmath>

#include <QSystemTrayIcon>
#include <QCoreApplication>
#include <QIODevice>
#include <QFile>
#include <QMenu>
#include <QIcon>
#include <QString>
#include <QUrl>
#include <QPixmap>
#include <QPainter>
#include <QPoint>
#include <QPolygon>
#include <QRect>
#include <QAction>
#include <QtEvents>
#include <QSettings>

#include "song.h"
#include "iconloader.h"
#include "utilities.h"
#include "core/logging.h"

#include "qtsystemtrayicon.h"

#include "settings/behavioursettingspage.h"

SystemTrayIcon::SystemTrayIcon(QObject *parent)
    : QSystemTrayIcon(parent),
      menu_(new QMenu),
      app_name_(QCoreApplication::applicationName()),
      icon_(IconLoader::Load("strawberry")),
      normal_icon_(icon_.pixmap(48, QIcon::Normal)),
      grey_icon_(icon_.pixmap(48, QIcon::Disabled)),
      playing_icon_(":/pictures/tiny-play.png"),
      paused_icon_(":/pictures/tiny-pause.png"),
      action_play_pause_(nullptr),
      action_stop_(nullptr),
      action_stop_after_this_track_(nullptr),
      action_mute_(nullptr),
      percentage_(0) {

  app_name_[0] = app_name_[0].toUpper();

#ifndef Q_OS_WIN
  de_ = Utilities::DesktopEnvironment().toLower();
  QFile pattern_file(":/html/playing-tooltip.html");
  if (pattern_file.open(QIODevice::ReadOnly)) {
    pattern_ = QString::fromLatin1(pattern_file.readAll());
    pattern_file.close();
  }

#endif

  if (isSystemTrayAvailable()) {
    setIcon(normal_icon_);
    ClearNowPlaying();
  }

  connect(this, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), SLOT(Clicked(QSystemTrayIcon::ActivationReason)));

}

SystemTrayIcon::~SystemTrayIcon() {
  delete menu_;
}

QPixmap SystemTrayIcon::CreateIcon(const QPixmap &icon, const QPixmap &grey_icon) {

  QRect rect(icon.rect());

  // The angle of the line that's used to cover the icon.
  // Centered on rect.topRight()
  double angle = double(100 - percentage_) / 100.0 * M_PI_2 + M_PI;
  double length = sqrt(pow(rect.width(), 2.0) + pow(rect.height(), 2.0));

  QPolygon mask;
  mask << rect.topRight();
  mask << rect.topRight() + QPoint(length * sin(angle), -length * cos(angle));

  if (percentage_ > 50) mask << rect.bottomLeft();

  mask << rect.topLeft();
  mask << rect.topRight();

  QPixmap ret(icon);
  QPainter p(&ret);

  // Draw the grey bit
  //p.setClipRegion(mask);
  //p.drawPixmap(0, 0, grey_icon);
  //p.setClipping(false);

  // Draw the playing or paused icon in the top-right
  if (!current_state_icon_.isNull()) {
    int height = rect.height() / 2;
    QPixmap scaled(current_state_icon_.scaledToHeight(height, Qt::SmoothTransformation));

    QRect state_rect(rect.width() - scaled.width(), 0, scaled.width(), scaled.height());
    p.drawPixmap(state_rect, scaled);
  }

  p.end();

  return ret;

}

void SystemTrayIcon::SetupMenu(QAction *previous, QAction *play, QAction *stop, QAction *stop_after, QAction *next, QAction *mute, QAction *love, QAction *quit) {

  // Creating new actions and connecting them to old ones.
  // This allows us to use old actions without displaying shortcuts that can not be used when Strawberry's window is hidden
  menu_->addAction(previous->icon(), previous->text(), previous, SLOT(trigger()));
  action_play_pause_ = menu_->addAction(play->icon(), play->text(), play, SLOT(trigger()));
  action_stop_ = menu_->addAction(stop->icon(), stop->text(), stop, SLOT(trigger()));
  action_stop_after_this_track_ = menu_->addAction(stop_after->icon(), stop_after->text(), stop_after, SLOT(trigger()));
  menu_->addAction(next->icon(), next->text(), next, SLOT(trigger()));

  menu_->addSeparator();
  action_mute_ = menu_->addAction(mute->icon(), mute->text(), mute, SLOT(trigger()));
  action_mute_->setCheckable(true);
  action_mute_->setChecked(mute->isChecked());

  menu_->addSeparator();
  action_love_ = menu_->addAction(love->icon(), love->text(), love, SLOT(trigger()));
  action_love_->setVisible(love->isVisible());
  action_love_->setEnabled(love->isEnabled());
  menu_->addSeparator();
  menu_->addAction(quit->icon(), quit->text(), quit, SLOT(trigger()));

  setContextMenu(menu_);

}

void SystemTrayIcon::UpdateIcon() {
  setIcon(CreateIcon(normal_icon_, grey_icon_));
}

void SystemTrayIcon::SetProgress(int percentage) {
  percentage_ = percentage;
  UpdateIcon();
}

void SystemTrayIcon::Clicked(QSystemTrayIcon::ActivationReason reason) {

  switch (reason) {
    case QSystemTrayIcon::DoubleClick:
    case QSystemTrayIcon::Trigger:
      emit ShowHide();
      break;

    case QSystemTrayIcon::MiddleClick:
      emit PlayPause();
      break;

    default:
      break;
  }

}

void SystemTrayIcon::ShowPopup(const QString &summary, const QString &message, int timeout) {
  showMessage(summary, message, QSystemTrayIcon::NoIcon, timeout);
}

void SystemTrayIcon::SetPlaying(bool enable_play_pause) {

  current_state_icon_ = playing_icon_;
  UpdateIcon();

  action_stop_->setEnabled(true);
  action_stop_after_this_track_->setEnabled(true);
  action_play_pause_->setIcon(IconLoader::Load("media-playback-start"));
  action_play_pause_->setText(tr("Play"));
  action_play_pause_->setEnabled(enable_play_pause);

}

void SystemTrayIcon::SetPaused() {

  current_state_icon_ = paused_icon_;
  UpdateIcon();

  action_stop_->setEnabled(true);
  action_stop_after_this_track_->setEnabled(true);
  action_play_pause_->setIcon(IconLoader::Load("media-playback-pause"));
  action_play_pause_->setText(tr("Pause"));
  action_play_pause_->setEnabled(true);

}

void SystemTrayIcon::SetStopped() {

  current_state_icon_ = QPixmap();
  UpdateIcon();

  action_stop_->setEnabled(false);
  action_stop_after_this_track_->setEnabled(false);
  action_play_pause_->setIcon(IconLoader::Load("media-playback-start"));
  action_play_pause_->setText(tr("Play"));

  action_play_pause_->setEnabled(true);

  action_love_->setEnabled(false);

}

void SystemTrayIcon::MuteButtonStateChanged(bool value) {
  if (action_mute_) action_mute_->setChecked(value);
}

void SystemTrayIcon::SetNowPlaying(const Song &song, const QUrl &cover_url) {

#ifdef Q_OS_WIN
  Q_UNUSED(song);
  Q_UNUSED(cover_url);
  // Windows doesn't support HTML in tooltips, so just show something basic
  setToolTip(song.PrettyTitleWithArtist());
#else

  if (de_ == "kde") {
    tray_->setToolTip(song.PrettyTitleWithArtist());
    return;
  }

  int columns = cover_url.isEmpty() ? 1 : 2;

  QString tooltip(pattern_);

  tooltip.replace("%columns"    , QString::number(columns));
  tooltip.replace("%appName"    , app_name_);

  tooltip.replace("%titleKey"   , tr("Title") % ":");
  tooltip.replace("%titleValue" , song.PrettyTitle().toHtmlEscaped());
  tooltip.replace("%artistKey"  , tr("Artist") % ":");
  tooltip.replace("%artistValue", song.artist().toHtmlEscaped());
  tooltip.replace("%albumKey"   , tr("Album") % ":");
  tooltip.replace("%albumValue" , song.album().toHtmlEscaped());

  tooltip.replace("%lengthKey"  , tr("Length") % ":");
  tooltip.replace("%lengthValue", song.PrettyLength().toHtmlEscaped());

  if (columns == 2) {
    QString final_path = cover_url.isLocalFile() ? cover_url.path() : cover_url.toString();
    tooltip.replace("%image", "    <td>      <img src=\"" % final_path % "\" />   </td>");
  }
  else {
    tooltip.replace("<td>%image</td>", "");
    tooltip.replace("%image", "");
  }

  // TODO: we should also repaint this
  setToolTip(tooltip);

#endif

}

void SystemTrayIcon::ClearNowPlaying() {
  setToolTip(app_name_);
}

bool SystemTrayIcon::event(QEvent *event) {

  if (event->type() == QEvent::Wheel) {
    QWheelEvent *e = static_cast<QWheelEvent*>(event);
    if (e->modifiers() == Qt::ShiftModifier) {
      if (e->delta() > 0) {
        emit SeekForward();
      }
      else {
        emit SeekBackward();
      }
    }
    else if (e->modifiers() == Qt::ControlModifier) {
      if (e->delta() < 0) {
        emit NextTrack();
      }
      else {
        emit PreviousTrack();
      }
    }
    else {
      QSettings s;
      s.beginGroup(BehaviourSettingsPage::kSettingsGroup);
      bool scrolltrayicon = s.value("scrolltrayicon").toBool();
      s.endGroup();
      if (scrolltrayicon) {
        if (e->delta() < 0) {
          emit NextTrack();
        }
        else {
          emit PreviousTrack();
        }
      }
      else {
        emit ChangeVolume(e->delta());
      }
    }
    return true;
  }

  return QSystemTrayIcon::event(event);

}

void SystemTrayIcon::LoveVisibilityChanged(bool value) {
  action_love_->setVisible(value);
}

void SystemTrayIcon::LoveStateChanged(bool value) {
  action_love_->setEnabled(value);
}

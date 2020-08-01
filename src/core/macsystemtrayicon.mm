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

#include "macsystemtrayicon.h"

#include "mac_delegate.h"
#include "song.h"
#include "iconloader.h"

#include <QApplication>
#include <QString>
#include <QUrl>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QPoint>
#include <QPolygon>
#include <QRect>
#include <QAction>
#include <QtDebug>

#include <AppKit/NSMenu.h>
#include <AppKit/NSMenuItem.h>

@interface Target :NSObject {
  QAction* action_;
}
- (id) initWithQAction: (QAction*)action;
- (void) clicked;
@end

@implementation Target  // <NSMenuValidation>
- (id) init {
  return [super init];
}

- (id) initWithQAction: (QAction*)action {
  action_ = action;
  return self;
}

- (BOOL) validateMenuItem: (NSMenuItem*)menuItem {
  Q_UNUSED(menuItem);
  // This is called when the menu is shown.
  return action_->isEnabled();
}

- (void) clicked {
  action_->trigger();
}
@end

class MacSystemTrayIconPrivate {
 public:
  MacSystemTrayIconPrivate() {
    dock_menu_ = [[NSMenu alloc] initWithTitle:@"DockMenu"];

    QString title = QT_TR_NOOP("Now Playing");
    NSString* t = [[NSString alloc] initWithUTF8String:title.toUtf8().constData()];
    now_playing_ = [[NSMenuItem alloc]
        initWithTitle:t
        action:nullptr
        keyEquivalent:@""];

    now_playing_artist_ = [[NSMenuItem alloc]
        initWithTitle:@"Nothing to see here"
                                   action:nullptr
                            keyEquivalent:@""];

    now_playing_title_ = [[NSMenuItem alloc]
        initWithTitle:@"Nothing to see here"
                                   action:nullptr
                            keyEquivalent:@""];

    [dock_menu_ insertItem:now_playing_title_ atIndex:0];
    [dock_menu_ insertItem:now_playing_artist_ atIndex:0];
    [dock_menu_ insertItem:now_playing_ atIndex:0];

    // Don't look now.
    // This must be called after our custom NSApplicationDelegate has been set.
    [(AppDelegate*)([NSApp delegate]) setDockMenu:dock_menu_];

    ClearNowPlaying();
  }

  void AddMenuItem(QAction* action) {
    // Strip accelarators from name.
    QString text = action->text().remove("&");
    NSString* title = [[NSString alloc] initWithUTF8String: text.toUtf8().constData()];
    // Create an object that can receive user clicks and pass them on to the QAction.
    Target* target = [[Target alloc] initWithQAction:action];
    NSMenuItem* item = [[[NSMenuItem alloc]
        initWithTitle:title
                                                   action:@selector(clicked)
                                            keyEquivalent:@""] autorelease];
    [item setEnabled:action->isEnabled()];
    [item setTarget:target];
    [dock_menu_ addItem:item];
    actions_[action] = item;
  }

  void ActionChanged(QAction* action) {
    NSMenuItem* item = actions_[action];
    NSString* title = [[NSString alloc] initWithUTF8String: action->text().toUtf8().constData()];
    [item setTitle:title];
  }

  void AddSeparator() {
    NSMenuItem* separator = [NSMenuItem separatorItem];
    [dock_menu_ addItem:separator];
  }

  void ShowNowPlaying(const QString& artist, const QString& title) {
    ClearNowPlaying();  // Makes sure the order is consistent.
    [now_playing_artist_ setTitle:
        [[NSString alloc] initWithUTF8String: artist.toUtf8().constData()]];
    [now_playing_title_ setTitle:
        [[NSString alloc] initWithUTF8String: title.toUtf8().constData()]];
    title.isEmpty() ? HideItem(now_playing_title_) : ShowItem(now_playing_title_);
    artist.isEmpty() ? HideItem(now_playing_artist_) : ShowItem(now_playing_artist_);
    artist.isEmpty() && title.isEmpty() ? HideItem(now_playing_) : ShowItem(now_playing_);
  }

  void ClearNowPlaying() {
    // Hiding doesn't seem to work in the dock menu.
    HideItem(now_playing_);
    HideItem(now_playing_artist_);
    HideItem(now_playing_title_);
  }

 private:
  void HideItem(NSMenuItem* item) {
    if ([dock_menu_ indexOfItem:item] != -1) {
      [dock_menu_ removeItem:item];
    }
  }

  void ShowItem(NSMenuItem* item, int index = 0) {
    if ([dock_menu_ indexOfItem:item] == -1) {
      [dock_menu_ insertItem:item atIndex:index];
    }
  }

  QMap<QAction*, NSMenuItem*> actions_;

  NSMenu* dock_menu_;
  NSMenuItem* now_playing_;
  NSMenuItem* now_playing_artist_;
  NSMenuItem* now_playing_title_;

  Q_DISABLE_COPY(MacSystemTrayIconPrivate);

};

SystemTrayIcon::SystemTrayIcon(QObject* parent)
    : QObject(parent),
      icon_(IconLoader::Load("strawberry")),
      normal_icon_(QPixmap(":/pictures/strawberry.png").scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation)),
      playing_icon_(":/pictures/tiny-play.png"),
      paused_icon_(":/pictures/tiny-pause.png"),
      percentage_(0) {

  QApplication::setWindowIcon(normal_icon_);

}

SystemTrayIcon::~SystemTrayIcon() {}

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

void SystemTrayIcon::SetupMenu(QAction* previous, QAction* play, QAction* stop, QAction* stop_after, QAction* next, QAction* mute, QAction* love, QAction* quit) {

  p_.reset(new MacSystemTrayIconPrivate());
  SetupMenuItem(previous);
  SetupMenuItem(play);
  SetupMenuItem(stop);
  SetupMenuItem(stop_after);
  SetupMenuItem(next);
  p_->AddSeparator();
  SetupMenuItem(mute);
  p_->AddSeparator();
  SetupMenuItem(love);
  Q_UNUSED(quit);  // Mac already has a Quit item.

}

void SystemTrayIcon::SetupMenuItem(QAction* action) {

  p_->AddMenuItem(action);
  connect(action, SIGNAL(changed()), SLOT(ActionChanged()));

}

void SystemTrayIcon::UpdateIcon() {
  QApplication::setWindowIcon(CreateIcon(normal_icon_, normal_icon_));
}

void SystemTrayIcon::SetProgress(int percentage) {
  percentage_ = percentage;
  UpdateIcon();
}

void SystemTrayIcon::ActionChanged() {

  QAction* action = qobject_cast<QAction*>(sender());
  p_->ActionChanged(action);

}

void SystemTrayIcon::SetPlaying(bool enable_play_pause) {

  current_state_icon_ = playing_icon_;
  UpdateIcon();

}

void SystemTrayIcon::SetPaused() {

  current_state_icon_ = paused_icon_;
  UpdateIcon();

}

void SystemTrayIcon::SetStopped() {

  current_state_icon_ = QPixmap();
  UpdateIcon();

}

void MacSystemTrayIcon::SetNowPlaying(const Song& song, const QUrl& cover_url) {
  Q_UNUSED(cover_url);
  p_->ShowNowPlaying(song.artist(), song.PrettyTitle());

}

void SystemTrayIcon::ClearNowPlaying() {

  p_->ClearNowPlaying();

}


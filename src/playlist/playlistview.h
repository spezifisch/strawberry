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

#ifndef PLAYLISTVIEW_H
#define PLAYLISTVIEW_H

#include "config.h"

#include <memory>

#include <QtGlobal>
#include <QObject>
#include <QAbstractItemDelegate>
#include <QAbstractItemModel>
#include <QStyleOptionViewItem>
#include <QAbstractItemView>
#include <QTreeView>
#include <QList>
#include <QByteArray>
#include <QString>
#include <QImage>
#include <QPixmap>
#include <QRect>
#include <QRegion>
#include <QStyleOption>
#include <QProxyStyle>
#include <QPoint>
#include <QBasicTimer>
#include <QCommonStyle>

#include "core/song.h"
#include "covermanager/albumcoverloaderresult.h"
#include "settings/appearancesettingspage.h"
#include "playlist.h"

class QWidget;
class QTimer;
class QTimeLine;
class QPainter;
class QEvent;
class QShowEvent;
class QContextMenuEvent;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QFocusEvent;
class QHideEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QTimerEvent;

class Application;
class CollectionBackend;
class PlaylistHeader;
class DynamicPlaylistControls;

// This proxy style works around a bug/feature introduced in Qt 4.7's QGtkStyle
// that uses Gtk to paint row backgrounds, ignoring any custom brush or palette the caller set in the QStyleOption.
// That breaks our currently playing track animation, which relies on the background painted by Qt to be transparent.
// This proxy style uses QCommonStyle to paint the affected elements.
// This class is used by internet search view as well.
class PlaylistProxyStyle : public QProxyStyle {
 public:
  explicit PlaylistProxyStyle();

  void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const override;
  void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const override;

 private:
  std::unique_ptr<QCommonStyle> common_style_;
};

class PlaylistView : public QTreeView {
  Q_OBJECT

 public:
  explicit PlaylistView(QWidget *parent = nullptr);
  ~PlaylistView() override;

  static ColumnAlignmentMap DefaultColumnAlignment();

  void Init(Application *app);
  void SetItemDelegates();
  void SetPlaylist(Playlist *playlist);
  void RemoveSelected();

  void SetReadOnlySettings(bool read_only) { read_only_settings_ = read_only; }

  Playlist *playlist() const { return playlist_; }
  AppearanceSettingsPage::BackgroundImageType background_image_type() const { return background_image_type_; }
  Qt::Alignment column_alignment(int section) const;

  void ResetHeaderState();

  // QTreeView
  void setModel(QAbstractItemModel *model) override;

 public slots:
  void ReloadSettings();
  void SaveSettings();
  void SetColumnAlignment(const int section, const Qt::Alignment alignment);
  void JumpToCurrentlyPlayingTrack();
  void edit(const QModelIndex &idx) { return QAbstractItemView::edit(idx); }

 signals:
  void PlayItem(QModelIndex idx, Playlist::AutoScroll autoscroll);
  void PlayPause(Playlist::AutoScroll autoscroll = Playlist::AutoScroll_Never);
  void RightClicked(QPoint global_pos, QModelIndex idx);
  void SeekForward();
  void SeekBackward();
  void FocusOnFilterSignal(QKeyEvent *event);
  void BackgroundPropertyChanged();
  void ColumnAlignmentChanged(ColumnAlignmentMap alignment);

 protected:
  // QWidget
  void keyPressEvent(QKeyEvent *event) override;
  void contextMenuEvent(QContextMenuEvent *e) override;
  void hideEvent(QHideEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void timerEvent(QTimerEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void leaveEvent(QEvent*) override;
  void paintEvent(QPaintEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragLeaveEvent(QDragLeaveEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  bool eventFilter(QObject *object, QEvent *event) override;
  void focusInEvent(QFocusEvent *event) override;
  void resizeEvent(QResizeEvent* event) override;

  // QTreeView
  void drawTree(QPainter *painter, const QRegion &region) const;
  void drawRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &idx) const override;

  // QAbstractScrollArea
  void scrollContentsBy(int dx, int dy) override;

  // QAbstractItemView
  void rowsInserted(const QModelIndex &parent, int start, int end) override;
  bool edit(const QModelIndex &idx, QAbstractItemView::EditTrigger trigger, QEvent *event) override;
  void closeEditor(QWidget *editor, QAbstractItemDelegate::EndEditHint hint) override;

 private slots:
  void SetHeaderState();
  void InhibitAutoscrollTimeout();
  void MaybeAutoscroll(const Playlist::AutoScroll autoscroll);
  void InvalidateCachedCurrentPixmap();
  void PlaylistDestroyed();
  void StretchChanged(const bool stretch);
  void FadePreviousBackgroundImage(const qreal value);
  void StopGlowing();
  void StartGlowing();
  void JumpToLastPlayedTrack();
  void CopyCurrentSongToClipboard() const;
  void Playing();
  void Stopped();
  void SongChanged(const Song &song);
  void AlbumCoverLoaded(const Song &song, AlbumCoverLoaderResult result = AlbumCoverLoaderResult());
  void DynamicModeChanged(bool dynamic);

 private:
  void LoadHeaderState();
  void RestoreHeaderState();

  void ReloadBarPixmaps();
  QList<QPixmap> LoadBarPixmap(const QString &filename);
  void UpdateCachedCurrentRowPixmap(QStyleOptionViewItem option, const QModelIndex &idx);

  void set_background_image_type(AppearanceSettingsPage::BackgroundImageType bg) {
    background_image_type_ = bg;
    emit BackgroundPropertyChanged();
  }
  // Save image as the background_image_ after applying some modifications (opacity, ...).
  // Should be used instead of modifying background_image_ directly
  void set_background_image(const QImage &image);

  void GlowIntensityChanged();

 private:
  static const int kGlowIntensitySteps;
  static const int kAutoscrollGraceTimeout;
  static const int kDropIndicatorWidth;
  static const int kDropIndicatorGradientWidth;

  QList<int> GetEditableColumns();
  QModelIndex NextEditableIndex(const QModelIndex &current);
  QModelIndex PrevEditableIndex(const QModelIndex &current);

  void RepositionDynamicControls();

  Application *app_;
  PlaylistProxyStyle *style_;
  Playlist *playlist_;
  PlaylistHeader *header_;

  AppearanceSettingsPage::BackgroundImageType background_image_type_;
  QString background_image_filename_;
  AppearanceSettingsPage::BackgroundImagePosition background_image_position_;
  int background_image_maxsize_;
  bool background_image_stretch_;
  bool background_image_do_not_cut_;
  bool background_image_keep_aspect_ratio_;
  int blur_radius_;
  int opacity_level_;

  bool background_initialized_;
  bool set_initial_header_layout_;
  bool read_only_settings_;
  bool header_state_loaded_;
  bool header_state_restored_;
  bool header_state_readonly_;

  QImage background_image_;
  QImage current_song_cover_art_;
  QPixmap cached_scaled_background_image_;

  // For fading when image change
  QPixmap previous_background_image_;
  qreal previous_background_image_opacity_;
  QTimeLine *fade_animation_;

  // To know if we should redraw the background or not
  bool force_background_redraw_;
  int last_height_;
  int last_width_;
  int current_background_image_x_;
  int current_background_image_y_;
  int previous_background_image_x_;
  int previous_background_image_y_;

  bool glow_enabled_;
  bool select_track_;

  bool currently_glowing_;
  QBasicTimer glow_timer_;
  int glow_intensity_step_;
  QModelIndex last_current_item_;
  QRect last_glow_rect_;

  QTimer *inhibit_autoscroll_timer_;
  bool inhibit_autoscroll_;
  bool currently_autoscrolling_;

  int row_height_;  // Used to invalidate the currenttrack_bar pixmaps
  QList<QPixmap> currenttrack_bar_left_;
  QList<QPixmap> currenttrack_bar_mid_;
  QList<QPixmap> currenttrack_bar_right_;
  QPixmap currenttrack_play_;
  QPixmap currenttrack_pause_;

  QRegion current_paint_region_;
  QPixmap cached_current_row_;
  QRect cached_current_row_rect_;
  int cached_current_row_row_;

  QPixmap cached_tree_;
  int drop_indicator_row_;
  bool drag_over_;

  QByteArray header_state_;
  ColumnAlignmentMap column_alignment_;

  Song song_playing_;

  DynamicPlaylistControls* dynamic_controls_;

};

#endif  // PLAYLISTVIEW_H

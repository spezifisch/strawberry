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

#include <QtGlobal>
#include <QDialog>
#include <QWidget>
#include <QMainWindow>
#include <QScreen>
#include <QWindow>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QTreeWidget>
#include <QList>
#include <QVariant>
#include <QString>
#include <QIcon>
#include <QPainter>
#include <QFrame>
#include <QKeySequence>
#include <QRect>
#include <QSize>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QLayout>
#include <QStackedWidget>
#include <QSettings>
#include <QShowEvent>
#include <QCloseEvent>

#include "core/application.h"
#include "core/player.h"
#include "widgets/groupediconview.h"
#include "collection/collectionmodel.h"

#include "settingsdialog.h"
#include "settingspage.h"
#include "behavioursettingspage.h"
#include "collectionsettingspage.h"
#include "backendsettingspage.h"
#include "playlistsettingspage.h"
#include "scrobblersettingspage.h"
#include "coverssettingspage.h"
#include "lyricssettingspage.h"
#include "transcodersettingspage.h"
#include "networkproxysettingspage.h"
#include "appearancesettingspage.h"
#include "contextsettingspage.h"
#include "notificationssettingspage.h"
#include "shortcutssettingspage.h"
#ifdef HAVE_MOODBAR
#  include "moodbarsettingspage.h"
#endif
#ifdef HAVE_SUBSONIC
#  include "subsonicsettingspage.h"
#endif
#ifdef HAVE_TIDAL
#  include "tidalsettingspage.h"
#endif

#include "ui_settingsdialog.h"

const char *SettingsDialog::kSettingsGroup = "SettingsDialog";

SettingsItemDelegate::SettingsItemDelegate(QObject *parent)
  : QStyledItemDelegate(parent) {}

QSize SettingsItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {

  const bool is_separator = index.data(SettingsDialog::Role_IsSeparator).toBool();
  QSize ret = QStyledItemDelegate::sizeHint(option, index);

  if (is_separator) {
    ret.setHeight(ret.height() * 2);
  }

  return ret;

}

void SettingsItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {

  const bool is_separator = index.data(SettingsDialog::Role_IsSeparator).toBool();

  if (is_separator) {
    GroupedIconView::DrawHeader(painter, option.rect, option.font, option.palette, index.data().toString());
  }
  else {
    QStyledItemDelegate::paint(painter, option, index);
  }

}

SettingsDialog::SettingsDialog(Application *app, OSDBase *osd, QMainWindow *mainwindow, QWidget *parent)
    : QDialog(parent),
      mainwindow_(mainwindow),
      app_(app),
      osd_(osd),
      player_(app_->player()),
      engine_(app_->player()->engine()),
      model_(app_->collection_model()->directory_model()),
      appearance_(app_->appearance()),
      ui_(new Ui_SettingsDialog),
      loading_settings_(false) {

  ui_->setupUi(this);
  ui_->list->setItemDelegate(new SettingsItemDelegate(this));

  QTreeWidgetItem *general = AddCategory(tr("General"));
  AddPage(Page_Behaviour, new BehaviourSettingsPage(this), general);
  AddPage(Page_Collection, new CollectionSettingsPage(this), general);
  AddPage(Page_Backend, new BackendSettingsPage(this), general);
  AddPage(Page_Playlist, new PlaylistSettingsPage(this), general);
  AddPage(Page_Scrobbler, new ScrobblerSettingsPage(this), general);
  AddPage(Page_Covers, new CoversSettingsPage(this), general);
  AddPage(Page_Lyrics, new LyricsSettingsPage(this), general);
#ifdef HAVE_GSTREAMER
  AddPage(Page_Transcoding, new TranscoderSettingsPage(this), general);
#endif
  AddPage(Page_Proxy, new NetworkProxySettingsPage(this), general);

  QTreeWidgetItem *iface = AddCategory(tr("User interface"));
  AddPage(Page_Appearance, new AppearanceSettingsPage(this), iface);
  AddPage(Page_Context, new ContextSettingsPage(this), iface);
  AddPage(Page_Notifications, new NotificationsSettingsPage(this), iface);

#ifdef HAVE_GLOBALSHORTCUTS
  AddPage(Page_GlobalShortcuts, new GlobalShortcutsSettingsPage(this), iface);
#endif

#ifdef HAVE_MOODBAR
  AddPage(Page_Moodbar, new MoodbarSettingsPage(this), iface);
#endif

#if defined(HAVE_SUBSONIC) || defined(HAVE_TIDAL)
  QTreeWidgetItem *streaming = AddCategory(tr("Streaming"));
#endif

#ifdef HAVE_SUBSONIC
  AddPage(Page_Subsonic, new SubsonicSettingsPage(this), streaming);
#endif
#ifdef HAVE_TIDAL
  AddPage(Page_Tidal, new TidalSettingsPage(this), streaming);
#endif

  // List box
  connect(ui_->list, SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)), SLOT(CurrentItemChanged(QTreeWidgetItem*)));
  ui_->list->setCurrentItem(pages_[Page_Behaviour].item_);

  // Make sure the list is big enough to show all the items
  ui_->list->setMinimumWidth(qobject_cast<QAbstractItemView*>(ui_->list)->sizeHintForColumn(0));

  ui_->buttonBox->button(QDialogButtonBox::Cancel)->setShortcut(QKeySequence::Close);

  connect(ui_->buttonBox, SIGNAL(clicked(QAbstractButton*)), this, SLOT(DialogButtonClicked(QAbstractButton*)));

}

SettingsDialog::~SettingsDialog() {
  delete ui_;
}

void SettingsDialog::showEvent(QShowEvent *e) {

  LoadGeometry();

  // Load settings
  loading_settings_ = true;
  for (const PageData &page : pages_.values()) {
    page.page_->Load();
  }
  loading_settings_ = false;

  QDialog::showEvent(e);

}

void SettingsDialog::closeEvent(QCloseEvent*) {

  SaveGeometry();

}

void SettingsDialog::accept() {

  for (const PageData &page : pages_.values()) {
    page.page_->Accept();
  }
  emit ReloadSettings();

  SaveGeometry();

  QDialog::accept();

}

void SettingsDialog::reject() {

  // Notify each page that user clicks on Cancel
  for (const PageData &page : pages_.values()) {
    page.page_->Reject();
  }
  SaveGeometry();

  QDialog::reject();

}

void SettingsDialog::LoadGeometry() {

  QSettings s;
  s.beginGroup(kSettingsGroup);
  if (s.contains("geometry")) {
    restoreGeometry(s.value("geometry").toByteArray());
  }
  s.endGroup();

  // Center the dialog on the same screen as mainwindow.
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
  QScreen *screen = mainwindow_->screen();
#else
  QScreen *screen = (mainwindow_->window() && mainwindow_->window()->windowHandle() ? mainwindow_->window()->windowHandle()->screen() : nullptr);
#endif
  if (screen) {
    const QRect sr = screen->availableGeometry();
    const QRect wr({}, size().boundedTo(sr.size()));
    resize(wr.size());
    move(sr.center() - wr.center());
  }

}

void SettingsDialog::SaveGeometry() {

  QSettings s;
  s.beginGroup(kSettingsGroup);
  s.setValue("geometry", saveGeometry());
  s.endGroup();

}

QTreeWidgetItem *SettingsDialog::AddCategory(const QString &name) {

  QTreeWidgetItem *item = new QTreeWidgetItem;
  item->setText(0, name);
  item->setData(0, Role_IsSeparator, true);
  item->setFlags(Qt::ItemIsEnabled);

  ui_->list->invisibleRootItem()->addChild(item);
  item->setExpanded(true);

  return item;

}

void SettingsDialog::AddPage(Page id, SettingsPage *page, QTreeWidgetItem *parent) {

  if (!parent) parent = ui_->list->invisibleRootItem();

  // Connect page's signals to the settings dialog's signals
  connect(page, SIGNAL(NotificationPreview(OSDBase::Behaviour, QString, QString)), SIGNAL(NotificationPreview(OSDBase::Behaviour, QString, QString)));

  // Create the list item
  QTreeWidgetItem *item = new QTreeWidgetItem;
  item->setText(0, page->windowTitle());
  item->setIcon(0, page->windowIcon());
  item->setData(0, Role_IsSeparator, false);

  if (!page->IsEnabled()) {
    item->setFlags(Qt::NoItemFlags);
  }

  parent->addChild(item);

  // Create a scroll area containing the page
  QScrollArea *area = new QScrollArea;
  area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  area->setWidget(page);
  area->setWidgetResizable(true);
  area->setFrameShape(QFrame::NoFrame);
  area->setMinimumWidth(page->layout()->minimumSize().width());

  // Add the page to the stack
  ui_->stacked_widget->addWidget(area);

  // Remember where the page is
  PageData page_data;
  page_data.item_ = item;
  page_data.scroll_area_ = area;
  page_data.page_ = page;
  pages_[id] = page_data;

}

void SettingsDialog::Save() {

  for (const PageData &page : pages_.values()) {
    page.page_->Apply();
  }
  emit ReloadSettings();

}


void SettingsDialog::DialogButtonClicked(QAbstractButton *button) {

  // While we only connect Apply at the moment, this might change in the future
  if (ui_->buttonBox->button(QDialogButtonBox::Apply) == button) {
    for (const PageData &page : pages_.values()) {
      page.page_->Apply();
    }
    emit ReloadSettings();
  }

}

void SettingsDialog::OpenAtPage(Page page) {

  if (!pages_.contains(page)) {
    return;
  }

  ui_->list->setCurrentItem(pages_[page].item_);
  show();

}

void SettingsDialog::CurrentItemChanged(QTreeWidgetItem *item) {

  if (!(item->flags() & Qt::ItemIsSelectable)) {
    return;
  }

  // Set the title
  ui_->title->setText("<b>" + item->text(0) + "</b>");

  // Display the right page
  for (const PageData &page : pages_.values()) {
    if (page.item_ == item) {
      ui_->stacked_widget->setCurrentWidget(page.scroll_area_);
      break;
    }
  }

}

void SettingsDialog::ComboBoxLoadFromSettings(const QSettings &s, QComboBox *combobox, const QString &setting, const QString &default_value) {

  QString value = s.value(setting, default_value).toString();
  int i = combobox->findData(value);
  if (i == -1) i = combobox->findData(default_value);
  combobox->setCurrentIndex(i);

}

void SettingsDialog::ComboBoxLoadFromSettings(const QSettings &s, QComboBox *combobox, const QString &setting, const int default_value) {

  int value = s.value(setting, default_value).toInt();
  int i = combobox->findData(value);
  if (i == -1) i = combobox->findData(default_value);
  combobox->setCurrentIndex(i);

}

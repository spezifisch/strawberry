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

#include "core/application.h"
#include "collection/collectionbackend.h"

#include "smartplaylistsviewcontainer.h"
#include "smartplaylistsmodel.h"
#include "smartplaylistsview.h"
#include "smartplaylistsearchterm.h"
#include "playlistquerygenerator.h"
#include "playlistgenerator_fwd.h"

#include "ui_smartplaylistsviewcontainer.h"

SmartPlaylistsViewContainer::SmartPlaylistsViewContainer(Application *app, QWidget* parent)
    : QWidget(parent),
    ui_(new Ui_SmartPlaylistsViewContainer),
    app_(app) {

  ui_->setupUi(this);

  model_ = new SmartPlaylistsModel(app_->collection_backend(), app_, this);
  ui_->view->SetModel(model_);
  ui_->view->SetApplication(app_);
  model_->set_default_smart_playlists(
    SmartPlaylistsModel::DefaultGenerators()
    << (SmartPlaylistsModel::GeneratorList()
          << PlaylistGeneratorPtr(new PlaylistQueryGenerator(
             QT_TRANSLATE_NOOP("SmartPlaylists", "50 random tracks"),
             SmartPlaylistSearch(SmartPlaylistSearch::Type_All, SmartPlaylistSearch::TermList(), SmartPlaylistSearch::Sort_Random, SmartPlaylistSearchTerm::Field_Title, 50)
            )
          )
          << PlaylistGeneratorPtr(
              new PlaylistQueryGenerator(
              QT_TRANSLATE_NOOP("SmartPlaylists", "Ever played"),
              SmartPlaylistSearch(SmartPlaylistSearch::Type_And, SmartPlaylistSearch::TermList() << SmartPlaylistSearchTerm( SmartPlaylistSearchTerm::Field_PlayCount, SmartPlaylistSearchTerm::Op_GreaterThan, 0), SmartPlaylistSearch::Sort_Random, SmartPlaylistSearchTerm::Field_Title)
            )
          )
          << PlaylistGeneratorPtr(
             new PlaylistQueryGenerator(
             QT_TRANSLATE_NOOP("SmartPlaylists", "Never played"),
             SmartPlaylistSearch(SmartPlaylistSearch::Type_And, SmartPlaylistSearch::TermList() << SmartPlaylistSearchTerm(SmartPlaylistSearchTerm::Field_PlayCount, SmartPlaylistSearchTerm::Op_Equals, 0), SmartPlaylistSearch::Sort_Random, SmartPlaylistSearchTerm::Field_Title)
            )
          )
          << PlaylistGeneratorPtr(
             new PlaylistQueryGenerator(
             QT_TRANSLATE_NOOP("SmartPlaylists", "Last played"),
             SmartPlaylistSearch(SmartPlaylistSearch::Type_All, SmartPlaylistSearch::TermList(), SmartPlaylistSearch::Sort_FieldDesc, SmartPlaylistSearchTerm::Field_LastPlayed)
            )
          )
          << PlaylistGeneratorPtr(
             new PlaylistQueryGenerator(
             QT_TRANSLATE_NOOP("SmartPlaylists", "Most played"),
             SmartPlaylistSearch(SmartPlaylistSearch::Type_All, SmartPlaylistSearch::TermList(), SmartPlaylistSearch::Sort_FieldDesc, SmartPlaylistSearchTerm::Field_PlayCount)
            )
          )
          << PlaylistGeneratorPtr(
             new PlaylistQueryGenerator(
             QT_TRANSLATE_NOOP("SmartPlaylists", "Newest tracks"),
             SmartPlaylistSearch(SmartPlaylistSearch::Type_All, SmartPlaylistSearch::TermList(),
             SmartPlaylistSearch::Sort_FieldDesc,
             SmartPlaylistSearchTerm::Field_DateCreated)
           )
         )
       )
    << (SmartPlaylistsModel::GeneratorList() << PlaylistGeneratorPtr(new PlaylistQueryGenerator(QT_TRANSLATE_NOOP("SmartPlaylists", "All tracks"), SmartPlaylistSearch(SmartPlaylistSearch::Type_All, SmartPlaylistSearch::TermList(), SmartPlaylistSearch::Sort_FieldAsc, SmartPlaylistSearchTerm::Field_Artist, -1))))
    << (SmartPlaylistsModel::GeneratorList() << PlaylistGeneratorPtr(new PlaylistQueryGenerator( QT_TRANSLATE_NOOP("SmartPlaylists", "Dynamic random mix"), SmartPlaylistSearch(SmartPlaylistSearch::Type_All, SmartPlaylistSearch::TermList(), SmartPlaylistSearch::Sort_Random, SmartPlaylistSearchTerm::Field_Title), true)))
  );

  model_->Init();

}

SmartPlaylistsViewContainer::~SmartPlaylistsViewContainer() { delete ui_; }

SmartPlaylistsView* SmartPlaylistsViewContainer::view() const { return ui_->view; }

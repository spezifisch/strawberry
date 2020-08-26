/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2013, David Sansome <me@davidsansome.com>
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

#include <QtGlobal>
#include <QObject>
#include <QIODevice>
#include <QDir>
#include <QByteArray>
#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "core/utilities.h"
#include "playlistparsers/xmlparser.h"
#include "version.h"
#include "wplparser.h"

class CollectionBackendInterface;

WplParser::WplParser(CollectionBackendInterface *collection, QObject *parent)
    : XMLParser(collection, parent) {}

bool WplParser::TryMagic(const QByteArray &data) const {
  return data.contains("<?wpl") || data.contains("<smil>");
}

SongList WplParser::Load(QIODevice *device, const QString &playlist_path, const QDir &dir) const {

  Q_UNUSED(playlist_path);

  SongList ret;

  QXmlStreamReader reader(device);
  if (!Utilities::ParseUntilElement(&reader, "smil") || !Utilities::ParseUntilElement(&reader, "body")) {
    return ret;
  }

  while (!reader.atEnd() && Utilities::ParseUntilElement(&reader, "seq")) {
    ParseSeq(dir, &reader, &ret);
  }
  return ret;

}

void WplParser::ParseSeq(const QDir &dir, QXmlStreamReader *reader, SongList *songs) const {

  while (!reader->atEnd()) {
    QXmlStreamReader::TokenType type = reader->readNext();
    QString name = reader->name().toString();
    switch (type) {
      case QXmlStreamReader::StartElement: {
        if (name == "media") {
          QString src = reader->attributes().value("src").toString();
          if (!src.isEmpty()) {
            Song song = LoadSong(src, 0, dir);
            if (song.is_valid()) {
              songs->append(song);
            }
          }
        }
        else {
          Utilities::ConsumeCurrentElement(reader);
        }
        break;
      }
      case QXmlStreamReader::EndElement: {
        if (name == "seq") {
          return;
        }
        break;
      }
      default:
        break;
    }
  }

}

void WplParser::Save(const SongList &songs, QIODevice *device, const QDir &dir, Playlist::Path path_type) const {

  QXmlStreamWriter writer(device);
  writer.setAutoFormatting(true);
  writer.setAutoFormattingIndent(2);
  writer.writeProcessingInstruction("wpl", "version=\"1.0\"");

  StreamElement smil("smil", &writer);

  {
    StreamElement head("head", &writer);
    WriteMeta("Generator", "Strawberry -- " STRAWBERRY_VERSION_DISPLAY, &writer);
    WriteMeta("ItemCount", QString::number(songs.count()), &writer);
  }

  {
    StreamElement body("body", &writer);
    {
      StreamElement seq("seq", &writer);
      for (const Song &song : songs) {
        writer.writeStartElement("media");
        writer.writeAttribute("src", URLOrFilename(song.url(), dir, path_type));
        writer.writeEndElement();
      }
    }
  }
}

void WplParser::WriteMeta(const QString &name, const QString &content, QXmlStreamWriter *writer) const {

  writer->writeStartElement("meta");
  writer->writeAttribute("name", name);
  writer->writeAttribute("content", content);
  writer->writeEndElement();

}

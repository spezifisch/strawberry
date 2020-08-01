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

#include <cstdlib>
#include <cstdint>

#include <QList>
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QtDebug>

#include "core/logging.h"
#include "mtpconnection.h"

MtpConnection::MtpConnection(const QUrl &url) : device_(nullptr) {

  QString hostname = url.host();
  // Parse the URL
  QRegularExpression host_re("^usb-(\\d+)-(\\d+)$");

  unsigned int bus_location = 0;
  unsigned int device_num = 0;

  QUrlQuery url_query(url);

  QRegularExpressionMatch re_match = host_re.match(hostname);
  if (re_match.hasMatch()) {
    bus_location = re_match.captured(1).toUInt();
    device_num = re_match.captured(2).toUInt();
  }
  else if (url_query.hasQueryItem("busnum")) {
    bus_location = url_query.queryItemValue("busnum").toUInt();
    device_num = url_query.queryItemValue("devnum").toUInt();
  }
  else {
    qLog(Warning) << "Invalid MTP device:" << hostname;
    return;
  }

  if (url_query.hasQueryItem("vendor")) {
    LIBMTP_raw_device_t *raw_device = static_cast<LIBMTP_raw_device_t*>(malloc(sizeof(LIBMTP_raw_device_t)));
    raw_device->device_entry.vendor = url_query.queryItemValue("vendor").toLatin1().data();
    raw_device->device_entry.product = url_query.queryItemValue("product").toLatin1().data();
    raw_device->device_entry.vendor_id = url_query.queryItemValue("vendor_id").toUShort();
    raw_device->device_entry.product_id = url_query.queryItemValue("product_id").toUShort();
    raw_device->device_entry.device_flags = url_query.queryItemValue("quirks").toUInt();

    raw_device->bus_location = bus_location;
    raw_device->devnum = device_num;

    device_ = LIBMTP_Open_Raw_Device(raw_device);
    return;
  }

  // Get a list of devices from libmtp and figure out which one is ours
  int count = 0;
  LIBMTP_raw_device_t *raw_devices = nullptr;
  LIBMTP_error_number_t err = LIBMTP_Detect_Raw_Devices(&raw_devices, &count);
  if (err != LIBMTP_ERROR_NONE) {
    qLog(Warning) << "MTP error:" << err;
    return;
  }

  LIBMTP_raw_device_t *raw_device = nullptr;
  for (int i = 0; i < count; ++i) {
    if (raw_devices[i].bus_location == bus_location && raw_devices[i].devnum == device_num) {
      raw_device = &raw_devices[i];
      break;
    }
  }

  if (!raw_device) {
    qLog(Warning) << "MTP device not found";
    free(raw_devices);
    return;
  }

  // Connect to the device
  device_ = LIBMTP_Open_Raw_Device(raw_device);

  free(raw_devices);

}

MtpConnection::~MtpConnection() {
  if (device_) LIBMTP_Release_Device(device_);
}

bool MtpConnection::GetSupportedFiletypes(QList<Song::FileType> *ret) {

  if (!device_) return false;

  uint16_t *list = nullptr;
  uint16_t length = 0;

  if (LIBMTP_Get_Supported_Filetypes(device_, &list, &length) || !list || !length)
    return false;

  for (int i = 0; i < length; ++i) {
    switch (LIBMTP_filetype_t(list[i])) {
      case LIBMTP_FILETYPE_WAV:  *ret << Song::FileType_WAV; break;
      case LIBMTP_FILETYPE_MP2:
      case LIBMTP_FILETYPE_MP3:  *ret << Song::FileType_MPEG; break;
      case LIBMTP_FILETYPE_WMA:  *ret << Song::FileType_ASF; break;
      case LIBMTP_FILETYPE_MP4:
      case LIBMTP_FILETYPE_M4A:
      case LIBMTP_FILETYPE_AAC:  *ret << Song::FileType_MP4; break;
      case LIBMTP_FILETYPE_FLAC:
        *ret << Song::FileType_FLAC;
        *ret << Song::FileType_OggFlac;
        break;
      case LIBMTP_FILETYPE_OGG:
        *ret << Song::FileType_OggVorbis;
        *ret << Song::FileType_OggSpeex;
        *ret << Song::FileType_OggFlac;
        break;
      default:
        qLog(Error) << "Unknown MTP file format" << LIBMTP_Get_Filetype_Description(LIBMTP_filetype_t(list[i]));
        break;
    }
  }

  free(list);
  return true;

}


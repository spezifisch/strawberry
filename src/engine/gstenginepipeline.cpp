/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2018, Jonas Kvinge <jonas@jkvinge.net>
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
#include <cstring>
#include <glib.h>
#include <glib-object.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/pbutils/pbutils.h>

#include <QtGlobal>
#include <QObject>
#include <QCoreApplication>
#include <QMutex>
#include <QByteArray>
#include <QList>
#include <QVariant>
#include <QString>
#include <QUrl>
#include <QTimeLine>
#include <QEasingCurve>
#include <QMetaObject>
#include <QUuid>
#include <QtDebug>

#include "core/concurrentrun.h"
#include "core/logging.h"
#include "core/signalchecker.h"
#include "core/timeconstants.h"
#include "core/song.h"
#include "enginebase.h"
#include "gstengine.h"
#include "gstenginepipeline.h"
#include "gstbufferconsumer.h"
#include "gstelementdeleter.h"

const int GstEnginePipeline::kGstStateTimeoutNanosecs = 10000000;
const int GstEnginePipeline::kFaderFudgeMsec = 2000;
const int GstEnginePipeline::kDiscoveryTimeoutS = 10;

const int GstEnginePipeline::kEqBandCount = 10;
const int GstEnginePipeline::kEqBandFrequencies[] = { 60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000 };

int GstEnginePipeline::sId = 1;
GstElementDeleter *GstEnginePipeline::sElementDeleter = nullptr;

GstEnginePipeline::GstEnginePipeline(GstEngine *engine)
    : QObject(nullptr),
      engine_(engine),
      id_(sId++),
      valid_(false),
      volume_enabled_(true),
      stereo_balancer_enabled_(false),
      eq_enabled_(false),
      rg_enabled_(false),
      stereo_balance_(0.0f),
      eq_preamp_(0),
      rg_mode_(0),
      rg_preamp_(0.0),
      rg_compression_(true),
      buffer_duration_nanosec_(1 * kNsecPerSec),
      buffer_min_fill_(33),
      buffering_(false),
      segment_start_(0),
      segment_start_received_(false),
      end_offset_nanosec_(-1),
      next_beginning_offset_nanosec_(-1),
      next_end_offset_nanosec_(-1),
      ignore_next_seek_(false),
      ignore_tags_(false),
      pipeline_is_initialised_(false),
      pipeline_is_connected_(false),
      pending_seek_nanosec_(-1),
      last_known_position_ns_(0),
      next_uri_set_(false),
      volume_percent_(100),
      volume_modifier_(1.0f),
      use_fudge_timer_(false),
      pipeline_(nullptr),
      audiobin_(nullptr),
      audioqueue_(nullptr),
      volume_(nullptr),
      audiopanorama_(nullptr),
      equalizer_(nullptr),
      equalizer_preamp_(nullptr),
      discoverer_(nullptr),
      pad_added_cb_id_(-1),
      notify_source_cb_id_(-1),
      about_to_finish_cb_id_(-1),
      bus_cb_id_(-1),
      discovery_finished_cb_id_(-1),
      discovery_discovered_cb_id_(-1),
      unsupported_analyzer_(false)
      {

  if (!sElementDeleter) {
    sElementDeleter = new GstElementDeleter(engine_);
  }

  for (int i = 0; i < kEqBandCount; ++i) eq_band_gains_ << 0;

}

GstEnginePipeline::~GstEnginePipeline() {

  if (discoverer_) {

    if (discovery_discovered_cb_id_ != -1)
      g_signal_handler_disconnect(G_OBJECT(discoverer_), discovery_discovered_cb_id_);
    if (discovery_finished_cb_id_ != -1)
      g_signal_handler_disconnect(G_OBJECT(discoverer_), discovery_finished_cb_id_);

    g_object_unref(discoverer_);
  }

  if (pipeline_) {

    if (pad_added_cb_id_ != -1)
      g_signal_handler_disconnect(G_OBJECT(pipeline_), pad_added_cb_id_);

    if (notify_source_cb_id_ != -1)
      g_signal_handler_disconnect(G_OBJECT(pipeline_), notify_source_cb_id_);

    if (about_to_finish_cb_id_ != -1)
      g_signal_handler_disconnect(G_OBJECT(pipeline_), about_to_finish_cb_id_);

    gst_bus_set_sync_handler(gst_pipeline_get_bus(GST_PIPELINE(pipeline_)), nullptr, nullptr, nullptr);

    if (bus_cb_id_ != -1)
      g_source_remove(bus_cb_id_);

    gst_element_set_state(pipeline_, GST_STATE_NULL);

    gst_object_unref(GST_OBJECT(pipeline_));
  }

}

void GstEnginePipeline::set_output_device(const QString &output, const QVariant &device) {

  output_ = output;
  device_ = device;

}

void GstEnginePipeline::set_volume_enabled(const bool enabled) {
  volume_enabled_ = enabled;
}

void GstEnginePipeline::set_stereo_balancer_enabled(const bool enabled) {
  stereo_balancer_enabled_ = enabled;
  if (!enabled) stereo_balance_ = 0.0f;
  if (pipeline_) UpdateStereoBalance();
}

void GstEnginePipeline::set_equalizer_enabled(const bool enabled) {
  eq_enabled_ = enabled;
  if (pipeline_) UpdateEqualizer();
}

void GstEnginePipeline::set_replaygain(const bool enabled, const int mode, const float preamp, const bool compression) {

  rg_enabled_ = enabled;
  rg_mode_ = mode;
  rg_preamp_ = preamp;
  rg_compression_ = compression;

}

void GstEnginePipeline::set_buffer_duration_nanosec(const qint64 buffer_duration_nanosec) {
  buffer_duration_nanosec_ = buffer_duration_nanosec;
}

void GstEnginePipeline::set_buffer_min_fill(int percent) {
  buffer_min_fill_ = percent;
}

bool GstEnginePipeline::InitFromUrl(const QByteArray &stream_url, const QUrl original_url, const qint64 end_nanosec) {

  stream_url_ = stream_url;
  original_url_ = original_url;
  end_offset_nanosec_ = end_nanosec;

  pipeline_ = engine_->CreateElement("playbin");
  if (!pipeline_) return false;

  g_object_set(G_OBJECT(pipeline_), "uri", stream_url.constData(), nullptr);

  gint flags;
  g_object_get(G_OBJECT(pipeline_), "flags", &flags, nullptr);
  flags |= 0x00000002;
  flags &= ~0x00000001;
  g_object_set(G_OBJECT(pipeline_), "flags", flags, nullptr);

  pad_added_cb_id_ = CHECKED_GCONNECT(G_OBJECT(pipeline_), "pad-added", &NewPadCallback, this);
  notify_source_cb_id_ = CHECKED_GCONNECT(G_OBJECT(pipeline_), "notify::source", &SourceSetupCallback, this);
  about_to_finish_cb_id_ = CHECKED_GCONNECT(G_OBJECT(pipeline_), "about-to-finish", &AboutToFinishCallback, this);

  // Setting up a discoverer
  discoverer_ = gst_discoverer_new(kDiscoveryTimeoutS * GST_SECOND, nullptr);
  if (discoverer_) {
    discovery_discovered_cb_id_ = CHECKED_GCONNECT(G_OBJECT(discoverer_), "discovered", &StreamDiscovered, this);
    discovery_finished_cb_id_ = CHECKED_GCONNECT(G_OBJECT(discoverer_), "finished", &StreamDiscoveryFinished, this);
    gst_discoverer_start(discoverer_);
  }

  if (!InitAudioBin()) return false;

  // Set playbin's sink to be our custom audio-sink.
  g_object_set(GST_OBJECT(pipeline_), "audio-sink", audiobin_, nullptr);
  pipeline_is_connected_ = true;

  return true;

}

bool GstEnginePipeline::InitAudioBin() {

  gst_segment_init(&last_playbin_segment_, GST_FORMAT_TIME);

  // Audio bin
  audiobin_ = gst_bin_new("audiobin");
  if (!audiobin_) return false;

  // Create the sink
  GstElement *audiosink = engine_->CreateElement(output_, audiobin_);
  if (!audiosink) {
    gst_object_unref(GST_OBJECT(audiobin_));
    return false;
  }

  if (device_.isValid() && g_object_class_find_property(G_OBJECT_GET_CLASS(audiosink), "device")) {
    switch (device_.type()) {
      case QVariant::String:
        if (device_.toString().isEmpty()) break;
        g_object_set(G_OBJECT(audiosink), "device", device_.toString().toUtf8().constData(), nullptr);
        break;
      case QVariant::ByteArray:
        g_object_set(G_OBJECT(audiosink), "device", device_.toByteArray().constData(), nullptr);
        break;
      case QVariant::LongLong:
        g_object_set(G_OBJECT(audiosink), "device", device_.toLongLong(), nullptr);
        break;
      case QVariant::Int:
        g_object_set(G_OBJECT(audiosink), "device", device_.toInt(), nullptr);
        break;
      case QVariant::Uuid:
        g_object_set(G_OBJECT(audiosink), "device", device_.toUuid(), nullptr);
        break;
      default:
        qLog(Warning) << "Unknown device type" << device_;
        break;
    }
  }

  // Create all the other elements

  audioqueue_ = engine_->CreateElement("queue2", audiobin_);
  GstElement *audioconverter = engine_->CreateElement("audioconvert", audiobin_);

  if (!audioqueue_ || !audioconverter) {
    gst_object_unref(GST_OBJECT(audiobin_));
    audiobin_ = nullptr;
    return false;
  }

  // Create the volume elements if it's enabled.
  if (volume_enabled_) {
    volume_ = engine_->CreateElement("volume", audiobin_);
  }

  // Create the stereo balancer elements if it's enabled.
  if (stereo_balancer_enabled_) {
    audiopanorama_ = engine_->CreateElement("audiopanorama", audiobin_, false);
    // Set the stereo balance.
    if (audiopanorama_) g_object_set(G_OBJECT(audiopanorama_), "panorama", stereo_balance_, nullptr);
  }

  // Create the equalizer elements if it's enabled.
  if (eq_enabled_) {
    equalizer_preamp_ = engine_->CreateElement("volume", audiobin_, false);
    equalizer_ = engine_->CreateElement("equalizer-nbands", audiobin_, false);
    // Setting the equalizer bands:
    //
    // GStreamer's GstIirEqualizerNBands sets up shelve filters for the first and last bands as corner cases.
    // That was causing the "inverted slider" bug.
    // As a workaround, we create two dummy bands at both ends of the spectrum.
    // This causes the actual first and last adjustable bands to be implemented using band-pass filters.

    if (equalizer_) {
      g_object_set(G_OBJECT(equalizer_), "num-bands", 10 + 2, nullptr);

      // Dummy first band (bandwidth 0, cutting below 20Hz):
      GstObject *first_band = GST_OBJECT(gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(equalizer_), 0));
      g_object_set(G_OBJECT(first_band), "freq", 20.0, "bandwidth", 0, "gain", 0.0f, nullptr);
      g_object_unref(G_OBJECT(first_band));

      // Dummy last band (bandwidth 0, cutting over 20KHz):
      GstObject *last_band = GST_OBJECT(gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(equalizer_), kEqBandCount + 1));
      g_object_set(G_OBJECT(last_band), "freq", 20000.0, "bandwidth", 0, "gain", 0.0f, nullptr);
      g_object_unref(G_OBJECT(last_band));

      int last_band_frequency = 0;
      for (int i = 0; i < kEqBandCount; ++i) {
        const int index_in_eq = i + 1;
        GstObject *band = GST_OBJECT(gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(equalizer_), index_in_eq));

        const float frequency = kEqBandFrequencies[i];
        const float bandwidth = frequency - last_band_frequency;
        last_band_frequency = frequency;

        g_object_set(G_OBJECT(band), "freq", frequency, "bandwidth", bandwidth, "gain", 0.0f, nullptr);
        g_object_unref(G_OBJECT(band));
      }
    }
  }

  // Create the replaygain elements if it's enabled.
  GstElement *eventprobe = audioqueue_;
  GstElement *rgvolume = nullptr;
  GstElement *rglimiter = nullptr;
  GstElement *rgconverter = nullptr;
  if (rg_enabled_) {
    rgvolume = engine_->CreateElement("rgvolume", audiobin_, false);
    rglimiter = engine_->CreateElement("rglimiter", audiobin_, false);
    rgconverter = engine_->CreateElement("audioconvert", audiobin_, false);
    if (rgvolume && rglimiter && rgconverter) {
      eventprobe = rgconverter;
      // Set replaygain settings
      g_object_set(G_OBJECT(rgvolume), "album-mode", rg_mode_, nullptr);
      g_object_set(G_OBJECT(rgvolume), "pre-amp", double(rg_preamp_), nullptr);
      g_object_set(G_OBJECT(rglimiter), "enabled", int(rg_compression_), nullptr);
    }
  }

  // Create a pad on the outside of the audiobin and connect it to the pad of the first element.
  GstPad *pad = gst_element_get_static_pad(audioqueue_, "sink");
  gst_element_add_pad(audiobin_, gst_ghost_pad_new("sink", pad));
  gst_object_unref(pad);

  // Add a data probe on the src pad of the audioconvert element for our scope.
  // We do it here because we want pre-equalized and pre-volume samples so that our visualization are not be affected by them.
  pad = gst_element_get_static_pad(eventprobe, "src");
  gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM, &EventHandoffCallback, this, nullptr);
  gst_object_unref(pad);

  // Set the buffer duration.
  // We set this on this queue instead of the playbin because setting it on the playbin only affects network sources.
  // Disable the default buffer and byte limits, so we only buffer based on time.

  g_object_set(G_OBJECT(audioqueue_), "max-size-buffers", 0, nullptr);
  g_object_set(G_OBJECT(audioqueue_), "max-size-bytes", 0, nullptr);
  g_object_set(G_OBJECT(audioqueue_), "max-size-time", buffer_duration_nanosec_, nullptr);
  g_object_set(G_OBJECT(audioqueue_), "low-percent", buffer_min_fill_, nullptr);
  if (buffer_duration_nanosec_ > 0) {
    g_object_set(G_OBJECT(audioqueue_), "use-buffering", true, nullptr);
  }

  // Link all elements

  GstElement *next = audioqueue_; // The next element to link from.

  // Link replaygain elements if enabled.
  if (rg_enabled_ && rgvolume && rglimiter && rgconverter) {
    gst_element_link_many(next, rgvolume, rglimiter, rgconverter, nullptr);
    next = rgconverter;
  }

  // Link equalizer elements if enabled.
  if (eq_enabled_ && equalizer_ && equalizer_preamp_) {
    gst_element_link_many(next, equalizer_preamp_, equalizer_, nullptr);
    next = equalizer_;
  }

  // Link stereo balancer elements if enabled.
  if (stereo_balancer_enabled_ && audiopanorama_) {
    gst_element_link(next, audiopanorama_);
    next = audiopanorama_;
  }

  // Link volume elements if enabled.
  if (volume_enabled_ && volume_) {
    gst_element_link(next, volume_);
    next = volume_;
  }

  gst_element_link(next, audioconverter);

  GstCaps *caps = gst_caps_new_empty_simple("audio/x-raw");
  gst_element_link_filtered(audioconverter, audiosink, caps);
  gst_caps_unref(caps);

  // Add probes and handlers.
  pad = gst_element_get_static_pad(audioqueue_, "src");
  gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, HandoffCallback, this, nullptr);
  gst_object_unref(pad);

  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline_));
  gst_bus_set_sync_handler(bus, BusCallbackSync, this, nullptr);
  bus_cb_id_ = gst_bus_add_watch(bus, BusCallback, this);
  gst_object_unref(bus);

  // Add request to discover the stream
  if (discoverer_) {
    if (!gst_discoverer_discover_uri_async(discoverer_, stream_url_.toStdString().c_str())) {
      qLog(Error) << "Failed to start stream discovery for" << stream_url_;
    }
  }

  unsupported_analyzer_ = false;

  return true;

}

GstPadProbeReturn GstEnginePipeline::EventHandoffCallback(GstPad*, GstPadProbeInfo *info, gpointer self) {

  GstEnginePipeline *instance = reinterpret_cast<GstEnginePipeline*>(self);

  GstEvent *e = gst_pad_probe_info_get_event(info);

  qLog(Debug) << instance->id() << "event" << GST_EVENT_TYPE_NAME(e);

  switch (GST_EVENT_TYPE(e)) {
    case GST_EVENT_SEGMENT:
      if (!instance->segment_start_received_) {
        // The segment start time is used to calculate the proper offset of data buffers from the start of the stream
        const GstSegment *segment = nullptr;
        gst_event_parse_segment(e, &segment);
        instance->segment_start_ = segment->start;
        instance->segment_start_received_ = true;
      }
      break;

    default:
      break;
  }

  return GST_PAD_PROBE_OK;

}

void GstEnginePipeline::SourceSetupCallback(GstPlayBin *bin, GParamSpec *, gpointer self) {

  GstEnginePipeline *instance = reinterpret_cast<GstEnginePipeline*>(self);

  GstElement *element = nullptr;
  g_object_get(bin, "source", &element, nullptr);
  if (!element) {
    return;
  }

  if (g_object_class_find_property(G_OBJECT_GET_CLASS(element), "device") && !instance->source_device().isEmpty()) {
    // Gstreamer is not able to handle device in URL (referring to Gstreamer documentation, this might be added in the future).
    // Despite that, for now we include device inside URL: we decompose it during Init and set device here, when this callback is called.
    g_object_set(element, "device", instance->source_device().toLocal8Bit().constData(), nullptr);
  }

  if (g_object_class_find_property(G_OBJECT_GET_CLASS(element), "user-agent")) {
    QString user_agent = QString("%1 %2").arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion());
    g_object_set(element, "user-agent", user_agent.toUtf8().constData(), nullptr);
    g_object_set(element, "ssl-strict", FALSE, nullptr);
  }

  // If the pipeline was buffering we stop that now.
  if (instance->buffering_) {
    instance->buffering_ = false;
    emit instance->BufferingFinished();
    instance->SetState(GST_STATE_PLAYING);
  }

  g_object_unref(element);

}

void GstEnginePipeline::NewPadCallback(GstElement*, GstPad *pad, gpointer self) {

  GstEnginePipeline *instance = reinterpret_cast<GstEnginePipeline*>(self);

  GstPad *const audiopad = gst_element_get_static_pad(instance->audiobin_, "sink");

  // Link playbin's sink pad to audiobin's src pad.
  if (GST_PAD_IS_LINKED(audiopad)) {
    qLog(Warning) << instance->id() << "audiopad is already linked, unlinking old pad";
    gst_pad_unlink(audiopad, GST_PAD_PEER(audiopad));
  }

  gst_pad_link(pad, audiopad);
  gst_object_unref(audiopad);

  // Offset the timestamps on all the buffers coming out of the playbin so they line up exactly with the end of the last buffer from the old playbin.
  // "Running time" is the time since the last flushing seek.
  GstClockTime running_time = gst_segment_to_running_time(&instance->last_playbin_segment_, GST_FORMAT_TIME, instance->last_playbin_segment_.position);
  gst_pad_set_offset(pad, running_time);

  // Add a probe to the pad so we can update last_playbin_segment_.
  gst_pad_add_probe(pad, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_EVENT_FLUSH), PlaybinProbe, instance, nullptr);

  instance->pipeline_is_connected_ = true;
  if (instance->pending_seek_nanosec_ != -1 && instance->pipeline_is_initialised_) {
    QMetaObject::invokeMethod(instance, "Seek", Qt::QueuedConnection, Q_ARG(qint64, instance->pending_seek_nanosec_));
  }

}

GstPadProbeReturn GstEnginePipeline::PlaybinProbe(GstPad *pad, GstPadProbeInfo *info, gpointer data) {

  GstEnginePipeline *instance = reinterpret_cast<GstEnginePipeline*>(data);

  const GstPadProbeType info_type = GST_PAD_PROBE_INFO_TYPE(info);

  if (info_type & GST_PAD_PROBE_TYPE_BUFFER) {
    // The playbin produced a buffer.  Record its end time, so we can offset the buffers produced by the next playbin when transitioning to the next song.
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);

    GstClockTime timestamp = GST_BUFFER_TIMESTAMP(buffer);
    GstClockTime duration = GST_BUFFER_DURATION(buffer);
    if (timestamp == GST_CLOCK_TIME_NONE) {
      timestamp = instance->last_playbin_segment_.position;
    }

    if (duration != GST_CLOCK_TIME_NONE) {
      timestamp += duration;
    }

    instance->last_playbin_segment_.position = timestamp;
  }
  else if (info_type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
    GstEventType event_type = GST_EVENT_TYPE(event);

    if (event_type == GST_EVENT_SEGMENT) {
      // A new segment started, we need to save this to calculate running time offsets later.
      gst_event_copy_segment(event, &instance->last_playbin_segment_);
    }
    else if (event_type == GST_EVENT_FLUSH_START) {
      // A flushing seek resets the running time to 0, so remove any offset we set on this pad before.
      gst_pad_set_offset(pad, 0);
    }
  }

  return GST_PAD_PROBE_OK;

}

GstPadProbeReturn GstEnginePipeline::HandoffCallback(GstPad *pad, GstPadProbeInfo *info, gpointer self) {

  GstEnginePipeline *instance = reinterpret_cast<GstEnginePipeline*>(self);

  GstCaps *caps = gst_pad_get_current_caps(pad);
  GstStructure *structure = gst_caps_get_structure(caps, 0);
  QString format = QString(gst_structure_get_string(structure, "format"));
  int channels = 0;
  int rate = 0;
  gst_structure_get_int(structure, "channels", &channels);
  gst_structure_get_int(structure, "rate", &rate);

  GstBuffer *buf = gst_pad_probe_info_get_buffer(info);
  GstBuffer *buf16 = nullptr;

  if (format.startsWith("S16LE")) {
    instance->unsupported_analyzer_ = false;
  }
  else if (format.startsWith("S32LE")) {

    GstMapInfo map_info;
    gst_buffer_map(buf, &map_info, GST_MAP_READ);

    int32_t *s = reinterpret_cast<int32_t*>(map_info.data);
    int samples = (map_info.size / sizeof(int32_t)) / channels;
    int buf16_size = samples * sizeof(int16_t) * channels;
    int16_t *d = static_cast<int16_t*>(g_malloc(buf16_size));
    memset(d, 0, buf16_size);
    for (int i = 0 ; i < (samples * channels) ; ++i) {
      d[i] = static_cast<int16_t>((s[i] >> 16));
    }
    gst_buffer_unmap(buf, &map_info);
    buf16 = gst_buffer_new_wrapped(d, buf16_size);
    GST_BUFFER_DURATION(buf16) = GST_FRAMES_TO_CLOCK_TIME(samples * sizeof(int16_t) * channels, rate);
    buf = buf16;

    instance->unsupported_analyzer_ = false;
  }

  else if (format.startsWith("F32LE")) {

    GstMapInfo map_info;
    gst_buffer_map(buf, &map_info, GST_MAP_READ);

    float *s = reinterpret_cast<float*>(map_info.data);
    int samples = (map_info.size / sizeof(float)) / channels;
    int buf16_size = samples * sizeof(int16_t) * channels;
    int16_t *d = static_cast<int16_t*>(g_malloc(buf16_size));
    memset(d, 0, buf16_size);
    for (int i = 0 ; i < (samples * channels) ; ++i) {
      float sample_float = (s[i] * 32768.0);
      d[i] = static_cast<int16_t>(sample_float);
    }
    gst_buffer_unmap(buf, &map_info);
    buf16 = gst_buffer_new_wrapped(d, buf16_size);
    GST_BUFFER_DURATION(buf16) = GST_FRAMES_TO_CLOCK_TIME(samples * sizeof(int16_t) * channels, rate);
    buf = buf16;

    instance->unsupported_analyzer_ = false;
  }
  else if (format.startsWith("S24LE")) {

    GstMapInfo map_info;
    gst_buffer_map(buf, &map_info, GST_MAP_READ);

    char *s24 = reinterpret_cast<char*>(map_info.data);
    char *s24e = s24 + map_info.size;
    int samples = (map_info.size / sizeof(char)) / channels;
    int buf16_size = samples * sizeof(int16_t) * channels;
    int16_t *s16 = static_cast<int16_t*>(g_malloc(buf16_size));
    memset(s16, 0, buf16_size);
    for (int i = 0 ; i < (samples * channels) ; ++i) {
      s16[i] = *(reinterpret_cast<int16_t*>(s24+1));
      s24 += 3;
      if (s24 >= s24e) break;
    }
    gst_buffer_unmap(buf, &map_info);
    buf16 = gst_buffer_new_wrapped(s16, buf16_size);
    GST_BUFFER_DURATION(buf16) = GST_FRAMES_TO_CLOCK_TIME(samples * sizeof(int16_t) * channels, rate);
    buf = buf16;

    instance->unsupported_analyzer_ = false;
  }
  else if (!instance->unsupported_analyzer_) {
    instance->unsupported_analyzer_ = true;
    qLog(Debug) << "Unsupported audio format for the analyzer" << format;
  }

  QList<GstBufferConsumer*> consumers;
  {
    QMutexLocker l(&instance->buffer_consumers_mutex_);
    consumers = instance->buffer_consumers_;
  }

  for (GstBufferConsumer *consumer : consumers) {
    gst_buffer_ref(buf);
    consumer->ConsumeBuffer(buf, instance->id(), format);
  }

  if (buf16) {
    gst_buffer_unref(buf16);
  }

  // Calculate the end time of this buffer so we can stop playback if it's after the end time of this song.
  if (instance->end_offset_nanosec_ > 0) {
    quint64 start_time = GST_BUFFER_TIMESTAMP(buf) - instance->segment_start_;
    quint64 duration = GST_BUFFER_DURATION(buf);
    qint64 end_time = start_time + duration;

    if (end_time > instance->end_offset_nanosec_) {
      if (instance->has_next_valid_url() && instance->next_stream_url_ == instance->stream_url_ && instance->next_beginning_offset_nanosec_ == instance->end_offset_nanosec_) {
          // The "next" song is actually the next segment of this file - so cheat and keep on playing, but just tell the Engine we've moved on.
          instance->end_offset_nanosec_ = instance->next_end_offset_nanosec_;
          instance->next_stream_url_.clear();
          instance->next_original_url_.clear();
          instance->next_beginning_offset_nanosec_ = 0;
          instance->next_end_offset_nanosec_ = 0;

          // GstEngine will try to seek to the start of the new section, but we're already there so ignore it.
          instance->ignore_next_seek_ = true;
          emit instance->EndOfStreamReached(instance->id(), true);
      }
      else {
        // There's no next song
        emit instance->EndOfStreamReached(instance->id(), false);
      }
    }
  }

  return GST_PAD_PROBE_OK;

}

void GstEnginePipeline::AboutToFinishCallback(GstPlayBin*, gpointer self) {

  GstEnginePipeline *instance = reinterpret_cast<GstEnginePipeline*>(self);

  if (instance->has_next_valid_url() && !instance->next_uri_set_) {
    // Set the next uri. When the current song ends it will be played automatically and a STREAM_START message is send to the bus.
    // When the next uri is not playable an error message is send when the pipeline goes to PLAY (or PAUSE) state or immediately if it is currently in PLAY state.
    instance->next_uri_set_ = true;
    g_object_set(G_OBJECT(instance->pipeline_), "uri", instance->next_stream_url_.constData(), nullptr);
  }

}

gboolean GstEnginePipeline::BusCallback(GstBus*, GstMessage *msg, gpointer self) {

  GstEnginePipeline *instance = reinterpret_cast<GstEnginePipeline*>(self);

  qLog(Debug) << instance->id() << "bus message" << GST_MESSAGE_TYPE_NAME(msg);

  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR:
      instance->ErrorMessageReceived(msg);
      break;

    case GST_MESSAGE_TAG:
      instance->TagMessageReceived(msg);
      break;

    case GST_MESSAGE_STATE_CHANGED:
      instance->StateChangedMessageReceived(msg);
      break;

    default:
      break;
  }

  return FALSE;

}

GstBusSyncReply GstEnginePipeline::BusCallbackSync(GstBus *, GstMessage *msg, gpointer self) {

  GstEnginePipeline *instance = reinterpret_cast<GstEnginePipeline*>(self);

  qLog(Debug) << instance->id() << "sync bus message" << GST_MESSAGE_TYPE_NAME(msg);

  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
      emit instance->EndOfStreamReached(instance->id(), false);
      break;

    case GST_MESSAGE_TAG:
      instance->TagMessageReceived(msg);
      break;

    case GST_MESSAGE_ERROR:
      instance->ErrorMessageReceived(msg);
      break;

    case GST_MESSAGE_ELEMENT:
      instance->ElementMessageReceived(msg);
      break;

    case GST_MESSAGE_STATE_CHANGED:
      instance->StateChangedMessageReceived(msg);
      break;

    case GST_MESSAGE_BUFFERING:
      instance->BufferingMessageReceived(msg);
      break;

    case GST_MESSAGE_STREAM_STATUS:
      instance->StreamStatusMessageReceived(msg);
      break;

    case GST_MESSAGE_STREAM_START:
      instance->StreamStartMessageReceived();
      break;

    default:
      break;
  }

  return GST_BUS_PASS;

}

void GstEnginePipeline::StreamStatusMessageReceived(GstMessage *msg) {

  GstStreamStatusType type;
  GstElement *owner;
  gst_message_parse_stream_status(msg, &type, &owner);

  if (type == GST_STREAM_STATUS_TYPE_CREATE) {
    const GValue *val = gst_message_get_stream_status_object(msg);
    if (G_VALUE_TYPE(val) == GST_TYPE_TASK) {
      GstTask *task = static_cast<GstTask*>(g_value_get_object(val));
      gst_task_set_enter_callback(task, &TaskEnterCallback, this, nullptr);
    }
  }

}

void GstEnginePipeline::StreamStartMessageReceived() {

  if (next_uri_set_) {
    next_uri_set_ = false;

    stream_url_ = next_stream_url_;
    original_url_ = next_original_url_;
    end_offset_nanosec_ = next_end_offset_nanosec_;
    next_stream_url_.clear();
    next_original_url_.clear();
    next_beginning_offset_nanosec_ = 0;
    next_end_offset_nanosec_ = 0;

    emit EndOfStreamReached(id(), true);
  }

}

void GstEnginePipeline::TaskEnterCallback(GstTask *, GThread *, gpointer) {

  // Bump the priority of the thread only on OS X

#ifdef Q_OS_MACOS
  sched_param param;
  memset(&param, 0, sizeof(param));

  param.sched_priority = 99;
  pthread_setschedparam(pthread_self(), SCHED_RR, &param);
#endif

}

void GstEnginePipeline::ElementMessageReceived(GstMessage *msg) {

  const GstStructure *structure = gst_message_get_structure(msg);

  if (gst_structure_has_name(structure, "redirect")) {
    const char *uri = gst_structure_get_string(structure, "new-location");

    // Set the redirect URL.  In mmssrc redirect messages come during the initial state change to PLAYING, so callers can pick up this URL after the state change has failed.
    redirect_url_ = uri;
  }

}

void GstEnginePipeline::ErrorMessageReceived(GstMessage *msg) {

  GError *error = nullptr;
  gchar *debugs = nullptr;

  gst_message_parse_error(msg, &error, &debugs);
  QString message = QString::fromLocal8Bit(error->message);
  QString debugstr = QString::fromLocal8Bit(debugs);
  int domain = error->domain;
  int code = error->code;
  g_error_free(error);
  g_free(debugs);

  if (state() == GST_STATE_PLAYING && pipeline_is_initialised_ && next_uri_set_ && (domain == static_cast<int>(GST_RESOURCE_ERROR) || domain == static_cast<int>(GST_STREAM_ERROR))) {
    // A track is still playing and the next uri is not playable. We ignore the error here so it can play until the end.
    // But there is no message send to the bus when the current track finishes, we have to add an EOS ourself.
    qLog(Debug) << "Ignoring error when loading next track";
    GstPad *sinkpad = gst_element_get_static_pad(audiobin_, "sink");
    gst_pad_send_event(sinkpad, gst_event_new_eos());
    gst_object_unref(sinkpad);
    return;
  }

  if (!redirect_url_.isEmpty() && debugstr.contains("A redirect message was posted on the bus and should have been handled by the application.")) {
    // mmssrc posts a message on the bus *and* makes an error message when it wants to do a redirect.
    // We handle the message, but now we have to ignore the error too.
    return;
  }

  qLog(Error) << __FUNCTION__ << id() << debugstr;

  emit Error(id(), message, domain, code);

}

void GstEnginePipeline::TagMessageReceived(GstMessage *msg) {

  if (ignore_tags_) return;

  GstTagList *taglist = nullptr;
  gst_message_parse_tag(msg, &taglist);

  Engine::SimpleMetaBundle bundle;
  bundle.url = original_url_;
  bundle.title = ParseStrTag(taglist, GST_TAG_TITLE);
  bundle.artist = ParseStrTag(taglist, GST_TAG_ARTIST);
  bundle.comment = ParseStrTag(taglist, GST_TAG_COMMENT);
  bundle.album = ParseStrTag(taglist, GST_TAG_ALBUM);
  bundle.bitrate = ParseUIntTag(taglist, GST_TAG_BITRATE) / 1000;
  bundle.lyrics = ParseStrTag(taglist, GST_TAG_LYRICS);

  if (!bundle.title.isEmpty() && bundle.artist.isEmpty() && bundle.album.isEmpty()) {
    if (bundle.title.contains(" - ")) {
      QStringList title_splitted = bundle.title.split(" - ");
      bundle.artist = title_splitted.first().trimmed();
      bundle.title = title_splitted.last().trimmed();
    }
    else if (bundle.title.contains('~') && bundle.title.count('~') >= 2) {
      QStringList title_splitted = bundle.title.split('~');
      int i = 1;
      for (const QString &part : title_splitted) {
        switch (i) {
          case 1:
            bundle.artist = part;
            break;
          case 2:
            bundle.title = part;
            break;
          case 3:
            bundle.album = part;
            break;
          default:
            break;
        }
        ++i;
      }
    }
  }

  gst_tag_list_free(taglist);

  emit MetadataFound(id(), bundle);

}

QString GstEnginePipeline::ParseStrTag(GstTagList *list, const char *tag) const {

  gchar *data = nullptr;
  bool success = gst_tag_list_get_string(list, tag, &data);

  QString ret;
  if (success && data) {
    ret = QString::fromUtf8(data);
    g_free(data);
  }
  return ret.trimmed();

}

guint GstEnginePipeline::ParseUIntTag(GstTagList *list, const char *tag) const {

  guint data;
  bool success = gst_tag_list_get_uint(list, tag, &data);

  guint ret = 0;
  if (success && data) ret = data;
  return ret;

}

void GstEnginePipeline::StateChangedMessageReceived(GstMessage *msg) {

  if (msg->src != GST_OBJECT(pipeline_)) {
    // We only care about state changes of the whole pipeline.
    return;
  }

  GstState old_state, new_state, pending;
  gst_message_parse_state_changed(msg, &old_state, &new_state, &pending);

  if (!pipeline_is_initialised_ && (new_state == GST_STATE_PAUSED || new_state == GST_STATE_PLAYING)) {
    pipeline_is_initialised_ = true;
    if (pending_seek_nanosec_ != -1 && pipeline_is_connected_) {
      QMetaObject::invokeMethod(this, "Seek", Qt::QueuedConnection, Q_ARG(qint64, pending_seek_nanosec_));
    }
  }

  if (pipeline_is_initialised_ && new_state != GST_STATE_PAUSED && new_state != GST_STATE_PLAYING) {
    pipeline_is_initialised_ = false;

    if (next_uri_set_ && new_state == GST_STATE_READY) {
      // Revert uri and go back to PLAY state again
      next_uri_set_ = false;
      g_object_set(G_OBJECT(pipeline_), "uri", stream_url_.constData(), nullptr);
      SetState(GST_STATE_PLAYING);

      // Add request to discover the stream
      if (discoverer_) {
        if (!gst_discoverer_discover_uri_async(discoverer_, stream_url_.toStdString().c_str())) {
          qLog(Error) << "Failed to start stream discovery for" << stream_url_;
        }
      }

    }
  }

}

void GstEnginePipeline::BufferingMessageReceived(GstMessage *msg) {

  // Only handle buffering messages from the queue2 element in audiobin - not the one that's created automatically by playbin.
  if (GST_ELEMENT(GST_MESSAGE_SRC(msg)) != audioqueue_) {
    return;
  }

  int percent = 0;
  gst_message_parse_buffering(msg, &percent);

  const GstState current_state = state();

  if (percent == 0 && current_state == GST_STATE_PLAYING && !buffering_) {
    buffering_ = true;
    emit BufferingStarted();

    SetState(GST_STATE_PAUSED);
  }
  else if (percent == 100 && buffering_) {
    buffering_ = false;
    emit BufferingFinished();

    SetState(GST_STATE_PLAYING);
  }
  else if (buffering_) {
    emit BufferingProgress(percent);
  }

}

qint64 GstEnginePipeline::position() const {

  if (pipeline_is_initialised_)
    gst_element_query_position(pipeline_, GST_FORMAT_TIME, &last_known_position_ns_);

  return last_known_position_ns_;

}

qint64 GstEnginePipeline::length() const {

  gint64 value = 0;
  gst_element_query_duration(pipeline_, GST_FORMAT_TIME, &value);

  return value;

}

GstState GstEnginePipeline::state() const {

  GstState s, sp;
  if (gst_element_get_state(pipeline_, &s, &sp, kGstStateTimeoutNanosecs) == GST_STATE_CHANGE_FAILURE)
    return GST_STATE_NULL;

  return s;

}

QFuture<GstStateChangeReturn> GstEnginePipeline::SetState(const GstState state) {
  return ConcurrentRun::Run<GstStateChangeReturn, GstElement*, GstState>(&set_state_threadpool_, &gst_element_set_state, pipeline_, state);

}

bool GstEnginePipeline::Seek(const qint64 nanosec) {

  if (ignore_next_seek_) {
    ignore_next_seek_ = false;
    return true;
  }

  if (!pipeline_is_connected_ || !pipeline_is_initialised_) {
    pending_seek_nanosec_ = nanosec;
    return true;
  }

  if (next_uri_set_) {
    pending_seek_nanosec_ = nanosec;
    SetState(GST_STATE_READY);
    return true;
  }

  pending_seek_nanosec_ = -1;
  last_known_position_ns_ = nanosec;
  return gst_element_seek_simple(pipeline_, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, nanosec);

}

void GstEnginePipeline::SetVolume(const int percent) {

  if (!volume_) return;
  volume_percent_ = percent;
  UpdateVolume();

}

void GstEnginePipeline::SetVolumeModifier(const qreal mod) {

  if (!volume_) return;
  volume_modifier_ = mod;
  UpdateVolume();

}

void GstEnginePipeline::UpdateVolume() {

  if (!volume_) return;
  float vol = double(volume_percent_) * 0.01 * volume_modifier_;
  g_object_set(G_OBJECT(volume_), "volume", vol, nullptr);

}

void GstEnginePipeline::SetStereoBalance(const float value) {

  stereo_balance_ = value;
  UpdateStereoBalance();

}

void GstEnginePipeline::UpdateStereoBalance() {

  if (audiopanorama_) {
    g_object_set(G_OBJECT(audiopanorama_), "panorama", stereo_balance_, nullptr);
  }

}

void GstEnginePipeline::SetEqualizerParams(const int preamp, const QList<int>& band_gains) {

  eq_preamp_ = preamp;
  eq_band_gains_ = band_gains;
  UpdateEqualizer();

}

void GstEnginePipeline::UpdateEqualizer() {

 if (!equalizer_ || !equalizer_preamp_) return;

  // Update band gains
  for (int i = 0; i < kEqBandCount; ++i) {
    float gain = eq_enabled_ ? eq_band_gains_[i] : 0.0;
    if (gain < 0)
      gain *= 0.24;
    else
      gain *= 0.12;

    const int index_in_eq = i + 1;
    // Offset because of the first dummy band we created.
    GstObject *band = GST_OBJECT(gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(equalizer_), index_in_eq));
    g_object_set(G_OBJECT(band), "gain", gain, nullptr);
    g_object_unref(G_OBJECT(band));
  }

  // Update preamp
  float preamp = 1.0;
  if (eq_enabled_) preamp = float(eq_preamp_ + 100) * 0.01;  // To scale from 0.0 to 2.0

  g_object_set(G_OBJECT(equalizer_preamp_), "volume", preamp, nullptr);

}

void GstEnginePipeline::StartFader(const qint64 duration_nanosec, const QTimeLine::Direction direction, const QEasingCurve::Type shape, const bool use_fudge_timer) {

  const int duration_msec = duration_nanosec / kNsecPerMsec;

  // If there's already another fader running then start from the same time that one was already at.
  int start_time = direction == QTimeLine::Forward ? 0 : duration_msec;
  if (fader_ && fader_->state() == QTimeLine::Running) {
    if (duration_msec == fader_->duration()) {
      start_time = fader_->currentTime();
    }
    else {
      // Calculate the position in the new fader with the same value from the old fader, so no volume jumps appear
      qreal time = qreal(duration_msec) * (qreal(fader_->currentTime()) / qreal(fader_->duration()));
      start_time = qRound(time);
    }
  }

  fader_.reset(new QTimeLine(duration_msec, this));
  connect(fader_.get(), SIGNAL(valueChanged(qreal)), SLOT(SetVolumeModifier(qreal)));
  connect(fader_.get(), SIGNAL(finished()), SLOT(FaderTimelineFinished()));
  fader_->setDirection(direction);
  fader_->setEasingCurve(shape);
  fader_->setCurrentTime(start_time);
  fader_->resume();

  fader_fudge_timer_.stop();
  use_fudge_timer_ = use_fudge_timer;

  SetVolumeModifier(fader_->currentValue());

}

void GstEnginePipeline::FaderTimelineFinished() {

  fader_.reset();

  // Wait a little while longer before emitting the finished signal (and probably distroying the pipeline) to account for delays in the audio server/driver.
  if (use_fudge_timer_) {
    fader_fudge_timer_.start(kFaderFudgeMsec, this);
  }
  else {
    // Even here we cannot emit the signal directly, as it result in a stutter when resuming playback.
    // So use a quest small time, so you won't notice the difference when resuming playback
    // (You get here when the pause fading is active)
    fader_fudge_timer_.start(250, this);
  }

}

void GstEnginePipeline::timerEvent(QTimerEvent *e) {

  if (e->timerId() == fader_fudge_timer_.timerId()) {
    fader_fudge_timer_.stop();
    emit FaderFinished();
    return;
  }

  QObject::timerEvent(e);

}

void GstEnginePipeline::AddBufferConsumer(GstBufferConsumer *consumer) {
  QMutexLocker l(&buffer_consumers_mutex_);
  buffer_consumers_ << consumer;
}

void GstEnginePipeline::RemoveBufferConsumer(GstBufferConsumer *consumer) {
  QMutexLocker l(&buffer_consumers_mutex_);
  buffer_consumers_.removeAll(consumer);
}

void GstEnginePipeline::RemoveAllBufferConsumers() {
  QMutexLocker l(&buffer_consumers_mutex_);
  buffer_consumers_.clear();
}

void GstEnginePipeline::SetNextUrl(const QByteArray &stream_url, const QUrl &original_url, const qint64 beginning_nanosec, const qint64 end_nanosec) {

  next_stream_url_ = stream_url;
  next_original_url_ = original_url;
  next_beginning_offset_nanosec_ = beginning_nanosec;
  next_end_offset_nanosec_ = end_nanosec;

  // Add request to discover the stream
  if (discoverer_) {
    if (!gst_discoverer_discover_uri_async(discoverer_, next_stream_url_.toStdString().c_str())) {
      qLog(Error) << "Failed to start stream discovery for" << next_stream_url_;
    }
  }

}

void GstEnginePipeline::StreamDiscovered(GstDiscoverer*, GstDiscovererInfo *info, GError*, gpointer self) {

  GstEnginePipeline *instance = reinterpret_cast<GstEnginePipeline*>(self);

  QString discovered_url(gst_discoverer_info_get_uri(info));

  GstDiscovererResult result = gst_discoverer_info_get_result(info);
  if (result != GST_DISCOVERER_OK) {
    QString error_message = GSTdiscovererErrorMessage(result);
    qLog(Error) << QString("Stream discovery for %1 failed: %2").arg(discovered_url).arg(error_message);
    return;
  }

  GList *audio_streams = gst_discoverer_info_get_audio_streams(info);
  if (audio_streams) {

    GstDiscovererStreamInfo *stream_info = reinterpret_cast<GstDiscovererStreamInfo*>(g_list_first(audio_streams)->data);

    Engine::SimpleMetaBundle bundle;
    if (discovered_url == instance->stream_url_) {
      bundle.url = instance->original_url_;
    }
    else if (discovered_url == instance->next_stream_url_) {
      bundle.url = instance->next_original_url_;
    }
    bundle.stream_url = QUrl(discovered_url);
    bundle.samplerate = gst_discoverer_audio_info_get_sample_rate(GST_DISCOVERER_AUDIO_INFO(stream_info));
    bundle.bitdepth = gst_discoverer_audio_info_get_depth(GST_DISCOVERER_AUDIO_INFO(stream_info));
    bundle.bitrate = gst_discoverer_audio_info_get_bitrate(GST_DISCOVERER_AUDIO_INFO(stream_info)) / 1000;

    GstCaps *caps = gst_discoverer_stream_info_get_caps(stream_info);
    gchar *codec_description = gst_pb_utils_get_codec_description(caps);
    QString filetype_description = (codec_description ? QString(codec_description) : QString("Unknown"));
    g_free(codec_description);

    gst_caps_unref(caps);
    gst_discoverer_stream_info_list_free(audio_streams);

    bundle.filetype = Song::FiletypeByDescription(filetype_description);
    qLog(Info) << "Got stream info for" << discovered_url + ":" << filetype_description;

    emit instance->MetadataFound(instance->id(), bundle);

  }
  else {
    qLog(Error) << "Could not detect an audio stream in" << discovered_url;
  }

}

void GstEnginePipeline::StreamDiscoveryFinished(GstDiscoverer*, gpointer) {}

QString GstEnginePipeline::GSTdiscovererErrorMessage(GstDiscovererResult result) {

  switch (result) {
    case GST_DISCOVERER_URI_INVALID:     return "The URI is invalid";
    case GST_DISCOVERER_TIMEOUT:         return "The discovery timed-out";
    case GST_DISCOVERER_BUSY:            return "The discoverer was already discovering a file";
    case GST_DISCOVERER_MISSING_PLUGINS: return "Some plugins are missing for full discovery";
    case GST_DISCOVERER_ERROR:
    default:                             return "An error happened and the GError is set";
  }

}

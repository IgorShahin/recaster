#include "include/recaster/recaster_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>
#include <sys/utsname.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "recaster_plugin_private.h"

#define RECASTER_PLUGIN(obj)                                                   \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), recaster_plugin_get_type(),               \
                              RecasterPlugin))

struct FrameData {
  int32_t width = 0;
  int32_t height = 0;
  std::vector<uint8_t> pixels;
};

struct _RecasterPlugin {
  GObject parent_instance;

  bool is_recording;
  int fps;
  int resolution_divisor;
  guint capture_source_id;
  gchar* current_output_path;
  std::vector<FrameData>* frames;
};

G_DEFINE_TYPE(RecasterPlugin, recaster_plugin, g_object_get_type())

namespace {

void write_fourcc(std::ofstream& file, const char* value) {
  file.write(value, 4);
}

void write_u32(std::ofstream& file, uint32_t value) {
  file.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void write_u16(std::ofstream& file, uint16_t value) {
  file.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

uint32_t streampos_to_u32(std::streampos pos) {
  return static_cast<uint32_t>(static_cast<std::streamoff>(pos));
}

std::streampos begin_chunk(std::ofstream& file, const char* fourcc) {
  write_fourcc(file, fourcc);
  const std::streampos size_pos = file.tellp();
  write_u32(file, 0);
  return size_pos;
}

void end_chunk(std::ofstream& file, std::streampos size_pos) {
  const std::streampos end_pos = file.tellp();
  uint32_t size = streampos_to_u32(end_pos - (size_pos + std::streamoff(4)));
  file.seekp(size_pos);
  write_u32(file, size);
  file.seekp(end_pos);
  if ((size & 1U) != 0U) {
    const uint8_t pad = 0;
    file.write(reinterpret_cast<const char*>(&pad), sizeof(pad));
  }
}

std::streampos begin_list(std::ofstream& file, const char* list_type) {
  write_fourcc(file, "LIST");
  const std::streampos size_pos = file.tellp();
  write_u32(file, 0);
  write_fourcc(file, list_type);
  return size_pos;
}

GtkWindow* find_target_window() {
  GList* windows = gtk_window_list_toplevels();
  for (GList* item = windows; item != nullptr; item = item->next) {
    if (!GTK_IS_WINDOW(item->data)) {
      continue;
    }
    GtkWindow* window = GTK_WINDOW(item->data);
    GtkWidget* widget = GTK_WIDGET(window);
    if (gtk_widget_get_visible(widget)) {
      g_list_free(windows);
      return window;
    }
  }
  g_list_free(windows);
  return nullptr;
}

bool capture_app_window_frame(FrameData* frame, int resolution_divisor) {
  if (frame == nullptr) {
    return false;
  }

  GtkWindow* window = find_target_window();
  if (window == nullptr) {
    return false;
  }

  GtkWidget* widget = GTK_WIDGET(window);
  GdkWindow* gdk_window = gtk_widget_get_window(widget);
  if (gdk_window == nullptr) {
    return false;
  }

  int width = 0;
  int height = 0;
  gdk_window_get_geometry(gdk_window, nullptr, nullptr, &width, &height);
  if (width <= 0 || height <= 0) {
    return false;
  }

  GdkPixbuf* pixbuf = gdk_pixbuf_get_from_window(gdk_window, 0, 0, width, height);
  if (pixbuf == nullptr) {
    return false;
  }

  if (resolution_divisor > 1) {
    const int scaled_width = std::max(1, width / resolution_divisor);
    const int scaled_height = std::max(1, height / resolution_divisor);
    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(
        pixbuf, scaled_width, scaled_height, GDK_INTERP_BILINEAR);
    g_object_unref(pixbuf);
    pixbuf = scaled;
    if (pixbuf == nullptr) {
      return false;
    }
    width = scaled_width;
    height = scaled_height;
  }

  const int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
  const int channels = gdk_pixbuf_get_n_channels(pixbuf);
  const guchar* src = gdk_pixbuf_read_pixels(pixbuf);
  if (src == nullptr || channels < 3) {
    g_object_unref(pixbuf);
    return false;
  }

  frame->width = width;
  frame->height = height;
  frame->pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4U);

  for (int y = 0; y < height; ++y) {
    const guchar* row = src + (static_cast<size_t>(y) * static_cast<size_t>(rowstride));
    uint8_t* dst = frame->pixels.data() +
                   (static_cast<size_t>(y) * static_cast<size_t>(width) * 4U);
    for (int x = 0; x < width; ++x) {
      const guchar* pixel = row + static_cast<size_t>(x) * static_cast<size_t>(channels);
      dst[0] = pixel[2];
      dst[1] = pixel[1];
      dst[2] = pixel[0];
      dst[3] = channels >= 4 ? pixel[3] : 255;
      dst += 4;
    }
  }

  g_object_unref(pixbuf);
  return true;
}

bool write_avi_file(const char* output_path,
                    const std::vector<FrameData>& frames,
                    int fps,
                    std::string* error_message) {
  if (output_path == nullptr || strlen(output_path) == 0) {
    if (error_message != nullptr) {
      *error_message = "outputPath is required.";
    }
    return false;
  }

  if (frames.empty()) {
    if (error_message != nullptr) {
      *error_message = "No frames were captured.";
    }
    return false;
  }

  const int width = frames.front().width;
  const int height = frames.front().height;
  if (width <= 0 || height <= 0) {
    if (error_message != nullptr) {
      *error_message = "Invalid frame size.";
    }
    return false;
  }

  const uint32_t frame_size = static_cast<uint32_t>(
      static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ULL);

  std::ofstream file(output_path, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    if (error_message != nullptr) {
      *error_message = "Failed to open output file.";
    }
    return false;
  }

  struct IndexEntry {
    uint32_t offset = 0;
    uint32_t size = 0;
  };
  std::vector<IndexEntry> index_entries;
  index_entries.reserve(frames.size());

  const std::streampos riff_size_pos = begin_chunk(file, "RIFF");
  write_fourcc(file, "AVI ");

  const std::streampos hdrl_size_pos = begin_list(file, "hdrl");

  const std::streampos avih_size_pos = begin_chunk(file, "avih");
  write_u32(file, static_cast<uint32_t>(1000000 / std::max(1, fps)));
  write_u32(file, frame_size * static_cast<uint32_t>(fps));
  write_u32(file, 0);
  write_u32(file, 0x10);
  write_u32(file, static_cast<uint32_t>(frames.size()));
  write_u32(file, 0);
  write_u32(file, 1);
  write_u32(file, frame_size);
  write_u32(file, static_cast<uint32_t>(width));
  write_u32(file, static_cast<uint32_t>(height));
  write_u32(file, 0);
  write_u32(file, 0);
  write_u32(file, 0);
  write_u32(file, 0);
  end_chunk(file, avih_size_pos);

  const std::streampos strl_size_pos = begin_list(file, "strl");

  const std::streampos strh_size_pos = begin_chunk(file, "strh");
  write_fourcc(file, "vids");
  write_fourcc(file, "DIB ");
  write_u32(file, 0);
  write_u16(file, 0);
  write_u16(file, 0);
  write_u32(file, 0);
  write_u32(file, 1);
  write_u32(file, static_cast<uint32_t>(std::max(1, fps)));
  write_u32(file, 0);
  write_u32(file, static_cast<uint32_t>(frames.size()));
  write_u32(file, frame_size);
  write_u32(file, 0xFFFFFFFF);
  write_u32(file, 0);
  write_u16(file, 0);
  write_u16(file, 0);
  write_u16(file, static_cast<uint16_t>(width));
  write_u16(file, static_cast<uint16_t>(height));
  end_chunk(file, strh_size_pos);

  const std::streampos strf_size_pos = begin_chunk(file, "strf");
  write_u32(file, 40);
  write_u32(file, static_cast<uint32_t>(width));
  write_u32(file, static_cast<uint32_t>(static_cast<int32_t>(-height)));
  write_u16(file, 1);
  write_u16(file, 32);
  write_u32(file, 0);
  write_u32(file, frame_size);
  write_u32(file, 0);
  write_u32(file, 0);
  write_u32(file, 0);
  write_u32(file, 0);
  end_chunk(file, strf_size_pos);

  end_chunk(file, strl_size_pos);
  end_chunk(file, hdrl_size_pos);

  const std::streampos movi_size_pos = begin_list(file, "movi");
  const std::streampos movi_data_start = file.tellp();

  for (const FrameData& frame : frames) {
    if (frame.width != width || frame.height != height ||
        frame.pixels.size() != frame_size) {
      continue;
    }

    const std::streampos chunk_start = file.tellp();
    const std::streampos frame_size_pos = begin_chunk(file, "00db");
    file.write(reinterpret_cast<const char*>(frame.pixels.data()),
               static_cast<std::streamsize>(frame.pixels.size()));
    end_chunk(file, frame_size_pos);

    IndexEntry entry;
    entry.offset = streampos_to_u32(chunk_start - movi_data_start);
    entry.size = static_cast<uint32_t>(frame.pixels.size());
    index_entries.push_back(entry);
  }

  end_chunk(file, movi_size_pos);

  const std::streampos idx1_size_pos = begin_chunk(file, "idx1");
  for (const IndexEntry& entry : index_entries) {
    write_fourcc(file, "00db");
    write_u32(file, 0x10);
    write_u32(file, entry.offset);
    write_u32(file, entry.size);
  }
  end_chunk(file, idx1_size_pos);

  end_chunk(file, riff_size_pos);
  file.flush();

  if (!file.good()) {
    if (error_message != nullptr) {
      *error_message = "Failed to finalize AVI output.";
    }
    return false;
  }
  return true;
}

gboolean on_capture_tick(gpointer user_data) {
  RecasterPlugin* self = RECASTER_PLUGIN(user_data);
  if (!self->is_recording) {
    return G_SOURCE_REMOVE;
  }

  FrameData frame;
  if (capture_app_window_frame(&frame, self->resolution_divisor)) {
    if (self->frames->empty() ||
        (frame.width == self->frames->front().width &&
         frame.height == self->frames->front().height)) {
      self->frames->push_back(std::move(frame));
    }
  }
  return G_SOURCE_CONTINUE;
}

FlMethodResponse* start_recording(RecasterPlugin* self, FlMethodCall* method_call) {
  if (self->is_recording) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "already_recording", "Screen recording is already running.", nullptr));
  }

  FlValue* args = fl_method_call_get_args(method_call);
  if (args == nullptr || fl_value_get_type(args) != FL_VALUE_TYPE_MAP) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "invalid_args", "Arguments are required.", nullptr));
  }

  FlValue* output_path_value = fl_value_lookup_string(args, "outputPath");
  if (output_path_value == nullptr ||
      fl_value_get_type(output_path_value) != FL_VALUE_TYPE_STRING) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "invalid_args", "outputPath is required.", nullptr));
  }

  const gchar* output_path = fl_value_get_string(output_path_value);
  if (output_path == nullptr || strlen(output_path) == 0) {
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "invalid_args", "outputPath is required.", nullptr));
  }

  int fps = 30;
  int resolution_divisor = 1;
  FlValue* fps_value = fl_value_lookup_string(args, "fps");
  if (fps_value != nullptr && fl_value_get_type(fps_value) == FL_VALUE_TYPE_INT) {
    const gint64 value = fl_value_get_int(fps_value);
    if (value > 0 && value <= 60) {
      fps = static_cast<int>(value);
    }
  }
  FlValue* divisor_value = fl_value_lookup_string(args, "resolutionDivisor");
  if (divisor_value != nullptr &&
      fl_value_get_type(divisor_value) == FL_VALUE_TYPE_INT) {
    const gint64 value = fl_value_get_int(divisor_value);
    if (value > 0 && value <= 8) {
      resolution_divisor = static_cast<int>(value);
    }
  }

  self->frames->clear();
  g_clear_pointer(&self->current_output_path, g_free);
  self->current_output_path = g_strdup(output_path);
  self->fps = fps;
  self->resolution_divisor = resolution_divisor;
  self->is_recording = true;

  const guint interval = static_cast<guint>(std::max(1, 1000 / std::max(1, fps)));
  self->capture_source_id = g_timeout_add(interval, on_capture_tick, self);

  return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
}

FlMethodResponse* stop_recording(RecasterPlugin* self) {
  if (!self->is_recording) {
    return FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
  }

  self->is_recording = false;
  if (self->capture_source_id != 0) {
    g_source_remove(self->capture_source_id);
    self->capture_source_id = 0;
  }

  std::string error_message;
  const bool written = write_avi_file(self->current_output_path, *self->frames,
                                      self->fps, &error_message);
  if (!written) {
    g_autoptr(FlValue) details = fl_value_new_string(error_message.c_str());
    self->frames->clear();
    g_clear_pointer(&self->current_output_path, g_free);
    return FL_METHOD_RESPONSE(fl_method_error_response_new(
        "stop_failed", "Failed to finalize recording.", details));
  }

  g_autoptr(FlValue) result = fl_value_new_string(self->current_output_path);
  self->frames->clear();
  g_clear_pointer(&self->current_output_path, g_free);
  return FL_METHOD_RESPONSE(fl_method_success_response_new(result));
}

FlMethodResponse* is_recording(RecasterPlugin* self) {
  g_autoptr(FlValue) result = fl_value_new_bool(self->is_recording);
  return FL_METHOD_RESPONSE(fl_method_success_response_new(result));
}

}

static void recaster_plugin_handle_method_call(RecasterPlugin* self,
                                               FlMethodCall* method_call) {
  g_autoptr(FlMethodResponse) response = nullptr;

  const gchar* method = fl_method_call_get_name(method_call);

  if (strcmp(method, "getPlatformVersion") == 0) {
    response = get_platform_version();
  } else if (strcmp(method, "startRecording") == 0) {
    response = start_recording(self, method_call);
  } else if (strcmp(method, "stopRecording") == 0) {
    response = stop_recording(self);
  } else if (strcmp(method, "isRecording") == 0) {
    response = is_recording(self);
  } else {
    response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
  }

  fl_method_call_respond(method_call, response, nullptr);
}

FlMethodResponse* get_platform_version() {
  struct utsname uname_data = {};
  uname(&uname_data);
  g_autofree gchar* version = g_strdup_printf("Linux %s", uname_data.version);
  g_autoptr(FlValue) result = fl_value_new_string(version);
  return FL_METHOD_RESPONSE(fl_method_success_response_new(result));
}

static void recaster_plugin_dispose(GObject* object) {
  RecasterPlugin* self = RECASTER_PLUGIN(object);
  if (self->capture_source_id != 0) {
    g_source_remove(self->capture_source_id);
    self->capture_source_id = 0;
  }
  self->is_recording = false;
  g_clear_pointer(&self->current_output_path, g_free);
  if (self->frames != nullptr) {
    delete self->frames;
    self->frames = nullptr;
  }
  G_OBJECT_CLASS(recaster_plugin_parent_class)->dispose(object);
}

static void recaster_plugin_class_init(RecasterPluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = recaster_plugin_dispose;
}

static void recaster_plugin_init(RecasterPlugin* self) {
  self->is_recording = false;
  self->fps = 30;
  self->resolution_divisor = 1;
  self->capture_source_id = 0;
  self->current_output_path = nullptr;
  self->frames = new std::vector<FrameData>();
}

static void method_call_cb(FlMethodChannel* channel, FlMethodCall* method_call,
                           gpointer user_data) {
  RecasterPlugin* plugin = RECASTER_PLUGIN(user_data);
  recaster_plugin_handle_method_call(plugin, method_call);
}

void recaster_plugin_register_with_registrar(FlPluginRegistrar* registrar) {
  RecasterPlugin* plugin = RECASTER_PLUGIN(
      g_object_new(recaster_plugin_get_type(), nullptr));

  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  g_autoptr(FlMethodChannel) channel =
      fl_method_channel_new(fl_plugin_registrar_get_messenger(registrar),
                            "recaster", FL_METHOD_CODEC(codec));
  fl_method_channel_set_method_call_handler(channel, method_call_cb,
                                            g_object_ref(plugin),
                                            g_object_unref);

  g_object_unref(plugin);
}

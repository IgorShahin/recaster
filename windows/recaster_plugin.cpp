#include "recaster_plugin.h"

#include <windows.h>

#include <VersionHelpers.h>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace recaster {

namespace {

void WriteFourCc(std::ofstream& file, const char* value) {
  file.write(value, 4);
}

void WriteU32(std::ofstream& file, uint32_t value) {
  file.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void WriteU16(std::ofstream& file, uint16_t value) {
  file.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

uint32_t StreamPosToU32(std::streampos pos) {
  return static_cast<uint32_t>(static_cast<std::streamoff>(pos));
}

std::streampos BeginChunk(std::ofstream& file, const char* fourcc) {
  WriteFourCc(file, fourcc);
  const std::streampos size_pos = file.tellp();
  WriteU32(file, 0);
  return size_pos;
}

void EndChunk(std::ofstream& file, std::streampos size_pos) {
  const std::streampos end_pos = file.tellp();
  uint32_t size = StreamPosToU32(end_pos - (size_pos + std::streamoff(4)));
  file.seekp(size_pos);
  WriteU32(file, size);
  file.seekp(end_pos);
  if ((size & 1U) != 0U) {
    const uint8_t pad = 0;
    file.write(reinterpret_cast<const char*>(&pad), sizeof(pad));
  }
}

std::streampos BeginList(std::ofstream& file, const char* list_type) {
  WriteFourCc(file, "LIST");
  const std::streampos size_pos = file.tellp();
  WriteU32(file, 0);
  WriteFourCc(file, list_type);
  return size_pos;
}

}

void RecasterPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  auto channel =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "recaster",
          &flutter::StandardMethodCodec::GetInstance());

  void* native_window = nullptr;
  if (registrar->GetView() != nullptr) {
    native_window = registrar->GetView()->GetNativeWindow();
  }
  auto plugin = std::make_unique<RecasterPlugin>(native_window);

  channel->SetMethodCallHandler(
      [plugin_pointer = plugin.get()](const auto& call, auto result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  registrar->AddPlugin(std::move(plugin));
}

RecasterPlugin::RecasterPlugin(void* native_window_handle)
    : native_window_handle_(native_window_handle) {}

RecasterPlugin::~RecasterPlugin() {
  std::string path;
  std::string error;
  StopRecording(&path, &error);
}

bool RecasterPlugin::CaptureWindowFrame(FrameData* frame) {
  if (frame == nullptr || native_window_handle_ == nullptr) {
    return false;
  }

  const HWND hwnd = static_cast<HWND>(native_window_handle_);
  RECT rect = {};
  if (!GetClientRect(hwnd, &rect)) {
    return false;
  }

  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0) {
    return false;
  }
  const int target_width = std::max(1, capture_width_);
  const int target_height = std::max(1, capture_height_);

  HDC window_dc = GetDC(hwnd);
  if (window_dc == nullptr) {
    return false;
  }

  HDC memory_dc = CreateCompatibleDC(window_dc);
  if (memory_dc == nullptr) {
    ReleaseDC(hwnd, window_dc);
    return false;
  }

  BITMAPINFO bitmap_info = {};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = target_width;
  bitmap_info.bmiHeader.biHeight = -target_height;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(window_dc, &bitmap_info, DIB_RGB_COLORS,
                                    &bits, nullptr, 0);
  if (bitmap == nullptr || bits == nullptr) {
    if (bitmap != nullptr) {
      DeleteObject(bitmap);
    }
    DeleteDC(memory_dc);
    ReleaseDC(hwnd, window_dc);
    return false;
  }

  HGDIOBJ old_bitmap = SelectObject(memory_dc, bitmap);
  const BOOL blt_ok = StretchBlt(memory_dc, 0, 0, target_width, target_height,
                                 window_dc, 0, 0, width, height,
                                 SRCCOPY | CAPTUREBLT);

  if (!blt_ok) {
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(hwnd, window_dc);
    return false;
  }

  const size_t bytes =
      static_cast<size_t>(target_width) * static_cast<size_t>(target_height) * 4U;
  frame->width = target_width;
  frame->height = target_height;
  frame->pixels.resize(bytes);
  std::memcpy(frame->pixels.data(), bits, bytes);

  SelectObject(memory_dc, old_bitmap);
  DeleteObject(bitmap);
  DeleteDC(memory_dc);
  ReleaseDC(hwnd, window_dc);
  return true;
}

void RecasterPlugin::CaptureLoop() {
  const auto frame_interval = std::chrono::milliseconds(
      std::max(1, 1000 / std::max(1, fps_)));

  while (is_recording_.load()) {
    FrameData frame;
    if (CaptureWindowFrame(&frame)) {
      std::lock_guard<std::mutex> lock(frames_mutex_);
      if (!frames_.empty()) {
        const FrameData& first = frames_.front();
        if (frame.width != first.width || frame.height != first.height) {
          frame.pixels.clear();
        }
      }
      if (!frame.pixels.empty()) {
        frames_.push_back(std::move(frame));
      }
    }
    std::this_thread::sleep_for(frame_interval);
  }
}

bool RecasterPlugin::WriteAviFile(const std::string& output_path,
                                  const std::vector<FrameData>& frames,
                                  int fps,
                                  std::string* error_message) {
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

  const std::streampos riff_size_pos = BeginChunk(file, "RIFF");
  WriteFourCc(file, "AVI ");

  const std::streampos hdrl_size_pos = BeginList(file, "hdrl");

  const std::streampos avih_size_pos = BeginChunk(file, "avih");
  WriteU32(file, static_cast<uint32_t>(1000000 / std::max(1, fps)));
  WriteU32(file, frame_size * static_cast<uint32_t>(fps));
  WriteU32(file, 0);
  WriteU32(file, 0x10);
  WriteU32(file, static_cast<uint32_t>(frames.size()));
  WriteU32(file, 0);
  WriteU32(file, 1);
  WriteU32(file, frame_size);
  WriteU32(file, static_cast<uint32_t>(width));
  WriteU32(file, static_cast<uint32_t>(height));
  WriteU32(file, 0);
  WriteU32(file, 0);
  WriteU32(file, 0);
  WriteU32(file, 0);
  EndChunk(file, avih_size_pos);

  const std::streampos strl_size_pos = BeginList(file, "strl");

  const std::streampos strh_size_pos = BeginChunk(file, "strh");
  WriteFourCc(file, "vids");
  WriteFourCc(file, "DIB ");
  WriteU32(file, 0);
  WriteU16(file, 0);
  WriteU16(file, 0);
  WriteU32(file, 0);
  WriteU32(file, 1);
  WriteU32(file, static_cast<uint32_t>(std::max(1, fps)));
  WriteU32(file, 0);
  WriteU32(file, static_cast<uint32_t>(frames.size()));
  WriteU32(file, frame_size);
  WriteU32(file, 0xFFFFFFFF);
  WriteU32(file, 0);
  WriteU16(file, 0);
  WriteU16(file, 0);
  WriteU16(file, static_cast<uint16_t>(width));
  WriteU16(file, static_cast<uint16_t>(height));
  EndChunk(file, strh_size_pos);

  const std::streampos strf_size_pos = BeginChunk(file, "strf");
  WriteU32(file, 40);
  WriteU32(file, static_cast<uint32_t>(width));
  WriteU32(file, static_cast<uint32_t>(static_cast<int32_t>(-height)));
  WriteU16(file, 1);
  WriteU16(file, 32);
  WriteU32(file, BI_RGB);
  WriteU32(file, frame_size);
  WriteU32(file, 0);
  WriteU32(file, 0);
  WriteU32(file, 0);
  WriteU32(file, 0);
  EndChunk(file, strf_size_pos);

  EndChunk(file, strl_size_pos);
  EndChunk(file, hdrl_size_pos);

  const std::streampos movi_size_pos = BeginList(file, "movi");
  const std::streampos movi_data_start = file.tellp();

  for (const FrameData& frame : frames) {
    if (frame.width != width || frame.height != height ||
        frame.pixels.size() != frame_size) {
      continue;
    }

    const std::streampos chunk_start = file.tellp();
    const std::streampos frame_size_pos = BeginChunk(file, "00db");
    file.write(reinterpret_cast<const char*>(frame.pixels.data()),
               static_cast<std::streamsize>(frame.pixels.size()));
    EndChunk(file, frame_size_pos);

    IndexEntry entry;
    entry.offset = StreamPosToU32(chunk_start - movi_data_start);
    entry.size = static_cast<uint32_t>(frame.pixels.size());
    index_entries.push_back(entry);
  }

  EndChunk(file, movi_size_pos);

  const std::streampos idx1_size_pos = BeginChunk(file, "idx1");
  for (const IndexEntry& entry : index_entries) {
    WriteFourCc(file, "00db");
    WriteU32(file, 0x10);
    WriteU32(file, entry.offset);
    WriteU32(file, entry.size);
  }
  EndChunk(file, idx1_size_pos);

  EndChunk(file, riff_size_pos);
  file.flush();

  if (!file.good()) {
    if (error_message != nullptr) {
      *error_message = "Failed to finalize AVI output.";
    }
    return false;
  }
  return true;
}

bool RecasterPlugin::StartRecording(const std::string& output_path,
                                    int fps,
                                    int resolution_divisor,
                                    std::string* error_message) {
  if (is_recording_.load()) {
    if (error_message != nullptr) {
      *error_message = "Screen recording is already running.";
    }
    return false;
  }
  if (native_window_handle_ == nullptr) {
    if (error_message != nullptr) {
      *error_message = "Native window handle is unavailable.";
    }
    return false;
  }
  const HWND hwnd = static_cast<HWND>(native_window_handle_);
  RECT rect = {};
  if (!GetClientRect(hwnd, &rect)) {
    if (error_message != nullptr) {
      *error_message = "Failed to read window size.";
    }
    return false;
  }
  const int source_width = rect.right - rect.left;
  const int source_height = rect.bottom - rect.top;
  if (source_width <= 0 || source_height <= 0) {
    if (error_message != nullptr) {
      *error_message = "Window has invalid size.";
    }
    return false;
  }

  std::lock_guard<std::mutex> lock(frames_mutex_);
  frames_.clear();
  current_output_path_ = output_path;
  fps_ = std::max(1, std::min(60, fps));
  resolution_divisor_ = std::max(1, std::min(8, resolution_divisor));
  capture_width_ = std::max(1, source_width / resolution_divisor_);
  capture_height_ = std::max(1, source_height / resolution_divisor_);
  is_recording_.store(true);

  capture_thread_ = std::thread([this]() { CaptureLoop(); });
  return true;
}

bool RecasterPlugin::StopRecording(std::string* saved_path,
                                   std::string* error_message) {
  if (!is_recording_.load()) {
    return false;
  }

  is_recording_.store(false);
  if (capture_thread_.joinable()) {
    capture_thread_.join();
  }

  std::vector<FrameData> captured_frames;
  {
    std::lock_guard<std::mutex> lock(frames_mutex_);
    captured_frames.swap(frames_);
  }

  std::string output_path = current_output_path_;
  current_output_path_.clear();
  if (captured_frames.empty()) {
    if (error_message != nullptr) {
      *error_message = "No frames captured.";
    }
    return false;
  }

  std::string write_error;
  const bool ok = WriteAviFile(output_path, captured_frames, fps_, &write_error);
  if (!ok) {
    if (error_message != nullptr) {
      *error_message = write_error;
    }
    return false;
  }

  if (saved_path != nullptr) {
    *saved_path = output_path;
  }
  return true;
}

void RecasterPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (method_call.method_name().compare("getPlatformVersion") == 0) {
    std::ostringstream version_stream;
    version_stream << "Windows ";
    if (IsWindows10OrGreater()) {
      version_stream << "10+";
    } else if (IsWindows8OrGreater()) {
      version_stream << "8";
    } else if (IsWindows7OrGreater()) {
      version_stream << "7";
    }
    result->Success(flutter::EncodableValue(version_stream.str()));
    return;
  }

  if (method_call.method_name().compare("startRecording") == 0) {
    const auto* arguments = std::get_if<flutter::EncodableMap>(method_call.arguments());
    if (arguments == nullptr) {
      result->Error("invalid_args", "Arguments are required.");
      return;
    }

    const auto output_path_it = arguments->find(flutter::EncodableValue("outputPath"));
    if (output_path_it == arguments->end()) {
      result->Error("invalid_args", "outputPath is required.");
      return;
    }

    const auto* output_path = std::get_if<std::string>(&output_path_it->second);
    if (output_path == nullptr || output_path->empty()) {
      result->Error("invalid_args", "outputPath is required.");
      return;
    }

    int fps = 30;
    int resolution_divisor = 1;
    const auto fps_it = arguments->find(flutter::EncodableValue("fps"));
    if (fps_it != arguments->end()) {
      if (const auto* fps_int = std::get_if<int32_t>(&fps_it->second)) {
        fps = *fps_int;
      } else if (const auto* fps_long = std::get_if<int64_t>(&fps_it->second)) {
        fps = static_cast<int>(*fps_long);
      }
    }
    const auto divisor_it =
        arguments->find(flutter::EncodableValue("resolutionDivisor"));
    if (divisor_it != arguments->end()) {
      if (const auto* divisor_int = std::get_if<int32_t>(&divisor_it->second)) {
        resolution_divisor = *divisor_int;
      } else if (const auto* divisor_long =
                     std::get_if<int64_t>(&divisor_it->second)) {
        resolution_divisor = static_cast<int>(*divisor_long);
      }
    }

    std::string error_message;
    if (!StartRecording(*output_path, fps, resolution_divisor, &error_message)) {
      result->Error("start_failed", error_message);
      return;
    }
    result->Success(flutter::EncodableValue());
    return;
  }

  if (method_call.method_name().compare("stopRecording") == 0) {
    std::string saved_path;
    std::string error_message;
    const bool stopped = StopRecording(&saved_path, &error_message);
    if (!stopped) {
      if (!error_message.empty()) {
        result->Error("stop_failed", error_message);
      } else {
        result->Success(flutter::EncodableValue());
      }
      return;
    }
    result->Success(flutter::EncodableValue(saved_path));
    return;
  }

  if (method_call.method_name().compare("isRecording") == 0) {
    result->Success(flutter::EncodableValue(is_recording_.load()));
    return;
  }

  result->NotImplemented();
}

}

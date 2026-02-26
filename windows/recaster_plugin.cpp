#include "recaster_plugin.h"

#include <windows.h>

#include <VersionHelpers.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace recaster {

namespace {

std::wstring Utf8ToWide(const std::string& input) {
  if (input.empty()) {
    return L"";
  }
  const int count = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, nullptr, 0);
  if (count <= 0) {
    return L"";
  }
  std::wstring output(static_cast<size_t>(count), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, output.data(), count);
  if (!output.empty() && output.back() == L'\0') {
    output.pop_back();
  }
  return output;
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

bool RecasterPlugin::EnsureOutputPathWritable(const std::string& output_path,
                                              std::string* error_code,
                                              std::string* error_message) {
  std::error_code ec;
  const std::filesystem::path file_path(output_path);
  const std::filesystem::path dir_path = file_path.parent_path();
  if (dir_path.empty()) {
    if (error_code != nullptr) {
      *error_code = "invalid_output_path";
    }
    if (error_message != nullptr) {
      *error_message = "Output path must include a directory.";
    }
    return false;
  }

  std::filesystem::create_directories(dir_path, ec);
  if (ec) {
    if (error_code != nullptr) {
      *error_code = "directory_create_failed";
    }
    if (error_message != nullptr) {
      *error_message = "Failed to create output directory.";
    }
    return false;
  }

  const std::filesystem::path probe_path =
      dir_path / std::filesystem::path(".recaster_write_probe.tmp");
  std::ofstream probe_file(probe_path.string(),
                           std::ios::binary | std::ios::trunc);
  if (!probe_file.is_open()) {
    if (error_code != nullptr) {
      *error_code = "path_not_writable";
    }
    if (error_message != nullptr) {
      *error_message = "Output directory is not writable.";
    }
    return false;
  }
  probe_file.close();
  std::filesystem::remove(probe_path, ec);
  return true;
}

bool RecasterPlugin::WriteMp4File(const std::string& output_path,
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

  const uint32_t frame_size = static_cast<uint32_t>(static_cast<uint64_t>(width) *
                                                    static_cast<uint64_t>(height) * 4ULL);
  const std::wstring wide_path = Utf8ToWide(output_path);
  if (wide_path.empty()) {
    if (error_message != nullptr) {
      *error_message = "Invalid output path encoding.";
    }
    return false;
  }

  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    if (error_message != nullptr) {
      *error_message = "Failed to initialize COM.";
    }
    return false;
  }
  const bool should_uninit_com = SUCCEEDED(hr);

  hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
  if (FAILED(hr)) {
    if (should_uninit_com) {
      CoUninitialize();
    }
    if (error_message != nullptr) {
      *error_message = "Failed to initialize Media Foundation.";
    }
    return false;
  }

  IMFSinkWriter* sink_writer = nullptr;
  IMFMediaType* output_media_type = nullptr;
  IMFMediaType* input_media_type = nullptr;
  DWORD stream_index = 0;
  bool success = false;

  do {
    hr = MFCreateSinkWriterFromURL(wide_path.c_str(), nullptr, nullptr, &sink_writer);
    if (FAILED(hr)) {
      if (error_message != nullptr) {
        *error_message = "Failed to create MP4 sink writer.";
      }
      break;
    }

    hr = MFCreateMediaType(&output_media_type);
    if (FAILED(hr)) {
      if (error_message != nullptr) {
        *error_message = "Failed to create output media type.";
      }
      break;
    }
    output_media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    output_media_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    output_media_type->SetUINT32(MF_MT_AVG_BITRATE, 300000);
    output_media_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(output_media_type, MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(output_media_type, MF_MT_FRAME_RATE, std::max(1, fps), 1);
    MFSetAttributeRatio(output_media_type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

    hr = sink_writer->AddStream(output_media_type, &stream_index);
    if (FAILED(hr)) {
      if (error_message != nullptr) {
        *error_message = "Failed to add output stream.";
      }
      break;
    }

    hr = MFCreateMediaType(&input_media_type);
    if (FAILED(hr)) {
      if (error_message != nullptr) {
        *error_message = "Failed to create input media type.";
      }
      break;
    }
    input_media_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    input_media_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    input_media_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(input_media_type, MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(input_media_type, MF_MT_FRAME_RATE, std::max(1, fps), 1);
    MFSetAttributeRatio(input_media_type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

    hr = sink_writer->SetInputMediaType(stream_index, input_media_type, nullptr);
    if (FAILED(hr)) {
      if (error_message != nullptr) {
        *error_message = "Failed to set input media type.";
      }
      break;
    }

    hr = sink_writer->BeginWriting();
    if (FAILED(hr)) {
      if (error_message != nullptr) {
        *error_message = "Failed to start MP4 writing.";
      }
      break;
    }

    const LONGLONG frame_duration = 10 * 1000 * 1000LL / std::max(1, fps);
    LONGLONG sample_time = 0;

    for (const FrameData& frame : frames) {
      if (frame.width != width || frame.height != height ||
          frame.pixels.size() != frame_size) {
        continue;
      }

      IMFMediaBuffer* media_buffer = nullptr;
      IMFSample* sample = nullptr;
      BYTE* dst = nullptr;

      hr = MFCreateMemoryBuffer(frame_size, &media_buffer);
      if (FAILED(hr)) {
        if (error_message != nullptr) {
          *error_message = "Failed to create frame buffer.";
        }
        if (sample != nullptr) sample->Release();
        if (media_buffer != nullptr) media_buffer->Release();
        break;
      }

      hr = media_buffer->Lock(&dst, nullptr, nullptr);
      if (FAILED(hr)) {
        if (error_message != nullptr) {
          *error_message = "Failed to lock frame buffer.";
        }
        media_buffer->Release();
        break;
      }
      std::memcpy(dst, frame.pixels.data(), frame_size);
      media_buffer->Unlock();
      media_buffer->SetCurrentLength(frame_size);

      hr = MFCreateSample(&sample);
      if (FAILED(hr)) {
        if (error_message != nullptr) {
          *error_message = "Failed to create sample.";
        }
        media_buffer->Release();
        break;
      }

      sample->AddBuffer(media_buffer);
      sample->SetSampleTime(sample_time);
      sample->SetSampleDuration(frame_duration);
      sample_time += frame_duration;

      hr = sink_writer->WriteSample(stream_index, sample);
      sample->Release();
      media_buffer->Release();
      if (FAILED(hr)) {
        if (error_message != nullptr) {
          *error_message = "Failed to encode frame.";
        }
        break;
      }
    }

    if (SUCCEEDED(hr)) {
      hr = sink_writer->Finalize();
      if (FAILED(hr)) {
        if (error_message != nullptr) {
          *error_message = "Failed to finalize MP4 output.";
        }
        break;
      }
      success = true;
    }
  } while (false);

  if (input_media_type != nullptr) {
    input_media_type->Release();
  }
  if (output_media_type != nullptr) {
    output_media_type->Release();
  }
  if (sink_writer != nullptr) {
    sink_writer->Release();
  }

  MFShutdown();
  if (should_uninit_com) {
    CoUninitialize();
  }
  return success;
}

bool RecasterPlugin::StartRecording(const std::string& output_path,
                                    int fps,
                                    int resolution_divisor,
                                    std::string* error_code,
                                    std::string* error_message) {
  if (is_recording_.load()) {
    if (error_message != nullptr) {
      *error_message = "Screen recording is already running.";
    }
    if (error_code != nullptr) {
      *error_code = "already_recording";
    }
    return false;
  }
  if (native_window_handle_ == nullptr) {
    if (error_code != nullptr) {
      *error_code = "window_handle_unavailable";
    }
    if (error_message != nullptr) {
      *error_message = "Native window handle is unavailable.";
    }
    return false;
  }
  if (!EnsureOutputPathWritable(output_path, error_code, error_message)) {
    return false;
  }
  const HWND hwnd = static_cast<HWND>(native_window_handle_);
  RECT rect = {};
  if (!GetClientRect(hwnd, &rect)) {
    if (error_code != nullptr) {
      *error_code = "window_size_failed";
    }
    if (error_message != nullptr) {
      *error_message = "Failed to read window size.";
    }
    return false;
  }
  const int source_width = rect.right - rect.left;
  const int source_height = rect.bottom - rect.top;
  if (source_width <= 0 || source_height <= 0) {
    if (error_code != nullptr) {
      *error_code = "window_size_invalid";
    }
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
  const bool ok = WriteMp4File(output_path, captured_frames, fps_, &write_error);
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

    std::string error_code;
    std::string error_message;
    if (!StartRecording(*output_path, fps, resolution_divisor, &error_code,
                        &error_message)) {
      result->Error(error_code.empty() ? "start_failed" : error_code,
                    error_message);
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

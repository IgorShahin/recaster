#ifndef FLUTTER_PLUGIN_RECASTER_PLUGIN_H_
#define FLUTTER_PLUGIN_RECASTER_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace recaster {

struct FrameData {
  int32_t width = 0;
  int32_t height = 0;
  std::vector<uint8_t> pixels;
};

class RecasterPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows *registrar);

  explicit RecasterPlugin(void* native_window_handle);

  virtual ~RecasterPlugin();

  RecasterPlugin(const RecasterPlugin&) = delete;
  RecasterPlugin& operator=(const RecasterPlugin&) = delete;

  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue> &method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

 private:
  bool StartRecording(const std::string& output_path,
                      int fps,
                      int resolution_divisor,
                      std::string* error_message);
  bool StopRecording(std::string* saved_path, std::string* error_message);
  bool CaptureWindowFrame(FrameData* frame);
  void CaptureLoop();
  bool WriteAviFile(const std::string& output_path,
                    const std::vector<FrameData>& frames,
                    int fps,
                    std::string* error_message);

  void* native_window_handle_ = nullptr;
  std::atomic<bool> is_recording_{false};
  std::thread capture_thread_;
  std::mutex frames_mutex_;
  std::vector<FrameData> frames_;
  int fps_ = 30;
  int resolution_divisor_ = 1;
  int capture_width_ = 0;
  int capture_height_ = 0;
  std::string current_output_path_;
};

}

#endif

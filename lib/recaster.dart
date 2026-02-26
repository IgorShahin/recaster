import 'recaster_platform_interface.dart';

class Recaster {
  Future<String?> getPlatformVersion() {
    return RecasterPlatform.instance.getPlatformVersion();
  }

  Future<void> startRecording({
    required String outputPath,
    int fps = 30,
    int resolutionDivisor = 1,
  }) {
    return RecasterPlatform.instance.startRecording(
      outputPath: outputPath,
      fps: fps,
      resolutionDivisor: resolutionDivisor,
    );
  }

  Future<String?> stopRecording() {
    return RecasterPlatform.instance.stopRecording();
  }

  Future<bool> isRecording() {
    return RecasterPlatform.instance.isRecording();
  }
}

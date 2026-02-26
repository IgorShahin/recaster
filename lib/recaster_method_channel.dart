import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import 'recaster_platform_interface.dart';

class MethodChannelRecaster extends RecasterPlatform {
  @visibleForTesting
  final methodChannel = const MethodChannel('recaster');

  @override
  Future<String?> getPlatformVersion() async {
    final version =
        await methodChannel.invokeMethod<String>('getPlatformVersion');
    return version;
  }

  @override
  Future<void> startRecording({
    required String outputPath,
    int fps = 30,
    int resolutionDivisor = 1,
  }) async {
    await methodChannel.invokeMethod<void>(
      'startRecording',
      <String, Object>{
        'outputPath': outputPath,
        'fps': fps,
        'resolutionDivisor': resolutionDivisor,
      },
    );
  }

  @override
  Future<String?> stopRecording() async {
    return methodChannel.invokeMethod<String>('stopRecording');
  }

  @override
  Future<bool> isRecording() async {
    final value = await methodChannel.invokeMethod<bool>('isRecording');
    return value ?? false;
  }
}

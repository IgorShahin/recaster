import 'package:plugin_platform_interface/plugin_platform_interface.dart';

import 'recaster_method_channel.dart';

abstract class RecasterPlatform extends PlatformInterface {
  RecasterPlatform() : super(token: _token);

  static final Object _token = Object();

  static RecasterPlatform _instance = MethodChannelRecaster();

  static RecasterPlatform get instance => _instance;

  static set instance(RecasterPlatform instance) {
    PlatformInterface.verifyToken(instance, _token);
    _instance = instance;
  }

  Future<String?> getPlatformVersion() {
    throw UnimplementedError('getPlatformVersion() has not been implemented.');
  }

  Future<void> startRecording({
    required String outputPath,
    int fps = 30,
    int resolutionDivisor = 1,
  }) {
    throw UnimplementedError('startRecording() has not been implemented.');
  }

  Future<String?> stopRecording() {
    throw UnimplementedError('stopRecording() has not been implemented.');
  }

  Future<bool> isRecording() {
    throw UnimplementedError('isRecording() has not been implemented.');
  }
}

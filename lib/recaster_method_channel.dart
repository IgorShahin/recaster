import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import 'recaster_platform_interface.dart';

/// An implementation of [RecasterPlatform] that uses method channels.
class MethodChannelRecaster extends RecasterPlatform {
  /// The method channel used to interact with the native platform.
  @visibleForTesting
  final methodChannel = const MethodChannel('recaster');

  @override
  Future<String?> getPlatformVersion() async {
    final version = await methodChannel.invokeMethod<String>('getPlatformVersion');
    return version;
  }
}

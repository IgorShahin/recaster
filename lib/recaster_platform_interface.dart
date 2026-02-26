import 'package:plugin_platform_interface/plugin_platform_interface.dart';

import 'recaster_method_channel.dart';

abstract class RecasterPlatform extends PlatformInterface {
  /// Constructs a RecasterPlatform.
  RecasterPlatform() : super(token: _token);

  static final Object _token = Object();

  static RecasterPlatform _instance = MethodChannelRecaster();

  /// The default instance of [RecasterPlatform] to use.
  ///
  /// Defaults to [MethodChannelRecaster].
  static RecasterPlatform get instance => _instance;

  /// Platform-specific implementations should set this with their own
  /// platform-specific class that extends [RecasterPlatform] when
  /// they register themselves.
  static set instance(RecasterPlatform instance) {
    PlatformInterface.verifyToken(instance, _token);
    _instance = instance;
  }

  Future<String?> getPlatformVersion() {
    throw UnimplementedError('platformVersion() has not been implemented.');
  }
}

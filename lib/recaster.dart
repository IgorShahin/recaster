
import 'recaster_platform_interface.dart';

class Recaster {
  Future<String?> getPlatformVersion() {
    return RecasterPlatform.instance.getPlatformVersion();
  }
}

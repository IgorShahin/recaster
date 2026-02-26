import 'package:flutter_test/flutter_test.dart';
import 'package:recaster/recaster.dart';
import 'package:recaster/recaster_platform_interface.dart';
import 'package:recaster/recaster_method_channel.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';

class MockRecasterPlatform
    with MockPlatformInterfaceMixin
    implements RecasterPlatform {

  @override
  Future<String?> getPlatformVersion() => Future.value('42');
}

void main() {
  final RecasterPlatform initialPlatform = RecasterPlatform.instance;

  test('$MethodChannelRecaster is the default instance', () {
    expect(initialPlatform, isInstanceOf<MethodChannelRecaster>());
  });

  test('getPlatformVersion', () async {
    Recaster recasterPlugin = Recaster();
    MockRecasterPlatform fakePlatform = MockRecasterPlatform();
    RecasterPlatform.instance = fakePlatform;

    expect(await recasterPlugin.getPlatformVersion(), '42');
  });
}

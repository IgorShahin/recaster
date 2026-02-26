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

  @override
  Future<bool> isRecording() => Future.value(true);

  @override
  Future<void> startRecording(
      {required String outputPath,
      int fps = 30,
      int resolutionDivisor = 1}) async {}

  @override
  Future<String?> stopRecording() => Future.value('/tmp/recording.mp4');
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

  test('isRecording', () async {
    Recaster recasterPlugin = Recaster();
    MockRecasterPlatform fakePlatform = MockRecasterPlatform();
    RecasterPlatform.instance = fakePlatform;

    expect(await recasterPlugin.isRecording(), true);
  });

  test('stopRecording', () async {
    Recaster recasterPlugin = Recaster();
    MockRecasterPlatform fakePlatform = MockRecasterPlatform();
    RecasterPlatform.instance = fakePlatform;

    expect(await recasterPlugin.stopRecording(), '/tmp/recording.mp4');
  });
}

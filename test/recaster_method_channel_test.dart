import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:recaster/recaster_method_channel.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  MethodChannelRecaster platform = MethodChannelRecaster();
  const MethodChannel channel = MethodChannel('recaster');
  final List<MethodCall> calls = <MethodCall>[];

  setUp(() {
    calls.clear();
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(
      channel,
      (MethodCall methodCall) async {
        calls.add(methodCall);
        switch (methodCall.method) {
          case 'getPlatformVersion':
            return '42';
          case 'isRecording':
            return true;
          case 'stopRecording':
            return '/tmp/out.mp4';
          case 'startRecording':
            return null;
          default:
            return null;
        }
      },
    );
  });

  tearDown(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, null);
  });

  test('getPlatformVersion', () async {
    expect(await platform.getPlatformVersion(), '42');
  });

  test('startRecording', () async {
    await platform.startRecording(
      outputPath: '/tmp/out.mp4',
      fps: 24,
      resolutionDivisor: 2,
    );
    expect(calls.single.method, 'startRecording');
    expect(
      calls.single.arguments,
      <String, Object>{
        'outputPath': '/tmp/out.mp4',
        'fps': 24,
        'resolutionDivisor': 2,
      },
    );
  });

  test('stopRecording', () async {
    expect(await platform.stopRecording(), '/tmp/out.mp4');
  });

  test('isRecording', () async {
    expect(await platform.isRecording(), true);
  });
}

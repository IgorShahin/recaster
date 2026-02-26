# recaster

Flutter plugin for desktop application-window recording with platform-specific native code.

## Supported platforms

- macOS: internal native recording pipeline (`AVFoundation` + app window capture).
- Windows: internal app-window frame capture via GDI, encoded to AVI in plugin.
- Linux: internal GTK app-window frame capture via GDK, encoded to AVI in plugin.

## Dart API

```dart
import 'package:recaster/recaster.dart';

final recaster = Recaster();

await recaster.startRecording(
  outputPath: '/tmp/recording.mp4',
  fps: 30,
  resolutionDivisor: 2, // 1=original, 2=half width/height, 3=one-third
);

final isRunning = await recaster.isRecording();
final savedPath = await recaster.stopRecording();
```

## Notes

- This version does not use `ffmpeg` or external recorder processes.
- On macOS output is `.mp4`; on Windows/Linux output is uncompressed `.avi`.
- Keep `outputPath` extension aligned with platform backend to avoid confusion.
- Use `resolutionDivisor` to reduce output resolution and file size.
- `stopRecording` returns the saved file path when recording finalized successfully.

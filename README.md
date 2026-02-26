# recaster

Desktop Flutter plugin for recording the **current app window** with native platform backends.

- No external recorder process
- No `ffmpeg` dependency
- Unified Dart API for macOS, Windows, Linux

## Features

- Start/stop recording
- Query current recording state
- Control FPS
- Reduce output resolution with `resolutionDivisor`

## Platform Support

| Platform | Backend | Output |
|---|---|---|
| macOS | AVFoundation + app window capture | `.mp4` |
| Windows | GDI window capture + internal AVI writer | `.avi` |
| Linux | GTK/GDK window capture + internal AVI writer | `.avi` |

## Installation

Add the plugin to `pubspec.yaml`:

```yaml
dependencies:
  recaster:
    git:
      url: git@github.com:IgorShahin/recaster.git
      ref: dev
```

or (for local development):

```yaml
dependencies:
  recaster:
    path: ../recaster
```

Then run:

```bash
flutter pub get
```

## API

```dart
Future<void> startRecording({
  required String outputPath,
  int fps = 30,
  int resolutionDivisor = 1,
});

Future<String?> stopRecording();
Future<bool> isRecording();
```

### Parameters

- `outputPath`: target file path
- `fps`: capture rate, typical range `15..60`
- `resolutionDivisor`:
  - `1` = original window size
  - `2` = half width/height
  - `3` = one-third width/height

## Usage Example

```dart
import 'package:recaster/recaster.dart';

final recaster = Recaster();

await recaster.startRecording(
  outputPath: '/tmp/recording.mp4', // use .avi on Windows/Linux
  fps: 30,
  resolutionDivisor: 2,
);

final recording = await recaster.isRecording();
final savedPath = await recaster.stopRecording();
```

## Important Notes

- Use `.mp4` on macOS and `.avi` on Windows/Linux.
- Windows/Linux use uncompressed AVI output; files can be large.
- `stopRecording()` returns saved file path on success.
- Calling `startRecording()` while already recording returns a platform error.

## Permissions

### macOS

Window/screen capture requires system permission.

1. Open `System Settings` -> `Privacy & Security` -> `Screen Recording`
2. Enable your app
3. Restart the app if required by macOS

## Example App

See the plugin example with UI controls for:

- output path
- FPS
- resolution divisor
- start/stop/status buttons

Path: `example/lib/main.dart`

## Current Limitations

- Recording target is the app window, not arbitrary desktop/window selection.
- No audio capture in current implementation.
- No pause/resume yet.

## License

See [LICENSE](LICENSE).

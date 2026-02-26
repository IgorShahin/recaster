<p align="center">
  <img src="docs/assets/logo.png" width="520" alt="Recaster" />
</p>

<h2 align="center">Desktop App-Window Recorder for Flutter</h2>

<p align="center">
  Native screen recording plugin for macOS, Windows, and Linux
</p>

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

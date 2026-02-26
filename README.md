<p align="center">
  <img src="doc/assets/logo.png" width="520" alt="Recaster" />
</p>
<h2 align="center">Desktop App-Window Recorder for Flutter</h2>

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

- Recording target is Flutter view content.
- Use `.mp4` on macOS and `.avi` on Windows/Linux.
- On Windows/Linux AVI is uncompressed and can be large.
- `startRecording` validates output path before capture starts.

## Permissions

### macOS

- First capture may require Screen Recording permission in:
  - `System Settings -> Privacy & Security -> Screen Recording`
- If app sandbox is enabled, writing to arbitrary paths may be restricted.

## Path Validation Errors

`startRecording` can return clear platform errors for invalid report paths:

- `invalid_output_path`
- `directory_create_failed`
- `path_not_writable`

Use these codes to handle invalid output path configuration in your app.

## Usage Recommendations

- Always pass an absolute `outputPath`.
- Create unique output file names to avoid overwriting previous recordings.
- Keep `resolutionDivisor > 1` for long runs to reduce size.
- Always call `stopRecording()` to finalize the output file.

## Troubleshooting

- `stop_failed` on macOS often means permission/path issues.
- If file is missing after stop, verify:
  - final path exists
  - process has write access
  - correct extension per platform

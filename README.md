<p align="center">
  <img src="doc/assets/logo.png" width="520" alt="Recaster" />
</p>
<h2 align="center">Desktop Flutter View Recorder</h2>

## Features

- Start/stop recording
- Query current recording state
- Control FPS
- Reduce output resolution with `resolutionDivisor`

## Platform Support

| Platform | Backend | Output |
|---|---|---|
| macOS | NSView (Flutter view) frame capture + AVAssetWriter (H.264) | `.mp4` |
| Windows | GDI capture of Flutter native view handle + internal AVI writer | `.avi` |
| Linux | GTK/GDK capture of `FlView` widget (fallback: root window) + internal AVI writer | `.avi` |

## Get Started

### 1. Add dependency

```yaml
dependencies:
  recaster: ^0.0.1
```

### 2. Install

```bash
flutter pub get
```

### 3. Import

```dart
import 'package:recaster/recaster.dart';
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

- Recording target is Flutter view content on all desktop platforms.
- `startRecording` validates output path before capture starts.
- Always call `stopRecording()` to finalize and flush the output file.

## Platform Notes

### macOS

- Capture source: Flutter view via `NSView` frame capture.
- Output format: `.mp4` (H.264, via `AVAssetWriter`).
- If app sandbox is enabled, writing to arbitrary paths may be restricted by entitlements.

### Windows

- Capture source: Flutter native view handle via GDI.
- Output format: `.avi` (internal AVI writer).
- AVI output is uncompressed and can be large.

### Linux

- Capture source: `FlView` widget via GTK/GDK (`root window` fallback).
- Output format: `.avi` (internal AVI writer).
- AVI output is uncompressed and can be large.

## Path Validation Errors

`startRecording` can return clear platform errors for invalid output paths:

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

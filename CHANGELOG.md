## 0.1.0

- Added desktop recording API: `startRecording`, `stopRecording`, `isRecording`.
- Added internal Flutter view capture on all desktop platforms.
- Added MP4 output on macOS and Windows:
  - macOS via AVFoundation
  - Windows via Media Foundation
- Added AVI output on Linux via internal GTK/GDK pipeline.
- Added `resolutionDivisor` in `startRecording` to reduce output resolution and file size.
- Added output path preflight validation with explicit platform error codes:
  - `invalid_output_path`
  - `directory_create_failed`
  - `path_not_writable`

import Cocoa
import AVFoundation
import FlutterMacOS

public class RecasterPlugin: NSObject, FlutterPlugin {
  private var assetWriter: AVAssetWriter?
  private var writerInput: AVAssetWriterInput?
  private var pixelBufferAdaptor: AVAssetWriterInputPixelBufferAdaptor?
  private var captureTimer: DispatchSourceTimer?
  private var frameIndex: Int64 = 0
  private var fps: Int32 = 30
  private var resolutionDivisor: Int = 1
  private var outputWidth: Int = 0
  private var outputHeight: Int = 0
  private var targetWindowId: CGWindowID = 0
  private var currentOutputPath: String?

  public static func register(with registrar: FlutterPluginRegistrar) {
    let channel = FlutterMethodChannel(name: "recaster", binaryMessenger: registrar.messenger)
    let instance = RecasterPlugin()
    registrar.addMethodCallDelegate(instance, channel: channel)
  }

  public func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    switch call.method {
    case "getPlatformVersion":
      result("macOS " + ProcessInfo.processInfo.operatingSystemVersionString)
    case "startRecording":
      startRecording(call: call, result: result)
    case "stopRecording":
      stopRecording(result: result)
    case "isRecording":
      result(assetWriter != nil)
    default:
      result(FlutterMethodNotImplemented)
    }
  }

  private func startRecording(call: FlutterMethodCall, result: @escaping FlutterResult) {
    guard assetWriter == nil else {
      result(
        FlutterError(
          code: "already_recording",
          message: "Screen recording is already running.",
          details: nil
        )
      )
      return
    }

    guard let args = call.arguments as? [String: Any],
      let outputPath = args["outputPath"] as? String,
      !outputPath.isEmpty
    else {
      result(
        FlutterError(
          code: "invalid_args",
          message: "outputPath is required.",
          details: nil
        )
      )
      return
    }

    guard let window = NSApplication.shared.keyWindow
      ?? NSApplication.shared.mainWindow
      ?? NSApplication.shared.windows.first(where: { $0.isVisible })
    else {
      result(
        FlutterError(
          code: "window_not_found",
          message: "Could not find an active application window to record.",
          details: nil
        )
      )
      return
    }

    let windowId = CGWindowID(window.windowNumber)
    let requestedFps = max(1, min(60, (args["fps"] as? Int) ?? 30))
    let requestedDivisor = max(1, min(8, (args["resolutionDivisor"] as? Int) ?? 1))
    self.fps = Int32(requestedFps)
    self.resolutionDivisor = requestedDivisor
    frameIndex = 0

    guard let firstFrame = captureWindowFrame(windowId: windowId) else {
      result(
        FlutterError(
          code: "capture_failed",
          message: "Unable to capture app window frame. Check screen recording permission.",
          details: nil
        )
      )
      return
    }

    var width = max(1, firstFrame.width / resolutionDivisor)
    var height = max(1, firstFrame.height / resolutionDivisor)
    if width > 1 && width % 2 != 0 {
      width -= 1
    }
    if height > 1 && height % 2 != 0 {
      height -= 1
    }
    if width <= 0 || height <= 0 {
      result(
        FlutterError(
          code: "invalid_size",
          message: "Captured frame has invalid size.",
          details: nil
        )
      )
      return
    }

    let outputURL = URL(fileURLWithPath: outputPath)
    try? FileManager.default.removeItem(at: outputURL)

    do {
      let writer = try AVAssetWriter(url: outputURL, fileType: .mp4)
      let settings: [String: Any] = [
        AVVideoCodecKey: AVVideoCodecType.h264,
        AVVideoWidthKey: width,
        AVVideoHeightKey: height,
      ]
      let input = AVAssetWriterInput(mediaType: .video, outputSettings: settings)
      input.expectsMediaDataInRealTime = true
      let attributes: [String: Any] = [
        kCVPixelBufferPixelFormatTypeKey as String: Int(kCVPixelFormatType_32BGRA),
        kCVPixelBufferWidthKey as String: width,
        kCVPixelBufferHeightKey as String: height,
        kCVPixelBufferCGImageCompatibilityKey as String: true,
        kCVPixelBufferCGBitmapContextCompatibilityKey as String: true,
      ]
      let adaptor = AVAssetWriterInputPixelBufferAdaptor(
        assetWriterInput: input,
        sourcePixelBufferAttributes: attributes
      )

      if !writer.canAdd(input) {
        result(
          FlutterError(
            code: "writer_setup_failed",
            message: "Could not configure video writer input.",
            details: nil
          )
        )
        return
      }

      writer.add(input)
      writer.startWriting()
      writer.startSession(atSourceTime: .zero)

      assetWriter = writer
      writerInput = input
      pixelBufferAdaptor = adaptor
      currentOutputPath = outputPath
      targetWindowId = windowId
      outputWidth = width
      outputHeight = height

      appendFrame(firstFrame)
      startCaptureTimer()
      result(nil)
    } catch {
      result(
        FlutterError(
          code: "start_failed",
          message: "Failed to start screen recording writer.",
          details: error.localizedDescription
        )
      )
    }
  }

  private func startCaptureTimer() {
    let timer = DispatchSource.makeTimerSource(queue: DispatchQueue.global(qos: .userInitiated))
    let interval = DispatchTimeInterval.milliseconds(max(1, 1000 / Int(fps)))
    timer.schedule(deadline: .now() + interval, repeating: interval)
    timer.setEventHandler { [weak self] in
      self?.captureTick()
    }
    captureTimer = timer
    timer.resume()
  }

  private func captureTick() {
    guard let frame = captureWindowFrame(windowId: targetWindowId) else {
      return
    }
    appendFrame(frame)
  }

  private func appendFrame(_ frame: CGImage) {
    guard
      let input = writerInput,
      let adaptor = pixelBufferAdaptor,
      let writer = assetWriter,
      writer.status == .writing,
      input.isReadyForMoreMediaData
    else {
      return
    }

    guard let pixelBuffer = makePixelBuffer(from: frame) else {
      return
    }

    let time = CMTime(value: frameIndex, timescale: fps)
    if adaptor.append(pixelBuffer, withPresentationTime: time) {
      frameIndex += 1
    }
  }

  private func captureWindowFrame(windowId: CGWindowID) -> CGImage? {
    return CGWindowListCreateImage(
      .null,
      .optionIncludingWindow,
      windowId,
      [.bestResolution, .boundsIgnoreFraming]
    )
  }

  private func makePixelBuffer(from image: CGImage) -> CVPixelBuffer? {
    let width = outputWidth
    let height = outputHeight
    if width <= 0 || height <= 0 {
      return nil
    }
    var pixelBuffer: CVPixelBuffer?
    let status = CVPixelBufferCreate(
      kCFAllocatorDefault,
      width,
      height,
      kCVPixelFormatType_32BGRA,
      [
        kCVPixelBufferCGImageCompatibilityKey: true,
        kCVPixelBufferCGBitmapContextCompatibilityKey: true,
      ] as CFDictionary,
      &pixelBuffer
    )
    if status != kCVReturnSuccess || pixelBuffer == nil {
      return nil
    }

    CVPixelBufferLockBaseAddress(pixelBuffer!, [])
    defer { CVPixelBufferUnlockBaseAddress(pixelBuffer!, []) }

    guard let baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer!) else {
      return nil
    }
    let bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer!)
    guard let context = CGContext(
      data: baseAddress,
      width: width,
      height: height,
      bitsPerComponent: 8,
      bytesPerRow: bytesPerRow,
      space: CGColorSpaceCreateDeviceRGB(),
      bitmapInfo: CGImageAlphaInfo.premultipliedFirst.rawValue
        | CGBitmapInfo.byteOrder32Little.rawValue
    ) else {
      return nil
    }

    context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
    return pixelBuffer
  }

  private func resetRecorderState() {
    captureTimer?.cancel()
    captureTimer = nil
    assetWriter = nil
    writerInput = nil
    pixelBufferAdaptor = nil
    targetWindowId = 0
    frameIndex = 0
    fps = 30
    resolutionDivisor = 1
    outputWidth = 0
    outputHeight = 0
  }

  private func stopRecording(result: @escaping FlutterResult) {
    guard let writer = assetWriter, let input = writerInput else {
      result(nil)
      return
    }

    captureTimer?.cancel()
    captureTimer = nil
    let outputPath = currentOutputPath
    currentOutputPath = nil
    input.markAsFinished()

    writer.finishWriting { [weak self] in
      DispatchQueue.main.async {
        if writer.status == .completed {
          self?.resetRecorderState()
          result(outputPath)
          return
        }

        let details = writer.error?.localizedDescription ?? "Unknown writer error."
        self?.resetRecorderState()
        result(
          FlutterError(
            code: "stop_failed",
            message: "Failed to finalize recording file.",
            details: details
          )
        )
      }
    }
  }
}

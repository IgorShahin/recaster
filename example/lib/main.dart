import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:recaster/recaster.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  String _platformVersion = 'Unknown';
  String _status = 'Idle';
  String _outputPath = '';
  String _lastSaved = '';
  final _recasterPlugin = Recaster();
  final _fpsController = TextEditingController(text: '30');
  final _resolutionDivisorController = TextEditingController(text: '1');
  final _pathController = TextEditingController();

  @override
  void initState() {
    super.initState();
    initPlatformState();
    _outputPath = _buildDefaultOutputPath();
    _pathController.text = _outputPath;
  }

  @override
  void dispose() {
    _fpsController.dispose();
    _resolutionDivisorController.dispose();
    _pathController.dispose();
    super.dispose();
  }

  Future<void> initPlatformState() async {
    String platformVersion;
    try {
      platformVersion = await _recasterPlugin.getPlatformVersion() ?? 'Unknown platform version';
    } on PlatformException {
      platformVersion = 'Failed to get platform version.';
    }

    if (!mounted) return;

    setState(() {
      _platformVersion = platformVersion;
    });
  }

  String _buildDefaultOutputPath() {
    final now = DateTime.now().millisecondsSinceEpoch;
    final extension = (Platform.isMacOS || Platform.isWindows) ? 'mp4' : 'avi';
    return '${Directory.systemTemp.path}${Platform.pathSeparator}recaster_$now.$extension';
  }

  Future<void> _refreshStatus() async {
    try {
      final recording = await _recasterPlugin.isRecording();
      if (!mounted) return;
      setState(() {
        _status = recording ? 'Recording' : 'Idle';
      });
    } on PlatformException catch (e) {
      if (!mounted) return;
      setState(() {
        _status = 'Status error: ${e.message ?? e.code}';
      });
    }
  }

  Future<void> _startRecording() async {
    final parsedFps = int.tryParse(_fpsController.text.trim()) ?? 30;
    final fps = parsedFps <= 0 ? 30 : parsedFps;
    final parsedDivisor = int.tryParse(_resolutionDivisorController.text.trim()) ?? 1;
    final resolutionDivisor = parsedDivisor <= 0 ? 1 : parsedDivisor;
    try {
      await _recasterPlugin.startRecording(
        outputPath: _outputPath,
        fps: fps,
        resolutionDivisor: resolutionDivisor,
      );
      if (!mounted) return;
      setState(() {
        _status = 'Recording';
        _lastSaved = '';
      });
    } on PlatformException catch (e) {
      if (!mounted) return;
      setState(() {
        final details = e.details?.toString();
        _status = details == null || details.isEmpty
            ? 'Start error: ${e.message ?? e.code}'
            : 'Start error: ${e.message ?? e.code} ($details)';
      });
    }
  }

  Future<void> _stopRecording() async {
    try {
      final path = await _recasterPlugin.stopRecording();
      final exists = path != null && File(path).existsSync();
      if (!mounted) return;
      setState(() {
        _status = exists
            ? 'Idle'
            : 'Stop warning: file was not found on disk';
        _lastSaved = path ?? '';
        _outputPath = _buildDefaultOutputPath();
        _pathController.text = _outputPath;
      });
    } on PlatformException catch (e) {
      if (!mounted) return;
      setState(() {
        final details = e.details?.toString();
        _status = details == null || details.isEmpty
            ? 'Stop error: ${e.message ?? e.code}'
            : 'Stop error: ${e.message ?? e.code} ($details)';
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(
          title: const Text('Recaster Example'),
        ),
        body: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text('Running on: $_platformVersion'),
              const SizedBox(height: 12),
              Text('Status: $_status'),
              const SizedBox(height: 12),
              TextField(
                decoration: const InputDecoration(
                  labelText: 'Output file path',
                  border: OutlineInputBorder(),
                ),
                controller: _pathController,
                onChanged: (value) {
                  _outputPath = value;
                },
              ),
              const SizedBox(height: 12),
              TextField(
                controller: _fpsController,
                keyboardType: TextInputType.number,
                decoration: const InputDecoration(
                  labelText: 'FPS',
                  border: OutlineInputBorder(),
                ),
              ),
              const SizedBox(height: 12),
              TextField(
                controller: _resolutionDivisorController,
                keyboardType: TextInputType.number,
                decoration: const InputDecoration(
                  labelText: 'Resolution divisor (1=full, 2=half)',
                  border: OutlineInputBorder(),
                ),
              ),
              const SizedBox(height: 16),
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  ElevatedButton(
                    onPressed: _startRecording,
                    child: const Text('Start'),
                  ),
                  ElevatedButton(
                    onPressed: _stopRecording,
                    child: const Text('Stop'),
                  ),
                  OutlinedButton(
                    onPressed: _refreshStatus,
                    child: const Text('Status'),
                  ),
                ],
              ),
              const SizedBox(height: 12),
              Text('Next output: $_outputPath'),
              if (_lastSaved.isNotEmpty) Text('Saved: $_lastSaved'),
            ],
          ),
        ),
      ),
    );
  }
}

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.2.0] - 2025-12-16

### Added
- Check if Premiere CEP HTTP server is alive before sending markers
- Original CEP extension code included in repository

### Changed
- Refactored main.c and split build_ui into multiple functions
- Turned clay_renderer_SDL3 into proper header+src files
- Standardized handler function naming conventions
- Use SDL standard functions where possible
- Use SDL atomic operations for connected app state

### Fixed
- Command injection vulnerability
- Cursor drag interaction
- Memory management: free clay memory buffer and image icons on shutdown
- Handle memory allocation failures in process_audio_file
- Additional guards and clamping for waveform interaction
- Null checks for tooltip text, CEP install error messages, and userData
- Proper exit code handling in extension installers
- Scrollbar hover style reset
- Null termination for last ignored version in updater state

## [2.1.0] - 2025-11-25

### Added
- Visual feedback for CEP extension installation process
- Thread-safety documentation for CEP install state

### Changed
- Made CEP installation section reusable

### Fixed
- Hide console window on Windows
- Use SDL_AtomicInt for CepInstallStatus to avoid data race
- Reset CEP install status on help modal activation
- Ensure base_path is null-terminated in install_cep_extension

## [2.0.0] - 2025-10-29

### Added
- Auto-update functionality with version checking and download support
- DMG packaging for macOS updates (switched from ZIP)
- macOS code signing and notarization
- AppImage packaging for Linux (unstable, non-recommended)
- GitHub releases automation with artifacts

### Changed
- Completely new UI, ditching PySide6 (Qt) in favour of [Clay](https://github.com/nicbarker/clay) and [SDL3](https://wiki.libsdl.org/SDL3)
- Replaced [librosa](https://librosa.org/) with [CARA](https://github.com/8g6-new/CARA) for audio beat detection
- Use [SDL_sound](https://github.com/libsdl-org/SDL_sound) for audio decoding

### Removed
- Removed sliders for selecting a subset of detected beats, instead this is now done via "selection markers" which provide a way to set an in-point and out-point on the waveform. The in-point defines the 0 time in the editor timeline, from which the markers will be placed.

[unreleased]: https://github.com/acrilique/automarker-clay/compare/v2.2.0...HEAD
[2.2.0]: https://github.com/acrilique/automarker-clay/compare/v2.1.0...v2.2.0
[2.1.0]: https://github.com/acrilique/automarker-clay/compare/v2.0.0...v2.1.0
[2.0.0]: https://github.com/acrilique/automarker-clay/commits/v2.0.0
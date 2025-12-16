
# AutoMarker

AutoMarker is a GUI application that allows users to place markers on a Premiere Pro sequence, an After Effects composition or a Davinci Resolve Studio timeline based on a music file's tempo. This is useful for placing clips faster when you want them to be related to the music that is playing. The application uses the CARA library to detect beats in the audio file.

## Features

 - Detects beats in an audio file and places/removes markers in a Premiere Pro sequence, an After Effects composition or a Davinci Resolve Studio timeline.

 - Supports audio files in formats such as .wav, .mp3, .flac, .ogg, and .aiff.

 - Checks if any of the apps are running and provides feedback to the user.

## Installation

AutoMarker is packaged for Windows and macOS. To obtain the latest release, go to the [releases section](https://github.com/acrilique/automarker-clay/releases). Regarding Linux, an AppImage is available, but it is recommended to build the app from source. Despite that, I do not recommend using this on Linux as it is not very well tested.

## Usage

Open the executable file to launch the application. 

**Important**: If you want to use this app with Premiere Pro, you will need to install the extension for it. For that, click on the "Help" button and press the "Install CEP Extension" button. You will need to restart Premiere Pro after the installation. The extension allows AutoMarker to send commands to Premiere Pro.

To use AutoMarker, follow these steps:

 1. Click the "Select audio file" button to choose an audio file.

 2. If Premiere, AfterFX or Resolve is running, click the "Create markers" button to place markers on the active sequence, composition or timeline based on the audio file's tempo. 

 3. You can adjust the selection markers to define the in-point and out-point on the waveform, which sets the range for placing markers in the editor timeline. A quick way to do this is by holding the Ctrl key and clicking+dragging on the waveform.

 4. In some programs, your sequence/timeline needs to have a duration equal or greater than the input audio file's. For that, please place some element (video or audio clip) in the sequence/timeline before sending the markers.

It is important to note that having more than 1 of the supported apps active at the same time can cause unexpected behaviour.

## Troubleshooting

If you encounter any issues while using AutoMarker, please get in touch via [GitHub Issues](https://github.com/acrilique/automarker-clay/issues) and I'll try to check it as soon as I'm free. I'm also open to feature suggestions!
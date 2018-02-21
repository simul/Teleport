# RemotePlay prototype

## Building

1. Open `Tools/StreamingTest/StreamingTest.sln` and build solution in both `Release` and `ReleaseLibrary` configurations. The latter will ensure proper deployment of UE4 plugin dependencies.
2. Generate UE4 project files.
3. Build `RemotePlay` UE4 project in `Win64 Development Editor` configuration.

## Testing NVENC video encoding

To check if NVENC hardware encoder is functional on your system run `Tools/StreamingTest/CheckNVENC.bat` script. You should see two `StreamingTest` application windows, one encoding via NVENC and streaming over UDP, the other one recieving & decoding the UDP stream.

The included test scene is quite expensive to render therefore framerate is not indicative of encoding overhead, which should be minimal.

## Testing RemotePlay plugin

To achieve best performance run two instances of `RemotePlay` project in standalone game mode (without the editor running in the background).

1. Open `RemotePlay.sln` in Visual Studio.
2. Set `RemotePlay` as start-up project.
3. Append `-game` to the command line.
4. Hit `Ctrl+F5` twice.

After two game windows appear select `Act as Play Server` in the first, and then `Connect to Play Server` in the second (default connection address is localhost, so it should work out of the box). Keep in mind that you can only control the player while the client window has input focus.

Use `StreamingTest` application to "peek" into the video stream sent by listen server (run `Tools/StreamingTest/Build/x64/Release/StreamingTest.exe`).

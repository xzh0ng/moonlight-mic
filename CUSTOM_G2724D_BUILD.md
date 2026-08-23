# Moonlight Mic + G2724D integration

This macOS build keeps the existing Moonlight microphone-passthrough protocol
and adds local monitor input switching directly to Moonlight.

## Behavior

- After a stream named **Remote Input + Audio** connects successfully,
  Moonlight activates itself, raises the SDL streaming window, waits briefly
  for macOS focus ordering, and switches display index 1 to DP1.
- Other Moonlight applications never trigger an automatic display switch.
- `Command-Control-G` switches display index 1 to DP1.
- `Option-Control-G` switches display index 1 to HDMI 1.
- **Remote Input + Audio** always starts in relative/FPS mouse mode for
  unrestricted camera turning, regardless of the saved global remote-desktop
  mouse preference. `Control-Option-Shift-M` remains available as a temporary
  in-stream mode toggle.
- After `Option-Control-G` switches to HDMI 1, Moonlight cleanly ends only the
  active stream and returns to its normal UI. Moonlight and Apollo stay open,
  making it quick to launch **Remote Input + Audio** again.
- After the stream ends, Moonlight waits for macOS to publish the HDMI display,
  moves its UI away from any offline screen, and raises it on the Mac desktop.
- DP1 is DDC input value 15; HDMI 1 is DDC input value 17.
- Monitor commands are serialized and executed directly without a shell.
- The packaged app contains the MIT-licensed `m1ddc` 1.2.0 arm64 binary in
  `Contents/Resources`, with Homebrew locations used only as a fallback.

Quit the standalone **G2724D Input Switcher** before starting this Moonlight
build. macOS only permits one application to own a global shortcut, so leaving
the old switcher open prevents Moonlight from registering the same shortcuts.

## Microphone permission

The bundle includes `NSMicrophoneUsageDescription`. After installing the app:

1. Open Moonlight directly from `/Applications`.
2. Enable **Stream client microphone to host** and select the desired input.
3. Start **Remote Input + Audio**.
4. Choose **Allow** when macOS requests microphone access.

If no prompt appears, enable Moonlight under **System Settings > Privacy &
Security > Microphone**. Do not launch Moonlight through the old G2724D helper;
doing so makes macOS attribute microphone access to the helper.

For Loopback, use mix-minus routing unless deliberately feeding received PC
audio back to Windows. Otherwise a feedback loop can occur.

## Build and verification

The current artifact is an arm64 build for Apple Silicon. Focused Qt tests cover
settings persistence, mic capability gating, mic lifecycle, the automatic
application-name gate, and exact `m1ddc` arguments.

The packaged DMG is:

`build/Moonlight-G2724D-Mic-arm64.dmg`

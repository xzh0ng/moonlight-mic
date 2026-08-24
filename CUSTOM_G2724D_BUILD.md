# Moonlight Mic + G2724D integration

This macOS build keeps the existing Moonlight microphone-passthrough protocol
and adds local monitor input switching directly to Moonlight.

## Behavior

- After a stream named **Remote Input + Audio** connects successfully,
  Moonlight activates itself, raises the SDL streaming window, waits briefly
  for macOS focus ordering, and switches the G2724D to DP1.
- Other Moonlight applications never trigger an automatic display switch.
- `Command-Control-G` switches the G2724D to DP1.
- `Option-Control-G` switches the G2724D to HDMI 1.
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
- When BetterDisplay is installed, Moonlight targets the physical G2724D by
  name through BetterDisplay's DDC command, avoiding virtual-display ordering.
- Without BetterDisplay, the packaged MIT-licensed `m1ddc` 1.2.0 arm64 binary
  is used for the original single-display setup.

Quit the standalone **G2724D Input Switcher** before starting this Moonlight
build. macOS only permits one application to own a global shortcut, so leaving
the old switcher open prevents Moonlight from registering the same shortcuts.

## BetterDisplay setup

The free BetterDisplay features are sufficient for this setup. BetterDisplay
must remain running because Moonlight uses its executable directly for DDC
input switching whenever it is installed.

1. Install **BetterDisplay.app** in `/Applications` and open it.
2. Create a basic 16:9 virtual screen and leave it connected. A 2560x1440
   virtual screen matches the G2724D; 60 Hz is sufficient because this screen
   only keeps the macOS desktop and Moonlight input capture alive.
3. Do not associate the virtual screen with the G2724D or configure it to
   disconnect when the physical display disappears. It must stay online after
   the monitor changes to DP1.
4. Enable BetterDisplay at login so the virtual screen and DDC backend are
   available before Moonlight starts.
5. In the G2724D on-screen menu, ensure **DDC/CI** is enabled.
6. If BetterDisplay's 14-day Pro trial is active, verify this setup works in
   free mode by disabling **Licensing & Pro Features** under **Settings >
   Application > Advanced settings & privacy**.

No separate BetterDisplay command-line tool, HTTP server, or paid license is
required. Moonlight selects the physical monitor by the name `G2724D`, so the
virtual screen cannot change the target through display-number reordering.

After installing this Moonlight build, start **Remote Input + Audio**. A
successful automatic switch contains these log entries:

```text
Display input: switching DELL G2724D to input 15 via BetterDisplay
Display input: BetterDisplay completed successfully
```

Press `Option-Control-G` to return to HDMI 1. The corresponding successful log
uses input value `17`. If BetterDisplay is not installed, Moonlight falls back
to bundled `m1ddc`, which is intended only for the original single-display
setup without a virtual screen.

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
application-name gate, and exact BetterDisplay and `m1ddc` arguments.

The packaged DMG is:

`build/Moonlight-G2724D-Mic-arm64.dmg`

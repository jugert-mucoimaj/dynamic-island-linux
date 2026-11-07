# Island

A native C++ / Qt hover panel for Ubuntu on X11. It combines real MPRIS media controls, background task updates, app-reported launcher progress, and new desktop notifications. It floats below GNOME's existing bar; it does not replace that bar yet.

## Contributions welcome

Help build a polished, native dynamic island for Linux. This project is early, working, and open to contributions—from first-time contributors to experienced C++ and Linux desktop developers.

Have an idea, found a bug, or want to improve the design? [Open an issue](https://github.com/jugert-mucoimaj/dynamic-island-linux/issues) or send a pull request. Useful areas include:

- GNOME Wayland integration and a full custom top bar.
- Media artwork, seeking, and player selection.
- Background activity adapters for more apps.
- Notification integration, accessibility, and multi-monitor behavior.
- Animation performance, visual polish, tests, and documentation.

To contribute, fork the repository, create a branch, build using the instructions below, and run `ctest --test-dir build --output-on-failure`. Keep changes focused and describe what changed and how you checked it. Include a screenshot or short recording for visual changes. For larger changes, open an issue first so we can discuss the approach.

For bug reports, include your distribution, desktop environment, X11/Wayland session type, Qt version, relevant renderer logs, and steps to reproduce. Please remove private notification contents from logs and screenshots.

## Build and run

Requires a C++17 compiler, CMake, Qt 5 or 6 Widgets/D-Bus/Test, and GIO development libraries. On Ubuntu:

```sh
sudo apt install build-essential cmake pkg-config qtbase5-dev libglib2.0-dev libx11-dev libxext-dev pulseaudio-utils
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
./build/island
```

Hover to expand; move away to collapse. Opening and closing animate a painted pill and cached control image inside a fixed native surface, avoiding window and OpenGL backing-buffer resizing on each frame. X11 Shape input regions follow the pill's outline so transparent areas pass clicks to windows underneath. Click the header to toggle. Drag the top strip to reposition the island; dragging preserves its open/closed state. Position is saved across restarts and kept inside the monitor’s available workspace. Right-click → Reset position returns it to the top center. Switch between Activities and Notifications under the media controls. Quit from the expanded panel or the pill's right-click menu. Only one instance can run on your session bus.

`./build/island --demo` adds two labeled, static sample activities. Media and notification integrations always remain real.

## Real background tasks

Wrap a command with `island-run` to show it while it runs, followed by Finished or Failed for 15 seconds. The command's output and input remain in the terminal. Its exit code is preserved (127 for failure to start, 128 for an otherwise unclassified crash). Commands run normally if Island is unavailable.

```sh
./build/island-run --title 'Building Island' -- cmake --build build -j2
./build/island-run --title 'Downloading archive' -- curl -L -o archive.tar.gz https://example.com/archive.tar.gz
```

The runner shows running/completed status; it does not invent percentage progress from arbitrary command output. Arguments are passed directly to the executable. Use `-- sh -c '...'` explicitly for shell syntax. SIGINT/SIGTERM to the runner terminate its direct child (with a two-second grace period) and report interruption. Descendant process trees are not managed; SIGKILL cannot be reported reliably.

Apps that emit `com.canonical.Unity.LauncherEntry.Update` automatically appear when their `progress-visible` flag is true. Partial percentage updates are merged, hidden progress is removed, and entries disappear if their publisher disconnects. This depends on app support; it is not arbitrary process/download discovery.

### Publish from your own integration

Any process on your user session bus can publish an activity. Reusing an ID updates its title, detail, and progress. Progress is 0–100 or -1 for unknown.

```sh
gdbus call --session --dest io.github.island \
  --object-path /io/github/island/Activities \
  --method io.github.island.Activities.Publish \
  'my-build' 'Building project' 'Compiling C++ sources' 35

gdbus call --session --dest io.github.island \
  --object-path /io/github/island/Activities \
  --method io.github.island.Activities.Finish \
  'my-build' true 'Build completed'

gdbus call --session --dest io.github.island \
  --object-path /io/github/island/Activities \
  --method io.github.island.Activities.Remove 'my-build'
```

Methods return a boolean. IDs must be nonempty and at most 128 characters, titles nonempty, and progress within range. Title/detail are capped at 160/320 characters. Up to 20 activities are kept in memory. Published tasks stay until Remove, Finish plus its 15-second expiry, or app exit. Publish resets completion/expiry for that ID. There is no persistence or action API yet.

## Notifications

A native GIO D-Bus monitor connection observes new `org.freedesktop.Notifications.Notify` calls. GNOME remains the notification server, including its popup presentation and actions. Replies are correlated to server IDs so replacements update existing entries. Island stores up to 30 local previews in memory and strips body markup into plain text.

The collapsed pill shows an unread count. Opening Notifications marks its entries read. Clear removes only Island's local history. Neither operation dismisses GNOME notifications. Existing GNOME history, closed state, actions, portal-only notifications, and Do Not Disturb settings are not synchronized; the previews represent observed requests, including requests GNOME may suppress. New notifications do not automatically expand the island.

Monitoring depends on session-bus policy. When unavailable, the tab shows a status tooltip and retries every ten seconds. No bus policy or GNOME settings are changed.

Read-only diagnostics:

```sh
gdbus call --session --dest io.github.island \
  --object-path /io/github/island/Notifications \
  --method org.freedesktop.DBus.Properties.GetAll io.github.island.Notifications
```

## Media

Compatible MPRIS players are discovered every two seconds. The playing player takes priority; otherwise the previous available selection is retained. Buttons follow advertised capabilities. The playback indicator uses an OpenGL-backed surface when available, with audio-driven motion targeting approximately 60 updates per second. Animation stops when paused or hidden; the clock and media polling are independent. The renderer is logged at startup. Headless platforms or failed context probes use a raster fallback. Force that fallback with `ISLAND_SOFTWARE_RENDERING=1 ./build/island`. Qt 6 builds additionally require the OpenGLWidgets development component (Ubuntu: `libqt6opengl6-dev`).

The five opaque bars now show measured low-to-high frequency bands from system output. A `parec` subprocess reads the default output monitor as 16 kHz mono PCM; the FFT and attack/release smoothing run in C++. This works with PulseAudio and PipeWire's PulseAudio compatibility server. It monitors the mixed output, rather than isolating the selected MPRIS player, and never requests a microphone source. Capture runs while the playback indicator is active, and samples are analyzed in memory without saving audio. Missing `parec` or capture failures leave quiet bars rather than simulated motion. Set `ISLAND_DISABLE_AUDIO=1` to disable capture. Existing monitor streams may need a playback pause/resume after changing output devices. Album artwork, seeking, and player selection are pending.

## Checks and preview

```sh
ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen ./build/island --demo --screenshot /tmp/island.png
```

Tests run on an isolated D-Bus session with fake notification and media services. They cover notification observation/replacement/unread state, launcher progress and publisher disappearance, actual runner execution and failures, activity validation/update/removal/expiry, media discovery/control/disappearance, and hover behavior.

## Desktop boundaries

X11 positioning only; GNOME Wayland requires separate integration and XWayland placement is not guaranteed. The island starts on the primary monitor and can be dragged between monitors. It does not reserve workspace or replace GNOME's bar. System tray/settings controls and notification actions are pending. No autostart or desktop settings are modified.

For a hardware rendering check on your live X11 display:

```sh
ISLAND_EXPECT_GL=1 ./build/island_tests playbackFramesAndIdle
```

To additionally verify the fixed native surface and its click-through input region:

```sh
ISLAND_DISABLE_AUDIO=1 ./build/island_tests fixedSurfaceAndNativeInputShape
ISLAND_EXPECT_AUDIO=1 ./build/island_tests audioMonitorIntegration
```

The second command briefly captures system output for verification without saving it. The normal headless test suite disables live audio capture and tests the FFT against known tones, smoothing, and opacity using synthetic samples.

This checks OpenGL initialization, animation frames, and idle suspension. Hardware versus software OpenGL depends on your driver; inspect the reported renderer name.

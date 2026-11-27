# Changelog

What changed in Wotah, from the side of the people who use it. Newest first.

## 0.1.0 - 2026-11-27

The first release: an endless beach by the sea, to look at and to play in.

### Added

- An endless beach to fly over: sand, dunes, green patches and hills inland along a winding coast, and the sea to the
  west, reaching to the horizon, where the sun sets.
- Waves that behave like real ones: they roll in from the open sea, rise up, break into foaming surf, run up the sand
  and drain back down into the next wave. The sand stays wet where they reached, and lines of foam mark the highest
  ones. Wave height goes from still water to a rough 3 m sea.
- Water that looks like water: reflections, the sea floor seen through shallow water, moving light patterns on it, sun
  and moon glints, and a view from under the surface.
- A day and night cycle: the sun rises and sets, then the moon and the stars come out; everything casts shadows.
- Balls that float, bob on the waves and get carried in by the surf. Add up to 32 with "Add ball" or B. Click a ball
  and drag its arrows to move it anywhere; let go and it drops.
- A free camera: W A S D to fly, Space and Shift for up and down, hold the right mouse button to look around, the
  wheel to come closer, F to go back to the start view.
- Settings (Tab): time of day, day length, how high the sun climbs, sun, moon and sky light, wave height, gravity, time
  speed, ball bounce and field of view.
- A bar at the bottom to add balls, pause the world, and reset the scene (also R) or the camera.
- For Windows and Linux, one program file each; it needs a graphics card with Vulkan 1.3.

### The journey so far

**23 Nov: tidy first.** It began as a small program that opened a window and drew a square with Vulkan. Before adding
anything, it got a structure: an app with create, run and destroy steps, its own window and device classes, a logger,
CMake presets and one code style.

**24 Nov: from a square to a scene.** The square became a lit cube, seen through a camera you fly with W A S D and the
mouse. The engine became a library with tests, including tests that draw on the GPU without a window and check the
pictures. Then GPU buffers, meshes, depth, and coloured lights with a sun and a moon.

**25 Nov: a world.** A computed sky, ground, shadows, physics with a falling ball, and the first UI. *Problem:* Windows
freezes a program's main thread while you drag or resize its window, so the picture stopped. Drawing moved to its own
thread.

**26 Nov: the beach.** A settings window and a day and night cycle with stars. The cube scene gave way to water, then
to an endless beach and sea that reach the horizon.

**27 Nov: water, waves, release.** Reflections, refraction, light patterns on the sea floor, foam, a view from under
the water; then surf that builds up, breaks, runs up the sand and drains back. *Problems:* reflections looked wrong
with a low sun; the waves shook, because each wave started its run up the sand from a slightly different line; the
ball barely reacted to the surf. All three were fixed. Balls can now be added and moved with arrows; with many balls
the frame rate dropped, mostly in debug builds. Sampling the water got twice as cheap and runs on several threads: 32
balls went from 12.9 to 1.35 ms per physics step.

**The first release** hit three problems on GitHub: Linux found no Vulkan library to link, Linux also stopped on
warnings inside a math library, and Windows' newest compiler made optimized builds in which a ball hit by another fell
through the floor. The Windows release is built with the Visual Studio 2022 compiler until that is fixed.

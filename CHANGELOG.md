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

**The goal.** I wanted a beach to stand on: endless land, an endless sea to the west where the sun sets, and water
that behaves like real water, with waves that build up, break and run up the sand. All drawn with Vulkan from scratch,
so it can grow into a game engine later.

**A window, a square, a cube.** It started with a Vulkan window and a square. The square became a lit cube I could fly
around with W A S D and the mouse, then got depth, a ball next to it, and coloured lights: a sun and a moon.

**Sky and shadows.** A computed sky, a ground to stand things on, and shadows from every light. Drawing moved onto its
own thread, because Windows stops a program while you drag its window, and the picture froze.

**Physics, a UI, day and night.** A ball that drops and bounces, a small UI to watch, pause and reset, and a real day:
the sun rises and sets, then the moon and the stars come out, and the light follows the sun.

**Water and the endless beach.** The cube made way for water, then for the beach: sand, dunes and hills along a winding
coast, land and sea reaching the horizon. The water got reflections, refraction, light patterns on the sea floor,
foam, glints and a view from under the surface. The reflection broke whenever the sun stood low; that took a while.

**Real waves.** The hardest part. The swell now builds up over the shallow floor, breaks into a foaming bore, runs up
the sand and drains back into the next wave; the sand stays wet behind it, with lines of foam. At first the run-up
shook, because every wave started from a slightly different line, and the ball barely noticed the surf. Now the
breaking waves carry it in.

**More balls.** Balls can be added and moved with arrows. Many balls dropped the frame rate, mostly in debug builds, so
the water under each ball is now worked out twice as fast and on several threads.

**The first release.** Building it on GitHub brought its own surprises: Linux missed the Vulkan library and stopped on
a math library's warnings, and Windows' newest compiler made builds in which a ball hit by another fell through the
floor. So the Windows release is built with the Visual Studio 2022 compiler for now.

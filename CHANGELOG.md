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

### the journey so far

the plan was a beach where i can just stand and look at the sea. endless land, endless sea to the west where the sun
goes down, and water that acts like real water, waves that build up, break and run up the sand. all in vulkan from
scratch so it can turn into a game engine later

started with a vulkan window and a square. then a lit cube i could fly around with wasd and the mouse, depth, a ball
next to it, colored lights with a sun and a moon

then a sky, ground and shadows from every light. the picture froze when i dragged the window bcs windows blocks the
main thread while doing that, so drawing got its own thread

a ball that drops and bounces, a small ui to pause and reset, and a real day where the sun goes up and down and at
night the moon and stars come out

then the cube had to go for water and after that the beach. sand, dunes, hills, a coast that winds a bit, land and sea
going to the horizon. the water got reflections, refraction, these light patterns on the ground, foam, glints and you
can look from under water too. the reflection was broken when the sun was low, took a while to fix

waves were the hardest part. the swell builds up when it gets shallow, breaks, runs up the sand and pulls back into
the next wave, the sand stays wet with foam lines. first it was shaky bcs every wave started from a slightly different
line and the ball didnt care about the surf at all. now the waves carry it to the beach

added more balls and you can move them with arrows. with lots of balls the fps went down, mostly in debug, so the water
under each ball is calculated twice as fast now and on multiple threads

the release itself was a fight too. linux didnt find the vulkan lib and stopped on warnings in a math lib, and the
newest windows compiler made builds where a ball that gets hit by another one falls through the floor. so windows is
built with the vs 2022 compiler for now

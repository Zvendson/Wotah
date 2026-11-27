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

this was a project where i wanted to learn realistic water waves. the plan was a beach where i can just stand and look
at the sea, with water that acts like real water. waves that build up, break and run up the sand. all in vulkan from
scratch. i used ai together with my own code to get it to where it is now

i didnt start with the water tho. water is basically a mirror, it shows the sky, the sun and whatever is around it. on
day 1 there would be nothing to reflect and it would look like a flat blue plate. and something has to float in it, so
i need physics first, and things need light and shadows or they look fake. so the order was: window, objects, light,
sky, shadows, physics, and only then water

started with a vulkan window and a square, glfw for the window bcs it runs on windows and linux. then a lit cube i can
fly around with wasd and the mouse. glm for the math bcs it works like the shader math. vulkans own memory handling is
a pain so i took vma for that, and spdlog to log into a file without slowing the frames down

the picture froze every time i dragged the window, windows blocks the main thread while you do that. so drawing got
its own thread and the main thread only handles the window

for physics i took jolt. its fast, uses all cores and has buoyancy built in, which i needed for the ball later. for the
ui i took dear imgui, stats and settings and buttons without building a ui system myself

after the sky and a day and night cycle with sun, moon and stars the cube had to go and the water came in. the first
ground was a fixed plate and you could see where the world ends, looked bad. so land and sea are endless now, grids
that follow the camera and get coarser far away. the land and the waves are the same formula on the cpu and the gpu,
so the physics feels exactly what you see, tests check that both stay equal

the reflection was broken when the sun was low and the ball in the water looked weird, took a while to get right

waves were the hardest part. normal ocean waves alone looked like a pool. real surf needs a swell that feels the floor,
gets higher in shallow water, breaks, runs up the sand and pulls back into the next wave, and leaves wet sand. first it
was shaky bcs every wave started its run up from a slightly different line, and the ball didnt care about the surf at
all bcs jolts water only slows things down. so the water got its own flow, the broken waves carry the ball in now

more balls and moving them was next. i went with move arrows (imguizmo) instead of grabbing on a spring. with lots of
balls the fps went down, mostly in debug. the water under every ball was calculated way too often, now its half the
work and runs on multiple threads

the release itself was a fight too. linux didnt find the vulkan lib and stopped on warnings in glm, and the newest
windows compiler made builds where a ball that gets hit by another one falls through the floor. so windows is built
with the vs 2022 compiler for now

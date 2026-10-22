#pragma once

#ifdef __ANDROID__
namespace android_touch_controls {
// Called on the SDL/input thread, never from the Android UI thread. (AI-assisted)
void initialize();
void shutdown();
void frame_presented();
// A frame that drew real game output (a DMA chain from GOAL), not just the empty window.
void game_frame_rendered();
}  // namespace android_touch_controls
#endif

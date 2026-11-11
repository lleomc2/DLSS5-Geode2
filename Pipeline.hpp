#pragma once

namespace dlss5 {

// Post-processes the frame that Geometry Dash just finished drawing into the default framebuffer.
// Must be called on the GL thread. It restores every piece of GL state it touches and turns itself
// off permanently (leaving the game untouched) if anything goes wrong.
void onFrameRendered();

} // namespace dlss5

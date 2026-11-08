// The car top view drawn from Track N Race's original SVG with QtSvg (desktop canvases and HMD panels). See carsvg.cpp.
#pragma once

namespace engine { class Engine; }

namespace carsvg {
bool install(engine::Engine *e); // after the engine's init and ImCanvas::setup
void shutdown(); // stops the HMD car worker thread
}

#pragma once
#include "vulkan.hpp"
namespace toy {
// Registration order is draw order. Add/reorder an experiment here without
// changing physics, the event loop or another pass's shaders.
void installPasses(Vulkan &renderer);
} // namespace toy

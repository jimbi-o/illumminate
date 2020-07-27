#include "renderer.h"
#include "d3d12_minimal_for_cpp.h"
namespace illuminate::gfx::d3d12 {
class RendererImpl : public illuminate::gfx::RendererInterface {
 public:
};
}
namespace illuminate::gfx {
RendererInterface* CreateRendererD3d12() {
  return new illuminate::gfx::d3d12::RendererImpl();
}
}

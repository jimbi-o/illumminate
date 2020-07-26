#include "renderer.h"
#include "minimal_for_cpp.h"
namespace illuminate::gfx::d3d12 {
class RendererImpl : public illuminate::gfx::RendererInterface {
 public:
  void ExecuteBatchedRendererPass(const BatchedRendererPass* const batch_list, const uint32_t batch_num, const BufferDescList& global_buffer_descs) {
    loginfo("Hello world!");
  }
};
}
namespace illuminate::gfx {
RendererInterface* CreateRendererD3d12() {
  return new illuminate::gfx::d3d12::RendererImpl();
}
}

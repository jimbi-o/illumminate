#include "renderer.h"
#include "doctest/doctest.h"
TEST_CASE("renderer test") {
  using namespace illuminate::gfx;
  BatchedRendererPass batch = {
    SID("batch"),
    {
      {
        SID("gpass1"),
        QueueType::kGraphics,
        AsyncCompute::kDisable,
        // viewport size
        { BufferSizeType::kSwapchainRelative,1.0f, 1.0f},
        // pass binded buffers
        {
          { SID("primary"), BufferRWType::kWriteRtv, },
          { SID("depth"), BufferRWType::kWriteDsv, },
        },
        // clear required buffers
        {
          { SID("primary"), GetClearValueDefaultRtv(), },
          { SID("depth"),   GetClearValueDefaultDsv(), },
        },
      },
      {
        SID("gpass2"),
        QueueType::kGraphics,
        AsyncCompute::kDisable,
        // viewport size
        { BufferSizeType::kSwapchainRelative,1.0f, 1.0f},
        // pass binded buffers
        {
          { SID("primary"), BufferRWType::kReadSrv, },
          { SID("primary"), BufferRWType::kWriteRtv, },
          { SID("depth"), BufferRWType::kReadDsv, },
        },
      },
      {
        SID("cpass1"),
        QueueType::kCompute,
        AsyncCompute::kEnable,
        // viewport size
        {},
        // pass binded buffers
        {
          { SID("buf0"), BufferRWType::kWriteUav, },
        },
        // clear required buffers
        {
          { SID("buf0"), {}, },
        },
      },
    },
    // batch local buffer desc
    {
      {SID("buf0"), { BufferFormat::kR8G8B8A8_Unorm, BufferSizeType::kAbsolute, 1024.0f, 1024.0f }}
    },
  };
  BufferDescList global_buffer_descs = {
    {
      SID("primary"),
      { BufferFormat::kRgbLinearSdrDefault, BufferSizeType::kViewportRelative, 1.0f, 1.0f }
    }, {
      SID("depth"),
      { BufferFormat::kDepthBufferDefault, BufferSizeType::kViewportRelative, 1.0f, 1.0f }
    },
  };
  auto renderer = CreateRendererD3d12();
  renderer->ExecuteBatchedRendererPass(&batch, 1, global_buffer_descs);
  delete renderer;
}

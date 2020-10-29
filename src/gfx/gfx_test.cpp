#include "render_graph.h"
#include "minimal_for_cpp.h"
namespace {
using namespace illuminate;
using namespace illuminate::gfx;
using RenderFunction = std::function<void()>;
inline auto CreateRenderPassPrez(std::pmr::memory_resource* memory_resource) {
  RenderPass<RenderFunction> render_pass{
    StrId("prez"),
    {{BufferConfig(StrId("depth"), BufferStateType::kDsv)}, memory_resource}
  };
  return render_pass;
}
inline auto CreateRenderPassGBuffer(std::pmr::memory_resource* memory_resource, const bool with_prez = true) {
  RenderPass<RenderFunction> render_pass{
    StrId("gbuffer"),
    {
      {
        BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(with_prez ? BufferLoadOpType::kLoadReadOnly : BufferLoadOpType::kClear),
        BufferConfig(StrId("gbuffer0"), BufferStateType::kRtv),
        BufferConfig(StrId("gbuffer1"), BufferStateType::kRtv),
        BufferConfig(StrId("gbuffer2"), BufferStateType::kRtv),
      },
      memory_resource}
  };
  return render_pass;
}
inline auto CreateRenderPassLighting(std::pmr::memory_resource* memory_resource) {
  RenderPass<RenderFunction> render_pass{
    StrId("lighting"),
    {
      {
        BufferConfig(StrId("depth"), BufferStateType::kSrv),
        BufferConfig(StrId("gbuffer0"), BufferStateType::kSrv),
        BufferConfig(StrId("gbuffer1"), BufferStateType::kSrv),
        BufferConfig(StrId("gbuffer2"), BufferStateType::kSrv),
        BufferConfig(StrId("shadow-tex"), BufferStateType::kSrv),
        BufferConfig(StrId("mainbuffer"), BufferStateType::kUav),
      },
      memory_resource}
  };
  return render_pass;
}
inline auto CreateRenderPassPostProcess(std::pmr::memory_resource* memory_resource) {
  RenderPass<RenderFunction> render_pass{
    StrId("postprocess"),
    {
      {
        BufferConfig(StrId("mainbuffer"), BufferStateType::kUav).LoadOpType(BufferLoadOpType::kLoadReadOnly),
        BufferConfig(StrId("mainbuffer"), BufferStateType::kUav),
      },
      memory_resource}
  };
  return render_pass;
}
inline auto CreateRenderPassShadowMap(std::pmr::memory_resource* memory_resource) {
  RenderPass<RenderFunction> render_pass{
    StrId("shadowmap"),
    {{BufferConfig(StrId("shadowmap"), BufferStateType::kDsv).Size(BufferSizeType::kAbsolute, 1024, 1024)}, memory_resource}
  };
  return render_pass;
}
inline auto CreateRenderPassDeferredShadowHard(std::pmr::memory_resource* memory_resource) {
  RenderPass<RenderFunction> render_pass{
    StrId("deferredshadow-hard"),
    {
      {
        BufferConfig(StrId("shadowmap"), BufferStateType::kSrv),
        BufferConfig(StrId("shadow-tex-hard"), BufferStateType::kRtv),
      },
      memory_resource}
  };
  return render_pass;
}
inline auto CreateRenderPassDeferredShadowPcss(std::pmr::memory_resource* memory_resource) {
  RenderPass<RenderFunction> render_pass{
    StrId("deferredshadow-pcss"),
    {
      {
        BufferConfig(StrId("shadowmap"), BufferStateType::kSrv),
        BufferConfig(StrId("shadow-tex-pcss"), BufferStateType::kRtv),
      },
      memory_resource}
  };
  return render_pass;
}
inline auto CreateRenderPassDebug(std::pmr::memory_resource* memory_resource) {
  RenderPass<RenderFunction> render_pass{
    StrId("debug"),
    {
      {
        BufferConfig(StrId("gbuffer0"), BufferStateType::kSrv),
        BufferConfig(StrId("mainbuffer"), BufferStateType::kRtv),
      },
      memory_resource}
  };
  return render_pass;
}
inline auto CreateRenderPassTransferTexture(std::pmr::memory_resource* memory_resource) {
  // TODO mandatory pass with fence operation
  RenderPass<RenderFunction> render_pass{
    StrId("transfer"),
    {
      {},
      memory_resource}
  };
  return render_pass;
}
auto CreateRenderPassListSimple(std::pmr::memory_resource* memory_resource) {
  RenderPassList<RenderFunction> render_pass_list{memory_resource};
  render_pass_list.push_back({CreateRenderPassGBuffer(memory_resource, false)});
  render_pass_list.push_back({CreateRenderPassLighting(memory_resource)});
  render_pass_list.push_back({CreateRenderPassPostProcess(memory_resource)});
  return render_pass_list;
}
auto CreateRenderPassListShadow(std::pmr::memory_resource* memory_resource) {
  RenderPassList<RenderFunction> render_pass_list{memory_resource};
  render_pass_list.push_back({CreateRenderPassPrez(memory_resource)});
  render_pass_list.push_back({CreateRenderPassShadowMap(memory_resource)});
  render_pass_list.push_back({CreateRenderPassGBuffer(memory_resource)});
  render_pass_list.push_back({CreateRenderPassDeferredShadowHard(memory_resource)});
  render_pass_list.push_back({CreateRenderPassDeferredShadowPcss(memory_resource)});
  render_pass_list.push_back({CreateRenderPassLighting(memory_resource)});
  render_pass_list.push_back({CreateRenderPassPostProcess(memory_resource)});
  return render_pass_list;
}
auto CreateRenderPassListDebug(std::pmr::memory_resource* memory_resource) {
  auto render_pass_list = CreateRenderPassListShadow(memory_resource);
  render_pass_list.push_back({CreateRenderPassDebug(memory_resource)});
  return render_pass_list;
}
auto CreateRenderPassListSkyboxCreation(std::pmr::memory_resource* memory_resource) {
  RenderPassList<RenderFunction> render_pass_list{memory_resource};
  RenderPass<RenderFunction> render_pass_a{
    StrId("skybox-a"),
    {
      {
        BufferConfig(StrId("skybox-tmp"), BufferStateType::kRtv).Size(BufferSizeType::kAbsolute, 1024, 1024),
      },
      memory_resource}
  };
  render_pass_list.push_back(std::move(render_pass_a));
  RenderPass<RenderFunction> render_pass_b{
    StrId("skybox-b"),
    {
      {
        BufferConfig(StrId("skybox-tmp"), BufferStateType::kSrv),
        BufferConfig(StrId("skybox"), BufferStateType::kRtv).Size(BufferSizeType::kAbsolute, 1024, 1024).Dimension(BufferDimensionType::kCube).RenderTargetIndex(3),
      },
      memory_resource}
  };
  render_pass_list.push_back(std::move(render_pass_b));
  return render_pass_list;
}
auto CreateRenderPassListWithSkyboxCreation(std::pmr::memory_resource* memory_resource) {
  auto render_pass_list = CreateRenderPassListShadow(memory_resource);
  auto render_pass_list_skybox = CreateRenderPassListSkyboxCreation(memory_resource);
  render_pass_list.reserve(render_pass_list.size() + render_pass_list_skybox.size());
  std::move(std::begin(render_pass_list_skybox), std::end(render_pass_list_skybox), std::back_inserter(render_pass_list));
  return render_pass_list;
}
auto CreateRenderPassListTransferTexture(std::pmr::memory_resource* memory_resource) {
  RenderPassList<RenderFunction> render_pass_list_transfer{memory_resource};
  render_pass_list_transfer.push_back(CreateRenderPassTransferTexture(memory_resource));
  auto render_pass_list = CreateRenderPassListShadow(memory_resource);
  render_pass_list_transfer.reserve(render_pass_list_transfer.size() + render_pass_list.size());
  std::move(std::begin(render_pass_list), std::end(render_pass_list), std::back_inserter(render_pass_list_transfer));
  return render_pass_list_transfer;
}
// TODO call shadows (none,hard,pcss) with alias names and check pass culling.
// TODO TAA - allow madatory output buffer in certain pass. (or require different name?)
}
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
TEST_CASE("BufferConfig") {
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).name == StrId("rtv"));
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).state_type == BufferStateType::kRtv);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).load_op_type == BufferLoadOpType::kDontCare);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).width == 1.0f);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("rtv"), BufferStateType::kRtv).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).size_type_depth == BufferSizeType::kAbsolute);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).index_to_render == 0);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).depth == 1.0f);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).name == StrId("srv"));
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).state_type == BufferStateType::kSrv);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).load_op_type == BufferLoadOpType::kLoadReadOnly);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).width == 1.0f);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("srv"), BufferStateType::kSrv).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).size_type_depth == BufferSizeType::kAbsolute);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).index_to_render == 0);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).depth == 1.0f);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).name == StrId("cbv"));
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).state_type == BufferStateType::kCbv);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).load_op_type == BufferLoadOpType::kLoadReadOnly);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).format == BufferFormat::kUnknown);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).width == 1.0f);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("cbv"), BufferStateType::kCbv).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).dimension_type == BufferDimensionType::kBuffer);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).size_type_depth == BufferSizeType::kAbsolute);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).index_to_render == 0);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).depth == 1.0f);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).name == StrId("uav"));
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).state_type == BufferStateType::kUav);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).load_op_type == BufferLoadOpType::kDontCare);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).width == 1.0f);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("uav"), BufferStateType::kUav).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).size_type_depth == BufferSizeType::kAbsolute);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).index_to_render == 0);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).depth == 1.0f);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).name == StrId("dsv"));
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).state_type == BufferStateType::kDsv);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).load_op_type == BufferLoadOpType::kClear);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).format == BufferFormat::kD32Float);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).width == 1.0f);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).height == 1.0f);
  CHECK(GetClearValueDepthBuffer(BufferConfig(StrId("dsv"), BufferStateType::kDsv).clear_value).depth == GetClearValueDepthBuffer(GetClearValueDefaultDepthBuffer()).depth);
  CHECK(GetClearValueDepthBuffer(BufferConfig(StrId("dsv"), BufferStateType::kDsv).clear_value).stencil == GetClearValueDepthBuffer(GetClearValueDefaultDepthBuffer()).stencil);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).size_type_depth == BufferSizeType::kAbsolute);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).index_to_render == 0);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).depth == 1.0f);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).name == StrId("copysrc"));
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).state_type == BufferStateType::kCopySrc);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).load_op_type == BufferLoadOpType::kLoadReadOnly);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).width == 1.0f);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).size_type_depth == BufferSizeType::kAbsolute);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).index_to_render == 0);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).depth == 1.0f);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).name == StrId("copydst"));
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).state_type == BufferStateType::kCopyDst);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).load_op_type == BufferLoadOpType::kDontCare);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).width == 1.0f);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).size_type_depth == BufferSizeType::kAbsolute);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).index_to_render == 0);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).depth == 1.0f);
  BufferConfig func_check(StrId("func-check"), BufferStateType::kRtv);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.state_type == BufferStateType::kRtv);
  CHECK(func_check.load_op_type == BufferLoadOpType::kDontCare);
  CHECK(func_check.format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(func_check.size_type == BufferSizeType::kMainbufferRelative);
  CHECK(func_check.width == 1.0f);
  CHECK(func_check.height == 1.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(func_check.dimension_type == BufferDimensionType::k2d);
  CHECK(func_check.size_type_depth == BufferSizeType::kAbsolute);
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 1.0f);
  CHECK(func_check.LoadOpType(BufferLoadOpType::kLoadWrite).load_op_type == BufferLoadOpType::kLoadWrite);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.state_type == BufferStateType::kRtv);
  CHECK(func_check.load_op_type == BufferLoadOpType::kLoadWrite);
  CHECK(func_check.format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(func_check.size_type == BufferSizeType::kMainbufferRelative);
  CHECK(func_check.width == 1.0f);
  CHECK(func_check.height == 1.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(func_check.dimension_type == BufferDimensionType::k2d);
  CHECK(func_check.size_type_depth == BufferSizeType::kAbsolute);
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 1.0f);
  CHECK(func_check.Format(BufferFormat::kD32Float).format == BufferFormat::kD32Float);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.state_type == BufferStateType::kRtv);
  CHECK(func_check.load_op_type == BufferLoadOpType::kLoadWrite);
  CHECK(func_check.format == BufferFormat::kD32Float);
  CHECK(func_check.size_type == BufferSizeType::kMainbufferRelative);
  CHECK(func_check.width == 1.0f);
  CHECK(func_check.height == 1.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(func_check.dimension_type == BufferDimensionType::k2d);
  CHECK(func_check.size_type_depth == BufferSizeType::kAbsolute);
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 1.0f);
  CHECK(func_check.Size(BufferSizeType::kAbsolute, 123, 456).size_type == BufferSizeType::kAbsolute);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.state_type == BufferStateType::kRtv);
  CHECK(func_check.load_op_type == BufferLoadOpType::kLoadWrite);
  CHECK(func_check.format == BufferFormat::kD32Float);
  CHECK(func_check.size_type == BufferSizeType::kAbsolute);
  CHECK(func_check.width == 123.0f);
  CHECK(func_check.height == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(func_check.dimension_type == BufferDimensionType::k2d);
  CHECK(func_check.size_type_depth == BufferSizeType::kAbsolute);
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 1.0f);
  CHECK(GetClearValueColorBuffer(func_check.ClearValue(ClearValue(std::array<float, 4>{123.0f, 456.0f, 789.0f, 100.0f})).clear_value)[0] == 123.0f);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.state_type == BufferStateType::kRtv);
  CHECK(func_check.load_op_type == BufferLoadOpType::kLoadWrite);
  CHECK(func_check.format == BufferFormat::kD32Float);
  CHECK(func_check.size_type == BufferSizeType::kAbsolute);
  CHECK(func_check.width == 123.0f);
  CHECK(func_check.height == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[0] == 123.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[1] == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[2] == 789.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[3] == 100.0f);
  CHECK(func_check.dimension_type == BufferDimensionType::k2d);
  CHECK(func_check.size_type_depth == BufferSizeType::kAbsolute);
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 1.0f);
  CHECK(func_check.SizeDepth(BufferSizeType::kMainbufferRelative, 0.123f).size_type_depth == BufferSizeType::kMainbufferRelative);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.state_type == BufferStateType::kRtv);
  CHECK(func_check.load_op_type == BufferLoadOpType::kLoadWrite);
  CHECK(func_check.format == BufferFormat::kD32Float);
  CHECK(func_check.size_type == BufferSizeType::kAbsolute);
  CHECK(func_check.width == 123.0f);
  CHECK(func_check.height == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[0] == 123.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[1] == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[2] == 789.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[3] == 100.0f);
  CHECK(func_check.dimension_type == BufferDimensionType::k2d);
  CHECK(func_check.size_type_depth == BufferSizeType::kMainbufferRelative);
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 0.123f);
  CHECK(func_check.Dimension(BufferDimensionType::k3d).dimension_type == BufferDimensionType::k3d);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.state_type == BufferStateType::kRtv);
  CHECK(func_check.load_op_type == BufferLoadOpType::kLoadWrite);
  CHECK(func_check.format == BufferFormat::kD32Float);
  CHECK(func_check.size_type == BufferSizeType::kAbsolute);
  CHECK(func_check.width == 123.0f);
  CHECK(func_check.height == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[0] == 123.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[1] == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[2] == 789.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[3] == 100.0f);
  CHECK(func_check.dimension_type == BufferDimensionType::k3d);
  CHECK(func_check.size_type_depth == BufferSizeType::kMainbufferRelative);
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 0.123f);
  CHECK(func_check.RenderTargetIndex(12, 34).index_to_render == 12);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.state_type == BufferStateType::kRtv);
  CHECK(func_check.load_op_type == BufferLoadOpType::kLoadWrite);
  CHECK(func_check.format == BufferFormat::kD32Float);
  CHECK(func_check.size_type == BufferSizeType::kAbsolute);
  CHECK(func_check.width == 123.0f);
  CHECK(func_check.height == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[0] == 123.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[1] == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[2] == 789.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[3] == 100.0f);
  CHECK(func_check.dimension_type == BufferDimensionType::k3d);
  CHECK(func_check.size_type_depth == BufferSizeType::kMainbufferRelative);
  CHECK(func_check.index_to_render == 12);
  CHECK(func_check.buffer_num_to_render == 34);
  CHECK(func_check.depth == 0.123f);
}
TEST_CASE("CreateBufferIdList") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  std::pmr::monotonic_buffer_resource memory_resource{1024}; // TODO implement original allocator.
  auto render_pass_list = CreateRenderPassListSimple(&memory_resource);
  auto buffer_id_list = CreateBufferIdList(render_pass_list, {}, &memory_resource);
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

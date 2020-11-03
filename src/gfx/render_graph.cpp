#include "render_graph.h"
namespace illuminate::gfx {
std::tuple<RenderPassIdMap, RenderPassOrder> FormatRenderPassList(RenderPassList&& render_pass_list, std::pmr::memory_resource* memory_resource) {
  RenderPassIdMap render_pass_id_map{memory_resource};
  render_pass_id_map.reserve(render_pass_list.size());
  RenderPassOrder render_pass_order{memory_resource};
  render_pass_order.reserve(render_pass_list.size());
  for (auto&& pass : render_pass_list) {
    render_pass_order.push_back(pass.name);
    render_pass_id_map.insert({render_pass_order.back(), std::move(pass)});
  }
  return {render_pass_id_map, render_pass_order};
}
BufferIdList CreateBufferIdList(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, std::pmr::memory_resource* memory_resource) {
  BufferIdList buffer_id_list{memory_resource};
  buffer_id_list.reserve(render_pass_order.size());
  BufferId new_id = 0;
  std::pmr::unordered_map<StrId, BufferId> known_buffer{memory_resource};
  for (auto& pass_name : render_pass_order) {
    auto& pass_buffer_ids = buffer_id_list.insert({pass_name, {}}).first->second;
    auto& pass = render_pass_id_map.at(pass_name);
    pass_buffer_ids.reserve(pass.buffer_list.size());
    for (auto& buffer : pass.buffer_list) {
      if (buffer.load_op_type == BufferLoadOpType::kDontCare || buffer.load_op_type == BufferLoadOpType::kClear || !known_buffer.contains(buffer.name)) {
        pass_buffer_ids.push_back(new_id);
        known_buffer.insert({buffer.name, new_id});
        new_id++;
      } else {
        pass_buffer_ids.push_back(known_buffer.at(buffer.name));
      }
    }
  }
  return buffer_id_list;
}
BufferIdList ApplyBufferNameAlias(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, BufferIdList&& buffer_id_list, const BufferNameAliasList& alias_list, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_map<StrId, BufferId> buffer_name_to_id(memory_resource);
  for (auto& pass_name : render_pass_order) {
    auto& pass = render_pass_id_map.at(pass_name);
    for (uint32_t buffer_index = 0; auto& buffer : pass.buffer_list) {
      for (auto& [buffer_name, alias_name] : alias_list) {
        if (buffer_name == buffer.name) {
          buffer_name_to_id[alias_name] = buffer_id_list.at(pass.name)[buffer_index];
        } else if (alias_name == buffer.name) {
          buffer_id_list.at(pass.name)[buffer_index] = buffer_name_to_id.at(alias_name);
        }
        buffer_index++;
      }
    }
  }
  return std::move(buffer_id_list);
}
RenderPassAdjacencyGraph CreateRenderPassAdjacencyGraph(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, const BufferIdList& buffer_id_list, std::pmr::memory_resource* memory_resource) {
  RenderPassAdjacencyGraph adjacency_graph(memory_resource);
  for (auto& pass_name : render_pass_order) {
    auto& pass = render_pass_id_map.at(pass_name);
    auto& pass_buffer_ids = buffer_id_list.at(pass_name);
    for (uint32_t buffer_index = 0; auto& buffer : pass.buffer_list) {
      if (IsOutputBuffer(buffer.state_type, buffer.load_op_type)) {
        if (!adjacency_graph.output_buffer_producer_pass.contains(pass_buffer_ids[buffer_index])) {
          adjacency_graph.output_buffer_producer_pass.insert({pass_buffer_ids[buffer_index], std::pmr::vector<StrId>(memory_resource)});
        }
        adjacency_graph.output_buffer_producer_pass.at(pass_buffer_ids[buffer_index]).push_back(pass_name);
      }
      if (IsInitialValueUsed(buffer.state_type, buffer.load_op_type)) {
        if (!adjacency_graph.consumer_pass_input_buffer.contains(pass_name)) {
          adjacency_graph.consumer_pass_input_buffer.insert({pass_name, std::pmr::vector<BufferId>(memory_resource)});
        }
        adjacency_graph.consumer_pass_input_buffer[pass_name].push_back(pass_buffer_ids[buffer_index]);
      }
      buffer_index++;
    }
  }
  return adjacency_graph;
}
MandatoryOutputBufferIdList IdentifyMandatoryOutputBufferId(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, const BufferIdList& buffer_id_list, const MandatoryOutputBufferNameList& mandatory_buffer_name_list, std::pmr::memory_resource* memory_resource) {
  MandatoryOutputBufferIdList mandatory_buffer_id_list(memory_resource);
  mandatory_buffer_id_list.reserve(mandatory_buffer_name_list.size());
  for (auto& buffer_name : mandatory_buffer_name_list) {
    for (auto it = render_pass_order.crbegin(); it != render_pass_order.crend(); it++) {
      bool buffer_found = false;
      auto& pass_name = *it;
      auto& pass = render_pass_id_map.at(pass_name);
      for (uint32_t buffer_index = 0; auto& buffer : pass.buffer_list) {
        if (buffer.name == buffer_name && IsOutputBuffer(buffer.state_type, buffer.load_op_type)) {
          mandatory_buffer_id_list.insert(buffer_id_list.at(pass_name)[buffer_index]);
          buffer_found = true;
          break;
        }
        buffer_index++;
      }
      if (buffer_found) break;
    }
  }
  return mandatory_buffer_id_list;
}
std::pmr::unordered_set<StrId> GetUsedRenderPassList(const RenderPassAdjacencyGraph& adjacency_graph, MandatoryOutputBufferIdList&& mandatory_buffer_id_list, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_set<StrId> used_pass(memory_resource);
  std::pmr::unordered_set<BufferId> used_buffers(memory_resource);
  used_buffers.reserve(mandatory_buffer_id_list.size());
  used_buffers.insert(mandatory_buffer_id_list.begin(), mandatory_buffer_id_list.end());
  while (!mandatory_buffer_id_list.empty()) {
    auto buffer_id = *mandatory_buffer_id_list.begin();
    mandatory_buffer_id_list.erase(buffer_id);
    if (!adjacency_graph.output_buffer_producer_pass.contains(buffer_id)) continue;
    auto& producer_pass_list = adjacency_graph.output_buffer_producer_pass.at(buffer_id);
    for (auto& pass_id : producer_pass_list) {
      if (used_pass.contains(pass_id)) continue;
      used_pass.insert(pass_id);
      if (!adjacency_graph.consumer_pass_input_buffer.contains(pass_id)) continue;
      auto& input_buffers = adjacency_graph.consumer_pass_input_buffer.at(pass_id);
      for (auto& input_buffer : input_buffers) {
        if (used_buffers.contains(input_buffer)) continue;
        used_buffers.insert(input_buffer);
        mandatory_buffer_id_list.insert(input_buffer);
      }
    }
  }
  return used_pass;
}
RenderPassOrder CullUnusedRenderPass(RenderPassOrder&& render_pass_order, const std::pmr::unordered_set<StrId>& used_render_pass_list) {
  std::erase_if(render_pass_order, [&used_render_pass_list](const StrId& pass_name) { return !used_render_pass_list.contains(pass_name); });
  return std::move(render_pass_order);
}
}
#include "minimal_for_cpp.h"
namespace {
using namespace illuminate;
using namespace illuminate::gfx;
inline auto CreateRenderPassPrez(std::pmr::memory_resource* memory_resource) {
  return RenderPass(
    StrId("prez"),
    {
      {
        BufferConfig(StrId("depth"), BufferStateType::kDsv)
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassGBuffer(std::pmr::memory_resource* memory_resource, const bool with_prez = true) {
  return RenderPass(
    StrId("gbuffer"),
    {
      {
        BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(with_prez ? BufferLoadOpType::kLoadReadOnly : BufferLoadOpType::kClear),
        BufferConfig(StrId("gbuffer0"), BufferStateType::kRtv),
        BufferConfig(StrId("gbuffer1"), BufferStateType::kRtv),
        BufferConfig(StrId("gbuffer2"), BufferStateType::kRtv),
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassLighting(std::pmr::memory_resource* memory_resource) {
  return RenderPass(
    StrId("lighting"),
    {
      {
        BufferConfig(StrId("depth"), BufferStateType::kSrv),
        BufferConfig(StrId("gbuffer0"), BufferStateType::kSrv),
        BufferConfig(StrId("gbuffer1"), BufferStateType::kSrv),
        BufferConfig(StrId("gbuffer2"), BufferStateType::kSrv),
        BufferConfig(StrId("shadowtex"), BufferStateType::kSrv),
        BufferConfig(StrId("mainbuffer"), BufferStateType::kUav),
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassPostProcess(std::pmr::memory_resource* memory_resource) {
  return RenderPass(
    StrId("postprocess"),
    {
      {
        BufferConfig(StrId("mainbuffer"), BufferStateType::kUav).LoadOpType(BufferLoadOpType::kLoadReadOnly),
        BufferConfig(StrId("mainbuffer"), BufferStateType::kUav),
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassShadowMap(std::pmr::memory_resource* memory_resource) {
  return RenderPass(
    StrId("shadowmap"),
    {
      {
        BufferConfig(StrId("shadowmap"), BufferStateType::kDsv).Size(BufferSizeType::kAbsolute, 1024, 1024)
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassDeferredShadowHard(std::pmr::memory_resource* memory_resource) {
  return RenderPass(
    StrId("deferredshadow-hard"),
    {
      {
        BufferConfig(StrId("shadowmap"), BufferStateType::kSrv),
        BufferConfig(StrId("shadowtex-hard"), BufferStateType::kRtv),
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassDeferredShadowPcss(std::pmr::memory_resource* memory_resource) {
  return RenderPass(
    StrId("deferredshadow-pcss"),
    {
      {
        BufferConfig(StrId("shadowmap"), BufferStateType::kSrv),
        BufferConfig(StrId("shadowtex-pcss"), BufferStateType::kRtv),
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassDebug(std::pmr::memory_resource* memory_resource) {
  return RenderPass( 
    StrId("debug"),
    {
      {
        BufferConfig(StrId("gbuffer0"), BufferStateType::kSrv),
        BufferConfig(StrId("mainbuffer"), BufferStateType::kRtv),
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassTransferTexture(std::pmr::memory_resource* memory_resource) {
  return RenderPass(StrId("transfer"), BufferConfigList(memory_resource)).Mandatory(true);
}
auto CreateRenderPassListSimple(std::pmr::memory_resource* memory_resource) {
  RenderPassList render_pass_list{memory_resource};
  render_pass_list.push_back({CreateRenderPassGBuffer(memory_resource, false)});
  render_pass_list.push_back({CreateRenderPassLighting(memory_resource)});
  render_pass_list.push_back({CreateRenderPassPostProcess(memory_resource)});
  return render_pass_list;
}
auto CreateRenderPassListShadow(std::pmr::memory_resource* memory_resource) {
  RenderPassList render_pass_list{memory_resource};
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
  RenderPassList render_pass_list{memory_resource};
  RenderPass render_pass_a(
    StrId("skybox-a"),
    {
      {
        BufferConfig(StrId("skybox-tmp"), BufferStateType::kRtv).Size(BufferSizeType::kAbsolute, 1024, 1024),
      },
      memory_resource
    }
  );
  render_pass_list.push_back(std::move(render_pass_a));
  RenderPass render_pass_b(
    StrId("skybox-b"),
    {
      {
        BufferConfig(StrId("skybox-tmp"), BufferStateType::kSrv),
        BufferConfig(StrId("skybox"), BufferStateType::kRtv).Size(BufferSizeType::kAbsolute, 1024, 1024).Dimension(BufferDimensionType::kCube).RenderTargetIndex(3),
      },
      memory_resource
    }
  );
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
  RenderPassList render_pass_list_transfer{memory_resource};
  render_pass_list_transfer.push_back(CreateRenderPassTransferTexture(memory_resource));
  auto render_pass_list = CreateRenderPassListShadow(memory_resource);
  render_pass_list_transfer.reserve(render_pass_list_transfer.size() + render_pass_list.size());
  std::move(std::begin(render_pass_list), std::end(render_pass_list), std::back_inserter(render_pass_list_transfer));
  return render_pass_list_transfer;
}
// TODO TAA - allow mandatory output buffer in certain pass. (or require different name?)
// TODO output to swapchain
}
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
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
TEST_CASE("CreateRenderPassListSimple") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  std::pmr::monotonic_buffer_resource memory_resource{1024}; // TODO implement original allocator.
  auto render_pass_list = CreateRenderPassListSimple(&memory_resource);
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), &memory_resource);
  CHECK(render_pass_id_map.size() == 3);
  CHECK(render_pass_id_map[StrId("gbuffer")].name == StrId("gbuffer"));
  CHECK(render_pass_id_map[StrId("lighting")].name == StrId("lighting"));
  CHECK(render_pass_id_map[StrId("postprocess")].name == StrId("postprocess"));
  CHECK(render_pass_order.size() == 3);
  CHECK(render_pass_order[0] == StrId("gbuffer"));
  CHECK(render_pass_order[1] == StrId("lighting"));
  CHECK(render_pass_order[2] == StrId("postprocess"));
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, &memory_resource);
  CHECK(buffer_id_list.size() == 3);
  CHECK(buffer_id_list[StrId("gbuffer")].size() == 4);
  CHECK(buffer_id_list[StrId("gbuffer")][0] == 0);
  CHECK(buffer_id_list[StrId("gbuffer")][1] == 1);
  CHECK(buffer_id_list[StrId("gbuffer")][2] == 2);
  CHECK(buffer_id_list[StrId("gbuffer")][3] == 3);
  CHECK(buffer_id_list[StrId("lighting")].size() == 6);
  CHECK(buffer_id_list[StrId("lighting")][0] == 0);
  CHECK(buffer_id_list[StrId("lighting")][1] == 1);
  CHECK(buffer_id_list[StrId("lighting")][2] == 2);
  CHECK(buffer_id_list[StrId("lighting")][3] == 3);
  CHECK(buffer_id_list[StrId("lighting")][4] == 4);
  CHECK(buffer_id_list[StrId("lighting")][5] == 5);
  CHECK(buffer_id_list[StrId("postprocess")].size() == 2);
  CHECK(buffer_id_list[StrId("postprocess")][0] == 5);
  CHECK(buffer_id_list[StrId("postprocess")][1] == 6);
  auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, &memory_resource);
  CHECK(render_pass_adjacency_graph.output_buffer_producer_pass[0][0] == StrId("gbuffer"));
  CHECK(render_pass_adjacency_graph.output_buffer_producer_pass[1][0] == StrId("gbuffer"));
  CHECK(render_pass_adjacency_graph.output_buffer_producer_pass[2][0] == StrId("gbuffer"));
  CHECK(render_pass_adjacency_graph.output_buffer_producer_pass[3][0] == StrId("gbuffer"));
  CHECK(render_pass_adjacency_graph.output_buffer_producer_pass[5][0] == StrId("lighting"));
  CHECK(render_pass_adjacency_graph.output_buffer_producer_pass[6][0] == StrId("postprocess"));
  CHECK(render_pass_adjacency_graph.consumer_pass_input_buffer[StrId("lighting")][0] == 0);
  CHECK(render_pass_adjacency_graph.consumer_pass_input_buffer[StrId("lighting")][1] == 1);
  CHECK(render_pass_adjacency_graph.consumer_pass_input_buffer[StrId("lighting")][2] == 2);
  CHECK(render_pass_adjacency_graph.consumer_pass_input_buffer[StrId("lighting")][3] == 3);
  CHECK(render_pass_adjacency_graph.consumer_pass_input_buffer[StrId("lighting")][4] == 4);
  CHECK(render_pass_adjacency_graph.consumer_pass_input_buffer[StrId("postprocess")][0] == 5);
  auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list, {StrId("mainbuffer")}, &memory_resource);
  CHECK(mandatory_buffer_id_list.size() == 1);
  CHECK(mandatory_buffer_id_list.contains(6));
  auto used_render_pass_list = GetUsedRenderPassList(render_pass_adjacency_graph, std::move(mandatory_buffer_id_list), &memory_resource);
  CHECK(used_render_pass_list.contains(StrId("gbuffer")));
  CHECK(used_render_pass_list.contains(StrId("lighting")));
  CHECK(used_render_pass_list.contains(StrId("postprocess")));
  auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list);
  CHECK(culled_render_pass_order[0] == StrId("gbuffer"));
  CHECK(culled_render_pass_order[1] == StrId("lighting"));
  CHECK(culled_render_pass_order[2] == StrId("postprocess"));
}
TEST_CASE("CreateRenderPassListShadow") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  std::pmr::monotonic_buffer_resource memory_resource{1024}; // TODO implement original allocator.
  auto render_pass_list = CreateRenderPassListShadow(&memory_resource);
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), &memory_resource);
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, &memory_resource);
  CHECK(buffer_id_list.size() == 7);
  CHECK(buffer_id_list[StrId("prez")].size() == 1);
  CHECK(buffer_id_list[StrId("prez")][0] == 0);
  CHECK(buffer_id_list[StrId("shadowmap")].size() == 1);
  CHECK(buffer_id_list[StrId("shadowmap")][0] == 1);
  CHECK(buffer_id_list[StrId("gbuffer")].size() == 4);
  CHECK(buffer_id_list[StrId("gbuffer")][0] == 0);
  CHECK(buffer_id_list[StrId("gbuffer")][1] == 2);
  CHECK(buffer_id_list[StrId("gbuffer")][2] == 3);
  CHECK(buffer_id_list[StrId("gbuffer")][3] == 4);
  CHECK(buffer_id_list[StrId("deferredshadow-hard")].size() == 2);
  CHECK(buffer_id_list[StrId("deferredshadow-hard")][0] == 1);
  CHECK(buffer_id_list[StrId("deferredshadow-hard")][1] == 5);
  CHECK(buffer_id_list[StrId("deferredshadow-pcss")].size() == 2);
  CHECK(buffer_id_list[StrId("deferredshadow-pcss")][0] == 1);
  CHECK(buffer_id_list[StrId("deferredshadow-pcss")][1] == 6);
  CHECK(buffer_id_list[StrId("lighting")].size() == 6);
  CHECK(buffer_id_list[StrId("lighting")][0] == 0);
  CHECK(buffer_id_list[StrId("lighting")][1] == 2);
  CHECK(buffer_id_list[StrId("lighting")][2] == 3);
  CHECK(buffer_id_list[StrId("lighting")][3] == 4);
  CHECK(buffer_id_list[StrId("lighting")][4] == 7);
  CHECK(buffer_id_list[StrId("lighting")][5] == 8);
  CHECK(buffer_id_list[StrId("postprocess")].size() == 2);
  CHECK(buffer_id_list[StrId("postprocess")][0] == 8);
  CHECK(buffer_id_list[StrId("postprocess")][1] == 9);
  BufferNameAliasList buffer_name_alias_list{&memory_resource};
  SUBCASE("shadow-hard") {
    buffer_name_alias_list.insert({StrId("shadowtex-hard"), StrId("shadowtex")});
    auto buffer_id_list_alias_applied = ApplyBufferNameAlias(render_pass_id_map, render_pass_order, std::move(buffer_id_list), buffer_name_alias_list, &memory_resource);
    CHECK(buffer_id_list_alias_applied.size() == 7);
    CHECK(buffer_id_list_alias_applied[StrId("prez")].size() == 1);
    CHECK(buffer_id_list_alias_applied[StrId("prez")][0] == 0);
    CHECK(buffer_id_list_alias_applied[StrId("shadowmap")].size() == 1);
    CHECK(buffer_id_list_alias_applied[StrId("shadowmap")][0] == 1);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")].size() == 4);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")][0] == 0);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")][1] == 2);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")][2] == 3);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")][3] == 4);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-hard")].size() == 2);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-hard")][0] == 1);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-hard")][1] == 5);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-pcss")].size() == 2);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-pcss")][0] == 1);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-pcss")][1] == 6);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")].size() == 6);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][0] == 0);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][1] == 2);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][2] == 3);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][3] == 4);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][4] == 5);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][5] == 8);
    CHECK(buffer_id_list_alias_applied[StrId("postprocess")].size() == 2);
    CHECK(buffer_id_list_alias_applied[StrId("postprocess")][0] == 8);
    CHECK(buffer_id_list_alias_applied[StrId("postprocess")][1] == 9);
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, &memory_resource);
    auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, {StrId("mainbuffer")}, &memory_resource);
    auto used_render_pass_list = GetUsedRenderPassList(render_pass_adjacency_graph, std::move(mandatory_buffer_id_list), &memory_resource);
    CHECK(used_render_pass_list.contains(StrId("prez")));
    CHECK(used_render_pass_list.contains(StrId("shadowmap")));
    CHECK(used_render_pass_list.contains(StrId("gbuffer")));
    CHECK(used_render_pass_list.contains(StrId("deferredshadow-hard")));
    CHECK(used_render_pass_list.contains(StrId("lighting")));
    CHECK(used_render_pass_list.contains(StrId("postprocess")));
    auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list);
    CHECK(culled_render_pass_order.size() == 6);
    CHECK(culled_render_pass_order[0] == StrId("prez"));
    CHECK(culled_render_pass_order[1] == StrId("shadowmap"));
    CHECK(culled_render_pass_order[2] == StrId("gbuffer"));
    CHECK(culled_render_pass_order[3] == StrId("deferredshadow-hard"));
    CHECK(culled_render_pass_order[4] == StrId("lighting"));
    CHECK(culled_render_pass_order[5] == StrId("postprocess"));
  }
  SUBCASE("shadow-pcss") {
    buffer_name_alias_list.insert({StrId("shadowtex-pcss"), StrId("shadowtex")});
    auto buffer_id_list_alias_applied = ApplyBufferNameAlias(render_pass_id_map, render_pass_order, std::move(buffer_id_list), buffer_name_alias_list, &memory_resource);
    CHECK(buffer_id_list_alias_applied.size() == 7);
    CHECK(buffer_id_list_alias_applied[StrId("prez")].size() == 1);
    CHECK(buffer_id_list_alias_applied[StrId("prez")][0] == 0);
    CHECK(buffer_id_list_alias_applied[StrId("shadowmap")].size() == 1);
    CHECK(buffer_id_list_alias_applied[StrId("shadowmap")][0] == 1);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")].size() == 4);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")][0] == 0);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")][1] == 2);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")][2] == 3);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")][3] == 4);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-hard")].size() == 2);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-hard")][0] == 1);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-hard")][1] == 5);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-pcss")].size() == 2);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-pcss")][0] == 1);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-pcss")][1] == 6);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")].size() == 6);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][0] == 0);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][1] == 2);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][2] == 3);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][3] == 4);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][4] == 6);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][5] == 8);
    CHECK(buffer_id_list_alias_applied[StrId("postprocess")].size() == 2);
    CHECK(buffer_id_list_alias_applied[StrId("postprocess")][0] == 8);
    CHECK(buffer_id_list_alias_applied[StrId("postprocess")][1] == 9);
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, &memory_resource);
    auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, {StrId("mainbuffer")}, &memory_resource);
    auto used_render_pass_list = GetUsedRenderPassList(render_pass_adjacency_graph, std::move(mandatory_buffer_id_list), &memory_resource);
    CHECK(used_render_pass_list.contains(StrId("prez")));
    CHECK(used_render_pass_list.contains(StrId("shadowmap")));
    CHECK(used_render_pass_list.contains(StrId("gbuffer")));
    CHECK(used_render_pass_list.contains(StrId("deferredshadow-pcss")));
    CHECK(used_render_pass_list.contains(StrId("lighting")));
    CHECK(used_render_pass_list.contains(StrId("postprocess")));
    auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list);
    CHECK(culled_render_pass_order.size() == 6);
    CHECK(culled_render_pass_order[0] == StrId("prez"));
    CHECK(culled_render_pass_order[1] == StrId("shadowmap"));
    CHECK(culled_render_pass_order[2] == StrId("gbuffer"));
    CHECK(culled_render_pass_order[3] == StrId("deferredshadow-pcss"));
    CHECK(culled_render_pass_order[4] == StrId("lighting"));
    CHECK(culled_render_pass_order[5] == StrId("postprocess"));
  }
}
// TODO check pass name dup.
#ifdef __clang__
#pragma clang diagnostic pop
#endif

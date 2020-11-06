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
RenderPassOrder CullUnusedRenderPass(RenderPassOrder&& render_pass_order, const std::pmr::unordered_set<StrId>& used_render_pass_list, const RenderPassIdMap& render_pass_id_map) {
  std::erase_if(render_pass_order, [&used_render_pass_list, &render_pass_id_map](const StrId& pass_name) { return !render_pass_id_map.at(pass_name).mandatory_pass && !used_render_pass_list.contains(pass_name); });
  return std::move(render_pass_order);
}
bool IsDuplicateRenderPassNameExists(const RenderPassList& list, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_set<StrId> names(memory_resource);
  for (auto& pass : list) {
    if (names.contains(pass.name)) return true;
    names.insert(pass.name);
  }
  return false;
}
auto GetUsedBufferList(const std::pmr::unordered_set<StrId>& used_render_pass_list, const BufferIdList& buffer_id_list, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_set<BufferId> used_buffers{memory_resource};
  for (auto& pass : used_render_pass_list) {
    auto& pass_buffers = buffer_id_list.at(pass);
    used_buffers.insert(pass_buffers.begin(), pass_buffers.end());
  }
  return used_buffers;
}
enum BufferStateFlags : uint32_t {
  kBufferStateFlagNone = 0x0000,
  kBufferStateFlagCbv = 0x0001,
  kBufferStateFlagSrv = 0x0002,
  kBufferStateFlagUav = 0x0004,
  kBufferStateFlagRtv = 0x0008,
  kBufferStateFlagDsv = 0x0010,
  kBufferStateFlagCopySrc = 0x0020,
  kBufferStateFlagCopyDst = 0x0040,
};
constexpr BufferStateFlags GetBufferStateFlag(const BufferStateType type) {
  switch (type) {
    case kCbv: return kBufferStateFlagCbv;
    case kSrv: return kBufferStateFlagSrv;
    case kUav: return kBufferStateFlagUav;
    case kRtv: return kBufferStateFlagRtv;
    case kDsv: return kBufferStateFlagDsv;
    case kCopySrc: return kBufferStateFlagCopySrc;
    case kCopyDst: return kBufferStateFlagCopyDst;
  }
}
class BufferCreationDesc {
 public:
  BufferCreationDesc()
      : format(BufferFormat::kUnknown),
        dimension_type(BufferDimensionType::k2d),
        initial_state(BufferStateType::kCbv),
        state_flags(kBufferStateFlagNone),
        width(0),
        height(0),
        depth(1),
        clear_value({})
  {}
  BufferCreationDesc(const BufferConfig& config, const BufferSize2d& mainbuffer, const BufferSize2d& swapchain)
      : format(config.format),
        dimension_type(config.dimension_type),
        initial_state(config.state_type),
        state_flags(GetBufferStateFlag(config.state_type)),
        width(GetPhsicalBufferWidth(config, mainbuffer, swapchain)),
        height(GetPhsicalBufferHeight(config, mainbuffer, swapchain)),
        depth(config.depth),
        clear_value(config.clear_value) // TODO consider using move.
  {}
  BufferFormat format;
  BufferDimensionType dimension_type;
  BufferStateType initial_state;
  BufferStateFlags state_flags;
  uint32_t width, height, depth;
  ClearValue clear_value;
};
using BufferCreationDescList = std::pmr::unordered_map<BufferId, BufferCreationDesc>;
auto ConfigureBufferCreationDescs(const RenderPassOrder& render_pass_order, const RenderPassIdMap& render_pass_id_map, const BufferIdList& buffer_id_list, const std::pmr::unordered_set<BufferId>& used_buffer_list, const BufferSize2d& mainbuffer_size, const BufferSize2d& swapchain_size, std::pmr::memory_resource* memory_resource) {
  BufferCreationDescList buffer_creation_descs{memory_resource};
  for (auto& pass_name : render_pass_order) {
    auto& pass = render_pass_id_map.at(pass_name);
    auto& pass_buffer_ids = buffer_id_list.at(pass_name);
    for (uint32_t buffer_index = 0; auto& buffer : pass.buffer_list) {
      auto buffer_id = pass_buffer_ids[buffer_index];
      if (!buffer_creation_descs.contains(buffer_id)) {
        buffer_creation_descs.insert({buffer_id, BufferCreationDesc(buffer, mainbuffer_size, swapchain_size)});
      }
      buffer_creation_descs.at(buffer_id).state_flags = static_cast<BufferStateFlags>(buffer_creation_descs.at(buffer_id).state_flags | GetBufferStateFlag(buffer.state_type));
      buffer_index++;
    }
  }
  return buffer_creation_descs;
}
auto GetPhysicalBufferSizeInByte(const BufferCreationDescList& buffer_creation_descs, std::function<std::tuple<size_t, uint32_t>(const BufferCreationDesc&)>&& buffer_creation_func, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_map<BufferId, size_t> physical_buffer_size_in_byte{memory_resource};
  std::pmr::unordered_map<BufferId, uint32_t> physical_buffer_alignment{memory_resource};
  for (auto& [id, desc] : buffer_creation_descs) {
    std::tie(physical_buffer_size_in_byte[id], physical_buffer_alignment[id]) = buffer_creation_func(desc);
  }
  return std::make_tuple(physical_buffer_size_in_byte, physical_buffer_alignment);
}
auto CalculatePhysicalBufferLiftime(const RenderPassOrder& render_pass_order, const BufferIdList& buffer_id_list, std::pmr::memory_resource* memory_resource) {
  return std::make_tuple(std::pmr::unordered_map<BufferId, StrId>{}, std::pmr::unordered_map<BufferId, StrId>());
}
auto GetPhysicalBufferAddressOffset(const RenderPassOrder& render_pass_order, const BufferIdList& buffer_id_list, const std::pmr::unordered_map<BufferId, StrId>& physical_buffer_lifetime_begin, const std::pmr::unordered_map<BufferId, StrId>& physical_buffer_lifetime_end, const std::pmr::unordered_map<BufferId, size_t>& physical_buffer_size_in_byte, const std::pmr::unordered_map<BufferId, uint32_t>& physical_buffer_alignment, std::pmr::memory_resource* memory_resource) {
  return std::pmr::unordered_map<BufferId, uint32_t>{memory_resource};
}
template <typename T>
using PhysicalBuffers = std::pmr::unordered_map<BufferId, T>;
template <typename T>
using PhysicalBufferAllocationFunc = std::function<T(const BufferCreationDesc&, const uint64_t, const uint32_t, const uint32_t)>;
template <typename T>
auto AllocatePhysicalBuffers(const BufferCreationDescList& buffer_creation_descs, const std::pmr::unordered_map<BufferId, size_t>& physical_buffer_size_in_byte, const std::pmr::unordered_map<BufferId, uint32_t>& physical_buffer_alignment, const std::pmr::unordered_map<BufferId, uint32_t>& physical_buffer_address_offset, std::function<T(const BufferCreationDesc&, const uint64_t, const uint32_t, const uint32_t)>&& alloc_func, std::pmr::memory_resource* memory_resource) {
  return PhysicalBuffers<T>{memory_resource};
}
}
#ifdef BUILD_WITH_TEST
#include "minimal_for_cpp.h"
namespace {
const uint32_t size_in_byte = 32 * 1024;
std::byte buffer[size_in_byte]{};
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
auto CreateRenderPassTransparent(std::pmr::memory_resource* memory_resource) {
  return RenderPass(
    StrId("transparent"),
    {
      {
        BufferConfig(StrId("mainbuffer"), BufferStateType::kRtv).LoadOpType(BufferLoadOpType::kLoadWrite),
      },
      memory_resource
    }
  );
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
auto InsertRenderPassListSkyboxCreation(RenderPassList&& render_pass_list, std::pmr::memory_resource* memory_resource) {
  auto render_pass_list_skybox = CreateRenderPassListSkyboxCreation(memory_resource);
  render_pass_list.reserve(render_pass_list.size() + render_pass_list_skybox.size());
  std::move(std::begin(render_pass_list_skybox), std::end(render_pass_list_skybox), std::back_inserter(render_pass_list));
  return std::move(render_pass_list);
}
auto CreateRenderPassListWithSkyboxCreation(std::pmr::memory_resource* memory_resource) {
  return InsertRenderPassListSkyboxCreation(CreateRenderPassListSimple(memory_resource), memory_resource);
}
auto CreateRenderPassListTransferTexture(std::pmr::memory_resource* memory_resource) {
  RenderPassList render_pass_list_transfer{memory_resource};
  render_pass_list_transfer.push_back(CreateRenderPassTransferTexture(memory_resource));
  auto render_pass_list = CreateRenderPassListSimple(memory_resource);
  render_pass_list_transfer.reserve(render_pass_list_transfer.size() + render_pass_list.size());
  std::move(std::begin(render_pass_list), std::end(render_pass_list), std::back_inserter(render_pass_list_transfer));
  return render_pass_list_transfer;
}
auto CreateRenderPassListTransparent(std::pmr::memory_resource* memory_resource) {
  auto render_pass_list = CreateRenderPassListSimple(memory_resource);
  auto it = std::find_if(render_pass_list.begin(), render_pass_list.end(), [](const RenderPass& pass) { return pass.name == StrId("postprocess"); });
  render_pass_list.insert(it, CreateRenderPassTransparent(memory_resource));
  return render_pass_list;
}
auto CreateRenderPassListCombined(std::pmr::memory_resource* memory_resource) {
  auto render_pass_list = CreateRenderPassListShadow(memory_resource);
  render_pass_list.insert(render_pass_list.begin(), CreateRenderPassTransferTexture(memory_resource));
  render_pass_list.insert(std::find_if(render_pass_list.begin(), render_pass_list.end(), [](const RenderPass& pass) { return pass.name == StrId("postprocess"); }), CreateRenderPassTransparent(memory_resource));
  return InsertRenderPassListSkyboxCreation(std::move(render_pass_list), memory_resource);
}
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
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).index_to_render == 0);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).depth == 1);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).name == StrId("srv"));
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).state_type == BufferStateType::kSrv);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).load_op_type == BufferLoadOpType::kLoadReadOnly);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).width == 1.0f);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("srv"), BufferStateType::kSrv).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).index_to_render == 0);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrv).depth == 1);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).name == StrId("cbv"));
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).state_type == BufferStateType::kCbv);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).load_op_type == BufferLoadOpType::kLoadReadOnly);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).format == BufferFormat::kUnknown);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).width == 1.0f);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("cbv"), BufferStateType::kCbv).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).dimension_type == BufferDimensionType::kBuffer);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).index_to_render == 0);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).depth == 1);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).name == StrId("uav"));
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).state_type == BufferStateType::kUav);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).load_op_type == BufferLoadOpType::kDontCare);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).width == 1.0f);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("uav"), BufferStateType::kUav).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).index_to_render == 0);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).depth == 1);
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
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).index_to_render == 0);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).depth == 1);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).name == StrId("copysrc"));
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).state_type == BufferStateType::kCopySrc);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).load_op_type == BufferLoadOpType::kLoadReadOnly);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).width == 1.0f);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).index_to_render == 0);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).depth == 1);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).name == StrId("copydst"));
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).state_type == BufferStateType::kCopyDst);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).load_op_type == BufferLoadOpType::kDontCare);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).width == 1.0f);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).index_to_render == 0);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).depth == 1);
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
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 1);
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
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 1);
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
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 1);
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
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 1);
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
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 1);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.Depth(123).depth == 123);
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
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 123);
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
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 123);
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
  CHECK(func_check.index_to_render == 12);
  CHECK(func_check.buffer_num_to_render == 34);
  CHECK(func_check.depth == 123);
}
TEST_CASE("CreateRenderPassListSimple") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, size_in_byte);
  auto render_pass_list = CreateRenderPassListSimple(memory_resource.get());
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  CHECK(render_pass_id_map.size() == 3);
  CHECK(render_pass_id_map[StrId("gbuffer")].name == StrId("gbuffer"));
  CHECK(render_pass_id_map[StrId("lighting")].name == StrId("lighting"));
  CHECK(render_pass_id_map[StrId("postprocess")].name == StrId("postprocess"));
  CHECK(render_pass_order.size() == 3);
  CHECK(render_pass_order[0] == StrId("gbuffer"));
  CHECK(render_pass_order[1] == StrId("lighting"));
  CHECK(render_pass_order[2] == StrId("postprocess"));
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
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
  auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
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
  auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list, {StrId("mainbuffer")}, memory_resource.get());
  CHECK(mandatory_buffer_id_list.size() == 1);
  CHECK(mandatory_buffer_id_list.contains(6));
  auto used_render_pass_list = GetUsedRenderPassList(render_pass_adjacency_graph, std::move(mandatory_buffer_id_list), memory_resource.get());
  CHECK(used_render_pass_list.size() == 3);
  CHECK(used_render_pass_list.contains(StrId("gbuffer")));
  CHECK(used_render_pass_list.contains(StrId("lighting")));
  CHECK(used_render_pass_list.contains(StrId("postprocess")));
  auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
  CHECK(culled_render_pass_order.size() == 3);
  CHECK(culled_render_pass_order[0] == StrId("gbuffer"));
  CHECK(culled_render_pass_order[1] == StrId("lighting"));
  CHECK(culled_render_pass_order[2] == StrId("postprocess"));
}
TEST_CASE("CreateRenderPassListShadow") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, size_in_byte);
  auto render_pass_list = CreateRenderPassListShadow(memory_resource.get());
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
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
  BufferNameAliasList buffer_name_alias_list{memory_resource.get()};
  SUBCASE("shadow-hard") {
    buffer_name_alias_list.insert({StrId("shadowtex-hard"), StrId("shadowtex")});
    auto buffer_id_list_alias_applied = ApplyBufferNameAlias(render_pass_id_map, render_pass_order, std::move(buffer_id_list), buffer_name_alias_list, memory_resource.get());
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
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, memory_resource.get());
    auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, {StrId("mainbuffer")}, memory_resource.get());
    auto used_render_pass_list = GetUsedRenderPassList(render_pass_adjacency_graph, std::move(mandatory_buffer_id_list), memory_resource.get());
    CHECK(used_render_pass_list.size() == 6);
    CHECK(used_render_pass_list.contains(StrId("prez")));
    CHECK(used_render_pass_list.contains(StrId("shadowmap")));
    CHECK(used_render_pass_list.contains(StrId("gbuffer")));
    CHECK(used_render_pass_list.contains(StrId("deferredshadow-hard")));
    CHECK(used_render_pass_list.contains(StrId("lighting")));
    CHECK(used_render_pass_list.contains(StrId("postprocess")));
    auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
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
    auto buffer_id_list_alias_applied = ApplyBufferNameAlias(render_pass_id_map, render_pass_order, std::move(buffer_id_list), buffer_name_alias_list, memory_resource.get());
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
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, memory_resource.get());
    auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, {StrId("mainbuffer")}, memory_resource.get());
    auto used_render_pass_list = GetUsedRenderPassList(render_pass_adjacency_graph, std::move(mandatory_buffer_id_list), memory_resource.get());
    CHECK(used_render_pass_list.size() == 6);
    CHECK(used_render_pass_list.contains(StrId("prez")));
    CHECK(used_render_pass_list.contains(StrId("shadowmap")));
    CHECK(used_render_pass_list.contains(StrId("gbuffer")));
    CHECK(used_render_pass_list.contains(StrId("deferredshadow-pcss")));
    CHECK(used_render_pass_list.contains(StrId("lighting")));
    CHECK(used_render_pass_list.contains(StrId("postprocess")));
    auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
    CHECK(culled_render_pass_order.size() == 6);
    CHECK(culled_render_pass_order[0] == StrId("prez"));
    CHECK(culled_render_pass_order[1] == StrId("shadowmap"));
    CHECK(culled_render_pass_order[2] == StrId("gbuffer"));
    CHECK(culled_render_pass_order[3] == StrId("deferredshadow-pcss"));
    CHECK(culled_render_pass_order[4] == StrId("lighting"));
    CHECK(culled_render_pass_order[5] == StrId("postprocess"));
  }
}
TEST_CASE("CreateRenderPassListDebug") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, size_in_byte);
  auto render_pass_list = CreateRenderPassListDebug(memory_resource.get());
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
  auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list, {StrId("mainbuffer")}, memory_resource.get());
  auto used_render_pass_list = GetUsedRenderPassList(render_pass_adjacency_graph, std::move(mandatory_buffer_id_list), memory_resource.get());
  CHECK(used_render_pass_list.size() == 3);
  CHECK(used_render_pass_list.contains(StrId("prez")));
  CHECK(used_render_pass_list.contains(StrId("gbuffer")));
  CHECK(used_render_pass_list.contains(StrId("debug")));
  auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
  CHECK(culled_render_pass_order.size() == 3);
  CHECK(culled_render_pass_order[0] == StrId("prez"));
  CHECK(culled_render_pass_order[1] == StrId("gbuffer"));
  CHECK(culled_render_pass_order[2] == StrId("debug"));
}
TEST_CASE("CreateRenderPassListWithSkyboxCreation") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, size_in_byte);
  auto render_pass_list = CreateRenderPassListWithSkyboxCreation(memory_resource.get());
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
  auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list, {StrId("mainbuffer"), StrId("skybox")}, memory_resource.get());
  auto used_render_pass_list = GetUsedRenderPassList(render_pass_adjacency_graph, std::move(mandatory_buffer_id_list), memory_resource.get());
  CHECK(used_render_pass_list.size() == 5);
  CHECK(used_render_pass_list.contains(StrId("gbuffer")));
  CHECK(used_render_pass_list.contains(StrId("lighting")));
  CHECK(used_render_pass_list.contains(StrId("postprocess")));
  CHECK(used_render_pass_list.contains(StrId("skybox-a")));
  CHECK(used_render_pass_list.contains(StrId("skybox-b")));
  auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
  CHECK(culled_render_pass_order.size() == 5);
  CHECK(culled_render_pass_order[0] == StrId("gbuffer"));
  CHECK(culled_render_pass_order[1] == StrId("lighting"));
  CHECK(culled_render_pass_order[2] == StrId("postprocess"));
  CHECK(culled_render_pass_order[3] == StrId("skybox-a"));
  CHECK(culled_render_pass_order[4] == StrId("skybox-b"));
}
TEST_CASE("CreateRenderPassListTransferTexture") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, size_in_byte);
  auto render_pass_list = CreateRenderPassListTransferTexture(memory_resource.get());
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
  auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list, {StrId("mainbuffer"), StrId("skybox")}, memory_resource.get());
  auto used_render_pass_list = GetUsedRenderPassList(render_pass_adjacency_graph, std::move(mandatory_buffer_id_list), memory_resource.get());
  auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
  CHECK(culled_render_pass_order.size() == 4);
  CHECK(culled_render_pass_order[0] == StrId("transfer"));
  CHECK(culled_render_pass_order[1] == StrId("gbuffer"));
  CHECK(culled_render_pass_order[2] == StrId("lighting"));
  CHECK(culled_render_pass_order[3] == StrId("postprocess"));
}
TEST_CASE("CreateRenderPassListTransparent") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, size_in_byte);
  auto render_pass_list = CreateRenderPassListTransparent(memory_resource.get());
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  CHECK(buffer_id_list[StrId("transparent")].size() == 1);
  CHECK(buffer_id_list[StrId("transparent")][0] == buffer_id_list[StrId("lighting")][5]);
  auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
  auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list, {StrId("mainbuffer")}, memory_resource.get());
  auto used_render_pass_list = GetUsedRenderPassList(render_pass_adjacency_graph, std::move(mandatory_buffer_id_list), memory_resource.get());
  auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
  CHECK(culled_render_pass_order.size() == 4);
  CHECK(culled_render_pass_order[0] == StrId("gbuffer"));
  CHECK(culled_render_pass_order[1] == StrId("lighting"));
  CHECK(culled_render_pass_order[2] == StrId("transparent"));
  CHECK(culled_render_pass_order[3] == StrId("postprocess"));
}
TEST_CASE("CreateRenderPassListCombined") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, size_in_byte);
  auto render_pass_list = CreateRenderPassListCombined(memory_resource.get());
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  CHECK(render_pass_order.size() == 11);
  CHECK(render_pass_order[0] == StrId("transfer"));
  CHECK(render_pass_order[1] == StrId("prez"));
  CHECK(render_pass_order[2] == StrId("shadowmap"));
  CHECK(render_pass_order[3] == StrId("gbuffer"));
  CHECK(render_pass_order[4] == StrId("deferredshadow-hard"));
  CHECK(render_pass_order[5] == StrId("deferredshadow-pcss"));
  CHECK(render_pass_order[6] == StrId("lighting"));
  CHECK(render_pass_order[7] == StrId("transparent"));
  CHECK(render_pass_order[8] == StrId("postprocess"));
  CHECK(render_pass_order[9] == StrId("skybox-a"));
  CHECK(render_pass_order[10] == StrId("skybox-b"));
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  CHECK(buffer_id_list[StrId("transparent")].size() == 1);
  CHECK(buffer_id_list[StrId("transparent")][0] == buffer_id_list[StrId("lighting")][5]);
  BufferNameAliasList buffer_name_alias_list{memory_resource.get()};
  SUBCASE("shadow-hard") {
    buffer_name_alias_list.insert({StrId("shadowtex-hard"), StrId("shadowtex")});
    auto buffer_id_list_alias_applied = ApplyBufferNameAlias(render_pass_id_map, render_pass_order, std::move(buffer_id_list), buffer_name_alias_list, memory_resource.get());
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-hard")][1] == buffer_id_list_alias_applied[StrId("lighting")][4]);
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, memory_resource.get());
    auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, {StrId("mainbuffer"), StrId("skybox")}, memory_resource.get());
    auto used_render_pass_list = GetUsedRenderPassList(render_pass_adjacency_graph, std::move(mandatory_buffer_id_list), memory_resource.get());
    CHECK(!used_render_pass_list.contains(StrId("deferredshadow-pcss")));
    auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
    CHECK(culled_render_pass_order.size() == 10);
    CHECK(culled_render_pass_order[0] == StrId("transfer"));
    CHECK(culled_render_pass_order[1] == StrId("prez"));
    CHECK(culled_render_pass_order[2] == StrId("shadowmap"));
    CHECK(culled_render_pass_order[3] == StrId("gbuffer"));
    CHECK(culled_render_pass_order[4] == StrId("deferredshadow-hard"));
    CHECK(culled_render_pass_order[5] == StrId("lighting"));
    CHECK(culled_render_pass_order[6] == StrId("transparent"));
    CHECK(culled_render_pass_order[7] == StrId("postprocess"));
    CHECK(culled_render_pass_order[8] == StrId("skybox-a"));
    CHECK(culled_render_pass_order[9] == StrId("skybox-b"));
  }
  SUBCASE("shadow-pcss") {
    buffer_name_alias_list.insert({StrId("shadowtex-pcss"), StrId("shadowtex")});
    auto buffer_id_list_alias_applied = ApplyBufferNameAlias(render_pass_id_map, render_pass_order, std::move(buffer_id_list), buffer_name_alias_list, memory_resource.get());
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-pcss")][1] == buffer_id_list_alias_applied[StrId("lighting")][4]);
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, memory_resource.get());
    auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, {StrId("mainbuffer"), StrId("skybox")}, memory_resource.get());
    auto used_render_pass_list = GetUsedRenderPassList(render_pass_adjacency_graph, std::move(mandatory_buffer_id_list), memory_resource.get());
    CHECK(!used_render_pass_list.contains(StrId("deferredshadow-hard")));
    auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
    CHECK(culled_render_pass_order.size() == 10);
    CHECK(culled_render_pass_order[0] == StrId("transfer"));
    CHECK(culled_render_pass_order[1] == StrId("prez"));
    CHECK(culled_render_pass_order[2] == StrId("shadowmap"));
    CHECK(culled_render_pass_order[3] == StrId("gbuffer"));
    CHECK(culled_render_pass_order[4] == StrId("deferredshadow-pcss"));
    CHECK(culled_render_pass_order[5] == StrId("lighting"));
    CHECK(culled_render_pass_order[6] == StrId("transparent"));
    CHECK(culled_render_pass_order[7] == StrId("postprocess"));
    CHECK(culled_render_pass_order[8] == StrId("skybox-a"));
    CHECK(culled_render_pass_order[9] == StrId("skybox-b"));
  }
  SUBCASE("no skybox") {
    buffer_name_alias_list.insert({StrId("shadowtex-hard"), StrId("shadowtex")});
    auto buffer_id_list_alias_applied = ApplyBufferNameAlias(render_pass_id_map, render_pass_order, std::move(buffer_id_list), buffer_name_alias_list, memory_resource.get());
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-hard")][1] == buffer_id_list_alias_applied[StrId("lighting")][4]);
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, memory_resource.get());
    auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, {StrId("mainbuffer")}, memory_resource.get());
    auto used_render_pass_list = GetUsedRenderPassList(render_pass_adjacency_graph, std::move(mandatory_buffer_id_list), memory_resource.get());
    CHECK(!used_render_pass_list.contains(StrId("deferredshadow-pcss")));
    auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
    CHECK(culled_render_pass_order.size() == 8);
    CHECK(culled_render_pass_order[0] == StrId("transfer"));
    CHECK(culled_render_pass_order[1] == StrId("prez"));
    CHECK(culled_render_pass_order[2] == StrId("shadowmap"));
    CHECK(culled_render_pass_order[3] == StrId("gbuffer"));
    CHECK(culled_render_pass_order[4] == StrId("deferredshadow-hard"));
    CHECK(culled_render_pass_order[5] == StrId("lighting"));
    CHECK(culled_render_pass_order[6] == StrId("transparent"));
    CHECK(culled_render_pass_order[7] == StrId("postprocess"));
  }
}
TEST_CASE("RenderPassNameDupCheck") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, size_in_byte);
  auto render_pass_list = CreateRenderPassListSimple(memory_resource.get());
  CHECK(!IsDuplicateRenderPassNameExists(render_pass_list, memory_resource.get()));
  render_pass_list.push_back(render_pass_list[0]);
  CHECK(IsDuplicateRenderPassNameExists(render_pass_list, memory_resource.get()));
}
TEST_CASE("buffer creation desc and allocation") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, size_in_byte);
  RenderPassList render_pass_list(memory_resource.get());
  render_pass_list.push_back(RenderPass(
      StrId("1"),
      {
        {
          BufferConfig(StrId("1"), BufferStateType::kRtv),
          BufferConfig(StrId("2"), BufferStateType::kUav).Size(BufferSizeType::kAbsolute, 5, 7),
          BufferConfig(StrId("3"), BufferStateType::kDsv),
          BufferConfig(StrId("5"), BufferStateType::kRtv).Dimension(BufferDimensionType::k3d).Depth(8),
        },
        memory_resource.get()
      }
  ));
  render_pass_list.push_back(RenderPass(
      StrId("2"),
      {
        {
          BufferConfig(StrId("1"), BufferStateType::kRtv).LoadOpType(BufferLoadOpType::kLoadWrite),
          BufferConfig(StrId("2"), BufferStateType::kSrv),
          BufferConfig(StrId("4"), BufferStateType::kRtv).Size(BufferSizeType::kSwapchainRelative, 2, 4),
        },
        memory_resource.get()
      }
  ));
  render_pass_list.push_back(RenderPass(
      StrId("3"),
      {
        {
          BufferConfig(StrId("6"), BufferStateType::kRtv).LoadOpType(BufferLoadOpType::kLoadWrite),
        },
        memory_resource.get()
      }
  ));
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
  auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list, {StrId("4")}, memory_resource.get());
  auto used_render_pass_list = GetUsedRenderPassList(render_pass_adjacency_graph, std::move(mandatory_buffer_id_list), memory_resource.get());
  auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
  auto used_buffer_list = GetUsedBufferList(used_render_pass_list, buffer_id_list, memory_resource.get());
  CHECK(used_buffer_list.size() == 5);
  CHECK(used_buffer_list.contains(0));
  CHECK(used_buffer_list.contains(1));
  CHECK(used_buffer_list.contains(2));
  CHECK(used_buffer_list.contains(3));
  CHECK(used_buffer_list.contains(4));
  auto buffer_creation_descs = ConfigureBufferCreationDescs(culled_render_pass_order, render_pass_id_map, buffer_id_list, used_buffer_list, {12, 34}, {56, 78}, memory_resource.get());
  CHECK(buffer_creation_descs.size() == 5);
  CHECK(buffer_creation_descs[0].initial_state == BufferStateType::kRtv);
  CHECK(buffer_creation_descs[0].state_flags == kBufferStateFlagRtv);
  CHECK(buffer_creation_descs[0].format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(buffer_creation_descs[0].width == 12);
  CHECK(buffer_creation_descs[0].height == 34);
  CHECK(GetClearValueColorBuffer(buffer_creation_descs[0].clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(buffer_creation_descs[0].dimension_type == BufferDimensionType::k2d);
  CHECK(buffer_creation_descs[0].depth == 1);
  CHECK(buffer_creation_descs[1].initial_state == BufferStateType::kUav);
  CHECK(buffer_creation_descs[1].state_flags == (kBufferStateFlagUav | kBufferStateFlagSrv));
  CHECK(buffer_creation_descs[1].format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(buffer_creation_descs[1].width == 5);
  CHECK(buffer_creation_descs[1].height == 7);
  CHECK(GetClearValueColorBuffer(buffer_creation_descs[1].clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(buffer_creation_descs[1].dimension_type == BufferDimensionType::k2d);
  CHECK(buffer_creation_descs[1].depth == 1);
  CHECK(buffer_creation_descs[2].initial_state == BufferStateType::kDsv);
  CHECK(buffer_creation_descs[2].state_flags == kBufferStateFlagDsv);
  CHECK(buffer_creation_descs[2].format == BufferFormat::kD32Float);
  CHECK(buffer_creation_descs[2].width == 12);
  CHECK(buffer_creation_descs[2].height == 34);
  CHECK(GetClearValueDepthBuffer(buffer_creation_descs[2].clear_value).depth == GetClearValueDepthBuffer(GetClearValueDefaultDepthBuffer()).depth);
  CHECK(GetClearValueDepthBuffer(buffer_creation_descs[2].clear_value).stencil == GetClearValueDepthBuffer(GetClearValueDefaultDepthBuffer()).stencil);
  CHECK(buffer_creation_descs[2].dimension_type == BufferDimensionType::k2d);
  CHECK(buffer_creation_descs[2].depth == 1);
  CHECK(buffer_creation_descs[3].initial_state == BufferStateType::kRtv);
  CHECK(buffer_creation_descs[3].state_flags == kBufferStateFlagRtv);
  CHECK(buffer_creation_descs[3].format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(buffer_creation_descs[3].width == 12);
  CHECK(buffer_creation_descs[3].height == 34);
  CHECK(GetClearValueColorBuffer(buffer_creation_descs[3].clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(buffer_creation_descs[3].dimension_type == BufferDimensionType::k3d);
  CHECK(buffer_creation_descs[3].depth == 8);
  CHECK(buffer_creation_descs[4].initial_state == BufferStateType::kRtv);
  CHECK(buffer_creation_descs[4].state_flags == kBufferStateFlagRtv);
  CHECK(buffer_creation_descs[4].format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(buffer_creation_descs[4].width == 56 * 2);
  CHECK(buffer_creation_descs[4].height == 78 * 4);
  CHECK(GetClearValueColorBuffer(buffer_creation_descs[4].clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(buffer_creation_descs[4].dimension_type == BufferDimensionType::k2d);
  CHECK(buffer_creation_descs[4].depth == 1);
  auto [physical_buffer_size_in_byte, physical_buffer_alignment] = GetPhysicalBufferSizeInByte(buffer_creation_descs, []([[maybe_unused]] const BufferCreationDesc& desc) { return std::make_tuple<size_t, uint32_t>(sizeof(uint32_t), 4); }, memory_resource.get());
  CHECK(physical_buffer_size_in_byte.size() == 5);
  CHECK(physical_buffer_size_in_byte[0] == 4);
  CHECK(physical_buffer_size_in_byte[1] == 4);
  CHECK(physical_buffer_size_in_byte[2] == 4);
  CHECK(physical_buffer_size_in_byte[3] == 4);
  CHECK(physical_buffer_size_in_byte[4] == 4);
  CHECK(physical_buffer_alignment.size() == 5);
  CHECK(physical_buffer_alignment[0] == 4);
  CHECK(physical_buffer_alignment[1] == 4);
  CHECK(physical_buffer_alignment[2] == 4);
  CHECK(physical_buffer_alignment[3] == 4);
  CHECK(physical_buffer_alignment[4] == 4);
  auto [physical_buffer_lifetime_begin, physical_buffer_lifetime_end] = CalculatePhysicalBufferLiftime(culled_render_pass_order, buffer_id_list, memory_resource.get());
  CHECK(physical_buffer_lifetime_begin[0] == StrId("1"));
  CHECK(physical_buffer_lifetime_end[0]   == StrId("2"));
  CHECK(physical_buffer_lifetime_begin[1] == StrId("1"));
  CHECK(physical_buffer_lifetime_end[1]   == StrId("2"));
  CHECK(physical_buffer_lifetime_begin[2] == StrId("1"));
  CHECK(physical_buffer_lifetime_end[2]   == StrId("1"));
  CHECK(physical_buffer_lifetime_begin[3] == StrId("1"));
  CHECK(physical_buffer_lifetime_end[3]   == StrId("1"));
  CHECK(physical_buffer_lifetime_begin[4] == StrId("2"));
  CHECK(physical_buffer_lifetime_end[4]   == StrId("2"));
  auto physical_buffer_address_offset = GetPhysicalBufferAddressOffset(culled_render_pass_order, buffer_id_list, physical_buffer_lifetime_begin, physical_buffer_lifetime_end, physical_buffer_size_in_byte, physical_buffer_alignment, memory_resource.get());
  CHECK(physical_buffer_address_offset[0] == 0);
  CHECK(physical_buffer_address_offset[1] == 4);
  CHECK(physical_buffer_address_offset[2] == 8);
  CHECK(physical_buffer_address_offset[3] == 12);
  CHECK(physical_buffer_address_offset[4] == 8);
  using PhysicalBufferType = uint32_t*;
  auto physical_buffers = AllocatePhysicalBuffers(buffer_creation_descs, physical_buffer_size_in_byte, physical_buffer_alignment, physical_buffer_address_offset, PhysicalBufferAllocationFunc<PhysicalBufferType>{[](const BufferCreationDesc& desc, const uint64_t size_in_byte, const uint32_t alignment, const uint32_t offset_in_byte) { return static_cast<uint32_t*>(static_cast<void*>(&buffer[offset_in_byte])); }}, memory_resource.get());
  CHECK(reinterpret_cast<std::uintptr_t>(physical_buffers[0]) == reinterpret_cast<std::uintptr_t>(buffer));
  CHECK(reinterpret_cast<std::uintptr_t>(physical_buffers[1]) == reinterpret_cast<std::uintptr_t>(physical_buffers[0]) + 4);
  CHECK(reinterpret_cast<std::uintptr_t>(physical_buffers[2]) == reinterpret_cast<std::uintptr_t>(physical_buffers[1]) + 4);
  CHECK(reinterpret_cast<std::uintptr_t>(physical_buffers[3]) == reinterpret_cast<std::uintptr_t>(physical_buffers[2]) + 4);
  CHECK(physical_buffers[4] == physical_buffers[2]);
  std::pmr::unordered_map<StrId, std::function<void(const PassBufferIdList&, const PhysicalBuffers<PhysicalBufferType>&)>> pass_functions{
    {
      StrId("1"),
      [](const PassBufferIdList& buffer_ids, const PhysicalBuffers<PhysicalBufferType>& physical_buffers) {
        *physical_buffers.at(buffer_ids[0]) = 255;
        *physical_buffers.at(buffer_ids[1]) = 512;
        *physical_buffers.at(buffer_ids[2]) = 1001;
        *physical_buffers.at(buffer_ids[3]) = 1010;
      }
    },
    {
      StrId("2"),
      [](const PassBufferIdList& buffer_ids, const PhysicalBuffers<PhysicalBufferType>& physical_buffers) {
        *physical_buffers.at(buffer_ids[0]) = *physical_buffers.at(buffer_ids[0]) + 1;
        *physical_buffers.at(buffer_ids[2]) = *physical_buffers.at(buffer_ids[1]) + 1024;
      }
    },
  };
  for (auto& pass_id : culled_render_pass_order) {
    pass_functions.at(pass_id)(buffer_id_list.at(pass_id), physical_buffers);
  }
  CHECK(*physical_buffers.at(0) == 256);
  CHECK(*physical_buffers.at(1) == 512);
  CHECK(*physical_buffers.at(2) == *physical_buffers.at(4));
  CHECK(*physical_buffers.at(3) == 1010);
  CHECK(*physical_buffers.at(4) == 512 + 1024);
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif
// TODO ConfigureBufferCreationDescs for ping-pong buffers

#include "renderer.h"
#include "minimal_for_cpp.h"
namespace illuminate::gfx {
using PassBindedBufferIdList = std::unordered_map<StrId, std::unordered_map<BufferViewType, std::unordered_map<StrId, uint32_t>>>;
PassBindedBufferIdList ExtractDataStoredBuffers(const BatchedRendererPass* const batch_list, const uint32_t batch_num) {
  PassBindedBufferIdList pass_binded_buffer_ids;
  uint32_t new_id = 0;
  for (uint32_t i = 0; i < batch_num; i++) {
    auto& batch = batch_list[i];
    for (auto& pass : batch.pass_configs) {
      auto pass_name = pass.pass_name;
      for (auto& buffer : pass.pass_binded_buffers) {
        if (IsBufferLoadOpLoad(buffer.state)) continue;
        if (!IsBufferStoreOpStore(buffer.state)) continue;
        pass_binded_buffer_ids[pass_name][GetBufferViewType(buffer.state)][buffer.buffer_name] = new_id;
        new_id++;
      }
    }
  }
  return pass_binded_buffer_ids;
}
constexpr bool IsPassExecutedOnSameQueue(const RenderPassConfig& pass, const RenderPassConfig& other) {
  switch (pass.queue_type) {
    case QueueType::kGraphics: {
      switch (other.queue_type) {
        case QueueType::kGraphics: return true;
        case QueueType::kCompute:  return other.async_compute_enabled == AsyncCompute::kDisabled;
        case QueueType::kTransfer: return false;
      }
      break;
    }
    case QueueType::kCompute: {
      switch (other.queue_type) {
        case QueueType::kGraphics: return pass.async_compute_enabled == AsyncCompute::kDisabled;
        case QueueType::kCompute:  return pass.async_compute_enabled == other.async_compute_enabled;
        case QueueType::kTransfer: return false;
      }
      break;
    }
    case QueueType::kTransfer: {
      switch (other.queue_type) {
        case QueueType::kGraphics: return false;
        case QueueType::kCompute:  return false;
        case QueueType::kTransfer: return true;
      }
      break;
    }
  }
  ASSERT(false && "invalid queue type", pass.pass_name, other.pass_name);
  return false;
}
uint32_t FindBufferIdFromLatestStore(const BatchedRendererPass* const batch_list, const uint32_t batch_num, const uint32_t used_batch_index, const uint32_t used_pass_index, const StrId& buffer_name, const PassBindedBufferIdList& pass_binded_buffer_ids) {
  uint32_t batch_index = used_batch_index;
  uint32_t pass_index = used_pass_index - 1;
  while (true) {
    auto& batch = batch_list[batch_index];
    while (pass_index != ~0u) {
      auto& pass = batch.pass_configs[pass_index];
      for (auto& buffer : pass.pass_binded_buffers) {
        if (buffer.buffer_name != buffer_name) continue;
        if (!IsBufferStoreOpStore(buffer.state)) continue;
        if (!IsContaining(pass_binded_buffer_ids, pass.pass_name)) continue;
        if (!IsContaining(pass_binded_buffer_ids.at(pass.pass_name), GetBufferViewType(buffer.state))) continue;
        if (!IsContaining(pass_binded_buffer_ids.at(pass.pass_name).at(GetBufferViewType(buffer.state)), buffer_name)) continue;
        if (batch_index == used_batch_index && !IsPassExecutedOnSameQueue(pass, batch.pass_configs[used_pass_index])) continue;
        return pass_binded_buffer_ids.at(pass.pass_name).at(GetBufferViewType(buffer.state)).at(buffer_name);
      }
      pass_index--;
    }
    if (batch_index == 0) break;
    batch_index--;
    pass_index = batch_list[batch_index].pass_configs.size() - 1;
  }
  logwarn("buffer:{} in batch{}pass{} not found", buffer_name, used_batch_index, used_pass_index);
  return ~0u;
}
void LinkLoadingBuffersToExistingBuffers(const BatchedRendererPass* const batch_list, const uint32_t batch_num, PassBindedBufferIdList* const pass_binded_buffer_ids) {
  for (uint32_t i = 0; i < batch_num; i++) {
    auto& batch = batch_list[i];
    auto pass_num = batch.pass_configs.size();
    for (uint32_t j = 0; j < pass_num; j++) {
      auto& pass = batch.pass_configs[j];
      const uint32_t buffer_num = pass.pass_binded_buffers.size();
      for (uint32_t k = 0; k < buffer_num; k++) {
        auto& buffer = pass.pass_binded_buffers[k];
        if (!IsBufferLoadOpLoad(buffer.state)) continue;
        (*pass_binded_buffer_ids)[pass.pass_name][GetBufferViewType(buffer.state)][buffer.buffer_name] = FindBufferIdFromLatestStore(batch_list, batch_num, i, j, buffer.buffer_name, *pass_binded_buffer_ids);
      }
    }
  }
}
PassBindedBufferIdList IdentifyPassBindedBuffers(const BatchedRendererPass* const batch_list, const uint32_t batch_num) {
  PassBindedBufferIdList pass_binded_buffer_ids = ExtractDataStoredBuffers(batch_list, batch_num);
  LinkLoadingBuffersToExistingBuffers(batch_list, batch_num, &pass_binded_buffer_ids);
  return pass_binded_buffer_ids;
}
using PassBindedBufferStateList = std::unordered_map<uint32_t, std::unordered_map<StrId, BufferState>>;
PassBindedBufferStateList ExtractBufferStateList(const BatchedRendererPass* const batch_list, const uint32_t batch_num, const PassBindedBufferIdList& pass_binded_buffer_ids) {
  PassBindedBufferStateList list;
  for (uint32_t i = 0; i < batch_num; i++) {
    auto& batch = batch_list[i];
    for (auto& pass : batch.pass_configs) {
      auto pass_name = pass.pass_name;
      for (auto& buffer : pass.pass_binded_buffers) {
        auto buffer_id = pass_binded_buffer_ids.at(pass_name).at(GetBufferViewType(buffer.state)).at(buffer.buffer_name);
        if (IsContaining(list[buffer_id], pass_name)) {
          logwarn("duplicate buffer:{},{},{},{},{} in {} {}", buffer_id, buffer.buffer_name, buffer.state, i, pass_name);
        }
        list[buffer_id][pass_name] = buffer.state;
      }
    }
  }
  return list;
}
void CombineMergeableBufferStates(BatchedRendererPass* const batch_list, const uint32_t batch_num, const PassBindedBufferIdList& pass_binded_buffer_ids, PassBindedBufferStateList* const pass_binded_buffer_states) {
  // TODO
}
void ProcessBatchedRendererPass(const BatchedRendererPass* const batch_list, const uint32_t batch_num, const BufferDescList& global_buffer_descs) {
  // TODO
}
namespace {
std::tuple<std::vector<BatchedRendererPass>, BufferDescList> CreateTestData() {
  std::vector<BatchedRendererPass> batches = {
    {
      SID("batch1"),
      {
        {
          SID("gpass1"),
          QueueType::kGraphics,
          AsyncCompute::kDisabled,
          SID("gpass1-func"),
          // viewport size
          { BufferSizeType::kSwapchainRelative,1.0f, 1.0f},
          // pass binded buffers
          {
            { SID("primary"), BufferState::kRtv, },
            { SID("reused"),  BufferState::kRtv, },
            { SID("depth"),   BufferState::kDsv, },
          },
        },
        {
          SID("gpass2"),
          QueueType::kGraphics,
          AsyncCompute::kDisabled,
          SID("gpass2-func"),
          // viewport size
          { BufferSizeType::kSwapchainRelative,1.0f, 1.0f},
          // pass binded buffers
          {
            { SID("primary"), BufferState::kSrv, },
            { SID("primary"), BufferState::kRtv, },
            { SID("reused"),  BufferState::kSrv, },
            { SID("depth"),   BufferState::kDsvReadOnly, },
          },
        },
        {
          SID("cpass1"),
          QueueType::kCompute,
          AsyncCompute::kEnabled,
          SID("cpass1-func"),
          // viewport size
          {},
          // pass binded buffers
          {
            { SID("buf0"), BufferState::kUav, },
            { SID("buf1"), BufferState::kUav, },
          },
        },
      },
      // batch local buffer desc
      {
        {SID("buf0"), { BufferFormat::kR8G8B8A8_Unorm, BufferSizeType::kAbsolute, 1024.0f, 1024.0f, 1.0f, {} }},
        {SID("buf1"), { BufferFormat::kR8G8B8A8_Unorm, BufferSizeType::kSwapchainRelative, 0.5f, 0.5f, 1.0f, {} }}
      },
    },
    {
      SID("batch2"),
      {
        {
          SID("gpass3"),
          QueueType::kGraphics,
          AsyncCompute::kDisabled,
          SID("gpass3-func"),
          // viewport size
          { BufferSizeType::kSwapchainRelative,1.0f, 1.0f},
          // pass binded buffers
          {
            { SID("primary"), BufferState::kSrv, },
            { SID("primary"), BufferState::kRtv, },
            { SID("sub"),     BufferState::kRtv, },
            { SID("buf0"),    BufferState::kUavReadOnly, },
            { SID("depth"),   BufferState::kDsvReadOnly, },
            { SID("reused"),  BufferState::kSrv, },
          },
        },
        {
          SID("gpass4"),
          QueueType::kGraphics,
          AsyncCompute::kDisabled,
          SID("gpass4-func"),
          // viewport size
          { BufferSizeType::kSwapchainRelative,1.0f, 1.0f},
          // pass binded buffers
          {
            { SID("primary"), BufferState::kSrv, },
            { SID("primary"), BufferState::kRtv, },
            { SID("buf0"),    BufferState::kRtv, },
            { SID("buf1"),    BufferState::kSrv, },
            { SID("reused"),  BufferState::kSrv, },
          },
        },
      },
      // batch local buffer desc
      {
        {SID("buf0"), { BufferFormat::kR8G8B8A8_Unorm, BufferSizeType::kAbsolute, 1024.0f, 1024.0f, 1.0f, {} }},
        {SID("buf1"), { BufferFormat::kR8G8B8A8_Unorm, BufferSizeType::kSwapchainRelative, 0.5f, 0.5f, 1.0f, {} }}
      },
    },
  };
  BufferDescList global_buffer_descs = {
    {
      SID("primary"),
      { BufferFormat::kRgbLinearSdrDefault, BufferSizeType::kViewportRelative, 1.0f, 1.0f, 1.0f, GetClearValueDefaultDsv() }
    }, {
      SID("depth"),
      { BufferFormat::kDepthBufferDefault, BufferSizeType::kViewportRelative, 1.0f, 1.0f, 1.0f, GetClearValueDefaultRtv() }
    },
  };
  return std::make_tuple(batches, global_buffer_descs);
}
std::tuple<std::vector<BatchedRendererPass>, PassBindedBufferIdList, PassBindedBufferStateList> CreateTestDataWithDsvTransition() {
  std::vector<BatchedRendererPass> batches = {
    {
      SID("batch1"),
      {
        {
          SID("pass1"),
          QueueType::kGraphics,
          AsyncCompute::kDisabled,
          SID("func"),
          // viewport size
          { BufferSizeType::kSwapchainRelative,1.0f, 1.0f},
          // pass binded buffers
          {
            { SID("primary"), BufferState::kRtv, },
            { SID("depth"),   BufferState::kDsv, },
          },
        },
        {
          SID("pass2"),
          QueueType::kGraphics,
          AsyncCompute::kDisabled,
          SID("gpass2-func"),
          // viewport size
          { BufferSizeType::kSwapchainRelative,1.0f, 1.0f},
          // pass binded buffers
          {
            { SID("primary"), BufferState::kRtv, },
            { SID("depth"),   BufferState::kDsvReadOnly, },
          },
        },
        {
          SID("pass3"),
          QueueType::kGraphics,
          AsyncCompute::kDisabled,
          SID("cpass1-func"),
          // viewport size
          {},
          // pass binded buffers
          {
            { SID("primary"), BufferState::kRtv, },
            { SID("depth"),   BufferState::kSrv, },
          },
        },
      },
    },
  };
  auto pass_binded_buffer_ids = IdentifyPassBindedBuffers(batches.data(), batches.size());
  auto pass_binded_buffer_states = ExtractBufferStateList(batches.data(), batches.size(), pass_binded_buffer_ids);
  return std::make_tuple(batches, pass_binded_buffer_ids, pass_binded_buffer_states);
}
}
}
#include "doctest/doctest.h"
TEST_CASE("BufferState test") {
  using namespace illuminate::gfx;
  CHECK(IsBufferLoadOpLoad(BufferState::kSrvLoadDontCare));
  CHECK(IsBufferLoadOpLoad(BufferState::kSrv));
  CHECK(!IsBufferLoadOpLoad(BufferState::kRtvDontCareStore));
  CHECK(!IsBufferLoadOpLoad(BufferState::kRtvClearStore));
  CHECK(IsBufferLoadOpLoad(BufferState::kRtvLoadStore));
  CHECK(!IsBufferLoadOpLoad(BufferState::kRtv));
  CHECK(IsBufferLoadOpLoad(BufferState::kRtvBlendable));
  CHECK(!IsBufferLoadOpLoad(BufferState::kDsvDontCareStore));
  CHECK(!IsBufferLoadOpLoad(BufferState::kDsvClearStore));
  CHECK(IsBufferLoadOpLoad(BufferState::kDsvLoadStore));
  CHECK(IsBufferLoadOpLoad(BufferState::kDsvLoadDontCare));
  CHECK(!IsBufferLoadOpLoad(BufferState::kDsv));
  CHECK(IsBufferLoadOpLoad(BufferState::kDsvReadOnly));
  CHECK(!IsBufferLoadOpLoad(BufferState::kUavDontCareStore));
  CHECK(!IsBufferLoadOpLoad(BufferState::kUavClearStore));
  CHECK(IsBufferLoadOpLoad(BufferState::kUavLoadStore));
  CHECK(IsBufferLoadOpLoad(BufferState::kUavLoadDontCare));
  CHECK(!IsBufferLoadOpLoad(BufferState::kUav));
  CHECK(IsBufferLoadOpLoad(BufferState::kUavReadOnly));
  CHECK(!IsBufferStoreOpStore(BufferState::kSrvLoadDontCare));
  CHECK(!IsBufferStoreOpStore(BufferState::kSrv));
  CHECK(IsBufferStoreOpStore(BufferState::kRtvDontCareStore));
  CHECK(IsBufferStoreOpStore(BufferState::kRtvClearStore));
  CHECK(IsBufferStoreOpStore(BufferState::kRtvLoadStore));
  CHECK(IsBufferStoreOpStore(BufferState::kRtv));
  CHECK(IsBufferStoreOpStore(BufferState::kRtvBlendable));
  CHECK(IsBufferStoreOpStore(BufferState::kDsvDontCareStore));
  CHECK(IsBufferStoreOpStore(BufferState::kDsvClearStore));
  CHECK(IsBufferStoreOpStore(BufferState::kDsvLoadStore));
  CHECK(!IsBufferStoreOpStore(BufferState::kDsvLoadDontCare));
  CHECK(IsBufferStoreOpStore(BufferState::kDsv));
  CHECK(!IsBufferStoreOpStore(BufferState::kDsvReadOnly));
  CHECK(IsBufferStoreOpStore(BufferState::kUavDontCareStore));
  CHECK(IsBufferStoreOpStore(BufferState::kUavClearStore));
  CHECK(IsBufferStoreOpStore(BufferState::kUavLoadStore));
  CHECK(!IsBufferStoreOpStore(BufferState::kUavLoadDontCare));
  CHECK(IsBufferStoreOpStore(BufferState::kUav));
  CHECK(!IsBufferStoreOpStore(BufferState::kUavReadOnly));
  CHECK(GetBufferViewType(BufferState::kSrv) == kBufferViewTypeSrv);
  CHECK(GetBufferViewType(BufferState::kRtv) == kBufferViewTypeRtv);
  CHECK(GetBufferViewType(BufferState::kDsv) == kBufferViewTypeDsv);
  CHECK(GetBufferViewType(BufferState::kUav) == kBufferViewTypeUav);
}
TEST_CASE("pass binded buffer id list") {
  using namespace illuminate::gfx;
  RenderPassConfig a{}, b{};
  a.queue_type = QueueType::kGraphics;
  b.queue_type = QueueType::kGraphics;
  CHECK(IsPassExecutedOnSameQueue(a, b));
  a.queue_type = QueueType::kGraphics;
  b.queue_type = QueueType::kCompute;
  b.async_compute_enabled = AsyncCompute::kEnabled;
  CHECK(!IsPassExecutedOnSameQueue(a, b));
  b.async_compute_enabled = AsyncCompute::kDisabled;
  CHECK(IsPassExecutedOnSameQueue(a, b));
  a.queue_type = QueueType::kCompute;
  b.queue_type = QueueType::kGraphics;
  a.async_compute_enabled = AsyncCompute::kEnabled;
  CHECK(!IsPassExecutedOnSameQueue(a, b));
  a.async_compute_enabled = AsyncCompute::kDisabled;
  CHECK(IsPassExecutedOnSameQueue(a, b));
  b.queue_type = QueueType::kCompute;
  a.async_compute_enabled = AsyncCompute::kEnabled;
  b.async_compute_enabled = AsyncCompute::kEnabled;
  CHECK(IsPassExecutedOnSameQueue(a, b));
  a.async_compute_enabled = AsyncCompute::kDisabled;
  b.async_compute_enabled = AsyncCompute::kEnabled;
  CHECK(!IsPassExecutedOnSameQueue(a, b));
  a.async_compute_enabled = AsyncCompute::kEnabled;
  b.async_compute_enabled = AsyncCompute::kDisabled;
  CHECK(!IsPassExecutedOnSameQueue(a, b));
  auto test_data = std::get<0>(CreateTestData());
  auto pass_binded_buffer_ids = ExtractDataStoredBuffers(test_data.data(), test_data.size());
  CHECK(IsContaining(pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeRtv], SID("primary")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeDsv], SID("depth")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeRtv], SID("reused")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("gpass2")][kBufferViewTypeRtv], SID("primary")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("cpass1")][kBufferViewTypeUav], SID("buf0")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("cpass1")][kBufferViewTypeUav], SID("buf1")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("gpass3")][kBufferViewTypeRtv], SID("primary")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("gpass3")][kBufferViewTypeRtv], SID("sub")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("gpass4")][kBufferViewTypeRtv], SID("primary")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("gpass4")][kBufferViewTypeRtv], SID("buf0")));
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 0, 1, SID("primary"), pass_binded_buffer_ids) == pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeRtv][SID("primary")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 0, 1, SID("depth"), pass_binded_buffer_ids)   == pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeDsv][SID("depth")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 0, 1, SID("reused"), pass_binded_buffer_ids)  == pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeRtv][SID("reused")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 1, 0, SID("primary"), pass_binded_buffer_ids) == pass_binded_buffer_ids[SID("gpass2")][kBufferViewTypeRtv][SID("primary")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 1, 0, SID("buf0"), pass_binded_buffer_ids)    == pass_binded_buffer_ids[SID("cpass1")][kBufferViewTypeUav][SID("buf0")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 1, 0, SID("depth"), pass_binded_buffer_ids)   == pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeDsv][SID("depth")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 1, 0, SID("reused"), pass_binded_buffer_ids)  == pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeRtv][SID("reused")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 1, 1, SID("primary"), pass_binded_buffer_ids) == pass_binded_buffer_ids[SID("gpass3")][kBufferViewTypeRtv][SID("primary")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 1, 1, SID("buf1"), pass_binded_buffer_ids)    == pass_binded_buffer_ids[SID("cpass1")][kBufferViewTypeUav][SID("buf1")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 1, 1, SID("reused"), pass_binded_buffer_ids)  == pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeRtv][SID("reused")]);
  pass_binded_buffer_ids = IdentifyPassBindedBuffers(test_data.data(), test_data.size());
  CHECK(pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeRtv][SID("primary")] == pass_binded_buffer_ids[SID("gpass2")][kBufferViewTypeSrv][SID("primary")]);
  CHECK(pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeRtv][SID("primary")] != pass_binded_buffer_ids[SID("gpass2")][kBufferViewTypeRtv][SID("primary")]);
  CHECK(pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeDsv][SID("depth")]   == pass_binded_buffer_ids[SID("gpass2")][kBufferViewTypeDsv][SID("depth")]);
  CHECK(pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeDsv][SID("depth")]   == pass_binded_buffer_ids[SID("gpass3")][kBufferViewTypeDsv][SID("depth")]);
  CHECK(pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeRtv][SID("reused")]  == pass_binded_buffer_ids[SID("gpass2")][kBufferViewTypeSrv][SID("reused")]);
  CHECK(pass_binded_buffer_ids[SID("gpass2")][kBufferViewTypeRtv][SID("primary")] == pass_binded_buffer_ids[SID("gpass3")][kBufferViewTypeSrv][SID("primary")]);
  CHECK(pass_binded_buffer_ids[SID("gpass2")][kBufferViewTypeSrv][SID("reused")]  == pass_binded_buffer_ids[SID("gpass3")][kBufferViewTypeSrv][SID("reused")]);
  CHECK(pass_binded_buffer_ids[SID("cpass1")][kBufferViewTypeUav][SID("buf0")]    == pass_binded_buffer_ids[SID("gpass3")][kBufferViewTypeUav][SID("buf0")]);
  CHECK(pass_binded_buffer_ids[SID("cpass1")][kBufferViewTypeUav][SID("buf0")]    != pass_binded_buffer_ids[SID("gpass4")][kBufferViewTypeRtv][SID("buf0")]);
  CHECK(pass_binded_buffer_ids[SID("cpass1")][kBufferViewTypeUav][SID("buf1")]    == pass_binded_buffer_ids[SID("gpass4")][kBufferViewTypeSrv][SID("buf1")]);
  CHECK(pass_binded_buffer_ids[SID("gpass3")][kBufferViewTypeRtv][SID("primary")] == pass_binded_buffer_ids[SID("gpass4")][kBufferViewTypeSrv][SID("primary")]);
  CHECK(pass_binded_buffer_ids[SID("gpass3")][kBufferViewTypeSrv][SID("reused")]  == pass_binded_buffer_ids[SID("gpass4")][kBufferViewTypeSrv][SID("reused")]);
  CHECK(pass_binded_buffer_ids[SID("gpass4")][kBufferViewTypeSrv][SID("buf1")]    == pass_binded_buffer_ids[SID("cpass1")][kBufferViewTypeUav][SID("buf1")]);
  auto buffer_state_list = ExtractBufferStateList(test_data.data(), test_data.size(), pass_binded_buffer_ids);
  auto gpass1_rtv_primary = pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeRtv][SID("primary")];
  auto gpass1_dsv_depth   = pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeDsv][SID("depth")];
  auto gpass1_rtv_reused  = pass_binded_buffer_ids[SID("gpass1")][kBufferViewTypeRtv][SID("reused")];
  auto gpass2_rtv_primary = pass_binded_buffer_ids[SID("gpass2")][kBufferViewTypeRtv][SID("primary")];
  auto cpass1_uav_buf0    = pass_binded_buffer_ids[SID("cpass1")][kBufferViewTypeUav][SID("buf0")];
  auto cpass1_uav_buf1    = pass_binded_buffer_ids[SID("cpass1")][kBufferViewTypeUav][SID("buf1")];
  auto gpass3_rtv_primary = pass_binded_buffer_ids[SID("gpass3")][kBufferViewTypeRtv][SID("primary")];
  auto gpass3_rtv_sub     = pass_binded_buffer_ids[SID("gpass3")][kBufferViewTypeRtv][SID("sub")];
  auto gpass4_rtv_primary = pass_binded_buffer_ids[SID("gpass4")][kBufferViewTypeRtv][SID("primary")];
  auto gpass4_rtv_buf0    = pass_binded_buffer_ids[SID("gpass4")][kBufferViewTypeRtv][SID("buf0")];
  CHECK(buffer_state_list[gpass1_rtv_primary][SID("gpass1")] == BufferState::kRtv);
  CHECK(buffer_state_list[gpass1_dsv_depth][SID("gpass1")]   == BufferState::kDsv);
  CHECK(buffer_state_list[gpass1_rtv_reused][SID("gpass1")]  == BufferState::kRtv);
  CHECK(buffer_state_list[gpass1_rtv_primary][SID("gpass2")] == BufferState::kSrv);
  CHECK(buffer_state_list[gpass2_rtv_primary][SID("gpass2")] == BufferState::kRtv);
  CHECK(buffer_state_list[gpass1_dsv_depth][SID("gpass2")]   == BufferState::kDsvReadOnly);
  CHECK(buffer_state_list[gpass1_rtv_reused][SID("gpass2")]  == BufferState::kSrv);
  CHECK(buffer_state_list[cpass1_uav_buf0][SID("cpass1")]    == BufferState::kUav);
  CHECK(buffer_state_list[cpass1_uav_buf1][SID("cpass1")]    == BufferState::kUav);
  CHECK(buffer_state_list[gpass2_rtv_primary][SID("gpass3")] == BufferState::kSrv);
  CHECK(buffer_state_list[gpass3_rtv_primary][SID("gpass3")] == BufferState::kRtv);
  CHECK(buffer_state_list[gpass3_rtv_sub][SID("gpass3")]     == BufferState::kRtv);
  CHECK(buffer_state_list[cpass1_uav_buf0][SID("gpass3")]    == BufferState::kUavReadOnly);
  CHECK(buffer_state_list[gpass1_dsv_depth][SID("gpass3")]   == BufferState::kDsvReadOnly);
  CHECK(buffer_state_list[gpass1_rtv_reused][SID("gpass3")]  == BufferState::kSrv);
  CHECK(buffer_state_list[gpass3_rtv_primary][SID("gpass4")] == BufferState::kSrv);
  CHECK(buffer_state_list[gpass4_rtv_primary][SID("gpass4")] == BufferState::kRtv);
  CHECK(buffer_state_list[gpass4_rtv_buf0][SID("gpass4")]    == BufferState::kRtv);
  CHECK(buffer_state_list[cpass1_uav_buf1][SID("gpass4")]    == BufferState::kSrv);
  CHECK(buffer_state_list[gpass1_rtv_reused][SID("gpass4")]  == BufferState::kSrv);
  auto testdata_dsvwrite_dsvread_srv = CreateTestDataWithDsvTransition();
  auto testdata_dsvwrite_dsvread_srv_pass_binded_buffer_ids = std::get<1>(testdata_dsvwrite_dsvread_srv);
  auto testdata_dsvwrite_dsvread_srv_buffer_states = std::get<2>(testdata_dsvwrite_dsvread_srv);
  auto dsv_buffer_id = testdata_dsvwrite_dsvread_srv_pass_binded_buffer_ids[SID("pass1")][kBufferViewTypeDsv][SID("depth")];
  CHECK(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id][SID("pass1")] == BufferState::kDsv);
  CHECK(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id][SID("pass2")] == BufferState::kDsvReadOnly);
  CHECK(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id][SID("pass3")] == BufferState::kSrv);
  CombineMergeableBufferStates(std::get<0>(testdata_dsvwrite_dsvread_srv).data(), std::get<0>(testdata_dsvwrite_dsvread_srv).size(), testdata_dsvwrite_dsvread_srv_pass_binded_buffer_ids, &testdata_dsvwrite_dsvread_srv_buffer_states);
  CHECK(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id][SID("pass1")] == BufferState::kDsv);
  CHECK(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id][SID("pass2")] == BufferState::kSrvDsvReadOnly);
  CHECK(!IsContaining(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id], SID("pass3")));
  CombineMergeableBufferStates(test_data.data(), test_data.size(), pass_binded_buffer_ids, &buffer_state_list);
  CHECK(buffer_state_list[gpass1_rtv_primary][SID("gpass1")] == BufferState::kRtv);
  CHECK(buffer_state_list[gpass1_dsv_depth][SID("gpass1")] == BufferState::kDsv);
  CHECK(buffer_state_list[gpass1_rtv_reused][SID("gpass1")] == BufferState::kRtv);
  CHECK(buffer_state_list[gpass1_rtv_primary][SID("gpass2")] == BufferState::kSrv);
  CHECK(buffer_state_list[gpass2_rtv_primary][SID("gpass2")] == BufferState::kRtv);
  CHECK(buffer_state_list[gpass1_rtv_reused][SID("gpass2")] == BufferState::kSrv);
  CHECK(buffer_state_list[gpass1_dsv_depth][SID("gpass2")] == BufferState::kDsvReadOnly);
  CHECK(buffer_state_list[cpass1_uav_buf0][SID("cpass1")] == BufferState::kUav);
  CHECK(buffer_state_list[cpass1_uav_buf1][SID("cpass1")] == BufferState::kUav);
  CHECK(buffer_state_list[gpass2_rtv_primary][SID("gpass3")] == BufferState::kSrv);
  CHECK(buffer_state_list[gpass3_rtv_primary][SID("gpass3")] == BufferState::kRtv);
  CHECK(buffer_state_list[gpass3_rtv_sub][SID("gpass3")] == BufferState::kRtv);
  CHECK(buffer_state_list[cpass1_uav_buf0][SID("gpass3")] == BufferState::kUavReadOnly);
  CHECK(!IsContaining(buffer_state_list[gpass1_dsv_depth], SID("gpass3")));
  CHECK(!IsContaining(buffer_state_list[gpass1_rtv_reused], SID("gpass3")));
  CHECK(buffer_state_list[gpass3_rtv_primary][SID("gpass4")] == BufferState::kSrv);
  CHECK(buffer_state_list[gpass4_rtv_primary][SID("gpass4")] == BufferState::kRtv);
  CHECK(buffer_state_list[gpass4_rtv_buf0][SID("gpass4")] == BufferState::kRtv);
  CHECK(buffer_state_list[cpass1_uav_buf1][SID("gpass4")] == BufferState::kSrv);
  CHECK(!IsContaining(buffer_state_list[gpass1_rtv_reused], SID("gpass4")));
}
TEST_CASE("renderer test") {
  using namespace illuminate::gfx;
  auto test_data = CreateTestData();
  ProcessBatchedRendererPass(std::get<0>(test_data).data(), std::get<0>(test_data).size(), std::get<1>(test_data));
}

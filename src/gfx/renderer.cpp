#include "renderer.h"
#include "minimal_for_cpp.h"
namespace illuminate::gfx {
using PassBindedBufferIdList = std::unordered_map<StrId, std::unordered_map<BufferState, std::unordered_map<StrId, uint32_t>>>;
PassBindedBufferIdList ExtractDataStoredBuffers(const BatchedRendererPass* const batch_list, const uint32_t batch_num) {
  PassBindedBufferIdList pass_binded_buffer_ids;
  uint32_t new_id = 0;
  for (uint32_t i = 0; i < batch_num; i++) {
    auto& batch = batch_list[i];
    for (auto& pass : batch.pass_configs) {
      auto pass_name = pass.pass_name;
      for (auto& buffer : pass.pass_binded_buffers) {
        if (buffer.load_op  == BufferLoadOp::kLoad)   continue;
        if (buffer.store_op != BufferStoreOp::kStore) continue;
        pass_binded_buffer_ids[pass_name][buffer.state][buffer.buffer_name] = new_id;
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
        if (buffer.store_op != BufferStoreOp::kStore) continue;
        if (!IsContaining(pass_binded_buffer_ids, pass.pass_name)) continue;
        if (!IsContaining(pass_binded_buffer_ids.at(pass.pass_name), buffer.state)) continue;
        if (!IsContaining(pass_binded_buffer_ids.at(pass.pass_name).at(buffer.state), buffer_name)) continue;
        if (batch_index == used_batch_index && !IsPassExecutedOnSameQueue(pass, batch.pass_configs[used_pass_index])) continue;
        return pass_binded_buffer_ids.at(pass.pass_name).at(buffer.state).at(buffer_name);
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
        if (buffer.load_op != BufferLoadOp::kLoad) continue;
        if (buffer.state == BufferState::kSrv && buffer.store_op != BufferStoreOp::kDontCare) {
          logwarn("invalid state for srv. {} {} {}({})", i, j, k, buffer.buffer_name);
        }
        (*pass_binded_buffer_ids)[pass.pass_name][buffer.state][buffer.buffer_name] = FindBufferIdFromLatestStore(batch_list, batch_num, i, j, buffer.buffer_name, *pass_binded_buffer_ids);
      }
    }
  }
}
PassBindedBufferIdList IdentifyPassBindedBuffers(const BatchedRendererPass* const batch_list, const uint32_t batch_num) {
  PassBindedBufferIdList pass_binded_buffer_ids = ExtractDataStoredBuffers(batch_list, batch_num);
  LinkLoadingBuffersToExistingBuffers(batch_list, batch_num, &pass_binded_buffer_ids);
  return pass_binded_buffer_ids;
}
using PassBindedBufferStateList = std::unordered_map<uint32_t, std::unordered_map<StrId, std::tuple<BufferState, BufferStoreOp>>>;
PassBindedBufferStateList ExtractBufferStateList(const BatchedRendererPass* const batch_list, const uint32_t batch_num, const PassBindedBufferIdList& pass_binded_buffer_ids) {
  PassBindedBufferStateList list;
  for (uint32_t i = 0; i < batch_num; i++) {
    auto& batch = batch_list[i];
    for (auto& pass : batch.pass_configs) {
      auto pass_name = pass.pass_name;
      for (auto& buffer : pass.pass_binded_buffers) {
        auto buffer_id = pass_binded_buffer_ids.at(pass_name).at(buffer.state).at(buffer.buffer_name);
        if (IsContaining(list[buffer_id], pass_name)) {
          logwarn("duplicate buffer:{},{},{},{},{} in {} {}", buffer_id, buffer.buffer_name, buffer.state, buffer.load_op, buffer.store_op, i, pass_name);
        }
        list[buffer_id][pass_name] = std::make_tuple(buffer.state, buffer.store_op);
      }
    }
  }
  return list;
}
bool IsBufferStateMergeable(const BufferState current_state, const BufferStoreOp current_store_op, const BufferState next_state, const BufferStoreOp next_store_op) {
  if (current_state == next_state) return true;
  switch (current_state) {
    case BufferState::kSrv:
      if (next_state == BufferState::kRtv) return false;
      return next_store_op == BufferStoreOp::kDontCare;
    case BufferState::kRtv:
      return false;
    case BufferState::kUav:
      if (next_state == BufferState::kSrv) return current_store_op == BufferStoreOp::kDontCare;
      return false;
    case BufferState::kDsv:
      if (next_state == BufferState::kSrv) return current_store_op == BufferStoreOp::kDontCare;
      return false;
  }
  ASSERT(false && "no valid buffer state combination", current_state, current_store_op, next_state, next_store_op);
  return false;
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
            { SID("primary"), BufferState::kRtv, BufferLoadOp::kDontCare, BufferStoreOp::kStore, },
            { SID("reused"),  BufferState::kRtv, BufferLoadOp::kDontCare, BufferStoreOp::kStore, },
            { SID("depth"),   BufferState::kDsv, BufferLoadOp::kClear,    BufferStoreOp::kStore, },
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
            { SID("primary"), BufferState::kSrv, BufferLoadOp::kLoad,     BufferStoreOp::kDontCare, },
            { SID("primary"), BufferState::kRtv, BufferLoadOp::kDontCare, BufferStoreOp::kStore, },
            { SID("reused"),  BufferState::kSrv, BufferLoadOp::kLoad,     BufferStoreOp::kDontCare, },
            { SID("depth"),   BufferState::kDsv, BufferLoadOp::kLoad,     BufferStoreOp::kDontCare, },
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
            { SID("buf0"), BufferState::kUav, BufferLoadOp::kDontCare, BufferStoreOp::kStore, },
            { SID("buf1"), BufferState::kUav, BufferLoadOp::kDontCare, BufferStoreOp::kStore, },
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
            { SID("primary"), BufferState::kSrv, BufferLoadOp::kLoad,     BufferStoreOp::kDontCare, },
            { SID("primary"), BufferState::kRtv, BufferLoadOp::kDontCare, BufferStoreOp::kStore, },
            { SID("sub"),     BufferState::kRtv, BufferLoadOp::kDontCare, BufferStoreOp::kStore, },
            { SID("buf0"),    BufferState::kUav, BufferLoadOp::kLoad,     BufferStoreOp::kDontCare, },
            { SID("depth"),   BufferState::kDsv, BufferLoadOp::kLoad,     BufferStoreOp::kDontCare, },
            { SID("reused"),  BufferState::kSrv, BufferLoadOp::kLoad,     BufferStoreOp::kDontCare, },
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
            { SID("primary"), BufferState::kSrv, BufferLoadOp::kLoad,     BufferStoreOp::kDontCare, },
            { SID("primary"), BufferState::kRtv, BufferLoadOp::kDontCare, BufferStoreOp::kStore, },
            { SID("buf0"),    BufferState::kRtv, BufferLoadOp::kDontCare, BufferStoreOp::kStore, },
            { SID("buf1"),    BufferState::kSrv, BufferLoadOp::kLoad,     BufferStoreOp::kDontCare, },
            { SID("reused"),  BufferState::kSrv, BufferLoadOp::kLoad,     BufferStoreOp::kDontCare, },
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
            { SID("primary"), BufferState::kRtv, BufferLoadOp::kDontCare, BufferStoreOp::kStore, },
            { SID("depth"),   BufferState::kDsv, BufferLoadOp::kClear,    BufferStoreOp::kStore, },
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
            { SID("primary"), BufferState::kRtv, BufferLoadOp::kDontCare, BufferStoreOp::kStore, },
            { SID("depth"),   BufferState::kDsv, BufferLoadOp::kLoad,     BufferStoreOp::kDontCare, },
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
            { SID("primary"), BufferState::kRtv, BufferLoadOp::kDontCare, BufferStoreOp::kStore, },
            { SID("depth"),   BufferState::kSrv, BufferLoadOp::kLoad,     BufferStoreOp::kDontCare, },
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
  CHECK(IsContaining(pass_binded_buffer_ids[SID("gpass1")][BufferState::kRtv], SID("primary")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("gpass1")][BufferState::kDsv], SID("depth")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("gpass1")][BufferState::kRtv], SID("reused")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("gpass2")][BufferState::kRtv], SID("primary")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("cpass1")][BufferState::kUav], SID("buf0")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("cpass1")][BufferState::kUav], SID("buf1")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("gpass3")][BufferState::kRtv], SID("primary")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("gpass3")][BufferState::kRtv], SID("sub")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("gpass4")][BufferState::kRtv], SID("primary")));
  CHECK(IsContaining(pass_binded_buffer_ids[SID("gpass4")][BufferState::kRtv], SID("buf0")));
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 0, 1, SID("primary"), pass_binded_buffer_ids) == pass_binded_buffer_ids[SID("gpass1")][BufferState::kRtv][SID("primary")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 0, 1, SID("depth"), pass_binded_buffer_ids)   == pass_binded_buffer_ids[SID("gpass1")][BufferState::kDsv][SID("depth")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 0, 1, SID("reused"), pass_binded_buffer_ids)  == pass_binded_buffer_ids[SID("gpass1")][BufferState::kRtv][SID("reused")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 1, 0, SID("primary"), pass_binded_buffer_ids) == pass_binded_buffer_ids[SID("gpass2")][BufferState::kRtv][SID("primary")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 1, 0, SID("buf0"), pass_binded_buffer_ids)    == pass_binded_buffer_ids[SID("cpass1")][BufferState::kUav][SID("buf0")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 1, 0, SID("depth"), pass_binded_buffer_ids)   == pass_binded_buffer_ids[SID("gpass1")][BufferState::kDsv][SID("depth")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 1, 0, SID("reused"), pass_binded_buffer_ids)  == pass_binded_buffer_ids[SID("gpass1")][BufferState::kRtv][SID("reused")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 1, 1, SID("primary"), pass_binded_buffer_ids) == pass_binded_buffer_ids[SID("gpass3")][BufferState::kRtv][SID("primary")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 1, 1, SID("buf1"), pass_binded_buffer_ids)    == pass_binded_buffer_ids[SID("cpass1")][BufferState::kUav][SID("buf1")]);
  CHECK(FindBufferIdFromLatestStore(test_data.data(), test_data.size(), 1, 1, SID("reused"), pass_binded_buffer_ids)  == pass_binded_buffer_ids[SID("gpass1")][BufferState::kRtv][SID("reused")]);
  pass_binded_buffer_ids = IdentifyPassBindedBuffers(test_data.data(), test_data.size());
  CHECK(pass_binded_buffer_ids[SID("gpass1")][BufferState::kRtv][SID("primary")] == pass_binded_buffer_ids[SID("gpass2")][BufferState::kSrv][SID("primary")]);
  CHECK(pass_binded_buffer_ids[SID("gpass1")][BufferState::kRtv][SID("primary")] != pass_binded_buffer_ids[SID("gpass2")][BufferState::kRtv][SID("primary")]);
  CHECK(pass_binded_buffer_ids[SID("gpass1")][BufferState::kDsv][SID("depth")]   == pass_binded_buffer_ids[SID("gpass2")][BufferState::kDsv][SID("depth")]);
  CHECK(pass_binded_buffer_ids[SID("gpass1")][BufferState::kDsv][SID("depth")]   == pass_binded_buffer_ids[SID("gpass3")][BufferState::kDsv][SID("depth")]);
  CHECK(pass_binded_buffer_ids[SID("gpass1")][BufferState::kRtv][SID("reused")]  == pass_binded_buffer_ids[SID("gpass2")][BufferState::kSrv][SID("reused")]);
  CHECK(pass_binded_buffer_ids[SID("gpass2")][BufferState::kRtv][SID("primary")] == pass_binded_buffer_ids[SID("gpass3")][BufferState::kSrv][SID("primary")]);
  CHECK(pass_binded_buffer_ids[SID("gpass2")][BufferState::kSrv][SID("reused")]  == pass_binded_buffer_ids[SID("gpass3")][BufferState::kSrv][SID("reused")]);
  CHECK(pass_binded_buffer_ids[SID("cpass1")][BufferState::kUav][SID("buf0")]    == pass_binded_buffer_ids[SID("gpass3")][BufferState::kUav][SID("buf0")]);
  CHECK(pass_binded_buffer_ids[SID("cpass1")][BufferState::kUav][SID("buf0")]    != pass_binded_buffer_ids[SID("gpass4")][BufferState::kRtv][SID("buf0")]);
  CHECK(pass_binded_buffer_ids[SID("cpass1")][BufferState::kUav][SID("buf1")]    == pass_binded_buffer_ids[SID("gpass4")][BufferState::kSrv][SID("buf1")]);
  CHECK(pass_binded_buffer_ids[SID("gpass3")][BufferState::kRtv][SID("primary")] == pass_binded_buffer_ids[SID("gpass4")][BufferState::kSrv][SID("primary")]);
  CHECK(pass_binded_buffer_ids[SID("gpass3")][BufferState::kSrv][SID("reused")]  == pass_binded_buffer_ids[SID("gpass4")][BufferState::kSrv][SID("reused")]);
  CHECK(pass_binded_buffer_ids[SID("gpass4")][BufferState::kSrv][SID("buf1")]    == pass_binded_buffer_ids[SID("cpass1")][BufferState::kUav][SID("buf1")]);
  auto buffer_state_list = ExtractBufferStateList(test_data.data(), test_data.size(), pass_binded_buffer_ids);
  auto gpass1_rtv_primary = pass_binded_buffer_ids[SID("gpass1")][BufferState::kRtv][SID("primary")];
  auto gpass1_dsv_depth   = pass_binded_buffer_ids[SID("gpass1")][BufferState::kDsv][SID("depth")];
  auto gpass1_rtv_reused  = pass_binded_buffer_ids[SID("gpass1")][BufferState::kRtv][SID("reused")];
  auto gpass2_rtv_primary = pass_binded_buffer_ids[SID("gpass2")][BufferState::kRtv][SID("primary")];
  auto cpass1_uav_buf0    = pass_binded_buffer_ids[SID("cpass1")][BufferState::kUav][SID("buf0")];
  auto cpass1_uav_buf1    = pass_binded_buffer_ids[SID("cpass1")][BufferState::kUav][SID("buf1")];
  auto gpass3_rtv_primary = pass_binded_buffer_ids[SID("gpass3")][BufferState::kRtv][SID("primary")];
  auto gpass3_rtv_sub     = pass_binded_buffer_ids[SID("gpass3")][BufferState::kRtv][SID("sub")];
  auto gpass4_rtv_primary = pass_binded_buffer_ids[SID("gpass4")][BufferState::kRtv][SID("primary")];
  auto gpass4_rtv_buf0    = pass_binded_buffer_ids[SID("gpass4")][BufferState::kRtv][SID("buf0")];
  CHECK(std::get<0>(buffer_state_list[gpass1_rtv_primary][SID("gpass1")]) == BufferState::kRtv);
  CHECK(std::get<1>(buffer_state_list[gpass1_rtv_primary][SID("gpass1")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[gpass1_dsv_depth][SID("gpass1")]) == BufferState::kDsv);
  CHECK(std::get<1>(buffer_state_list[gpass1_dsv_depth][SID("gpass1")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[gpass1_rtv_reused][SID("gpass1")]) == BufferState::kRtv);
  CHECK(std::get<1>(buffer_state_list[gpass1_rtv_reused][SID("gpass1")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[gpass1_rtv_primary][SID("gpass2")]) == BufferState::kSrv);
  CHECK(std::get<1>(buffer_state_list[gpass1_rtv_primary][SID("gpass2")]) == BufferStoreOp::kDontCare);
  CHECK(std::get<0>(buffer_state_list[gpass2_rtv_primary][SID("gpass2")]) == BufferState::kRtv);
  CHECK(std::get<1>(buffer_state_list[gpass2_rtv_primary][SID("gpass2")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[gpass1_dsv_depth][SID("gpass2")]) == BufferState::kDsv);
  CHECK(std::get<1>(buffer_state_list[gpass1_dsv_depth][SID("gpass2")]) == BufferStoreOp::kDontCare);
  CHECK(std::get<0>(buffer_state_list[gpass1_rtv_reused][SID("gpass2")]) == BufferState::kSrv);
  CHECK(std::get<1>(buffer_state_list[gpass1_rtv_reused][SID("gpass2")]) == BufferStoreOp::kDontCare);
  CHECK(std::get<0>(buffer_state_list[cpass1_uav_buf0][SID("cpass1")]) == BufferState::kUav);
  CHECK(std::get<1>(buffer_state_list[cpass1_uav_buf0][SID("cpass1")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[cpass1_uav_buf1][SID("cpass1")]) == BufferState::kUav);
  CHECK(std::get<1>(buffer_state_list[cpass1_uav_buf1][SID("cpass1")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[gpass2_rtv_primary][SID("gpass3")]) == BufferState::kSrv);
  CHECK(std::get<1>(buffer_state_list[gpass2_rtv_primary][SID("gpass3")]) == BufferStoreOp::kDontCare);
  CHECK(std::get<0>(buffer_state_list[gpass3_rtv_primary][SID("gpass3")]) == BufferState::kRtv);
  CHECK(std::get<1>(buffer_state_list[gpass3_rtv_primary][SID("gpass3")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[gpass3_rtv_sub][SID("gpass3")]) == BufferState::kRtv);
  CHECK(std::get<1>(buffer_state_list[gpass3_rtv_sub][SID("gpass3")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[cpass1_uav_buf0][SID("gpass3")]) == BufferState::kUav);
  CHECK(std::get<1>(buffer_state_list[cpass1_uav_buf0][SID("gpass3")]) == BufferStoreOp::kDontCare);
  CHECK(std::get<0>(buffer_state_list[gpass1_dsv_depth][SID("gpass3")]) == BufferState::kDsv);
  CHECK(std::get<1>(buffer_state_list[gpass1_dsv_depth][SID("gpass3")]) == BufferStoreOp::kDontCare);
  CHECK(std::get<0>(buffer_state_list[gpass1_rtv_reused][SID("gpass3")]) == BufferState::kSrv);
  CHECK(std::get<1>(buffer_state_list[gpass1_rtv_reused][SID("gpass3")]) == BufferStoreOp::kDontCare);
  CHECK(std::get<0>(buffer_state_list[gpass3_rtv_primary][SID("gpass4")]) == BufferState::kSrv);
  CHECK(std::get<1>(buffer_state_list[gpass3_rtv_primary][SID("gpass4")]) == BufferStoreOp::kDontCare);
  CHECK(std::get<0>(buffer_state_list[gpass4_rtv_primary][SID("gpass4")]) == BufferState::kRtv);
  CHECK(std::get<1>(buffer_state_list[gpass4_rtv_primary][SID("gpass4")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[gpass4_rtv_buf0][SID("gpass4")]) == BufferState::kRtv);
  CHECK(std::get<1>(buffer_state_list[gpass4_rtv_buf0][SID("gpass4")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[cpass1_uav_buf1][SID("gpass4")]) == BufferState::kSrv);
  CHECK(std::get<1>(buffer_state_list[cpass1_uav_buf1][SID("gpass4")]) == BufferStoreOp::kDontCare);
  CHECK(std::get<0>(buffer_state_list[gpass1_rtv_reused][SID("gpass4")]) == BufferState::kSrv);
  CHECK(std::get<1>(buffer_state_list[gpass1_rtv_reused][SID("gpass4")]) == BufferStoreOp::kDontCare);
  CHECK(!IsBufferStateMergeable(BufferState::kSrv, BufferStoreOp::kDontCare, BufferState::kRtv, BufferStoreOp::kStore));
  CHECK(!IsBufferStateMergeable(BufferState::kSrv, BufferStoreOp::kDontCare, BufferState::kDsv, BufferStoreOp::kStore));
  CHECK( IsBufferStateMergeable(BufferState::kSrv, BufferStoreOp::kDontCare, BufferState::kDsv, BufferStoreOp::kDontCare));
  CHECK(!IsBufferStateMergeable(BufferState::kSrv, BufferStoreOp::kDontCare, BufferState::kUav, BufferStoreOp::kStore));
  CHECK( IsBufferStateMergeable(BufferState::kSrv, BufferStoreOp::kDontCare, BufferState::kUav, BufferStoreOp::kDontCare));
  CHECK(!IsBufferStateMergeable(BufferState::kRtv, BufferStoreOp::kStore, BufferState::kDsv, BufferStoreOp::kStore));
  CHECK(!IsBufferStateMergeable(BufferState::kRtv, BufferStoreOp::kStore, BufferState::kDsv, BufferStoreOp::kDontCare));
  CHECK(!IsBufferStateMergeable(BufferState::kRtv, BufferStoreOp::kStore, BufferState::kUav, BufferStoreOp::kStore));
  CHECK(!IsBufferStateMergeable(BufferState::kRtv, BufferStoreOp::kStore, BufferState::kUav, BufferStoreOp::kDontCare));
  CHECK(!IsBufferStateMergeable(BufferState::kUav, BufferStoreOp::kStore, BufferState::kDsv, BufferStoreOp::kStore));
  CHECK(!IsBufferStateMergeable(BufferState::kUav, BufferStoreOp::kStore, BufferState::kDsv, BufferStoreOp::kDontCare));
  CHECK(!IsBufferStateMergeable(BufferState::kUav, BufferStoreOp::kDontCare, BufferState::kDsv, BufferStoreOp::kStore));
  CHECK(!IsBufferStateMergeable(BufferState::kUav, BufferStoreOp::kDontCare, BufferState::kDsv, BufferStoreOp::kDontCare));
  auto testdata_dsvwrite_dsvread_srv = CreateTestDataWithDsvTransition();
  auto testdata_dsvwrite_dsvread_srv_pass_binded_buffer_ids = std::get<1>(testdata_dsvwrite_dsvread_srv);
  auto testdata_dsvwrite_dsvread_srv_buffer_states = std::get<2>(testdata_dsvwrite_dsvread_srv);
  auto dsv_buffer_id = testdata_dsvwrite_dsvread_srv_pass_binded_buffer_ids[SID("pass1")][BufferState::kDsv][SID("depth")];
  CHECK(std::get<0>(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id][SID("pass1")]) == BufferState::kDsv);
  CHECK(std::get<1>(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id][SID("pass1")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id][SID("pass2")]) == BufferState::kDsv);
  CHECK(std::get<1>(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id][SID("pass2")]) == BufferStoreOp::kDontCare);
  CHECK(std::get<0>(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id][SID("pass3")]) == BufferState::kSrv);
  CHECK(std::get<1>(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id][SID("pass3")]) == BufferStoreOp::kDontCare);
  CombineMergeableBufferStates(std::get<0>(testdata_dsvwrite_dsvread_srv).data(), std::get<0>(testdata_dsvwrite_dsvread_srv).size(), testdata_dsvwrite_dsvread_srv_pass_binded_buffer_ids, &testdata_dsvwrite_dsvread_srv_buffer_states);
  CHECK(std::get<0>(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id][SID("pass1")]) == BufferState::kDsv);
  CHECK(std::get<1>(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id][SID("pass1")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id][SID("pass2")]) == (BufferState::kDsv | BufferState::kSrv));
  CHECK(std::get<1>(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id][SID("pass2")]) == BufferStoreOp::kDontCare);
  CHECK(!IsContaining(testdata_dsvwrite_dsvread_srv_buffer_states[dsv_buffer_id], SID("pass3")));
  CombineMergeableBufferStates(test_data.data(), test_data.size(), pass_binded_buffer_ids, &buffer_state_list);
  CHECK(std::get<0>(buffer_state_list[gpass1_rtv_primary][SID("gpass1")]) == BufferState::kRtv);
  CHECK(std::get<1>(buffer_state_list[gpass1_rtv_primary][SID("gpass1")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[gpass1_dsv_depth][SID("gpass1")]) == BufferState::kDsv);
  CHECK(std::get<1>(buffer_state_list[gpass1_dsv_depth][SID("gpass1")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[gpass1_rtv_reused][SID("gpass1")]) == BufferState::kRtv);
  CHECK(std::get<1>(buffer_state_list[gpass1_rtv_reused][SID("gpass1")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[gpass1_rtv_primary][SID("gpass2")]) == BufferState::kSrv);
  CHECK(std::get<1>(buffer_state_list[gpass1_rtv_primary][SID("gpass2")]) == BufferStoreOp::kDontCare);
  CHECK(std::get<0>(buffer_state_list[gpass2_rtv_primary][SID("gpass2")]) == BufferState::kRtv);
  CHECK(std::get<1>(buffer_state_list[gpass2_rtv_primary][SID("gpass2")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[gpass1_rtv_reused][SID("gpass2")]) == BufferState::kSrv);
  CHECK(std::get<1>(buffer_state_list[gpass1_rtv_reused][SID("gpass2")]) == BufferStoreOp::kDontCare);
  CHECK(std::get<0>(buffer_state_list[gpass1_dsv_depth][SID("gpass2")]) == BufferState::kDsv);
  CHECK(std::get<1>(buffer_state_list[gpass1_dsv_depth][SID("gpass2")]) == BufferStoreOp::kDontCare);
  CHECK(std::get<0>(buffer_state_list[cpass1_uav_buf0][SID("cpass1")]) == BufferState::kUav);
  CHECK(std::get<1>(buffer_state_list[cpass1_uav_buf0][SID("cpass1")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[cpass1_uav_buf1][SID("cpass1")]) == BufferState::kUav);
  CHECK(std::get<1>(buffer_state_list[cpass1_uav_buf1][SID("cpass1")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[gpass2_rtv_primary][SID("gpass3")]) == BufferState::kSrv);
  CHECK(std::get<1>(buffer_state_list[gpass2_rtv_primary][SID("gpass3")]) == BufferStoreOp::kDontCare);
  CHECK(std::get<0>(buffer_state_list[gpass3_rtv_primary][SID("gpass3")]) == BufferState::kRtv);
  CHECK(std::get<1>(buffer_state_list[gpass3_rtv_primary][SID("gpass3")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[gpass3_rtv_sub][SID("gpass3")]) == BufferState::kRtv);
  CHECK(std::get<1>(buffer_state_list[gpass3_rtv_sub][SID("gpass3")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[cpass1_uav_buf0][SID("gpass3")]) == BufferState::kUav);
  CHECK(std::get<1>(buffer_state_list[cpass1_uav_buf0][SID("gpass3")]) == BufferStoreOp::kDontCare);
  CHECK(!IsContaining(buffer_state_list[gpass1_dsv_depth], SID("gpass3")));
  CHECK(!IsContaining(buffer_state_list[gpass1_rtv_reused], SID("gpass3")));
  CHECK(std::get<0>(buffer_state_list[gpass3_rtv_primary][SID("gpass4")]) == BufferState::kSrv);
  CHECK(std::get<1>(buffer_state_list[gpass3_rtv_primary][SID("gpass4")]) == BufferStoreOp::kDontCare);
  CHECK(std::get<0>(buffer_state_list[gpass4_rtv_primary][SID("gpass4")]) == BufferState::kRtv);
  CHECK(std::get<1>(buffer_state_list[gpass4_rtv_primary][SID("gpass4")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[gpass4_rtv_buf0][SID("gpass4")]) == BufferState::kRtv);
  CHECK(std::get<1>(buffer_state_list[gpass4_rtv_buf0][SID("gpass4")]) == BufferStoreOp::kStore);
  CHECK(std::get<0>(buffer_state_list[cpass1_uav_buf1][SID("gpass4")]) == BufferState::kSrv);
  CHECK(std::get<1>(buffer_state_list[cpass1_uav_buf1][SID("gpass4")]) == BufferStoreOp::kDontCare);
  CHECK(!IsContaining(buffer_state_list[gpass1_rtv_reused], SID("gpass4")));
}
TEST_CASE("renderer test") {
  using namespace illuminate::gfx;
  auto test_data = CreateTestData();
  ProcessBatchedRendererPass(std::get<0>(test_data).data(), std::get<0>(test_data).size(), std::get<1>(test_data));
}

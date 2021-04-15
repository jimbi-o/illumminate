#include "render_graph.h"
namespace illuminate::gfx {
using RenderPassBufferInfo = vector<BufferStateSet>;
using RenderPassBufferInfoList = vector<RenderPassBufferInfo>;
struct BufferStateListPerBuffer {
  vector<uint32_t> pass_index_list;
  vector<BufferStateFlags> buffer_state_list;
};
unordered_map<BufferId, BufferStateListPerBuffer> ConfigureRenderPassBufferUsages(RenderPassBufferInfoList&& render_pass_buffer_info_list, std::pmr::memory_resource* memory_resource) {
  unordered_map<BufferId, BufferStateListPerBuffer> buffer_usage_list{memory_resource};
  for (uint32_t pass_index = 0; auto&& render_pass_buffer_info_list_per_pass : render_pass_buffer_info_list) {
    for (auto&& buffer_info : render_pass_buffer_info_list_per_pass) {
      if (!buffer_usage_list.contains(buffer_info.buffer_id)) {
        buffer_usage_list.insert_or_assign(buffer_info.buffer_id, BufferStateListPerBuffer{vector<uint32_t>{memory_resource}, vector<BufferStateFlags>{memory_resource}});
      }
      buffer_usage_list.at(buffer_info.buffer_id).pass_index_list.push_back(pass_index);
      buffer_usage_list.at(buffer_info.buffer_id).buffer_state_list.push_back(buffer_info.state);
    }
    pass_index++;
  }
  return buffer_usage_list;
}
struct BarrierTransition {
  BufferStateFlags state_before;
  BufferStateFlags state_after;
};
struct BarrierConfig {
  BufferId buffer_id;
  BarrierSplitType split_type;
  std::byte _pad[3]{};
  std::variant<BarrierTransition> params;
};
using BarrierUserPassIndexMap = unordered_map<BufferId, vector<uint32_t>>;
using BarrierListMap = unordered_map<BufferId, vector<BarrierConfig>>;
std::tuple<BarrierUserPassIndexMap, BarrierListMap> ConfigureBarriersPerBuffer(unordered_map<BufferId, BufferStateListPerBuffer>&& buffer_state_list, std::pmr::memory_resource* memory_resource) {
  BarrierUserPassIndexMap barrier_user_pass_index_map{memory_resource};
  BarrierListMap barrier_list{memory_resource};
  for (auto& [buffer_id, state_info_list] : buffer_state_list) {
    const auto& state_num = state_info_list.pass_index_list.size();
    if (state_num <= 1) continue;
    ASSERT(state_num == state_info_list.buffer_state_list.size());
    vector<uint32_t> barrier_user_pass_index_per_buffer{memory_resource};
    vector<BarrierConfig> barrier_list_per_buffer{memory_resource};
    for (uint32_t i = 0; i < state_num - 1; i++) {
      auto barrier_exists = !barrier_list_per_buffer.empty();
      auto& current_state = barrier_exists ? std::get<BarrierTransition>(barrier_list_per_buffer.back().params).state_after : state_info_list.buffer_state_list[i];
      auto& next_state    = state_info_list.buffer_state_list[i + 1];
      if (current_state == next_state || (current_state & next_state) == next_state) continue;
      if (barrier_exists && IsBufferStateFlagsMergeable(current_state, next_state)) {
        std::get<BarrierTransition>(barrier_list_per_buffer.back().params).state_after = MergeBufferStateFlags(current_state, next_state);
        continue;
      }
      BarrierConfig barrier{
        .buffer_id = buffer_id,
        .split_type = BarrierSplitType::kNone,
        .params = BarrierTransition{
          .state_before = current_state,
          .state_after  = next_state,
        },
      };
      auto& current_pass_index = state_info_list.pass_index_list[i];
      auto& next_pass_index = state_info_list.pass_index_list[i + 1];
      if (current_pass_index + 1 >= next_pass_index)  {
        barrier_user_pass_index_per_buffer.push_back(current_pass_index);
        barrier_list_per_buffer.push_back(std::move(barrier));
      } else {
        barrier_user_pass_index_per_buffer.push_back(current_pass_index);
        barrier_user_pass_index_per_buffer.push_back(next_pass_index - 1);
        barrier.split_type = BarrierSplitType::kBegin;
        barrier_list_per_buffer.push_back(barrier);
        barrier.split_type = BarrierSplitType::kEnd;
        barrier_list_per_buffer.push_back(std::move(barrier));
      }
    }
    barrier_user_pass_index_map.insert_or_assign(buffer_id, std::move(barrier_user_pass_index_per_buffer));
    barrier_list.insert_or_assign(buffer_id, std::move(barrier_list_per_buffer));
  }
  return {barrier_user_pass_index_map, barrier_list};
}
using BarrierConfigList = vector<vector<BarrierConfig>>;
BarrierConfigList ConfigureBarriersBetweenRenderPass(BarrierUserPassIndexMap&& barrier_user_pass_index_map, BarrierListMap&& barrier_list_map, const uint32_t render_pass_num, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  set<BufferId> buffer_id_list{memory_resource_work};
  for (auto& [buffer_id, list] : barrier_user_pass_index_map) {
    buffer_id_list.insert(buffer_id);
  }
  BarrierConfigList barriers{memory_resource};
  barriers.resize(render_pass_num + 1);
  for (auto& buffer_id : buffer_id_list) {
    auto& pass_index_list = barrier_user_pass_index_map.at(buffer_id);
    const auto& barrier_num = pass_index_list.size();
    ASSERT(barrier_list_map.contains(buffer_id));
    auto& barrier_list = barrier_list_map.at(buffer_id);
    ASSERT(barrier_num == barrier_list.size());
    for (uint32_t i = 0; i < barrier_num; i++) {
      barriers[pass_index_list[i]].push_back(std::move(barrier_list[i]));
    }
  }
  return barriers;
}
}
#ifdef BUILD_WITH_TEST
namespace {
const uint32_t buffer_offset_in_bytes_persistent = 0;
const uint32_t buffer_size_in_bytes_persistent = 16 * 1024;
const uint32_t buffer_offset_in_bytes_scene = buffer_offset_in_bytes_persistent + buffer_size_in_bytes_persistent;
const uint32_t buffer_size_in_bytes_scene = 4 * 1024;
const uint32_t buffer_offset_in_bytes_frame = buffer_offset_in_bytes_scene + buffer_size_in_bytes_scene;
const uint32_t buffer_size_in_bytes_frame = 4 * 1024;
const uint32_t buffer_offset_in_bytes_work = buffer_offset_in_bytes_frame + buffer_size_in_bytes_frame;
const uint32_t buffer_size_in_bytes_work = 4 * 1024;
std::byte buffer[buffer_offset_in_bytes_work + buffer_size_in_bytes_work]{};
}
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
#include "doctest/doctest.h"
TEST_CASE("barrier for load from srv") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_work(&buffer[buffer_size_in_bytes_work], buffer_size_in_bytes_work);
  RenderPassBufferInfo render_pass_buffer_info_initial{&memory_resource_work};
  render_pass_buffer_info_initial.push_back({
      .buffer_id = 2,
      .state = kBufferStateFlagPresent,
    });
  render_pass_buffer_info_initial.push_back({
      .buffer_id = 1,
      .state = kBufferStateFlagRtv,
    });
  RenderPassBufferInfo render_pass_buffer_info_draw{&memory_resource_work};
  render_pass_buffer_info_draw.push_back({
      .buffer_id = 1,
      .state = kBufferStateFlagRtv,
    });
  RenderPassBufferInfo render_pass_buffer_info_copy{&memory_resource_work};
  render_pass_buffer_info_copy.push_back({
      .buffer_id = 1,
      .state = kBufferStateFlagSrvPsOnly,
    });
  render_pass_buffer_info_copy.push_back({
      .buffer_id = 2,
      .state = kBufferStateFlagRtv,
    });
  RenderPassBufferInfo render_pass_buffer_info_final{&memory_resource_work};
  render_pass_buffer_info_final.push_back({
      .buffer_id = 1,
      .state = kBufferStateFlagRtv,
    });
  render_pass_buffer_info_final.push_back({
      .buffer_id = 2,
      .state = kBufferStateFlagPresent,
    });
  RenderPassBufferInfoList pass_buffer_info_list{
    {
      std::move(render_pass_buffer_info_initial),
      std::move(render_pass_buffer_info_draw),
      std::move(render_pass_buffer_info_copy),
      std::move(render_pass_buffer_info_final),
    },
    &memory_resource_work
  };
  auto buffer_usage_list = ConfigureRenderPassBufferUsages(std::move(pass_buffer_info_list), &memory_resource_work);
  CHECK(buffer_usage_list.size() == 2);
  CHECK(buffer_usage_list.contains(1));
  CHECK(buffer_usage_list.at(1).pass_index_list.size() == 4);
  CHECK(buffer_usage_list.at(1).pass_index_list[0] == 0);
  CHECK(buffer_usage_list.at(1).pass_index_list[1] == 1);
  CHECK(buffer_usage_list.at(1).pass_index_list[2] == 2);
  CHECK(buffer_usage_list.at(1).pass_index_list[3] == 3);
  CHECK(buffer_usage_list.at(1).buffer_state_list.size() == 4);
  CHECK(buffer_usage_list.at(1).buffer_state_list[0] == kBufferStateFlagRtv);
  CHECK(buffer_usage_list.at(1).buffer_state_list[1] == kBufferStateFlagRtv);
  CHECK(buffer_usage_list.at(1).buffer_state_list[2] == kBufferStateFlagSrvPsOnly);
  CHECK(buffer_usage_list.at(1).buffer_state_list[3] == kBufferStateFlagRtv);
  CHECK(buffer_usage_list.contains(2));
  CHECK(buffer_usage_list.at(2).pass_index_list.size() == 3);
  CHECK(buffer_usage_list.at(2).pass_index_list[0] == 0);
  CHECK(buffer_usage_list.at(2).pass_index_list[1] == 2);
  CHECK(buffer_usage_list.at(2).pass_index_list[2] == 3);
  CHECK(buffer_usage_list.at(2).buffer_state_list.size() == 3);
  CHECK(buffer_usage_list.at(2).buffer_state_list[0] == kBufferStateFlagPresent);
  CHECK(buffer_usage_list.at(2).buffer_state_list[1] == kBufferStateFlagRtv);
  CHECK(buffer_usage_list.at(2).buffer_state_list[2] == kBufferStateFlagPresent);
  auto [barrier_user_pass_index_map, barrier_list_map] = ConfigureBarriersPerBuffer(std::move(buffer_usage_list), &memory_resource_work);
  CHECK(barrier_user_pass_index_map.size() == 2);
  CHECK(barrier_user_pass_index_map.contains(1));
  CHECK(barrier_user_pass_index_map.at(1).size() == 2);
  CHECK(barrier_user_pass_index_map.at(1)[0] == 1);
  CHECK(barrier_user_pass_index_map.at(1)[1] == 2);
  CHECK(barrier_user_pass_index_map.contains(2));
  CHECK(barrier_user_pass_index_map.at(2).size() == 3);
  CHECK(barrier_user_pass_index_map.at(2)[0] == 0);
  CHECK(barrier_user_pass_index_map.at(2)[1] == 1);
  CHECK(barrier_user_pass_index_map.at(2)[2] == 2);
  CHECK(barrier_list_map.size() == 2);
  CHECK(barrier_list_map.contains(1));
  CHECK(barrier_list_map.contains(2));
  CHECK(barrier_list_map.at(1).size() == 2);
  {
    auto& barrier = barrier_list_map.at(1)[0];
    CHECK(barrier.buffer_id == 1);
    CHECK(barrier.split_type == BarrierSplitType::kNone);
    CHECK(std::get<BarrierTransition>(barrier.params).state_before == kBufferStateFlagRtv);
    CHECK(std::get<BarrierTransition>(barrier.params).state_after  == kBufferStateFlagSrvPsOnly);
  }
  {
    auto& barrier = barrier_list_map.at(1)[1];
    CHECK(barrier.buffer_id == 1);
    CHECK(barrier.split_type == BarrierSplitType::kNone);
    CHECK(std::get<BarrierTransition>(barrier.params).state_before == kBufferStateFlagSrvPsOnly);
    CHECK(std::get<BarrierTransition>(barrier.params).state_after  == kBufferStateFlagRtv);
  }
  CHECK(barrier_list_map.at(2).size() == 3);
  {
    auto& barrier = barrier_list_map.at(2)[0];
    CHECK(barrier.buffer_id == 2);
    CHECK(barrier.split_type == BarrierSplitType::kBegin);
    CHECK(std::get<BarrierTransition>(barrier.params).state_before == kBufferStateFlagPresent);
    CHECK(std::get<BarrierTransition>(barrier.params).state_after  == kBufferStateFlagRtv);
  }
  {
    auto& barrier = barrier_list_map.at(2)[1];
    CHECK(barrier.buffer_id == 2);
    CHECK(barrier.split_type == BarrierSplitType::kEnd);
    CHECK(std::get<BarrierTransition>(barrier.params).state_before == kBufferStateFlagPresent);
    CHECK(std::get<BarrierTransition>(barrier.params).state_after  == kBufferStateFlagRtv);
  }
  {
    auto& barrier = barrier_list_map.at(2)[2];
    CHECK(barrier.buffer_id == 2);
    CHECK(barrier.split_type == BarrierSplitType::kNone);
    CHECK(std::get<BarrierTransition>(barrier.params).state_before == kBufferStateFlagRtv);
    CHECK(std::get<BarrierTransition>(barrier.params).state_after  == kBufferStateFlagPresent);
  }
  auto barriers = ConfigureBarriersBetweenRenderPass(std::move(barrier_user_pass_index_map), std::move(barrier_list_map), 2, &memory_resource_work, &memory_resource_work);
  CHECK(barriers.size() == 3);
  CHECK(barriers[0].size() == 1);
  CHECK(barriers[0][0].buffer_id == 2);
  CHECK(barriers[0][0].split_type == BarrierSplitType::kBegin);
  CHECK(std::holds_alternative<BarrierTransition>(barriers[0][0].params));
  CHECK(std::get<BarrierTransition>(barriers[0][0].params).state_before == kBufferStateFlagPresent);
  CHECK(std::get<BarrierTransition>(barriers[0][0].params).state_after  == kBufferStateFlagRtv);
  CHECK(barriers[1].size() == 2);
  CHECK(barriers[1][0].buffer_id == 1);
  CHECK(barriers[1][0].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers[1][0].params));
  CHECK(std::get<BarrierTransition>(barriers[1][0].params).state_before == kBufferStateFlagRtv);
  CHECK(std::get<BarrierTransition>(barriers[1][0].params).state_after  == kBufferStateFlagSrvPsOnly);
  CHECK(barriers[1][1].buffer_id == 2);
  CHECK(barriers[1][1].split_type == BarrierSplitType::kEnd);
  CHECK(std::holds_alternative<BarrierTransition>(barriers[1][1].params));
  CHECK(std::get<BarrierTransition>(barriers[1][1].params).state_before == kBufferStateFlagPresent);
  CHECK(std::get<BarrierTransition>(barriers[1][1].params).state_after  == kBufferStateFlagRtv);
  CHECK(barriers[2].size() == 2);
  CHECK(barriers[2][0].buffer_id == 1);
  CHECK(barriers[2][0].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers[2][0].params));
  CHECK(std::get<BarrierTransition>(barriers[2][0].params).state_before == kBufferStateFlagSrvPsOnly);
  CHECK(std::get<BarrierTransition>(barriers[2][0].params).state_after  == kBufferStateFlagRtv);
  CHECK(barriers[2][1].buffer_id == 2);
  CHECK(barriers[2][1].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers[2][1].params));
  CHECK(std::get<BarrierTransition>(barriers[2][1].params).state_before == kBufferStateFlagRtv);
  CHECK(std::get<BarrierTransition>(barriers[2][1].params).state_after  == kBufferStateFlagPresent);
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif

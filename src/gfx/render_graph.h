#ifndef ILLUMINATE_RENDER_GRAPH_H
#define ILLUMINATE_RENDER_GRAPH_H
#include <map>
#include <set>
#include <vector>
#include "gfx_def.h"
#include "illuminate/illuminate.h"
namespace illuminate::gfx {
template <typename T>
using unordered_set = std::pmr::unordered_set<T>;
template <typename T>
using set = std::pmr::set<T>;
template <typename K, typename V>
using unordered_map = std::pmr::unordered_map<K,V>;
template <typename K, typename V>
using map = std::pmr::map<K,V>;
template <typename T>
using vector = std::pmr::vector<T>;
using PassId = StrId;
using BufferId = uint32_t;
enum BufferStateFlags : uint32_t {
  kBufferStateFlagNone      = 0x0000,
  kBufferStateFlagCbvUpload = 0x0001,
  kBufferStateFlagSrvPsOnly = 0x0002,
  kBufferStateFlagSrvNonPs  = 0x0004,
  kBufferStateFlagUav       = 0x0008,
  kBufferStateFlagRtv       = 0x0010,
  kBufferStateFlagDsv       = 0x0020,
  kBufferStateFlagCopySrc   = 0x0040,
  kBufferStateFlagCopyDst   = 0x0080,
  kBufferStateFlagPresent   = 0x0100,
  kBufferStateFlagCommon    = kBufferStateFlagPresent,
  kBufferStateReadFlag      = 0x10000000,
  kBufferStateWriteFlag     = 0x20000000,
  kBufferStateFlagUavRead   = (kBufferStateFlagUav | kBufferStateReadFlag),
  kBufferStateFlagUavWrite  = (kBufferStateFlagUav | kBufferStateWriteFlag),
  kBufferStateFlagUavRW     = (kBufferStateFlagUavRead | kBufferStateFlagUavWrite),
  kBufferStateFlagDsvRead   = (kBufferStateFlagDsv | kBufferStateReadFlag),
  kBufferStateFlagDsvWrite  = (kBufferStateFlagDsv | kBufferStateReadFlag),
  kBufferStateFlagDsvRW     = (kBufferStateFlagDsvRead | kBufferStateFlagDsvWrite),
};
enum ReadWriteFlag : uint8_t {
  kReadFlag      = 0x1,
  kWriteFlag     = 0x2,
  kReadWriteFlag = (kReadFlag | kWriteFlag),
};
enum class BufferClearFlag : uint8_t { kNone = 0, kClear, };
struct RenderGraphBufferStateConfig {
  StrId buffer_name;
  BufferStateFlags state;
  ReadWriteFlag read_write_flag;
  BufferClearFlag buffer_clear_flag;
  std::byte _pad[2]{};
};
struct RenderPassConfig {
  StrId pass_name;
  CommandQueueType command_queue_type;
  std::byte _pad[7]{};
};
using RenderPassBufferStateList = vector<vector<RenderGraphBufferStateConfig>>;
enum class DepthStencilFlag : uint8_t { kDefault = 0, kDepthStencilReadOnly, kDepthReadOnly, kStencilReadOnly, };
struct BufferSizeInfo {
  BufferSizeType type;
  std::byte _pad[3]{};
  float width;
  float height;
};
struct BufferConfig {
  uint32_t width;
  uint32_t height;
  BufferStateFlags state_flags;
  BufferStateFlags initial_state_flags;
  ClearValue clear_value;
  BufferDimensionType dimension;
  BufferFormat format;
  DepthStencilFlag depth_stencil_flag;
  std::byte _pad{};
};
class RenderGraphConfig {
 public:
  RenderGraphConfig(std::pmr::memory_resource* memory_resource)
      : memory_resource_(memory_resource)
      , render_pass_id_map_(memory_resource_)
      , render_pass_command_queue_type_list_(memory_resource_)
      , render_pass_buffer_state_list_(memory_resource_)
      , initial_buffer_state_list_(memory_resource_)
      , final_buffer_state_list_(memory_resource_)
      , primary_buffer_width_(1920)
      , primary_buffer_height_(1080)
      , swapchain_buffer_width_(1920)
      , swapchain_buffer_height_(1080)
      , buffer_format_list_(memory_resource_)
      , buffer_default_clear_value_list_(memory_resource_)
      , buffer_depth_stencil_flag_list_(memory_resource_)
      , buffer_size_info_list_(memory_resource_)
      , buffer_dimension_type_list_(memory_resource_)
      , external_buffer_name_(memory_resource_)
      , mandatory_buffer_name_list_(memory_resource_)
      , pass_num_(0)
  {
    SetBufferSizeInfo(StrId("swapchain"), BufferSizeType::kSwapchainRelative, 1.0f, 1.0f);
  }
  void SetPrimaryBufferSize(const uint32_t width, const uint32_t height) {
    primary_buffer_width_ = width;
    primary_buffer_height_ = height;
  }
  void SetSwapchainBufferSize(const uint32_t width, const uint32_t height) {
    swapchain_buffer_width_ = width;
    swapchain_buffer_height_ = height;
  }
  void SetBufferSizeInfoFunction(std::function<std::tuple<uint32_t, uint32_t>(const BufferConfig&)>&& buffer_size_info_function) { buffer_size_info_function_ = std::move(buffer_size_info_function); }
  uint32_t CreateNewRenderPass(RenderPassConfig&& render_pass_config) {
    if (render_pass_id_map_.contains(render_pass_config.pass_name)) {
      logerror("render pass name dup. {}", render_pass_config.pass_name);
      return ~0u;
    }
    const auto pass_index = pass_num_;
    render_pass_id_map_.insert_or_assign(render_pass_config.pass_name, pass_index);
    ASSERT(pass_index == render_pass_command_queue_type_list_.size());
    render_pass_command_queue_type_list_.push_back(render_pass_config.command_queue_type);
    ASSERT(render_pass_buffer_state_list_.size() == pass_index);
    render_pass_buffer_state_list_.push_back(vector<RenderGraphBufferStateConfig>{memory_resource_});
    pass_num_++;
    return pass_index;
  }
  void AppendRenderPassBufferConfig(const uint32_t pass_index, RenderGraphBufferStateConfig&& config) {
    render_pass_buffer_state_list_[pass_index].push_back(std::move(config));
  }
  void AddBufferInitialState(const StrId& buffer_name, const BufferStateFlags flag) { initial_buffer_state_list_.insert_or_assign(buffer_name, flag); }
  void AddBufferFinalState(const StrId& buffer_name, const BufferStateFlags flag) { final_buffer_state_list_.insert_or_assign(buffer_name, flag); }
  void AddExternalBufferName(const StrId& buffer_name) { external_buffer_name_.insert(buffer_name); }
  void AddMandatoryBufferName(const StrId& buffer_name) { mandatory_buffer_name_list_.insert(buffer_name); }
  void SetBufferFormat(const StrId& buffer_name, const BufferFormat format) { buffer_format_list_.insert_or_assign(buffer_name, format); }
  void SetBufferSizeInfo(const StrId& buffer_name, const BufferSizeType type, const float width, const float height) { buffer_size_info_list_.insert_or_assign(buffer_name, BufferSizeInfo{.type = type, .width = width, .height = height}); }
  void SetBufferDefaultClearValue(const StrId& buffer_name, const ClearValue& clear_value) { buffer_default_clear_value_list_.insert_or_assign(buffer_name, clear_value); }
  void SetBufferDepthStencilFlag(const StrId& buffer_name, const DepthStencilFlag flag) { buffer_depth_stencil_flag_list_.insert_or_assign(buffer_name, flag); }
  void SetBufferDimensionType(const StrId& buffer_name, const BufferDimensionType type) { buffer_dimension_type_list_.insert_or_assign(buffer_name, type); }
  constexpr auto GetRenderPassNum() const { return pass_num_; }
  uint32_t GetRenderPassIndex(const StrId& pass_id) const { return render_pass_id_map_.at(pass_id); }
  constexpr const auto& GetRenderPassCommandQueueTypeList() const { return render_pass_command_queue_type_list_; }
  constexpr const auto& GetRenderPassBufferStateList() const { return render_pass_buffer_state_list_; }
  constexpr const auto& GetBufferInitialStateList() const { return initial_buffer_state_list_; }
  constexpr const auto& GetBufferFinalStateList() const { return final_buffer_state_list_; }
  constexpr auto GetPrimaryBufferWidth() const { return primary_buffer_width_; }
  constexpr auto GetPrimaryBufferHeight() const { return primary_buffer_height_; }
  constexpr auto GetSwapchainBufferWidth() const { return swapchain_buffer_width_; }
  constexpr auto GetSwapchainBufferHeight() const { return swapchain_buffer_height_; }
  constexpr const auto& GetBufferFormatList() const { return buffer_format_list_; }
  constexpr const auto& GetBufferDefaultClearValueList() const { return buffer_default_clear_value_list_; }
  constexpr const auto& GetBufferDepthStencilFlagList() const { return buffer_depth_stencil_flag_list_; }
  constexpr const auto& GetBufferSizeInfoList() const { return buffer_size_info_list_; }
  constexpr const auto& GetBufferDimensionTypeList() const { return buffer_dimension_type_list_; }
  constexpr const auto& GetExternalBufferNameList() const { return external_buffer_name_; }
  constexpr const auto& GetBufferSizeInfoFunction() const { return buffer_size_info_function_; }
  constexpr const auto& GetMandatoryBufferNameList() const { return mandatory_buffer_name_list_; }
 private:
  std::pmr::memory_resource* memory_resource_;
  unordered_map<StrId, uint32_t> render_pass_id_map_;
  vector<CommandQueueType> render_pass_command_queue_type_list_;
  RenderPassBufferStateList render_pass_buffer_state_list_;
  unordered_map<StrId, BufferStateFlags> initial_buffer_state_list_;
  unordered_map<StrId, BufferStateFlags> final_buffer_state_list_;
  uint32_t primary_buffer_width_, primary_buffer_height_;
  uint32_t swapchain_buffer_width_, swapchain_buffer_height_;
  unordered_map<StrId, BufferFormat> buffer_format_list_;
  unordered_map<StrId, ClearValue> buffer_default_clear_value_list_;
  unordered_map<StrId, DepthStencilFlag> buffer_depth_stencil_flag_list_;
  unordered_map<StrId, BufferSizeInfo> buffer_size_info_list_;
  unordered_map<StrId, BufferDimensionType> buffer_dimension_type_list_;
  unordered_set<StrId> external_buffer_name_;
  unordered_set<StrId> mandatory_buffer_name_list_;
  std::function<std::tuple<uint32_t, uint32_t>(const BufferConfig&)> buffer_size_info_function_;
  uint32_t pass_num_;
  [[maybe_unused]] std::byte _pad[4];
  RenderGraphConfig() = delete;
  RenderGraphConfig(const RenderGraphConfig&) = delete;
  void operator=(const RenderGraphConfig&) = delete;
};
enum class BarrierPosType : uint8_t { kPrePass, kPostPass, };
struct BufferStateChangeInfo {
  uint32_t barrier_begin_pass_index;
  uint32_t barrier_end_pass_index;
  BufferStateFlags state_before;
  BufferStateFlags state_after;
  BarrierPosType barrier_begin_pass_pos_type;
  BarrierPosType barrier_end_pass_pos_type;
  std::byte _pad[2]{};
};
struct BarrierTransition {
  BufferStateFlags state_before;
  BufferStateFlags state_after;
};
using BarrierUav = std::monostate;
struct BarrierAliasing {
  BufferId buffer_id_after_aliasing;
};
enum class BarrierSplitType : uint8_t { kNone = 0, kBegin, kEnd, };
struct BarrierConfig {
  BufferId buffer_id;
  BarrierSplitType split_type;
  std::byte _pad[3]{};
  std::variant<BarrierTransition, BarrierUav, BarrierAliasing> params;
};
enum class BufferClearType : uint8_t { kDontCare, kClear, };
class RenderGraph {
 public:
  RenderGraph(std::pmr::memory_resource* memory_resource) : memory_resource_(memory_resource) {}
  void Build(const RenderGraphConfig& config, std::pmr::memory_resource* memory_resource_work);
  constexpr uint32_t GetRenderPassNum() const { return render_pass_num_; }
  constexpr const auto& GetBufferIdList() const { return buffer_id_list_; }
  constexpr const auto& GetRenderPassBufferIdList() const { return render_pass_buffer_id_list_; }
  constexpr const auto& GetRenderPassBufferStateFlagList() const { return render_pass_buffer_state_flag_list_; }
  constexpr const auto& GetRenderPassCommandQueueTypeList() const { return render_pass_command_queue_type_list_; }
  constexpr const auto& GetBufferConfigList() const { return buffer_config_list_; }
  constexpr const auto& GetBarriersPrePass() const { return barriers_pre_pass_; }
  constexpr const auto& GetBarriersPostPass() const { return barriers_post_pass_; }
  constexpr const auto& GetQueueSignals() const { return queue_signals_; }
  constexpr const auto& GetBufferSizeList() const { return buffer_size_list_; }
  constexpr const auto& GetBufferAlignmentSizeList() const { return buffer_alignment_list_; }
  constexpr const auto& GetBufferAddressOffsetList() const { return buffer_address_offset_list_; }
  constexpr const auto& GetRenderPassBufferClearInfo() const { return render_pass_buffer_clear_info_; }
 private:
  std::pmr::memory_resource* memory_resource_;
  vector<BufferId> buffer_id_list_;
  vector<vector<BufferId>> render_pass_buffer_id_list_;
  vector<vector<BufferStateFlags>> render_pass_buffer_state_flag_list_;
  vector<CommandQueueType> render_pass_command_queue_type_list_;
  unordered_map<BufferId, BufferConfig> buffer_config_list_;
  vector<vector<BarrierConfig>> barriers_pre_pass_;
  vector<vector<BarrierConfig>> barriers_post_pass_;
  unordered_map<uint32_t, unordered_set<uint32_t>> queue_signals_;
  unordered_map<BufferId, uint32_t> buffer_size_list_;
  unordered_map<BufferId, uint32_t> buffer_alignment_list_;
  unordered_map<BufferId, uint32_t> buffer_address_offset_list_;
  vector<unordered_map<BufferId, BufferClearType>> render_pass_buffer_clear_info_;
  uint32_t render_pass_num_;
  [[maybe_unused]] std::byte _pad[4]{};
  RenderGraph() = delete;
  RenderGraph(const RenderGraph&) = delete;
  void operator=(const RenderGraph&) = delete;
};
}
#endif

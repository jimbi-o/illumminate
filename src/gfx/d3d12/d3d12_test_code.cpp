#include "d3d12_command_allocator.h"
#include "d3d12_command_list.h"
#include "d3d12_command_queue.h"
#include "d3d12_descriptor_heap.h"
#include "d3d12_device.h"
#include "d3d12_dxgi_core.h"
#include "d3d12_minimal_for_cpp.h"
#include "d3d12_shader_compiler.h"
#include "d3d12_shader_visible_descriptor_heap.h"
#include "d3d12_swapchain.h"
#include "d3dx12.h"
#include "illuminate/gfx/win32/win32_window.h"
#include "render_graph.h"
namespace illuminate::gfx::d3d12 {
class DeviceSet {
 public:
  bool Init(const uint32_t frame_buffer_num, const illuminate::gfx::BufferSize2d& swapchain_size, const uint32_t swapchain_buffer_num) {
    if (!dxgi_core.Init()) return false;
    if (!device.Init(dxgi_core.GetAdapter())) return false;
    if (!command_queue.Init(device.Get())) return false;
    if (!window.Init("swapchain test", swapchain_size.width, swapchain_size.height)) return false;
    if (!swapchain.Init(dxgi_core.GetFactory(), command_queue.Get(CommandQueueType::kGraphics), device.Get(), window.GetHwnd(), swapchain_buffer_num, frame_buffer_num)) return false;
    return true;
  }
  void Term() {
    swapchain.Term();
    window.Term();
    command_queue.Term();
    device.Term();
    dxgi_core.Term();
  }
  constexpr auto GetDevice() { return device.Get(); }
  auto GetCommandQueue(const CommandQueueType type) { return command_queue.Get(type); }
  void WaitAll() { command_queue.WaitAll(); }
  DxgiCore dxgi_core;
  Device device;
  CommandQueue command_queue;
  illuminate::gfx::win32::Window window;
  Swapchain swapchain;
};
class CommandListSet {
 public:
  CommandListSet(std::pmr::memory_resource* memory_resource)
      : allocator_buffer_(memory_resource)
      , command_list_in_use_(memory_resource)
      , command_list_num_in_use_(memory_resource)
  {
  }
  bool Init(D3d12Device* device, const uint32_t allocator_buffer_len) {
    if (!allocator_.Init(device)) return false;
    if (!pool_.Init(device)) return false;
    allocator_buffer_index_ = 0;
    allocator_buffer_len_ = allocator_buffer_len;
    allocator_buffer_.resize(allocator_buffer_len_);
    return true;
  }
  void Term() {
    for (uint32_t i = 0; i < allocator_buffer_len_; i++) {
      ReturnCommandAllocatorInBuffer(i);
    }
    pool_.Term();
    allocator_.Term();
  }
  void RotateCommandAllocators() {
    allocator_buffer_index_ = (allocator_buffer_index_ + 1) % allocator_buffer_len_;
    ReturnCommandAllocatorInBuffer(allocator_buffer_index_);
    ASSERT(command_list_in_use_.empty());
    ASSERT(command_list_num_in_use_.empty());
  }
  D3d12CommandList** GetCommandList(const CommandQueueType command_queue_type, const uint32_t num) {
    if (command_list_in_use_.contains(command_queue_type)) {
      ASSERT(command_list_num_in_use_.at(command_queue_type) == num);
      return command_list_in_use_.at(command_queue_type);
    }
    auto allocators = allocator_.RetainCommandAllocator(command_queue_type, num);
    auto command_list = pool_.RetainCommandList(command_queue_type, num, allocators);
    allocator_buffer_[allocator_buffer_index_].push_back(allocators);
    command_list_in_use_.insert_or_assign(command_queue_type, command_list);
    command_list_num_in_use_.insert_or_assign(command_queue_type, num);
    return command_list;
  }
  void ExecuteCommandLists(ID3D12CommandQueue* command_queue, const CommandQueueType command_queue_type) {
    auto& list = command_list_in_use_.at(command_queue_type);
    auto& list_len = command_list_num_in_use_.at(command_queue_type);
    for (uint32_t i = 0; i < list_len; i++) {
      list[i]->Close();
    }
    command_queue->ExecuteCommandLists(list_len, reinterpret_cast<ID3D12CommandList**>(list));
    pool_.ReturnCommandList(list);
    command_list_in_use_.erase(command_queue_type);
    command_list_num_in_use_.erase(command_queue_type);
  }
 private:
  void ReturnCommandAllocatorInBuffer(const uint32_t buffer_index) {
    for (auto& a : allocator_buffer_[buffer_index]) {
      allocator_.ReturnCommandAllocator(a);
    }
    allocator_buffer_[buffer_index].clear();
  }
  CommandAllocator allocator_;
  CommandList pool_;
  uint32_t allocator_buffer_index_ = 0;
  uint32_t allocator_buffer_len_ = 0;
  vector<vector<ID3D12CommandAllocator**>> allocator_buffer_;
  unordered_map<CommandQueueType, D3d12CommandList**> command_list_in_use_;
  unordered_map<CommandQueueType, uint32_t> command_list_num_in_use_;
};
class ShaderResourceSet {
 public:
  using DepthStencilEnableFlag = illuminate::core::EnableDisable;
  ShaderResourceSet(std::pmr::memory_resource* memory_resource)
      : rootsig_list_(memory_resource)
      , pso_list_(memory_resource)
  {}
  bool Init(D3d12Device* device) {
    if (!shader_compiler_.Init(device)) return false;
    return true;
  }
  void Term() {
    for (auto& [key, rootsig] : rootsig_list_) {
      rootsig->Release();
    }
    for (auto& pso : pso_list_) {
      pso->Release();
    }
    shader_compiler_.Term();
  }
  std::tuple<ID3D12RootSignature*, ID3D12PipelineState*> CreateVsPsPipelineStateObject(D3d12Device* const device, LPCWSTR vs, LPCWSTR ps, const StrId& rootsig_id, D3D12_RT_FORMAT_ARRAY&& output_dxgi_format, DepthStencilEnableFlag enable_depth_stencil, std::pmr::memory_resource* memory_resource_work) {
    auto shader_result_ps = shader_compiler_.Compile(ps, ShaderType::kPs, memory_resource_work);
    if (!rootsig_list_.contains(rootsig_id)) {
      auto root_signature_blob = ShaderCompiler::GetResultOutput<IDxcBlob>(shader_result_ps, DXC_OUT_ROOT_SIGNATURE);
      ID3D12RootSignature* root_signature = nullptr;
      auto hr = device->CreateRootSignature(0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature));
      root_signature_blob->Release();
      if (FAILED(hr)) {
        shader_result_ps->Release();
        return {};
      }
      root_signature->SetName(L"rootsig-vsps");
      rootsig_list_.insert({rootsig_id, root_signature});
    }
    auto root_signature = rootsig_list_.at(rootsig_id);
    auto shader_result_vs = shader_compiler_.Compile(vs, ShaderType::kVs, memory_resource_work);
    auto shader_object_vs = ShaderCompiler::GetResultOutput<IDxcBlob>(shader_result_vs, DXC_OUT_OBJECT);
    auto shader_object_ps = ShaderCompiler::GetResultOutput<IDxcBlob>(shader_result_ps, DXC_OUT_OBJECT);
    struct {
      CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE root_signature;
      CD3DX12_PIPELINE_STATE_STREAM_VS vs;
      CD3DX12_PIPELINE_STATE_STREAM_PS ps;
      CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS render_target_formats;
      CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY topology;
      CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL depth_stencil;
    } desc_local {
      root_signature,
      D3D12_SHADER_BYTECODE{shader_object_vs->GetBufferPointer(), shader_object_vs->GetBufferSize()},
      D3D12_SHADER_BYTECODE{shader_object_ps->GetBufferPointer(), shader_object_ps->GetBufferSize()},
      std::move(output_dxgi_format),
      D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    };
    if (enable_depth_stencil == DepthStencilEnableFlag::kDisabled) {
      ((CD3DX12_DEPTH_STENCIL_DESC&)(desc_local.depth_stencil)).DepthEnable = false;
    }
    D3D12_PIPELINE_STATE_STREAM_DESC desc{sizeof(desc_local), &desc_local};
    ID3D12PipelineState* pipeline_state = nullptr;
    auto hr = device->CreatePipelineState(&desc, IID_PPV_ARGS(&pipeline_state));
    shader_object_ps->Release();
    shader_object_vs->Release();
    shader_result_ps->Release();
    shader_result_vs->Release();
    if (FAILED(hr)) return {};
    pipeline_state->SetName(L"pso-vsps");
    pso_list_.push_back(pipeline_state);
    return {root_signature, pipeline_state};
  }
  std::tuple<ID3D12RootSignature*, ID3D12PipelineState*> CreateCsPipelineStateObject(D3d12Device* const device, LPCWSTR cs, const StrId& rootsig_id, std::pmr::memory_resource* memory_resource_work) {
    auto shader_result_cs = shader_compiler_.Compile(cs, ShaderType::kCs, memory_resource_work);
    if (!rootsig_list_.contains(rootsig_id)) {
      auto root_signature_blob = ShaderCompiler::GetResultOutput<IDxcBlob>(shader_result_cs, DXC_OUT_ROOT_SIGNATURE);
      ID3D12RootSignature* root_signature = nullptr;
      auto hr = device->CreateRootSignature(0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature));
      root_signature_blob->Release();
      if (FAILED(hr)) {
        shader_result_cs->Release();
        return {};
      }
      root_signature->SetName(L"rootsig-cs");
      rootsig_list_.insert({rootsig_id, root_signature});
    }
    auto root_signature = rootsig_list_.at(rootsig_id);
    auto shader_object_cs = ShaderCompiler::GetResultOutput<IDxcBlob>(shader_result_cs, DXC_OUT_OBJECT);
    struct {
      CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE root_signature;
      CD3DX12_PIPELINE_STATE_STREAM_CS cs;
    } desc_local {
      root_signature,
      D3D12_SHADER_BYTECODE{shader_object_cs->GetBufferPointer(), shader_object_cs->GetBufferSize()},
    };
    D3D12_PIPELINE_STATE_STREAM_DESC desc{sizeof(desc_local), &desc_local};
    ID3D12PipelineState* pipeline_state = nullptr;
    auto hr = device->CreatePipelineState(&desc, IID_PPV_ARGS(&pipeline_state));
    shader_object_cs->Release();
    shader_result_cs->Release();
    if (FAILED(hr)) return {};
    pipeline_state->SetName(L"pso-cs");
    pso_list_.push_back(pipeline_state);
    return {root_signature, pipeline_state};
  }
 private:
  ShaderCompiler shader_compiler_;
  unordered_map<StrId, ID3D12RootSignature*> rootsig_list_;
  vector<ID3D12PipelineState*> pso_list_;
};
struct SignalValues {
  unordered_map<CommandQueueType, uint64_t> used_signal_val;
  vector<unordered_map<CommandQueueType, uint64_t>> frame_wait_signal;
  SignalValues(std::pmr::memory_resource* memory_resource, const uint32_t frame_buffer_num)
      : used_signal_val(memory_resource)
      , frame_wait_signal(frame_buffer_num, memory_resource)
  {}
};
void SignalQueueOnFrameEnd(CommandQueue* const command_queue, CommandQueueType command_queue_type, unordered_map<CommandQueueType, uint64_t>* const used_signal_val, unordered_map<CommandQueueType, uint64_t>* const frame_wait_signal) {
  auto& signal_val = ++(*used_signal_val)[command_queue_type];
  command_queue->RegisterSignal(command_queue_type, signal_val);
  (*frame_wait_signal)[command_queue_type] = signal_val;
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
const uint32_t kTestFrameNum = 10;
}
#endif
#include "doctest/doctest.h"
TEST_CASE("create pso") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  const BufferSize2d swapchain_size{1600, 900};
  const uint32_t frame_buffer_num = 2;
  const uint32_t swapchain_buffer_num = frame_buffer_num + 1;
  DeviceSet devices;
  CHECK(devices.Init(frame_buffer_num, swapchain_size, swapchain_buffer_num));
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_size_in_bytes_persistent], buffer_size_in_bytes_persistent);
  ShaderResourceSet shader_resource_set(&memory_resource_persistent);
  CHECK(shader_resource_set.Init(devices.GetDevice()));
  PmrLinearAllocator memory_resource_work(&buffer[buffer_size_in_bytes_work], buffer_size_in_bytes_work);
  D3D12_RT_FORMAT_ARRAY array{{devices.swapchain.GetDxgiFormat()}, 1};
  auto [rootsig, pso] = shader_resource_set.CreateVsPsPipelineStateObject(devices.GetDevice(), L"shader/test/fullscreen-triangle.vs.hlsl", L"shader/test/copysrv.ps.hlsl", StrId("rootsig_tmp"), {{devices.swapchain.GetDxgiFormat()}, 1}, ShaderResourceSet::DepthStencilEnableFlag::kDisabled, &memory_resource_work);
  CHECK(rootsig);
  CHECK(pso);
  memory_resource_work.Reset();
  std::tie(rootsig, pso) = shader_resource_set.CreateCsPipelineStateObject(devices.GetDevice(), L"shader/test/fill-screen.cs.hlsl", StrId("rootsig_cs_tmp"), &memory_resource_work);
  CHECK(rootsig);
  CHECK(pso);
  shader_resource_set.Term();
  devices.Term();
}
TEST_CASE("clear swapchain buffer") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  const BufferSize2d swapchain_size{1600, 900};
  const uint32_t frame_buffer_num = 2;
  const uint32_t swapchain_buffer_num = frame_buffer_num + 1;
  DeviceSet devices;
  CHECK(devices.Init(frame_buffer_num, swapchain_size, swapchain_buffer_num));
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_size_in_bytes_persistent], buffer_size_in_bytes_persistent);
  CommandListSet command_list_set(&memory_resource_persistent);
  CHECK(command_list_set.Init(devices.GetDevice(), frame_buffer_num));
  SignalValues signal_values(&memory_resource_persistent, frame_buffer_num);
  CHECK(signal_values.used_signal_val.empty());
  CHECK(signal_values.frame_wait_signal.size() == frame_buffer_num);
  for (auto& map : signal_values.frame_wait_signal) {
    CHECK(map.empty());
  }
  for (uint32_t frame_no = 0; frame_no < kTestFrameNum; frame_no++) {
    auto frame_index = frame_no % frame_buffer_num;
    devices.command_queue.WaitOnCpu(signal_values.frame_wait_signal[frame_index]);
    command_list_set.RotateCommandAllocators();
    devices.swapchain.UpdateBackBufferIndex();
    {
      auto command_list = command_list_set.GetCommandList(CommandQueueType::kGraphics, 1)[0];
      D3D12_RESOURCE_BARRIER barrier{};
      {
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.pResource   = devices.swapchain.GetResource();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
      }
      command_list->ResourceBarrier(1, &barrier);
      {
        const FLOAT clear_color[4] = {0.0f,1.0f,1.0f,1.0f};
        command_list->ClearRenderTargetView(devices.swapchain.GetRtvHandle(), clear_color, 0, nullptr);
      }
      {
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
      }
      command_list->ResourceBarrier(1, &barrier);
      command_list_set.ExecuteCommandLists(devices.GetCommandQueue(CommandQueueType::kGraphics), CommandQueueType::kGraphics);
    }
    devices.swapchain.Present();
    SignalQueueOnFrameEnd(&devices.command_queue, CommandQueueType::kGraphics, &signal_values.used_signal_val, &signal_values.frame_wait_signal[frame_index]);
  }
  devices.WaitAll();
  command_list_set.Term();
  devices.Term();
}
TEST_CASE("draw to swapchain buffer") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  const BufferSize2d swapchain_size{1600, 900};
  const uint32_t frame_buffer_num = 2;
  const uint32_t swapchain_buffer_num = frame_buffer_num + 1;
  DeviceSet devices;
  CHECK(devices.Init(frame_buffer_num, swapchain_size, swapchain_buffer_num));
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_size_in_bytes_persistent], buffer_size_in_bytes_persistent);
  CommandListSet command_list_set(&memory_resource_persistent);
  CHECK(command_list_set.Init(devices.GetDevice(), frame_buffer_num));
  ShaderResourceSet shader_resource_set(&memory_resource_persistent);
  shader_resource_set.Init(devices.GetDevice());
  SignalValues signal_values(&memory_resource_persistent, frame_buffer_num);
  CHECK(signal_values.used_signal_val.empty());
  CHECK(signal_values.frame_wait_signal.size() == frame_buffer_num);
  for (auto& map : signal_values.frame_wait_signal) {
    CHECK(map.empty());
  }
  PmrLinearAllocator memory_resource_work(&buffer[buffer_size_in_bytes_work], buffer_size_in_bytes_work);
  auto [rootsig, pso] = shader_resource_set.CreateVsPsPipelineStateObject(devices.GetDevice(), L"shader/test/fullscreen-triangle.vs.hlsl", L"shader/test/test.ps.hlsl", StrId("rootsig_tmp"), {{devices.swapchain.GetDxgiFormat()}, 1}, ShaderResourceSet::DepthStencilEnableFlag::kDisabled, &memory_resource_work);
  for (uint32_t frame_no = 0; frame_no < kTestFrameNum; frame_no++) {
    auto frame_index = frame_no % frame_buffer_num;
    devices.command_queue.WaitOnCpu(signal_values.frame_wait_signal[frame_index]);
    command_list_set.RotateCommandAllocators();
    devices.swapchain.UpdateBackBufferIndex();
    {
      auto command_list = command_list_set.GetCommandList(CommandQueueType::kGraphics, 1)[0];
      D3D12_RESOURCE_BARRIER barrier{};
      {
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.pResource   = devices.swapchain.GetResource();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
      }
      command_list->ResourceBarrier(1, &barrier);
      {
        auto& width = swapchain_size.width;
        auto& height = swapchain_size.height;
        command_list->SetGraphicsRootSignature(rootsig);
        D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH};
        command_list->RSSetViewports(1, &viewport);
        D3D12_RECT scissor_rect{0L, 0L, static_cast<LONG>(width), static_cast<LONG>(height)};
        command_list->RSSetScissorRects(1, &scissor_rect);
        command_list->SetPipelineState(pso);
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        auto cpu_handle = devices.swapchain.GetRtvHandle();
        command_list->OMSetRenderTargets(1, &cpu_handle, true, nullptr);
        command_list->DrawInstanced(3, 1, 0, 0);
      }
      {
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
      }
      command_list->ResourceBarrier(1, &barrier);
      command_list_set.ExecuteCommandLists(devices.GetCommandQueue(CommandQueueType::kGraphics), CommandQueueType::kGraphics);
    }
    devices.swapchain.Present();
    SignalQueueOnFrameEnd(&devices.command_queue, CommandQueueType::kGraphics, &signal_values.used_signal_val, &signal_values.frame_wait_signal[frame_index]);
  }
  devices.WaitAll();
  shader_resource_set.Term();
  command_list_set.Term();
  devices.Term();
}

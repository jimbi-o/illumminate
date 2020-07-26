namespace illuminate::gfx::d3d12 {
void PrepareRenderpassFunctions() {
  using renderpass_function = std::function<void()>;
  std::unordered_map<StrId, renderpass_function> funcs;
  funcs.emplace(SID("g1"), [](const BufferInfo& buffer_info, DeviceProxy* const device_proxy, CommandListD3d12** const command_lists) {
      auto command_list =  command_lists[0];
      auto& handles  = buffer_info.handles;
      command_list->RSSetViewports(1, &GetDefaultViewport(viewport.width, viewport.height));
      command_list->RSSetScissorRects(1, &GetDefaultScissorRect(viewport.width, viewport.height));
      auto& viewport = buffer_info.viewport;
      command_list->OMSetRenderTargets(handles.rtv.size(), handles.rtv.data(), false, &handles.dsv);
      command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      command_list->SetGraphicsRootSignature(device_proxy->GetRootSignature(SID("g1")));
      command_list->SetPipelineState(device_proxy->GetPipelineState(SID("g1")));
      command_list->SetGraphicsRoot32BitConstants(0, 16, val, 0);
      command_list->SetGraphicsRootDescriptorTable(1, handles.cbv_uav_srv);
      command_list->DrawIndexedInstanced(3, 1, 0, 0, 0);
    });
  funcs.emplace(SID("g2"), [](const BufferInfo& buffer_info, DeviceProxy* const device_proxy, CommandListD3d12** const command_lists) {
      auto command_list =  command_lists[0];
      command_list->RSSetViewports(1, &GetDefaultViewport(viewport.width, viewport.height));
      command_list->RSSetScissorRects(1, &GetDefaultScissorRect(viewport.width, viewport.height));
      command_list->OMSetRenderTargets(handles.rtv.size(), handles.rtv.data(), false, &handles.dsv);
      command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      command_list->SetGraphicsRootSignature(device_proxy->GetRootSignature(SID("g2")));
      command_list->SetPipelineState(device_proxy->GetPipelineState(SID("g2")));
      command_list->SetGraphicsRoot32BitConstants(0, 16, val, 0);
      command_list->SetGraphicsRootDescriptorTable(1, handles.cbv_uav_srv);
      command_list->DrawIndexedInstanced(3, 1, 0, 0, 0);
    });
  funcs.emplace(SID("c1"), [](const BufferInfo& buffer_info, DeviceProxy* const device_proxy, CommandListD3d12** const command_lists) {
      auto command_list =  command_lists[0];
      command_list->SetComputeRootSignature(device_proxy->GetRootSignature(SID("c1")));
      command_list->SetPipelineState(device_proxy->GetPipelineState(SID("c1")));
      command_list->SetComputeRoot32BitConstants(0, 16, val, 0);
      command_list->SetComputeRootDescriptorTable(0, handles.cbv_uav_srv);
      command_list->Dispatch(1,1,1);
    });
}
}

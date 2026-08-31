#pragma once
#include "imgui.h"
struct ID3D11Device; struct ID3D11DeviceContext; struct ImDrawData;
bool ImGui_ImplDX11_Init( ID3D11Device *, ID3D11DeviceContext * );
void ImGui_ImplDX11_Shutdown();
void ImGui_ImplDX11_NewFrame();
void ImGui_ImplDX11_RenderDrawData( ImDrawData * );
void ImGui_ImplDX11_InvalidateDeviceObjects();
bool ImGui_ImplDX11_CreateDeviceObjects();

#pragma once
#include "imgui.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;

namespace solace::snapshot
{

void request();

bool ready();

ImTextureID texture();
void poll(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swap_chain);

void release();
void shutdown();

void attach(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swap_chain);

void capture_backdrop(ImDrawList* draw_list);

void invalidate_backdrop();
bool backdrop_ready();
ImTextureID backdrop();
} // namespace solace::snapshot

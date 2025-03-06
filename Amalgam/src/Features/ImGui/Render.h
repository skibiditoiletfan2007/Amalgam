#pragma once
#include "../../SDK/SDK.h"
#include <ImGui/imgui_impl_dx9.h>
#include <ImGui/imgui.h>

class CRender
{
public:
	void Render(IDirect3DDevice9* pDevice);
	void Initialize(IDirect3DDevice9* pDevice);

	void LoadColors();
	void LoadFonts();
	void LoadStyle();

	int Cursor = 2;

	// Colors
	ImColor Accent = {};
	ImColor AccentLight = {};
	ImColor Background = {};
	ImColor Foreground = {};
	ImColor Foremost = {};
	ImColor ForemostLight = {};
	ImColor Inactive = {};
	ImColor Active = {};

	// Fonts
	ImFont* FontSmall = nullptr;
	ImFont* FontRegular = nullptr;
	ImFont* FontBold = nullptr;
	ImFont* FontLarge = nullptr;
	ImFont* FontMono = nullptr;

	ImFont* IconFont = nullptr;
};

ADD_FEATURE(CRender, Render);
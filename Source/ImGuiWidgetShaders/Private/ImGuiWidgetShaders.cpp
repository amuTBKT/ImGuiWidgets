// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

#include "ImGuiTextureDisplayShaders.h"

IMPLEMENT_GLOBAL_SHADER(FTextureDisplayPS, "/Plugin/ImGuiWidgets/TextureDisplay.usf", "Main", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FTextureDisplay_CopyPixelValueCS, "/Plugin/ImGuiWidgets/TextureDisplay.usf", "CopyPixelValueCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTextureDisplay_TileMinMaxCS, "/Plugin/ImGuiWidgets/TextureDisplayMinMaxValues.usf", "TileMinMaxCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTextureDisplay_ResultMinMaxCS, "/Plugin/ImGuiWidgets/TextureDisplayMinMaxValues.usf", "ResultMinMaxCS", SF_Compute);

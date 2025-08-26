// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

#include "ImGuiTextureDisplayShaders.h"

IMPLEMENT_GLOBAL_SHADER(FTextureVisualizerPS, "/Plugin/ImGuiWidgets/TextureDisplay.usf", "Main", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FHistogramTileMinMaxCS, "/Plugin/ImGuiWidgets/Histogram.usf", "TileMinMaxCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FHistogramResultMinMaxCS, "/Plugin/ImGuiWidgets/Histogram.usf", "ResultMinMaxCS", SF_Compute);

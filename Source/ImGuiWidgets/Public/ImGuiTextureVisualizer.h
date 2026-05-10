// Copyright 2026 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTexture;
class FRHITexture;
class FTextureResource;

namespace ImGuiTextureVisualizer
{
	// display the specified texture, the pointer is stored as WeakObjectPtr.
	IMGUIWIDGETS_API void SetTextureOverride_GameThread(const UTexture* Texture);
	
	// display the specified texture resource, the pointer lifetime is not tracked.
	IMGUIWIDGETS_API void SetTextureOverride_GameThread(const FString& DisplayName, const FTextureResource* TextureResource);

	// display the specified texture rhi resource, stored as ref counted pointer
	IMGUIWIDGETS_API void SetTextureOverride_RenderThread(const FString& DisplayName, FRHITexture* Texture);

	// reset override
	IMGUIWIDGETS_API void ClearTextureOverride_GameThread();
}

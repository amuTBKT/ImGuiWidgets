// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTexture;
class FTextureResource;

namespace ImGuiTextureVisualizer
{
	// display the specified texture, the pointer is stored as WeakObjectPtr.
	IMGUIWIDGETS_API void SetTextureOverride(const UTexture* Texture);
	
	// display the specified texture, the pointer lifetime is not tracked.
	IMGUIWIDGETS_API void SetTextureOverride(const FString& DisplayName, const FTextureResource* TextureResource);

	// reset override
	IMGUIWIDGETS_API void ClearTextureOverride();
}

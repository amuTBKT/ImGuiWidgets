// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "SceneViewExtension.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "Runtime/Launch/Resources/Version.h"

// copied from Voxel core (https://github.com/VoxelPlugin/VoxelCore/blob/master/Source/VoxelCore/Public/VoxelMinimal/VoxelMacros.h)
#define DEFINE_PRIVATE_ACCESS(Class, Property) \
namespace PrivateAccess \
{ \
	template<typename> \
	struct TClass_ ## Property; \
	\
	template<> \
	struct TClass_ ## Property<Class> \
	{ \
		template<auto PropertyPtr> \
		struct TProperty_ ## Property \
		{ \
			friend auto& Property(Class& Object) \
			{ \
				return Object.*PropertyPtr; \
			} \
			friend auto& Property(const Class& Object) \
			{ \
				return Object.*PropertyPtr; \
			} \
		}; \
	}; \
	template struct TClass_ ## Property<Class>::TProperty_ ## Property<&Class::Property>; \
	\
	auto& Property(Class& Object); \
	auto& Property(const Class& Object); \
}

namespace ImGuiTextureVisualizer
{
	DEFINE_PRIVATE_ACCESS(FRDGBuilder, Textures);

	// used to extract textures from GraphBuilder
	class FTextureCollectorSceneViewExtension final : public FSceneViewExtensionBase
	{
	public:
		FTextureCollectorSceneViewExtension(const FAutoRegister& AutoRegister)
			: FSceneViewExtensionBase(AutoRegister)
		{
		}

		virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
		virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
		virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

		virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override {}
		virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override
		{
			if (InViewFamily.EngineShowFlags.HitProxies)
			{
				return;
			}

			TArray<FRDGTextureRef> Results;

			auto& Textures = PrivateAccess::Textures(GraphBuilder);
			Textures.Enumerate([&](FRDGTexture* Texture)
				{
					AvailableTextures.Add(Texture->Name);

					if (!DisplayedTextureName.IsEmpty() && FStringView(Texture->Name) == DisplayedTextureName)
					{
						Results.AddUnique(Texture);
					}
				});

			if (!Results.IsEmpty() && (Results[0]->HasBeenProduced() || Results[0]->IsExternal()))
			{
				GraphBuilder.QueueTextureExtraction(Results[0], &TextureToDisplay);
			}
			else
			{
				TextureToDisplay.SafeRelease();
			}
		}

		void SetTextureNameToDisplay(FString TextureName)
		{
			TextureToDisplay.SafeRelease();
			DisplayedTextureName = MoveTemp(TextureName);
		}

		FString DisplayedTextureName;
		TSet<FString> AvailableTextures;
		TRefCountPtr<IPooledRenderTarget> TextureToDisplay;
	};

	// used to copy pixel data for readback
	class FPixelValueDestBuffer : public FVertexBuffer
	{
	public:
		virtual void InitRHI(FRHICommandListBase& RHICmdList) override
		{
			const int32 SizeInBytes = sizeof(FUintVector4) * 4; // only 2 entries are used (Min/Max value)

#if ((ENGINE_MAJOR_VERSION * 100u + ENGINE_MINOR_VERSION) > 505) //(Version > 5.5)
			FRHIBufferCreateDesc BufferDesc =
				FRHIBufferCreateDesc::CreateVertex(TEXT("TexDisplay_PixelValueDestBuffer"), SizeInBytes)
				.AddUsage(EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess)
				.SetInitialState(ERHIAccess::UAVCompute)
				.SetInitActionNone();
			VertexBufferRHI = RHICmdList.CreateBuffer(BufferDesc);
#else
			FRHIResourceCreateInfo CreateInfo(TEXT("TexDisplay_PixelValueDestBuffer"));
			VertexBufferRHI = RHICmdList.CreateVertexBuffer(SizeInBytes, BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess, CreateInfo);
#endif
		}
	};

	namespace PixelFormatUtils
	{
		FORCEINLINE bool IsSignedIntegerFormat(EPixelFormat PixelFormat)
		{
			switch (PixelFormat)
			{
			case PF_R32_SINT:
			case PF_R16_SINT:
			case PF_R16G16B16A16_SINT:
			case PF_R32G32B32_SINT:
			case PF_R8_SINT:
			case PF_R16G16_SINT:
				return true;
			}
			return false;
		}

		FORCEINLINE EPixelFormatChannelFlags GetValidChannelsForFormat(EPixelFormat PixelFormat)
		{
			EPixelFormatChannelFlags ValidTextureChannels = GetPixelFormatValidChannels(PixelFormat);
			
			// NOTE: fix for `GetPixelFormatValidChannels` not handling certain formats correctly
			if (PixelFormat == PF_R32_SINT)
			{
				ValidTextureChannels = EPixelFormatChannelFlags::R;
			}
			else if (PixelFormat == PF_ShadowDepth)
			{
				ValidTextureChannels = EPixelFormatChannelFlags::R;
			}

			return ValidTextureChannels;
		}

		FORCEINLINE void DetermineShaderTypesForTexture(
			const FPooledRenderTargetDesc& TextureDesc, bool bReadAsStencil,
			ETexDisplay_ResourceType& OutResType, ETexDisplay_ShaderBaseType& OutBaseType)
		{
			OutResType = ETexDisplay_ResourceType::Tex2D;
			OutBaseType = ETexDisplay_ShaderBaseType::Float;
			
			if (IsInteger(TextureDesc.Format))
			{
				OutBaseType = PixelFormatUtils::IsSignedIntegerFormat(TextureDesc.Format) ? ETexDisplay_ShaderBaseType::SInt : ETexDisplay_ShaderBaseType::UInt;
			}
			else
			{
				OutBaseType = ETexDisplay_ShaderBaseType::Float;
			}

			if (IsStencilFormat(TextureDesc.Format))
			{
				if (bReadAsStencil)
				{
					OutResType = TextureDesc.NumSamples > 1 ? ETexDisplay_ResourceType::StencilMS : ETexDisplay_ResourceType::Stencil;
					OutBaseType = ETexDisplay_ShaderBaseType::UInt;
				}
				else
				{
					OutResType = TextureDesc.NumSamples > 1 ? ETexDisplay_ResourceType::DepthMS : ETexDisplay_ResourceType::Depth;
					OutBaseType = ETexDisplay_ShaderBaseType::Float;
				}
			}
			else if (TextureDesc.Is2DTexture() || TextureDesc.IsCubemap())
			{
				if (TextureDesc.IsCubemap())
				{
					check(TextureDesc.NumSamples == 1);
				}
				OutResType = TextureDesc.NumSamples > 1 ? ETexDisplay_ResourceType::Tex2DMS : ETexDisplay_ResourceType::Tex2D;
			}
			else if (TextureDesc.Is3DTexture())
			{
				OutResType = ETexDisplay_ResourceType::Tex3D;
			}
		}

		FAnsiString GetPixelValueAsString(const uint8* RawValue, EPixelFormat Format, bool bReadAsStencil)
		{
			FAnsiString ValueAsString;

			const EPixelFormatChannelFlags ValidTextureChannels = GetValidChannelsForFormat(Format);
			if (IsStencilFormat(Format))
			{
				if (bReadAsStencil)
				{
					FIntVector4 Value; FMemory::Memcpy(&Value, RawValue, sizeof(FIntVector4));
					ValueAsString += FAnsiString::Printf("Stencil: %i (0x%x)\n", Value.X, Value.X);
				}
				else
				{
					FVector4f Value; FMemory::Memcpy(&Value, RawValue, sizeof(FVector4f));
					ValueAsString += FAnsiString::Printf("Depth: %f\n", Value.X);
				}
			}
			else if (IsSignedIntegerFormat(Format))
			{
				FIntVector4 Value; FMemory::Memcpy(&Value, RawValue, sizeof(FIntVector4));
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
				{
					ValueAsString += FAnsiString::Printf("R: %i (0x%x)\n", Value.X, Value.X);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
				{
					ValueAsString += FAnsiString::Printf("G: %i (0x%x)\n", Value.Y, Value.Y);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
				{
					ValueAsString += FAnsiString::Printf("B: %i (0x%x)\n", Value.Z, Value.Z);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
				{
					ValueAsString += FAnsiString::Printf("A: %i (0x%x)\n", Value.W, Value.W);
				}
			}
			else if (IsInteger(Format))
			{
				FUintVector4 Value; FMemory::Memcpy(&Value, RawValue, sizeof(FUintVector4));
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
				{
					ValueAsString += FAnsiString::Printf("R: %i (0x%x)\n", Value.X, Value.X);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
				{
					ValueAsString += FAnsiString::Printf("G: %i (0x%x)\n", Value.Y, Value.Y);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
				{
					ValueAsString += FAnsiString::Printf("B: %i (0x%x)\n", Value.Z, Value.Z);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
				{
					ValueAsString += FAnsiString::Printf("A: %i (0x%x)\n", Value.W, Value.W);
				}
			}
			else
			{
				FVector4f Value; FMemory::Memcpy(&Value, RawValue, sizeof(FVector4f));
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
				{
					ValueAsString += FAnsiString::Printf("R: %.5f\n", Value.X);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
				{
					ValueAsString += FAnsiString::Printf("G: %.5f\n", Value.Y);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
				{
					ValueAsString += FAnsiString::Printf("B: %.5f\n", Value.Z);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
				{
					ValueAsString += FAnsiString::Printf("A: %.5f\n", Value.W);
				}
			}

			return ValueAsString;
		}

		FAnsiString GetPixelValueAsStringInline(const uint8* RawValue, EPixelFormat Format, bool bReadAsStencil)
		{
			FAnsiString ValueAsString;

			const EPixelFormatChannelFlags ValidTextureChannels = GetValidChannelsForFormat(Format);
			if (IsStencilFormat(Format))
			{
				if (bReadAsStencil)
				{
					FIntVector4 Value; FMemory::Memcpy(&Value, RawValue, sizeof(FIntVector4));
					ValueAsString += FAnsiString::Printf("(Stencil)%i", Value.X);
				}
				else
				{
					FVector4f Value; FMemory::Memcpy(&Value, RawValue, sizeof(FVector4f));
					ValueAsString += FAnsiString::Printf("(Depth)%f", Value.X);
				}
			}
			else if (IsSignedIntegerFormat(Format))
			{
				FIntVector4 Value; FMemory::Memcpy(&Value, RawValue, sizeof(FIntVector4));
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
				{
					ValueAsString += FAnsiString::Printf("(R)%i ", Value.X);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
				{
					ValueAsString += FAnsiString::Printf("(G)%i ", Value.Y);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
				{
					ValueAsString += FAnsiString::Printf("(B)%i ", Value.Z);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
				{
					ValueAsString += FAnsiString::Printf("(A)%i ", Value.W);
				}
			}
			else if (IsInteger(Format))
			{
				FUintVector4 Value; FMemory::Memcpy(&Value, RawValue, sizeof(FUintVector4));
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
				{
					ValueAsString += FAnsiString::Printf("(R)%u ", Value.X);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
				{
					ValueAsString += FAnsiString::Printf("(G)%u ", Value.Y);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
				{
					ValueAsString += FAnsiString::Printf("(B)%u ", Value.Z);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
				{
					ValueAsString += FAnsiString::Printf("(A)%u ", Value.W);
				}
			}
			else
			{
				FVector4f Value; FMemory::Memcpy(&Value, RawValue, sizeof(FVector4f));
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
				{
					ValueAsString += FAnsiString::Printf("(R)%.5f ", Value.X);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
				{
					ValueAsString += FAnsiString::Printf("(G)%.5f ", Value.Y);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
				{
					ValueAsString += FAnsiString::Printf("(B)%.5f ", Value.Z);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
				{
					ValueAsString += FAnsiString::Printf("(A)%.5f ", Value.W);
				}
			}

			return ValueAsString;
		}

		template <typename TColorFormat>
		void DrawPixelColorComponentWidget(const TColorFormat& ColorValue, int32 ComponentIndex, const char* LabelFmt, const char* ClipboardFmt, ImU32 MarkerColor)
		{
			char Buffer[128];
			sprintf_s(Buffer, sizeof(Buffer), LabelFmt, ColorValue[ComponentIndex]);
			if (ImGui::Button(Buffer))
			{
				sprintf_s(Buffer, sizeof(Buffer), ClipboardFmt, ColorValue[ComponentIndex]);
				ImGui::SetClipboardText(Buffer);
			}
			ImGui::SetItemTooltip("Copy to clipboard");
			ImGui::RenderColorComponentMarker({ ImGui::GetItemRectMin(), ImGui::GetItemRectMax() }, MarkerColor, ImGui::GetStyle().FrameRounding);
		}

		void DrawPixelValueWidget(const uint8* RawValue, EPixelFormat Format, bool bReadAsStencil)
		{
			const EPixelFormatChannelFlags ValidTextureChannels = GetValidChannelsForFormat(Format);

			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_FrameBg));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_FrameBgHovered));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetColorU32(ImGuiCol_FrameBgActive));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.f, ImGui::GetStyle().FramePadding.y));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f, ImGui::GetStyle().ItemSpacing.y));

			ImGui::BeginGroup();
			ImGui::GetCurrentWindow()->DC.LayoutType = ImGuiLayoutType_Horizontal;
			if (IsStencilFormat(Format))
			{
				if (bReadAsStencil)
				{
					FIntVector4 Value; FMemory::Memcpy(&Value, RawValue, sizeof(FIntVector4));
					DrawPixelColorComponentWidget(Value, 0, "%i##STENCIL", "%i", 0xFFFFFFFF);
				}
				else
				{
					FVector4f Value; FMemory::Memcpy(&Value, RawValue, sizeof(FVector4f));
					DrawPixelColorComponentWidget(Value, 0, "%.7f##DEPTH", "%.7f", 0xFFFFFFFF);
				}
			}
			else if (IsSignedIntegerFormat(Format))
			{
				FIntVector4 Value; FMemory::Memcpy(&Value, RawValue, sizeof(FIntVector4));
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
				{
					DrawPixelColorComponentWidget(Value, 0, "%i##RED", "%i", 0xFF0000FF);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
				{
					DrawPixelColorComponentWidget(Value, 1, "%i##GREEN", "%i", 0xFF00FF00);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
				{
					DrawPixelColorComponentWidget(Value, 2, "%i##BLUE", "%i", 0xFFFF0000);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
				{
					DrawPixelColorComponentWidget(Value, 3, "%i##ALPHA", "%i", 0xFFFFFFFF);
				}
			}
			else if (IsInteger(Format))
			{
				FUintVector4 Value; FMemory::Memcpy(&Value, RawValue, sizeof(FUintVector4));
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
				{
					DrawPixelColorComponentWidget(Value, 0, "%u##RED", "%u", 0xFF0000FF);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
				{
					DrawPixelColorComponentWidget(Value, 1, "%u##GREEN", "%u", 0xFF00FF00);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
				{
					DrawPixelColorComponentWidget(Value, 2, "%u##BLUE", "%u", 0xFFFF0000);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
				{
					DrawPixelColorComponentWidget(Value, 3, "%u##ALPHA", "%u", 0xFFFFFFFF);
				}
			}
			else
			{
				FVector4f Value; FMemory::Memcpy(&Value, RawValue, sizeof(FVector4f));
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
				{
					DrawPixelColorComponentWidget(Value, 0, "%.5f##RED", "%.5f", 0xFF0000FF);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
				{
					DrawPixelColorComponentWidget(Value, 1, "%.5f##GREEN", "%.5f", 0xFF00FF00);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
				{
					DrawPixelColorComponentWidget(Value, 2, "%.5f##BLUE", "%.5f", 0xFFFF0000);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
				{
					DrawPixelColorComponentWidget(Value, 3, "%.5f##ALPHA", "%.5f", 0xFFFFFFFF);
				}
			}
			ImGui::NewLine();
			ImGui::EndGroup();

			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(3);
		}
	}

	ImVec2 ConstrainCanvasToAspectRatio(int32 SizeX, int32 SizeY, ImVec2 CanvasSize)
	{
		const float AspectRatio = (float)SizeX / (float)SizeY;
		if (AspectRatio > 1.f)
		{
			CanvasSize.y = FMath::Min(CanvasSize.y, CanvasSize.x / AspectRatio);
			CanvasSize.x = CanvasSize.y * AspectRatio;
		}
		else
		{
			CanvasSize.x = FMath::Min(CanvasSize.x, CanvasSize.y * AspectRatio);
			CanvasSize.y = CanvasSize.x / AspectRatio;
		}
		return CanvasSize;
	}
}

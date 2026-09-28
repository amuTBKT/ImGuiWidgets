// Copyright 2026 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "SceneView.h"
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

namespace ImGuiTextureViewer
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

		const char* GetPixelValueAsText(const uint8* RawValue, EPixelFormat Format, bool bReadAsStencil)
		{
			static TAnsiStringBuilder<256> ValueAsString;
			ValueAsString.Reset();

			const EPixelFormatChannelFlags ValidTextureChannels = GetValidChannelsForFormat(Format);
			if (IsStencilFormat(Format))
			{
				if (bReadAsStencil)
				{
					FIntVector4 Value; FMemory::Memcpy(&Value, RawValue, sizeof(FIntVector4));
					ValueAsString.Appendf("Stencil: %i (0x%x)\n", Value.X, Value.X);
				}
				else
				{
					FVector4f Value; FMemory::Memcpy(&Value, RawValue, sizeof(FVector4f));
					ValueAsString.Appendf("Depth: %f\n", Value.X);
				}
			}
			else if (IsSignedIntegerFormat(Format))
			{
				FIntVector4 Value; FMemory::Memcpy(&Value, RawValue, sizeof(FIntVector4));
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
				{
					ValueAsString.Appendf("R: %i (0x%x)\n", Value.X, Value.X);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
				{
					ValueAsString.Appendf("G: %i (0x%x)\n", Value.Y, Value.Y);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
				{
					ValueAsString.Appendf("B: %i (0x%x)\n", Value.Z, Value.Z);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
				{
					ValueAsString.Appendf("A: %i (0x%x)\n", Value.W, Value.W);
				}
			}
			else if (IsInteger(Format))
			{
				FUintVector4 Value; FMemory::Memcpy(&Value, RawValue, sizeof(FUintVector4));
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
				{
					ValueAsString.Appendf("R: %i (0x%x)\n", Value.X, Value.X);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
				{
					ValueAsString.Appendf("G: %i (0x%x)\n", Value.Y, Value.Y);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
				{
					ValueAsString.Appendf("B: %i (0x%x)\n", Value.Z, Value.Z);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
				{
					ValueAsString.Appendf("A: %i (0x%x)\n", Value.W, Value.W);
				}
			}
			else
			{
				FVector4f Value; FMemory::Memcpy(&Value, RawValue, sizeof(FVector4f));
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
				{
					ValueAsString.Appendf("R: %.5f\n", Value.X);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
				{
					ValueAsString.Appendf("G: %.5f\n", Value.Y);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
				{
					ValueAsString.Appendf("B: %.5f\n", Value.Z);
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
				{
					ValueAsString.Appendf("A: %.5f\n", Value.W);
				}
			}

			return ValueAsString.GetData();
		}

		template <typename TColorFormat, size_t LabelFmtLen, size_t ClipboardFmtLen>
		void DrawPixelColorComponentWidget(const TColorFormat& ColorValue, int32 ComponentIndex, const char(&LabelFmt)[LabelFmtLen], const char(&ClipboardFmt)[ClipboardFmtLen], ImU32 MarkerColor)
		{
			char Buffer[128];
			FCStringAnsi::Snprintf(Buffer, sizeof(Buffer), LabelFmt, ColorValue[ComponentIndex]);
			if (ImGui::Button(Buffer))
			{
				FCStringAnsi::Snprintf(Buffer, sizeof(Buffer), ClipboardFmt, ColorValue[ComponentIndex]);
				ImGui::SetClipboardText(Buffer);
			}
			ImGui::SetItemTooltip("%s", "Copy to clipboard");
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
					DrawPixelColorComponentWidget(Value, 0, "%i##STENCIL", "%i", IM_COL32_WHITE);
				}
				else
				{
					FVector4f Value; FMemory::Memcpy(&Value, RawValue, sizeof(FVector4f));
					DrawPixelColorComponentWidget(Value, 0, "%.7f##DEPTH", "%.7f", IM_COL32_WHITE);
				}
			}
			else if (IsSignedIntegerFormat(Format))
			{
				FIntVector4 Value; FMemory::Memcpy(&Value, RawValue, sizeof(FIntVector4));
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
				{
					DrawPixelColorComponentWidget(Value, 0, "%i##RED", "%i", IM_COL32(255, 0, 0, 255));
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
				{
					DrawPixelColorComponentWidget(Value, 1, "%i##GREEN", "%i", IM_COL32(0, 255, 0, 255));
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
				{
					DrawPixelColorComponentWidget(Value, 2, "%i##BLUE", "%i", IM_COL32(0, 0, 255, 255));
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
				{
					DrawPixelColorComponentWidget(Value, 3, "%i##ALPHA", "%i", IM_COL32_WHITE);
				}
			}
			else if (IsInteger(Format))
			{
				FUintVector4 Value; FMemory::Memcpy(&Value, RawValue, sizeof(FUintVector4));
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
				{
					DrawPixelColorComponentWidget(Value, 0, "%u##RED", "%u", IM_COL32(255, 0, 0, 255));
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
				{
					DrawPixelColorComponentWidget(Value, 1, "%u##GREEN", "%u", IM_COL32(0, 255, 0, 255));
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
				{
					DrawPixelColorComponentWidget(Value, 2, "%u##BLUE", "%u", IM_COL32(0, 0, 255, 255));
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
				{
					DrawPixelColorComponentWidget(Value, 3, "%u##ALPHA", "%u", IM_COL32_WHITE);
				}
			}
			else
			{
				FVector4f Value; FMemory::Memcpy(&Value, RawValue, sizeof(FVector4f));
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
				{
					DrawPixelColorComponentWidget(Value, 0, "%.5f##RED", "%.5f", IM_COL32(255, 0, 0, 255));
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
				{
					DrawPixelColorComponentWidget(Value, 1, "%.5f##GREEN", "%.5f", IM_COL32(0, 255, 0, 255));
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
				{
					DrawPixelColorComponentWidget(Value, 2, "%.5f##BLUE", "%.5f", IM_COL32(0, 0, 255, 255));
				}
				if (EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
				{
					DrawPixelColorComponentWidget(Value, 3, "%.5f##ALPHA", "%.5f", IM_COL32_WHITE);
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

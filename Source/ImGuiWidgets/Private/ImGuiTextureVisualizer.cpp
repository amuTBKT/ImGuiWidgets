#include "ImGuiSubsystem.h"
#include "ImGuiStaticWidget.h"
#include "ImGuiCommonWidgets.h"
#include "ImGuiTextureDisplayShaders.h"
#include "ImGuiTextureVisualizerUtils.h"

#include "Engine/Engine.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "RHIStaticStates.h"
#include "ScreenRendering.h"
#include "PipelineStateCache.h"
#include "CommonRenderResources.h"
#include "RenderCaptureInterface.h"
#include "PostProcess/DrawRectangle.h"

UE_DISABLE_OPTIMIZATION

namespace ImGuiTextureVisualizer
{
	FVertexBuffer* GPixelValueDestBuffer = new TGlobalResource<FPixelValueDestBuffer, FRenderResource::EInitPhase::Default>;

	struct FTextureMetadata
	{
		int32		 SizeX		= 1;
		int32		 SizeY		= 1;
		int32		 SizeZ		= 0;
		int32		 ArraySize	= 1;
		EPixelFormat Format		= PF_Unknown;
		uint8		 NumMips	= 1;
		uint8		 bIsArray	: 1 = 0;
		uint8		 bIsCubemap : 1 = 0;

		uint8		 SelectedPixelValue[sizeof(FUintVector4)];

		void Reset()
		{
			SizeX = SizeY = ArraySize = NumMips = 1;
			SizeZ = 0;
			Format = PF_Unknown;
			bIsArray = false;
			bIsCubemap = false;

			FMemory::Memset(SelectedPixelValue, 0, sizeof(FUintVector4));
		}
		bool IsValid() const
		{
			return Format != PF_Unknown;
		}

		int32 GetSizeX(int32 MipIndex) const
		{
			return SizeX >> MipIndex;
		}
		int32 GetSizeY(int32 MipIndex) const
		{
			return SizeY >> MipIndex;
		}
	};
	static FTextureMetadata DoubleBufferedTextureMetadata[2];
	static uint8 TextureMetadataReadIndex_RT = 0;
	static uint8 TextureMetadataReadIndex_GT = 0;
	FTextureMetadata& GetTextureMetadata(bool bOnRenderThread)
	{
		return DoubleBufferedTextureMetadata[bOnRenderThread ? TextureMetadataReadIndex_RT : TextureMetadataReadIndex_GT];
	}

	struct FTexturePreviewOptions
	{
		enum class ERequestedZoomLevel
		{
			None,
			OneToOne,
			Fit
		};

		int32 CurrentArraySlice		= 0; // if volume texture or array
		int32 CurrentFace			= 0; // if cubemap
		double RangeMin				= 0.;
		double RangeValueMin		= 0.;
		double RangeMax				= 1.;
		double RangeValueMax		= 1.;
		uint8 CurrentMip			= 0;
		bool bDisplayRedChannel		= true;
		bool bDisplayGreenChannel	= true;
		bool bDisplayBlueChannel	= true;
		bool bDisplayAlphaChannel	= false;
		bool bDisplayDepth			= true;
		bool bDisplayStencil		= false;
		bool bSRGB					= false;

		float CanvasZoomPercentage	 = 100.;
		FVector2f CanvasScrollOffset = FVector2f(0.f, 0.f);
		FVector4f UVScaleAndOffset	 = FVector4f(1.f, 1.f, 0.f, 0.f);
		ERequestedZoomLevel RequestedZoomLevel = ERequestedZoomLevel::None;
		
		FLinearColor BackgroundColor = FLinearColor(0.f, 0.f, 0.f, 0.f);

		FIntPoint CursorPosition = FIntPoint::ZeroValue;

		FIntPoint TextureInspectorCursorPosition = FIntPoint::ZeroValue;
		FIntVector4 TextureInspectorRect = FIntVector4::ZeroValue;

		void Reset()
		{
			CurrentArraySlice = 0;
			CurrentFace = 0;
			CurrentMip = 0;
			bDisplayRedChannel = true;
			bDisplayGreenChannel = true;
			bDisplayBlueChannel = true;
			bDisplayAlphaChannel = false;
			bDisplayDepth = true;
			bDisplayStencil = false;
			bSRGB = false;

			// fit to new texture on reset
			RequestedZoomLevel = ERequestedZoomLevel::Fit;
		}
	};
	static FTexturePreviewOptions TexturePreviewOptions;
	
	struct FTexturePreviewUserData
	{
		ImVec2 ViewportSize;
		ImVec2 ClipRectMin;
		ImVec2 ClipRectMax;
		FTexturePreviewOptions Options;
	};

	static TArray<FAnsiString> AvailableTextures;
	static TSharedPtr<FTextureCollectorSceneViewExtension> ViewExtension = nullptr;

	static void Initialize()
	{
		ViewExtension = FSceneViewExtensions::NewExtension<FTextureCollectorSceneViewExtension>();
		FCoreDelegates::OnEnginePreExit.AddLambda(
			[]()
			{
				ViewExtension.Reset();
			});
	}

	static FVector2D GetMinMaxTextureValue(FRHICommandListImmediate& RHICommandList, const FTexturePreviewOptions& PreviewOptions)
	{
		if (!ViewExtension->TextureToDisplay)
		{
			return FVector2D(0.f, 1.f);
		}
		const FPooledRenderTargetDesc& TextureDesc = ViewExtension->TextureDesc;

		const uint64_t TextureExtentMax = TextureDesc.Extent.GetMax();
		const uint64_t BlockPixSize = FTextureDisplay_TileMinMaxCS::HGRAM_PIXELS_PER_TILE * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK;
		const uint64_t BlockCount = (TextureExtentMax * TextureExtentMax) / (BlockPixSize * BlockPixSize);

#if ((ENGINE_MAJOR_VERSION * 100u + ENGINE_MINOR_VERSION) > 505) //(Version > 5.5)
		FRHIBufferCreateDesc TileMinMaxBufferDesc =
			FRHIBufferCreateDesc::CreateIndex<FVector4f>(
				TEXT("Histogram_TileMinMax"),
				BlockCount * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK * sizeof(FVector4f) * 2)
				.AddUsage(EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess)
				.SetInitialState(ERHIAccess::UAVCompute)
				.SetInitActionNone();
		FBufferRHIRef TileMinMaxBuffer = RHICmdList.CreateBuffer(TileMinMaxBufferDesc);

		FRHIBufferCreateDesc ResultMinMaxBufferDesc =
			FRHIBufferCreateDesc::CreateIndex<FVector4f>(TEXT("Histogram_ResultMinMax"), sizeof(FVector4f) * 2)
				.AddUsage(EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess)
				.SetInitialState(ERHIAccess::UAVCompute)
				.SetInitActionNone();
		FBufferRHIRef ResultMinMaxBuffer = RHICmdList.CreateBuffer(ResultMinMaxBufferDesc);
#else
		FRHIResourceCreateInfo TileMinMaxBufferCreateInfo(TEXT("Histogram_TileMinMax"));
		FBufferRHIRef TileMinMaxBuffer = RHICommandList.CreateBuffer(
			BlockCount * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK * sizeof(FVector4f) * 2,
			EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess,
			sizeof(FVector4f), ERHIAccess::UAVCompute, TileMinMaxBufferCreateInfo);

		FRHIResourceCreateInfo ResultMinMaxBufferCreateInfo(TEXT("Histogram_ResultMinMax"));
		FBufferRHIRef ResultMinMaxBuffer = RHICommandList.CreateBuffer(
			sizeof(FVector4f) * 2,
			EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess,
			sizeof(FVector4f), ERHIAccess::UAVCompute, ResultMinMaxBufferCreateInfo);
#endif

		ETexDisplay_ResourceType ResType;
		ETexDisplay_ShaderBaseType BaseType;
		PixelFormatUtils::DetermineShaderTypesForTexture(TextureDesc, PreviewOptions.bDisplayStencil, ResType, BaseType);

		{
			RenderCaptureInterface::FScopedCapture RenderCapture(false, &RHICommandList, TEXT("HistogramMinMax"));

			auto* ShaderMap = GetGlobalShaderMap(GetMaxSupportedFeatureLevel(GMaxRHIShaderPlatform));

			// tile min max
			{
				FTextureDisplay_TileMinMaxCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FTextureDisplay_TileMinMaxCS::FResourceType>(ResType);
				PermutationVector.Set<FTextureDisplay_TileMinMaxCS::FShaderBaseType>(BaseType);
				TShaderMapRef<FTextureDisplay_TileMinMaxCS> ComputeShader(ShaderMap, PermutationVector);

				FRHITextureSRVCreateInfo SRVInfo(
					0, TextureDesc.NumMips,
					0, TextureDesc.ArraySize,
					(IsStencilFormat(TextureDesc.Format) && PreviewOptions.bDisplayStencil) ? PF_X24_G8 : TextureDesc.Format);

				EPixelFormat BufferFormat = BufferFormat = PF_A32B32G32R32F;
				switch (BaseType)
				{
				case ETexDisplay_ShaderBaseType::UInt:  BufferFormat = PF_R32G32B32A32_UINT; break;
				case ETexDisplay_ShaderBaseType::SInt:  BufferFormat = PF_R32G32B32A32_UINT; break; // NOTE: PF_R32G32B32A32_SINT is not available
				case ETexDisplay_ShaderBaseType::Float: BufferFormat = PF_A32B32G32R32F; break;
				default: checkNoEntry(); break;
				}

				SetComputePipelineState(RHICommandList, ComputeShader.GetComputeShader());
				SetShaderParametersLegacyCS(
					RHICommandList, ComputeShader,
					RHICommandList.CreateUnorderedAccessView(TileMinMaxBuffer.GetReference(), UE_PIXELFORMAT_TO_UINT8(BufferFormat)),
					RHICommandList.CreateShaderResourceView(ViewExtension->TextureToDisplay->GetRHI(), SRVInfo),
					FIntVector3(TextureDesc.Extent.X, TextureDesc.Extent.Y, TextureDesc.Depth),
					PreviewOptions.CurrentMip,
					TextureDesc.bIsCubemap ? PreviewOptions.CurrentFace : PreviewOptions.CurrentArraySlice);

				const int32 ThreadGroupCountX = FMath::DivideAndRoundUp(TextureDesc.Extent.X, FTextureDisplay_TileMinMaxCS::HGRAM_PIXELS_PER_TILE * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK);
				const int32 ThreadGroupCountY = FMath::DivideAndRoundUp(TextureDesc.Extent.Y, FTextureDisplay_TileMinMaxCS::HGRAM_PIXELS_PER_TILE * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK);

				RHICommandList.DispatchComputeShader(ThreadGroupCountX, ThreadGroupCountY, 1);
			}

			// result min max
			{
				FTextureDisplay_ResultMinMaxCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FTextureDisplay_ResultMinMaxCS::FShaderBaseType>(BaseType);
				TShaderMapRef<FTextureDisplay_ResultMinMaxCS> ComputeShader(ShaderMap, PermutationVector);

				EPixelFormat BufferFormat = PF_A32B32G32R32F;
				switch (BaseType)
				{
				case ETexDisplay_ShaderBaseType::UInt:  BufferFormat = PF_R32G32B32A32_UINT; break;
				case ETexDisplay_ShaderBaseType::SInt:  BufferFormat = PF_R32G32B32A32_UINT; break; // NOTE: PF_R32G32B32A32_SINT is not available
				case ETexDisplay_ShaderBaseType::Float: BufferFormat = PF_A32B32G32R32F; break;
				default: checkNoEntry(); break;
				}

				SetComputePipelineState(RHICommandList, ComputeShader.GetComputeShader());
				SetShaderParametersLegacyCS(
					RHICommandList, ComputeShader,
					RHICommandList.CreateUnorderedAccessView(ResultMinMaxBuffer.GetReference(), UE_PIXELFORMAT_TO_UINT8(BufferFormat)),
					RHICommandList.CreateShaderResourceView(TileMinMaxBuffer.GetReference(), sizeof(FVector4f), UE_PIXELFORMAT_TO_UINT8(BufferFormat)),
					FIntVector3(TextureDesc.Extent.X, TextureDesc.Extent.Y, TextureDesc.Depth));

				RHICommandList.DispatchComputeShader(1, 1, 1);
			}
		}

		// copy
		FVector4 MinValue = FVector4::Zero();
		FVector4 MaxValue = FVector4::One();
		{
			FRHIGPUBufferReadback Readback(TEXT("TexDisplay::MinMaxReadback"));
			uint32_t NumBytes = sizeof(FVector4f) * 2;
			Readback.EnqueueCopy(RHICommandList, ResultMinMaxBuffer.GetReference(), NumBytes);
			RHICommandList.BlockUntilGPUIdle();
			RHICommandList.FlushResources();

			if (const void* SrcRawBuffer = Readback.Lock(NumBytes))
			{
				if (BaseType == ETexDisplay_ShaderBaseType::UInt)
				{
					const FUintVector4 val0 = ((FUintVector4*)SrcRawBuffer)[0];
					const FUintVector4 val1 = ((FUintVector4*)SrcRawBuffer)[1];

					MinValue = FVector4(val0.X, val0.Y, val0.Z, val0.W);
					MaxValue = FVector4(val1.X, val1.Y, val1.Z, val1.W);
				}
				else if (BaseType == ETexDisplay_ShaderBaseType::SInt)
				{
					const FIntVector4 val0 = ((FIntVector4*)SrcRawBuffer)[0];
					const FIntVector4 val1 = ((FIntVector4*)SrcRawBuffer)[1];

					MinValue = FVector4(val0.X, val0.Y, val0.Z, val0.W);
					MaxValue = FVector4(val1.X, val1.Y, val1.Z, val1.W);
				}
				else if (BaseType == ETexDisplay_ShaderBaseType::Float)
				{
					const FVector4f val0 = ((FVector4f*)SrcRawBuffer)[0];
					const FVector4f val1 = ((FVector4f*)SrcRawBuffer)[1];

					MinValue = FVector4(val0.X, val0.Y, val0.Z, val0.W);
					MaxValue = FVector4(val1.X, val1.Y, val1.Z, val1.W);
				}

				Readback.Unlock();
			}
		}

		FVector2D Result = FVector2D(FLT_MAX, -FLT_MAX);
		if (IsStencilFormat(TextureDesc.Format))
		{
			Result.X = FMath::Min(Result.X, MinValue.X);
			Result.Y = FMath::Max(Result.Y, MaxValue.X);
		}
		else
		{
			const EPixelFormatChannelFlags ValidTextureChannels = PixelFormatUtils::GetValidChannelsForFormat(TextureDesc.Format);

			bool bFoundAnyValidChannels = false;
			if (PreviewOptions.bDisplayRedChannel && EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
			{
				bFoundAnyValidChannels = true;
				Result.X = FMath::Min(Result.X, MinValue.X);
				Result.Y = FMath::Max(Result.Y, MaxValue.X);
			}
			if (PreviewOptions.bDisplayGreenChannel && EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
			{
				bFoundAnyValidChannels = true;
				Result.X = FMath::Min(Result.X, MinValue.Y);
				Result.Y = FMath::Max(Result.Y, MaxValue.Y);
			}
			if (PreviewOptions.bDisplayBlueChannel && EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
			{
				bFoundAnyValidChannels = true;
				Result.X = FMath::Min(Result.X, MinValue.Z);
				Result.Y = FMath::Max(Result.Y, MaxValue.Z);
			}
			if (PreviewOptions.bDisplayAlphaChannel && EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
			{
				bFoundAnyValidChannels = true;
				Result.X = FMath::Min(Result.X, MinValue.W);
				Result.Y = FMath::Max(Result.Y, MaxValue.W);
			}

			if (!bFoundAnyValidChannels)
			{
				Result = FVector2D(0., 0.);
			}
		}

		TileMinMaxBuffer.SafeRelease();
		ResultMinMaxBuffer.SafeRelease();

		return Result;
	}

	static void TexturePreviewCallback(void* immediate_command_list, void* user_data, size_t user_data_size)
	{
		if (!ensure(user_data && (sizeof(FTexturePreviewUserData) == user_data_size)))
		{
			return;
		}

		if (!ViewExtension->TextureToDisplay)
		{
			return;
		}

		FRHICommandListImmediate& RHICommandList = *(FRHICommandListImmediate*)immediate_command_list;
		const FTexturePreviewUserData& PreviewParams = *(const FTexturePreviewUserData*)user_data;
		const FPooledRenderTargetDesc& TextureDesc = ViewExtension->TextureDesc;

		uint32 TextureVisShowFlags = 0u;
		if (IsStencilFormat(TextureDesc.Format))
		{
			TextureVisShowFlags |= (PreviewParams.Options.bDisplayDepth) ? uint32(ETexDisplay_ShowFlag::DepthChannelMask) : 0u;
			TextureVisShowFlags |= (PreviewParams.Options.bDisplayStencil) ? uint32(ETexDisplay_ShowFlag::StencilChannelMask) : 0u;
		}
		else
		{
			TextureVisShowFlags |= (PreviewParams.Options.bDisplayRedChannel) ? uint32(ETexDisplay_ShowFlag::RedChannelMask) : 0u;
			TextureVisShowFlags |= (PreviewParams.Options.bDisplayGreenChannel) ? uint32(ETexDisplay_ShowFlag::GreenChannelMask) : 0u;
			TextureVisShowFlags |= (PreviewParams.Options.bDisplayBlueChannel) ? uint32(ETexDisplay_ShowFlag::BlueChannelMask) : 0u;
			TextureVisShowFlags |= (PreviewParams.Options.bDisplayAlphaChannel) ? uint32(ETexDisplay_ShowFlag::AlphaChannelMask) : 0u;
			TextureVisShowFlags |= (PreviewParams.Options.bSRGB) ? uint32(ETexDisplay_ShowFlag::sRGBMask) : 0u;
		}

		auto* ShaderMap = GetGlobalShaderMap(GetMaxSupportedFeatureLevel(GMaxRHIShaderPlatform));

		TShaderMapRef<FScreenVS> VertexShader(ShaderMap);

		ETexDisplay_ResourceType ResType;
		ETexDisplay_ShaderBaseType BaseType;
		PixelFormatUtils::DetermineShaderTypesForTexture(TextureDesc, PreviewParams.Options.bDisplayStencil, ResType, BaseType);

		FTextureDisplayPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FTextureDisplayPS::FResourceType>(ResType);
		PermutationVector.Set<FTextureDisplayPS::FShaderBaseType>(BaseType);
		TShaderMapRef<FTextureDisplayPS> PixelShader(ShaderMap, PermutationVector);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICommandList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICommandList, GraphicsPSOInit, 0);

		FRHITextureSRVCreateInfo SRVInfo(
			0, TextureDesc.NumMips,
			0, TextureDesc.ArraySize,
			(IsStencilFormat(TextureDesc.Format) && PreviewParams.Options.bDisplayStencil) ? PF_X24_G8 : TextureDesc.Format);
		
		SetShaderParametersLegacyPS(
			RHICommandList, PixelShader,
			RHICommandList.CreateShaderResourceView(ViewExtension->TextureToDisplay->GetRHI(), SRVInfo),
			TStaticSamplerState<SF_Point>::GetRHI(),
			FIntVector3(TextureDesc.Extent.X, TextureDesc.Extent.Y, TextureDesc.Depth),
			TextureVisShowFlags,
			PreviewParams.Options.CurrentMip,
			TextureDesc.bIsCubemap ? PreviewParams.Options.CurrentFace : PreviewParams.Options.CurrentArraySlice,
			FVector2f(PreviewParams.Options.RangeValueMin, 1.f / (PreviewParams.Options.RangeValueMax - PreviewParams.Options.RangeValueMin)),
			PreviewParams.Options.UVScaleAndOffset,
			PreviewParams.Options.BackgroundColor.A > 0.5f ? PreviewParams.Options.BackgroundColor.ToFColor(/*sRGB=*/false).DWColor() : 0u,
			FIntPoint(TexturePreviewOptions.TextureInspectorCursorPosition.X, TexturePreviewOptions.TextureInspectorCursorPosition.Y),
			PreviewParams.Options.TextureInspectorRect);

		RHICommandList.SetViewport(0.f, 0.f, 0.f, PreviewParams.ViewportSize.x, PreviewParams.ViewportSize.y, 1.f);
		RHICommandList.SetScissorRect(true, PreviewParams.ClipRectMin.x, PreviewParams.ClipRectMin.y, PreviewParams.ClipRectMax.x, PreviewParams.ClipRectMax.y);

		UE::Renderer::PostProcess::DrawRectangle(
			RHICommandList,
			VertexShader,
			PreviewParams.ClipRectMin.x,
			PreviewParams.ClipRectMin.y,
			PreviewParams.ClipRectMax.x - PreviewParams.ClipRectMin.x,
			PreviewParams.ClipRectMax.y - PreviewParams.ClipRectMin.y,
			0, 0,
			TextureDesc.Extent.X, TextureDesc.Extent.Y,
			FIntPoint(PreviewParams.ViewportSize.x, PreviewParams.ViewportSize.y),
			FIntPoint(TextureDesc.Extent.X, TextureDesc.Extent.Y),
			EDRF_Default);

		FTextureMetadata& TextureInfo = GetTextureMetadata(/*bOnRenderThread=*/true);
		TextureInfo.SizeX = TextureDesc.Extent.X;
		TextureInfo.SizeY = TextureDesc.Extent.Y;
		TextureInfo.SizeZ = TextureDesc.Is3DTexture() ? TextureDesc.Depth : 0;
		TextureInfo.ArraySize = TextureDesc.IsArray() ? TextureDesc.ArraySize : 1;
		TextureInfo.Format = TextureDesc.Format;
		TextureInfo.NumMips = TextureDesc.NumMips;
		TextureInfo.bIsArray = TextureDesc.bIsArray;
		TextureInfo.bIsCubemap = TextureDesc.bIsCubemap;

		// copy
		if ((TexturePreviewOptions.TextureInspectorRect.Z - TexturePreviewOptions.TextureInspectorRect.X) > 0)
		{
			FTextureDisplay_CopyPixelValueCS::FPermutationDomain PermutationVector1;
			PermutationVector1.Set<FTextureDisplay_CopyPixelValueCS::FResourceType>(ResType);
			PermutationVector1.Set<FTextureDisplay_CopyPixelValueCS::FShaderBaseType>(BaseType);
			TShaderMapRef<FTextureDisplay_CopyPixelValueCS> ComputeShader(ShaderMap, PermutationVector1);

			EPixelFormat BufferFormat = PF_A32B32G32R32F;
			switch (BaseType)
			{
			case ETexDisplay_ShaderBaseType::UInt:  BufferFormat = PF_R32G32B32A32_UINT; break;
			case ETexDisplay_ShaderBaseType::SInt:  BufferFormat = PF_R32G32B32A32_UINT; break; // NOTE: PF_R32G32B32A32_SINT is not available
			case ETexDisplay_ShaderBaseType::Float: BufferFormat = PF_A32B32G32R32F; break;
			default: checkNoEntry(); break;
			}

			const int32 HoveredTexCoordX = PreviewParams.Options.TextureInspectorCursorPosition.X >> PreviewParams.Options.CurrentMip;
			const int32 HoveredTexCoordY = PreviewParams.Options.TextureInspectorCursorPosition.Y >> PreviewParams.Options.CurrentMip;

			SetComputePipelineState(RHICommandList, ComputeShader.GetComputeShader());
			SetShaderParametersLegacyCS(
				RHICommandList, ComputeShader,
				RHICommandList.CreateUnorderedAccessView(GPixelValueDestBuffer->VertexBufferRHI, UE_PIXELFORMAT_TO_UINT8(BufferFormat)),
				RHICommandList.CreateShaderResourceView(ViewExtension->TextureToDisplay->GetRHI(), SRVInfo),
				FIntVector3(TextureDesc.Extent.X, TextureDesc.Extent.Y, TextureDesc.Depth),
				PreviewParams.Options.CurrentMip,
				TextureDesc.bIsCubemap ? PreviewParams.Options.CurrentFace : PreviewParams.Options.CurrentArraySlice,
				FIntPoint(HoveredTexCoordX, HoveredTexCoordY));

			RHICommandList.DispatchComputeShader(1, 1, 1);

			uint8* Dst = TextureInfo.SelectedPixelValue;
			{
				FRHIGPUBufferReadback Readback(TEXT("TexDisplay::MinMaxReadback"));
				uint32_t NumBytes = sizeof(FUintVector4);
				Readback.EnqueueCopy(RHICommandList, GPixelValueDestBuffer->VertexBufferRHI, NumBytes);
				RHICommandList.BlockUntilGPUIdle();
				RHICommandList.FlushResources();

				if (const void* SrcRawBuffer = Readback.Lock(NumBytes))
				{
					if (BaseType == ETexDisplay_ShaderBaseType::UInt)
					{
						const FUintVector4 value = ((FUintVector4*)SrcRawBuffer)[0];
						FMemory::Memcpy(Dst, &value, sizeof(FUintVector4));
					}
					else if (BaseType == ETexDisplay_ShaderBaseType::SInt)
					{
						const FIntVector4 value = ((FIntVector4*)SrcRawBuffer)[0];
						FMemory::Memcpy(Dst, &value, sizeof(FIntVector4));
					}
					else if (BaseType == ETexDisplay_ShaderBaseType::Float)
					{
						const FVector4f value = ((FVector4f*)SrcRawBuffer)[0];
						FMemory::Memcpy(Dst, &value, sizeof(FVector4f));
					}

					Readback.Unlock();
				}
			}
		}
	}

	static bool DrawTextureList(ImGuiContext* Context, FAnsiString& InOutSelectedTextureName)
	{
		static FImGuiTextFilter SearchFilter = FImGuiTextFilter::MakeWidget(64u);
		static bool bSetFocusOnSelectedEntry = false; // flag checked on next frame, hence static
		
		const float GlobalScale = ImGui::GetStyle().FontScaleMain;
		const FAnsiString PreviouslySelectedTextureName = InOutSelectedTextureName;
		ImGui::SetNextItemWidth(400.f * GlobalScale);
		const bool bShowTextureList = ImGui::BeginCombo("##TextureList", InOutSelectedTextureName.IsEmpty() ? "Select a Texture..." : *InOutSelectedTextureName);
		const ImVec2 ComboBoxSize = ImGui::GetItemRectSize();
		if (bShowTextureList)
		{
			ImGui::BeginGroup();
			{
				SearchFilter.Draw(Context, "##Filter", "Search Textures", 400.f * GlobalScale, ImGui::IsWindowAppearing());
				
				ImGui::SameLine();
				if (ImGui::Button("Refresh") || AvailableTextures.IsEmpty())
				{
					AvailableTextures.Reset();

					FlushRenderingCommands();
					for (const FString& TextureName : ViewExtension->AvailableTextures)
					{
						AvailableTextures.AddUnique(TCHAR_TO_ANSI(*TextureName));
					}
				}
			}
			ImGui::EndGroup();

			for (const auto& TextureName : AvailableTextures)
			{
				if (!SearchFilter.PassFilter(TextureName))
				{
					continue;
				}

				if (ImGui::Selectable(*TextureName, TextureName.Equals(PreviouslySelectedTextureName)))
				{
					InOutSelectedTextureName = TextureName;

					ImGui::CloseCurrentPopup();
					break;
				}

				if (TextureName.Equals(PreviouslySelectedTextureName))
				{
					if (bSetFocusOnSelectedEntry)
					{
						bSetFocusOnSelectedEntry = false;

						ImGui::ScrollToItem();
						ImGui::SetItemDefaultFocus();
					}

					if (ImGui::IsWindowAppearing())
					{
						bSetFocusOnSelectedEntry = true;
					}
				}
			}

			ImGui::EndCombo();
		}
		
		// reset icon
		if (!InOutSelectedTextureName.IsEmpty())
		{
			ImGui::SameLine();
			
			const FImGuiImageBindingParams ResetToDefaultIcon = UImGuiSubsystem::Get()->RegisterOneFrameResource(IMGUI_ICON("Icon.ResetToDefault"), FVector2D(ComboBoxSize.y));

			ImGui::PushStyleColor(ImGuiCol_Button, 0xBFFFFFFF);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFFFFFFFF);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFFFFFFFF);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));

			if (FImGui::TransparentImageButton("ResetTexture", ResetToDefaultIcon))
			{
				InOutSelectedTextureName.Reset();
			}
			ImGui::SetItemTooltip("Reset");
			
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(3);
		}

		return !InOutSelectedTextureName.Equals(PreviouslySelectedTextureName);
	}

	static void DrawTextureControls(ImGuiContext* Context, const FTextureMetadata& InTextureInfo, FTexturePreviewOptions& InOutTexturePreviewOptions)
	{
		const float GlobalScale = ImGui::GetStyle().FontScaleMain;
		const float ControlPadding = 10.f * GlobalScale;

		ImGui::Dummy(ImVec2(0., 2.f));

		// zoom controls
		{
			ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = ImGui::GetStyle().FramePadding.y;
			ImGui::Text("Zoom");
			ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = 0.f;
			ImGui::SameLine();
			if (ImGui::Button("1:1"))
			{
				InOutTexturePreviewOptions.RequestedZoomLevel = FTexturePreviewOptions::ERequestedZoomLevel::OneToOne;
			}
			ImGui::SameLine();
			if (ImGui::Button("Fit"))
			{
				InOutTexturePreviewOptions.RequestedZoomLevel = FTexturePreviewOptions::ERequestedZoomLevel::Fit;
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(90.f * GlobalScale);
			if (ImGui::BeginCombo("##Zoom", *FAnsiString::Printf("%i%%", int32(InOutTexturePreviewOptions.CanvasZoomPercentage)), ImGuiComboFlags_None))
			{
				static const int32 AvailableZoomLevels[] = { 10, 25, 75, 50, 100, 200, 400, 800 };
				for (int32 Index = 0; Index < UE_ARRAY_COUNT(AvailableZoomLevels); Index++)
				{
					if (ImGui::Selectable(*FAnsiString::Printf("%i%%", AvailableZoomLevels[Index])))
					{
						InOutTexturePreviewOptions.CanvasZoomPercentage = AvailableZoomLevels[Index];
					}
				}

				ImGui::EndCombo();
			}
		}

		ImGui::SameLine();
		ImGui::Dummy(ImVec2(ControlPadding, 0));
		ImGui::SameLine();

		// texture channel controls
		if (IsStencilFormat(InTextureInfo.Format))
		{
			if (ImGui::RadioButton("Depth", InOutTexturePreviewOptions.bDisplayDepth))
			{
				InOutTexturePreviewOptions.bDisplayDepth = true;
				InOutTexturePreviewOptions.bDisplayStencil = false;
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Stencil", InOutTexturePreviewOptions.bDisplayStencil))
			{
				InOutTexturePreviewOptions.bDisplayStencil = true;
				InOutTexturePreviewOptions.bDisplayDepth = false;
			}
		}
		else
		{
			auto AddButton = [](const char* Label, bool& bInOutState)
			{
				const bool bWasActive = bInOutState;
				if (bWasActive)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(2.f / 7.0f, 0.6f, 0.6f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(2.f / 7.0f, 0.7f, 0.7f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(2.f / 7.0f, 0.8f, 0.8f));
				}
				
				if (ImGui::Button(Label))
				{
					bInOutState = !bInOutState;
				}

				if (bWasActive)
				{
					ImGui::PopStyleColor(3);
				}
				
				return bWasActive != bInOutState;
			};

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.f * GlobalScale, 0));
			AddButton("R", InOutTexturePreviewOptions.bDisplayRedChannel);
			ImGui::SameLine();
			AddButton("G", InOutTexturePreviewOptions.bDisplayGreenChannel);
			ImGui::SameLine();
			AddButton("B", InOutTexturePreviewOptions.bDisplayBlueChannel);
			ImGui::SameLine();
			AddButton("A", InOutTexturePreviewOptions.bDisplayAlphaChannel);
			ImGui::PopStyleVar(1);
		}

		ImGui::SameLine();
		ImGui::Dummy(ImVec2(ControlPadding, 0));
		ImGui::SameLine();

		// texture mip controls
		{
			ImGui::Text("Mip"); ImGui::SameLine(); ImGui::SetNextItemWidth(128.f * GlobalScale);
			ImGui::BeginDisabled(InTextureInfo.NumMips <= 1);
			if (ImGui::BeginCombo("##Mip", *FAnsiString::Printf("%i - %ix%i", InOutTexturePreviewOptions.CurrentMip, InTextureInfo.GetSizeX(InOutTexturePreviewOptions.CurrentMip), InTextureInfo.GetSizeY(InOutTexturePreviewOptions.CurrentMip)), ImGuiComboFlags_None))
			{
				for (uint8 MipIndex = 0; MipIndex < InTextureInfo.NumMips; MipIndex++)
				{
					const bool bIsSelected = (InOutTexturePreviewOptions.CurrentMip == MipIndex);
					if (ImGui::Selectable(*FAnsiString::Printf("%i - %ix%i", MipIndex, InTextureInfo.GetSizeX(MipIndex), InTextureInfo.GetSizeY(MipIndex)), bIsSelected))
					{
						InOutTexturePreviewOptions.CurrentMip = MipIndex;
					}
					if (bIsSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::EndDisabled();

			if (ImGui::IsItemHovered() && (InTextureInfo.NumMips > 1) && (FMath::Abs(ImGui::GetIO().MouseWheel) > KINDA_SMALL_NUMBER))
			{
				InOutTexturePreviewOptions.CurrentMip = FMath::Clamp(InOutTexturePreviewOptions.CurrentMip + (ImGui::GetIO().MouseWheel > 0 ? -1 : 1), 0, (int32)InTextureInfo.NumMips - 1);
			}
		}

		ImGui::SameLine();
		ImGui::Dummy(ImVec2(ControlPadding, 0));
		ImGui::SameLine();

		// texture slice/face controls
		{
			if (InTextureInfo.bIsCubemap)
			{
				static const char* FaceNames[6] = { "X+", "X-", "Y+", "Y-", "Z+", "Z-" };

				ImGui::Text("Face"); ImGui::SameLine(); ImGui::SetNextItemWidth(128.f * GlobalScale);
				if (ImGui::BeginCombo("##Face", *FAnsiString::Printf("%s", FaceNames[InOutTexturePreviewOptions.CurrentFace % 6]), ImGuiComboFlags_None))
				{
					for (int32 FaceIndex = 0; FaceIndex < (InTextureInfo.ArraySize * 6); FaceIndex++)
					{
						const bool bIsSelected = (InOutTexturePreviewOptions.CurrentFace == FaceIndex);
						if (ImGui::Selectable(*FAnsiString::Printf("%s", FaceNames[FaceIndex % 6]), bIsSelected))
						{
							InOutTexturePreviewOptions.CurrentFace = FaceIndex;
						}
						if (bIsSelected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}

				if (ImGui::IsItemHovered() && (FMath::Abs(ImGui::GetIO().MouseWheel) > KINDA_SMALL_NUMBER))
				{
					InOutTexturePreviewOptions.CurrentFace = FMath::Clamp(InOutTexturePreviewOptions.CurrentFace + (ImGui::GetIO().MouseWheel > 0 ? -1 : 1), 0, int32(InTextureInfo.ArraySize * 6) - 1);
				}
			}
			else
			{
				ImGui::Text("Slice"); ImGui::SameLine(); ImGui::SetNextItemWidth(128.f * GlobalScale);
				ImGui::BeginDisabled(FMath::Max(InTextureInfo.ArraySize, InTextureInfo.SizeZ) <= 1);
				if (ImGui::BeginCombo("##Slice", *FAnsiString::Printf("Slice %i", InOutTexturePreviewOptions.CurrentArraySlice), ImGuiComboFlags_None))
				{
					for (int32 SliceIndex = 0; SliceIndex < FMath::Max(InTextureInfo.ArraySize, InTextureInfo.SizeZ); SliceIndex++)
					{
						const bool bIsSelected = (InOutTexturePreviewOptions.CurrentArraySlice == SliceIndex);
						if (ImGui::Selectable(*FAnsiString::Printf("Slice %i", SliceIndex), bIsSelected))
						{
							InOutTexturePreviewOptions.CurrentArraySlice = SliceIndex;
						}
						if (bIsSelected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
				ImGui::EndDisabled();

				if (ImGui::IsItemHovered() && (FMath::Max(InTextureInfo.ArraySize, InTextureInfo.SizeZ) > 1) && (FMath::Abs(ImGui::GetIO().MouseWheel) > KINDA_SMALL_NUMBER))
				{
					InOutTexturePreviewOptions.CurrentArraySlice = FMath::Clamp(InOutTexturePreviewOptions.CurrentArraySlice + (ImGui::GetIO().MouseWheel > 0 ? -1 : 1), 0, FMath::Max(InTextureInfo.ArraySize, InTextureInfo.SizeZ) - 1);
				}
			}
		}

		ImGui::SameLine();
		ImGui::Dummy(ImVec2(ControlPadding, 0));
		ImGui::SameLine();

		// srgb control
		{
			ImGui::BeginDisabled(IsStencilFormat(InTextureInfo.Format));
			ImGui::Text("sRGB"); ImGui::SameLine(); ImGui::Checkbox("##sRGB", &InOutTexturePreviewOptions.bSRGB);
			ImGui::EndDisabled();
		}

		ImGui::SameLine();
		ImGui::Dummy(ImVec2(ControlPadding, 0));
		ImGui::SameLine();

		// background controls
		{
			ImGui::Text("Background");
			ImGui::SameLine();

			const bool bIsEnabled = InOutTexturePreviewOptions.BackgroundColor.A > 0.5f;
			if (ImGui::ColorEdit3("##BGColor", &InOutTexturePreviewOptions.BackgroundColor.R, ImGuiColorEditFlags_NoInputs | (bIsEnabled ? 0 : ImGuiColorEditFlags_NoTooltip)))
			{
				InOutTexturePreviewOptions.BackgroundColor.A = 1.f;
			}
			if (!bIsEnabled)
			{
				ImGui::SetItemTooltip("Pick solid background color");
			}
			else
			{
				ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor::HSV(2.f / 7.0f, 0.6f, 0.6f), 0.f, ImDrawFlags_None, 2.f);
			}
			ImGui::SameLine();

			{
				const float CurrentLineHeight = ImGui::GetItemRectSize().y;
				const FImGuiImageBindingParams CheckerboardIcon = UImGuiSubsystem::Get()->RegisterOneFrameResource(IMGUI_ICON("Icon.CheckerPattern"), FVector2D(CurrentLineHeight));

				const bool bIsActive = InOutTexturePreviewOptions.BackgroundColor.A < 0.5f;
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

				if (FImGui::ImageButton("CheckerBG", CheckerboardIcon))
				{
					InOutTexturePreviewOptions.BackgroundColor.A = 0.f;
				}
				ImGui::SetItemTooltip("Show checker background");

				if (bIsActive)
				{
					ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImColor::HSV(2.f / 7.0f, 0.6f, 0.6f), 0.f, ImDrawFlags_None, 2.f);
				}
				ImGui::PopStyleVar();
			}
		}

		// range controls (on a new row)
		ImGui::Dummy(ImVec2(0.f, 4.f));
		{
			ImGui::SetNextItemWidth(256.f);
			FImGui::SliderWithTwoHandles(
				Context,
				"Range",
				InOutTexturePreviewOptions.RangeValueMin,
				InOutTexturePreviewOptions.RangeValueMax,
				InOutTexturePreviewOptions.RangeMin,
				InOutTexturePreviewOptions.RangeMax,
				128.f, 400.f);
			const float CurrentLineHeight = ImGui::GetItemRectSize().y;

			ImGui::SameLine();
			{
				const FImGuiImageBindingParams ZoomToTextureIcon = UImGuiSubsystem::Get()->RegisterOneFrameResource(IMGUI_ICON("Icon.Find"), FVector2D(CurrentLineHeight));

				ImGui::PushStyleColor(ImGuiCol_Button, 0xBFFFFFFF);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFFFFFFFF);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFFFFFFFF);
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 0));

				if (FImGui::TransparentImageButton("AutoFitRange", ZoomToTextureIcon))
				{
					FVector2D Range;
					ENQUEUE_RENDER_COMMAND(AutoFit)(
						[&Range, PreviewParams = TexturePreviewOptions](FRHICommandListImmediate& RHICommandList)
						{
							Range = GetMinMaxTextureValue(RHICommandList, PreviewParams);
						});
					FlushRenderingCommands();

					InOutTexturePreviewOptions.RangeValueMin = InOutTexturePreviewOptions.RangeMin = Range.X;
					InOutTexturePreviewOptions.RangeValueMax = InOutTexturePreviewOptions.RangeMax = Range.Y;
				}
				ImGui::SetItemTooltip("Set range based on texture Min/Max value");

				ImGui::PopStyleVar();
				ImGui::PopStyleColor(3);
			}

			ImGui::SameLine();
			{
				const FImGuiImageBindingParams ZoomToFitIcon = UImGuiSubsystem::Get()->RegisterOneFrameResource(IMGUI_ICON("Icon.FrameSelected"), FVector2D(CurrentLineHeight));

				ImGui::PushStyleColor(ImGuiCol_Button, 0xBFFFFFFF);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFFFFFFFF);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFFFFFFFF);
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 0));

				if (FImGui::TransparentImageButton("ZoomToCurrentRange", ZoomToFitIcon))
				{
					InOutTexturePreviewOptions.RangeMin = InOutTexturePreviewOptions.RangeValueMin;
					InOutTexturePreviewOptions.RangeMax = InOutTexturePreviewOptions.RangeValueMax;
				}
				ImGui::SetItemTooltip("Fit to slider Min/Max range");

				ImGui::PopStyleVar();
				ImGui::PopStyleColor(3);
			}
			
			ImGui::SameLine();
			{
				const FImGuiImageBindingParams ResetToDefaultIcon = UImGuiSubsystem::Get()->RegisterOneFrameResource(IMGUI_ICON("Icon.ResetToDefault"), FVector2D(CurrentLineHeight));

				ImGui::PushStyleColor(ImGuiCol_Button, 0xBFFFFFFF);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFFFFFFFF);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFFFFFFFF);
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 0));

				if (FImGui::TransparentImageButton("ResetRange", ResetToDefaultIcon))
				{
					InOutTexturePreviewOptions.RangeMin = 0.;
					InOutTexturePreviewOptions.RangeMax = 1.;
					InOutTexturePreviewOptions.RangeValueMin = 0.;
					InOutTexturePreviewOptions.RangeValueMax = 1.;
				}
				ImGui::SetItemTooltip("Reset");

				ImGui::PopStyleVar();
				ImGui::PopStyleColor(3);
			}
		}
		ImGui::Dummy(ImVec2(0.f, 4.f));
	}

	static void DrawTextureCanvas(
		ImGuiContext* Context,
		ImVec2 InCanvasSize,
		const FTextureMetadata& InTextureInfo,
		FTexturePreviewOptions& InOutTexturePreviewOptions)
	{
		ImVec2 ConstrainedCanvasSize = ConstrainCanvasToAspectRatio(InTextureInfo.SizeX, InTextureInfo.SizeY, InCanvasSize);
		if (InTextureInfo.IsValid())
		{
			if (TexturePreviewOptions.RequestedZoomLevel == FTexturePreviewOptions::ERequestedZoomLevel::OneToOne)
			{
				TexturePreviewOptions.CanvasZoomPercentage = 100.f;
				TexturePreviewOptions.CanvasScrollOffset = FVector2f(0.f, 0.f);
			}
			else if (TexturePreviewOptions.RequestedZoomLevel == FTexturePreviewOptions::ERequestedZoomLevel::Fit)
			{
				TexturePreviewOptions.CanvasZoomPercentage = FMath::Min(256.f, ConstrainedCanvasSize.x / (float)InTextureInfo.SizeX) * 100.f;
				TexturePreviewOptions.CanvasScrollOffset = FVector2f(0.f, 0.f);
			}

			TexturePreviewOptions.RequestedZoomLevel = FTexturePreviewOptions::ERequestedZoomLevel::None;
		}
		ConstrainedCanvasSize = ImVec2(InTextureInfo.SizeX, InTextureInfo.SizeY) * (TexturePreviewOptions.CanvasZoomPercentage / 100.f);

		TexturePreviewOptions.TextureInspectorRect = FIntVector4::ZeroValue;
		
		ImGui::InvisibleButton("TextureCanvas", InCanvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

		const bool bIsCanvasHovered = ImGui::IsItemHovered();
		const bool bIsCanvasClicked = ImGui::IsItemActive();
		if (bIsCanvasHovered && (FMath::Abs(ImGui::GetIO().MouseWheel) > KINDA_SMALL_NUMBER))
		{
			TexturePreviewOptions.CanvasZoomPercentage += 8.f * (ImGui::GetIO().MouseWheel > 0 ? 1 : -1);
			TexturePreviewOptions.CanvasZoomPercentage = FMath::Clamp(TexturePreviewOptions.CanvasZoomPercentage, 5.f, 25600.f);
		}
		if (bIsCanvasClicked && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f))
		{
			TexturePreviewOptions.CanvasScrollOffset.X += ImGui::GetIO().MouseDelta.x;
			TexturePreviewOptions.CanvasScrollOffset.Y += ImGui::GetIO().MouseDelta.y;
		}

		if (ConstrainedCanvasSize.x < InCanvasSize.x || ConstrainedCanvasSize.y < InCanvasSize.y)
		{
			TexturePreviewOptions.CanvasScrollOffset = FVector2f(0.f, 0.f);
		}
		else
		{
			const ImRect TranslatedCanvas = ImRect(
				ImVec2(TexturePreviewOptions.CanvasScrollOffset.X, TexturePreviewOptions.CanvasScrollOffset.Y),
				ConstrainedCanvasSize + ImVec2(TexturePreviewOptions.CanvasScrollOffset.X, TexturePreviewOptions.CanvasScrollOffset.Y));

			if (TranslatedCanvas.Min.x > 0)
			{
				TexturePreviewOptions.CanvasScrollOffset.X = 0.f;
			}
			else if (TranslatedCanvas.Max.x < InCanvasSize.x)
			{
				TexturePreviewOptions.CanvasScrollOffset.X = InCanvasSize.x - ConstrainedCanvasSize.x;
			}

			if (TranslatedCanvas.Min.y > 0)
			{
				TexturePreviewOptions.CanvasScrollOffset.Y = 0.f;
			}
			else if (TranslatedCanvas.Max.y < InCanvasSize.y)
			{
				TexturePreviewOptions.CanvasScrollOffset.Y = InCanvasSize.y - ConstrainedCanvasSize.y;
			}
		}

		TexturePreviewOptions.UVScaleAndOffset =
		FVector4f
		{
			ImGui::GetItemRectSize().x / ConstrainedCanvasSize.x,
			ImGui::GetItemRectSize().y / ConstrainedCanvasSize.y,
			-TexturePreviewOptions.CanvasScrollOffset.X / ConstrainedCanvasSize.x,
			-TexturePreviewOptions.CanvasScrollOffset.Y / ConstrainedCanvasSize.y
		};

		//if (bIsCanvasHovered)
		{
			ImVec2 RelativeMousePos = (ImGui::GetIO().MousePos - ImGui::GetItemRectMin());
			RelativeMousePos.x = FMath::Clamp(RelativeMousePos.x, 0.f, ImGui::GetItemRectSize().x);
			RelativeMousePos.y = FMath::Clamp(RelativeMousePos.y, 0.f, ImGui::GetItemRectSize().y);

			FVector2f Scale = FVector2f(InTextureInfo.SizeX, InTextureInfo.SizeY) / FVector2f(ConstrainedCanvasSize.x, ConstrainedCanvasSize.y);
			FVector2f CursorPos = (FVector2f(RelativeMousePos.x, RelativeMousePos.y) - TexturePreviewOptions.CanvasScrollOffset) * Scale;
			TexturePreviewOptions.CursorPosition = FIntPoint(CursorPos.X, CursorPos.Y);
		}

		if (bIsCanvasClicked && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.f))
		{
			ImVec2 RelativeMousePos = (ImGui::GetIO().MousePos - ImGui::GetItemRectMin());
			RelativeMousePos.x = FMath::Clamp(RelativeMousePos.x, 0.f, ImGui::GetItemRectSize().x);
			RelativeMousePos.y = FMath::Clamp(RelativeMousePos.y, 0.f, ImGui::GetItemRectSize().y);
			
			FVector2f Scale = FVector2f(InTextureInfo.SizeX, InTextureInfo.SizeY) / FVector2f(ConstrainedCanvasSize.x, ConstrainedCanvasSize.y);
			FVector2f CursorPos = (FVector2f(RelativeMousePos.x, RelativeMousePos.y) - TexturePreviewOptions.CanvasScrollOffset) * Scale;
			TexturePreviewOptions.TextureInspectorCursorPosition = FIntPoint(CursorPos.X, CursorPos.Y);

			const float TextureInspectorSize = 144.f;
			const float TextureInspectorInfoWidgetSize = 256.f;
			const float AvailableSpaceLeft = RelativeMousePos.x;
			const float AvailableSpaceRight = ImGui::GetItemRectSize().x - RelativeMousePos.x;
			const float AvailableSpaceTop = RelativeMousePos.y;
			const float AvailableSpaceBottom = ImGui::GetItemRectSize().y - RelativeMousePos.y;

			ImVec2 AbsoluteMousePos = RelativeMousePos + ImGui::GetItemRectMin();

			ImVec2 TextureInspectorOffset = ImVec2(32.f, -TextureInspectorSize);
			float TextureInspectorInfoWidgetOffsetX = TextureInspectorSize;
			if (AvailableSpaceTop < TextureInspectorSize)
			{
				TextureInspectorOffset.y = 0;
			}
			if (AvailableSpaceRight < (TextureInspectorSize + TextureInspectorInfoWidgetSize))
			{
				TextureInspectorOffset.x = -(32.f + TextureInspectorSize);
				TextureInspectorInfoWidgetOffsetX = -TextureInspectorInfoWidgetSize;
			}

			TexturePreviewOptions.TextureInspectorRect.X = AbsoluteMousePos.x + TextureInspectorOffset.x;
			TexturePreviewOptions.TextureInspectorRect.Y = AbsoluteMousePos.y + TextureInspectorOffset.y;
			TexturePreviewOptions.TextureInspectorRect.Z = AbsoluteMousePos.x + TextureInspectorOffset.x + TextureInspectorSize;
			TexturePreviewOptions.TextureInspectorRect.W = AbsoluteMousePos.y + TextureInspectorOffset.y + TextureInspectorSize;

			const int32 HoveredTexCoordX = TexturePreviewOptions.TextureInspectorCursorPosition.X >> TexturePreviewOptions.CurrentMip;
			const int32 HoveredTexCoordY = TexturePreviewOptions.TextureInspectorCursorPosition.Y >> TexturePreviewOptions.CurrentMip;
			
			ImGui::SetNextWindowPos(ImVec2(TexturePreviewOptions.TextureInspectorRect.X + TextureInspectorInfoWidgetOffsetX, TexturePreviewOptions.TextureInspectorRect.Y), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(TextureInspectorInfoWidgetSize, TextureInspectorSize), ImGuiCond_Always);
			if (ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePrevious, ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse))
			{
				ImGui::Text("UV: %.4f %.4f",
					(float)HoveredTexCoordX / (float)InTextureInfo.GetSizeX(InOutTexturePreviewOptions.CurrentMip),
					(float)HoveredTexCoordY / (float)InTextureInfo.GetSizeY(InOutTexturePreviewOptions.CurrentMip));
				ImGui::Text("Coord: %i %i", HoveredTexCoordX, HoveredTexCoordY);
				ImGui::Separator();
				ImGui::TextUnformatted(*PixelFormatUtils::GetPixelValueAsString(InTextureInfo.SelectedPixelValue, InTextureInfo.Format, InOutTexturePreviewOptions.bDisplayStencil));
				ImGui::EndTooltip();
			}
		}
	}

	static void Tick(ImGuiContext* Context)
	{
		FImGuiTickScope Scope{ Context };

		// TODO: Find a better/reliable way to check if docknode is already active
#if WITH_EDITOR //dockspace already created if not using standlone widgets
		ImGuiDockNodeFlags DockingFlags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoTabBar;
		const ImGuiID MainDockSpaceID = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), DockingFlags);
		ImGui::SetNextWindowDockID(MainDockSpaceID, ImGuiCond_Always);
#endif

		if (ImGui::Begin("TextureVisualizer", nullptr, ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoScrollbar))
		{
			static FAnsiString VisTextureName;
			const bool bRequestNewTexture = DrawTextureList(Context, VisTextureName);
			if (bRequestNewTexture)
			{
				FlushRenderingCommands();

				GetTextureMetadata(/*bOnRenderThread=*/false).Reset();
				GetTextureMetadata(/*bOnRenderThread=*/true).Reset();
				TexturePreviewOptions.Reset();
			}

			const FTextureMetadata& TextureInfo = GetTextureMetadata(/*bOnRenderThread=*/false);

			ImGui::Separator();

			auto PrevPreviewOptions = TexturePreviewOptions;
			DrawTextureControls(Context, TextureInfo, TexturePreviewOptions);

			ImGui::Separator();

			const int32 TextureDetailsWidgetHeight = 40.f * ImGui::GetStyle().FontScaleMain;
			
			const ImVec2 TextureCanvasSize = ImGui::GetContentRegionAvail() - ImVec2(0.f, TextureDetailsWidgetHeight);
			DrawTextureCanvas(Context, TextureCanvasSize, TextureInfo, TexturePreviewOptions);

			FTexturePreviewUserData Params;
			Params.ViewportSize = ImGui::GetIO().DisplaySize;
			Params.ClipRectMin = ImGui::GetItemRectMin();
			Params.ClipRectMax = ImGui::GetItemRectMax();
			Params.Options = TexturePreviewOptions;
			ImGui::GetWindowDrawList()->AddCallback(TexturePreviewCallback, &Params, sizeof(Params));

			ImGui::SetCursorPosY(ImGui::GetWindowHeight() - TextureDetailsWidgetHeight);
			ImGui::Separator();

			if (VisTextureName.IsEmpty() || TextureInfo.Format == PF_Unknown)
			{
				ImGui::Text("No texture selected!");
			}
			else
			{
				if (TextureInfo.SizeZ > 0)
				{
					FAnsiString FormatName = TCHAR_TO_ANSI(GPixelFormats[TextureInfo.Format].Name);
					ImGui::Text("%s - %ix%ix%i %i mips - %s", *VisTextureName, TextureInfo.SizeX, TextureInfo.SizeY, TextureInfo.SizeZ, TextureInfo.NumMips, *FormatName);
				}
				else
				{
					FAnsiString FormatName = TCHAR_TO_ANSI(GPixelFormats[TextureInfo.Format].Name);
					ImGui::Text("%s - %ix%i %i mips - %s", *VisTextureName, TextureInfo.SizeX, TextureInfo.SizeY, TextureInfo.NumMips, *FormatName);
				}

				const int32 HoveredTexCoordX = TexturePreviewOptions.CursorPosition.X >> TexturePreviewOptions.CurrentMip;
				const int32 HoveredTexCoordY = TexturePreviewOptions.CursorPosition.Y >> TexturePreviewOptions.CurrentMip;
				ImGui::Text("Hover - %i, %i (%f, %f) - Right click to pick a pixel",
					HoveredTexCoordX,
					HoveredTexCoordY,
					(float)HoveredTexCoordX / (float)TextureInfo.GetSizeX(TexturePreviewOptions.CurrentMip),
					(float)HoveredTexCoordY / (float)TextureInfo.GetSizeY(TexturePreviewOptions.CurrentMip));
			}

			TextureMetadataReadIndex_GT = (TextureMetadataReadIndex_GT + 1) % 2;
			ENQUEUE_RENDER_COMMAND(Flip)(
				[bRequestNewTexture, TextureMetadataReadIndex=((TextureMetadataReadIndex_GT + 1) % 2)](FRHICommandListImmediate& RHICommandList)
				{
					TextureMetadataReadIndex_RT = TextureMetadataReadIndex;
					
					if (bRequestNewTexture)
					{
						ViewExtension->SetTextureToDisplay(ANSI_TO_TCHAR(*VisTextureName));
					}
				});
		}
		ImGui::End();
	}

	FStaticWidgetRegisterParams Params =
	{
		.InitFunction = &Initialize,
		.TickFunction = &Tick,
		.WidgetIcon = FSlateIcon(FName("EditorStyle"), FName("ClassIcon.UserDefinedStruct")),
		.WidgetName = "Texture Visualizer",
		.WidgetDescription = "Tool for visualizing rendering resources."
	};
	IMGUI_REGISTER_STANDALONE_WIDGET(Params);
}

UE_ENABLE_OPTIMIZATION
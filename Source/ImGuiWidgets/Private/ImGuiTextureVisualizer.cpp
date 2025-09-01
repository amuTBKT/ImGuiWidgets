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

		bool bRequestMinMaxTextureValues = false;

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

	static FVector2D GetMinMaxTextureValue(FRHICommandListImmediate& RHICmdList, const FTexturePreviewOptions& InPreviewOptions)
	{
		if (!ViewExtension->TextureToDisplay)
		{
			return FVector2D(0.f, 1.f);
		}
		const FPooledRenderTargetDesc& TextureDesc = ViewExtension->TextureDesc;

		const uint64 TextureExtentMax = TextureDesc.Extent.GetMax();
		const uint64 BlockPixSize = FTextureDisplay_TileMinMaxCS::HGRAM_PIXELS_PER_TILE * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK;
		const uint64 BlockCount = uint64(TextureExtentMax * TextureExtentMax) / (BlockPixSize * BlockPixSize);

#if ((ENGINE_MAJOR_VERSION * 100u + ENGINE_MINOR_VERSION) > 505) //(Version > 5.5)
		FRHIBufferCreateDesc TileMinMaxBufferDesc =
			FRHIBufferCreateDesc::Create(
				TEXT("Histogram_TileMinMax"),
				BlockCount * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK * sizeof(FVector4f) * 2,
				sizeof(FVector4f), EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess)
				.SetInitialState(ERHIAccess::UAVCompute)
				.SetInitActionNone();
		FBufferRHIRef TileMinMaxBuffer = RHICmdList.CreateBuffer(TileMinMaxBufferDesc);
#else
		FRHIResourceCreateInfo TileMinMaxBufferCreateInfo(TEXT("Histogram_TileMinMax"));
		FBufferRHIRef TileMinMaxBuffer = RHICmdList.CreateBuffer(
			BlockCount * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK * sizeof(FVector4f) * 2,
			EBufferUsageFlags::Static | EBufferUsageFlags::ShaderResource | EBufferUsageFlags::UnorderedAccess,
			sizeof(FVector4f), ERHIAccess::UAVCompute, TileMinMaxBufferCreateInfo);
#endif

		ETexDisplay_ResourceType ResType;
		ETexDisplay_ShaderBaseType BaseType;
		PixelFormatUtils::DetermineShaderTypesForTexture(TextureDesc, InPreviewOptions.bDisplayStencil, ResType, BaseType);

		{
			RenderCaptureInterface::FScopedCapture RenderCapture(false, &RHICmdList, TEXT("HistogramMinMax"));

			auto* ShaderMap = GetGlobalShaderMap(GetMaxSupportedFeatureLevel(GMaxRHIShaderPlatform));

			// tile min max
			{
				FTextureDisplay_TileMinMaxCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FTextureDisplay_TileMinMaxCS::FResourceType>(ResType);
				PermutationVector.Set<FTextureDisplay_TileMinMaxCS::FShaderBaseType>(BaseType);
				TShaderMapRef<FTextureDisplay_TileMinMaxCS> ComputeShader(ShaderMap, PermutationVector);

				auto TextureSRVDesc = FRHIViewDesc::CreateTextureSRV()
					.SetDimensionFromTexture(ViewExtension->TextureToDisplay->GetRHI())
					.SetMipRange(0, TextureDesc.NumMips)
					.SetArrayRange(0, TextureDesc.ArraySize)
					.SetFormat((IsStencilFormat(TextureDesc.Format) && InPreviewOptions.bDisplayStencil) ? PF_X24_G8 : TextureDesc.Format);

				EPixelFormat BufferFormat = PF_A32B32G32R32F;
				switch (BaseType)
				{
				case ETexDisplay_ShaderBaseType::UInt:  BufferFormat = PF_R32G32B32A32_UINT; break;
				case ETexDisplay_ShaderBaseType::SInt:  BufferFormat = PF_R32G32B32A32_UINT; break; // NOTE: PF_R32G32B32A32_SINT is not available
				case ETexDisplay_ShaderBaseType::Float: BufferFormat = PF_A32B32G32R32F; break;
				default: checkNoEntry(); break;
				}

				SetComputePipelineState(RHICmdList, ComputeShader.GetComputeShader());
				SetShaderParametersLegacyCS(
					RHICmdList, ComputeShader,
					RHICmdList.CreateUnorderedAccessView(TileMinMaxBuffer.GetReference(), FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(BufferFormat)),
					RHICmdList.CreateShaderResourceView(ViewExtension->TextureToDisplay->GetRHI(), TextureSRVDesc),
					FIntVector3(TextureDesc.Extent.X, TextureDesc.Extent.Y, TextureDesc.Depth),
					InPreviewOptions.CurrentMip,
					TextureDesc.bIsCubemap ? InPreviewOptions.CurrentFace : InPreviewOptions.CurrentArraySlice);

				const int32 ThreadGroupCountX = FMath::DivideAndRoundUp(TextureDesc.Extent.X, FTextureDisplay_TileMinMaxCS::HGRAM_PIXELS_PER_TILE * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK);
				const int32 ThreadGroupCountY = FMath::DivideAndRoundUp(TextureDesc.Extent.Y, FTextureDisplay_TileMinMaxCS::HGRAM_PIXELS_PER_TILE * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK);

				RHICmdList.DispatchComputeShader(ThreadGroupCountX, ThreadGroupCountY, 1);
			}

			RHICmdList.Transition(FRHITransitionInfo(TileMinMaxBuffer.GetReference(), ERHIAccess::UAVCompute, ERHIAccess::SRVCompute));

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

				SetComputePipelineState(RHICmdList, ComputeShader.GetComputeShader());
				SetShaderParametersLegacyCS(
					RHICmdList, ComputeShader,
					RHICmdList.CreateUnorderedAccessView(GPixelValueDestBuffer->VertexBufferRHI, FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(BufferFormat)),
					RHICmdList.CreateShaderResourceView(TileMinMaxBuffer.GetReference(), FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(BufferFormat)),
					FIntVector3(TextureDesc.Extent.X, TextureDesc.Extent.Y, TextureDesc.Depth));

				RHICmdList.DispatchComputeShader(1, 1, 1);
			}
		}

		// default to [0, 1] range if readback fails
		FVector4 MinValue = FVector4::Zero();
		FVector4 MaxValue = FVector4::One();
		{
			FRHIGPUBufferReadback Readback(TEXT("TexDisplay::MinMaxReadback"));
			uint32 NumBytes = sizeof(FVector4f) * 2;
			Readback.EnqueueCopy(RHICmdList, GPixelValueDestBuffer->VertexBufferRHI, NumBytes);
			RHICmdList.BlockUntilGPUIdle();
			RHICmdList.FlushResources();
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

			if (const uint8* SrcRawBuffer = (const uint8*)Readback.Lock(NumBytes))
			{
				if (BaseType == ETexDisplay_ShaderBaseType::UInt)
				{
					FUintVector4 val0; FMemory::Memcpy(&val0, SrcRawBuffer, sizeof(val0));
					FUintVector4 val1; FMemory::Memcpy(&val1, SrcRawBuffer + sizeof(val0), sizeof(val1));

					MinValue = FVector4(val0.X, val0.Y, val0.Z, val0.W);
					MaxValue = FVector4(val1.X, val1.Y, val1.Z, val1.W);
				}
				else if (BaseType == ETexDisplay_ShaderBaseType::SInt)
				{
					FIntVector4 val0; FMemory::Memcpy(&val0, SrcRawBuffer, sizeof(val0));
					FIntVector4 val1; FMemory::Memcpy(&val1, SrcRawBuffer + sizeof(val0), sizeof(val1));

					MinValue = FVector4(val0.X, val0.Y, val0.Z, val0.W);
					MaxValue = FVector4(val1.X, val1.Y, val1.Z, val1.W);
				}
				else if (BaseType == ETexDisplay_ShaderBaseType::Float)
				{
					FVector4f val0; FMemory::Memcpy(&val0, SrcRawBuffer, sizeof(val0));
					FVector4f val1; FMemory::Memcpy(&val1, SrcRawBuffer + sizeof(val0), sizeof(val1));

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
			if (InPreviewOptions.bDisplayRedChannel && EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::R))
			{
				bFoundAnyValidChannels = true;
				Result.X = FMath::Min(Result.X, MinValue.X);
				Result.Y = FMath::Max(Result.Y, MaxValue.X);
			}
			if (InPreviewOptions.bDisplayGreenChannel && EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::G))
			{
				bFoundAnyValidChannels = true;
				Result.X = FMath::Min(Result.X, MinValue.Y);
				Result.Y = FMath::Max(Result.Y, MaxValue.Y);
			}
			if (InPreviewOptions.bDisplayBlueChannel && EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::B))
			{
				bFoundAnyValidChannels = true;
				Result.X = FMath::Min(Result.X, MinValue.Z);
				Result.Y = FMath::Max(Result.Y, MaxValue.Z);
			}
			if (InPreviewOptions.bDisplayAlphaChannel && EnumHasAnyFlags(ValidTextureChannels, EPixelFormatChannelFlags::A))
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

		return Result;
	}

	static void ReadCurrentPixelValue(FRHICommandListImmediate& RHICmdList, const FTexturePreviewOptions& InPreviewOptions)
	{
		if ((InPreviewOptions.TextureInspectorRect.Z - InPreviewOptions.TextureInspectorRect.X) > 0)
		{
			if (!ViewExtension->TextureToDisplay)
			{
				return;
			}
			const FPooledRenderTargetDesc& TextureDesc = ViewExtension->TextureDesc;

			ETexDisplay_ResourceType ResType;
			ETexDisplay_ShaderBaseType BaseType;
			PixelFormatUtils::DetermineShaderTypesForTexture(TextureDesc, InPreviewOptions.bDisplayStencil, ResType, BaseType);

			auto* ShaderMap = GetGlobalShaderMap(GetMaxSupportedFeatureLevel(GMaxRHIShaderPlatform));

			FTextureDisplay_CopyPixelValueCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FTextureDisplay_CopyPixelValueCS::FResourceType>(ResType);
			PermutationVector.Set<FTextureDisplay_CopyPixelValueCS::FShaderBaseType>(BaseType);
			TShaderMapRef<FTextureDisplay_CopyPixelValueCS> ComputeShader(ShaderMap, PermutationVector);

			EPixelFormat BufferFormat = PF_A32B32G32R32F;
			switch (BaseType)
			{
			case ETexDisplay_ShaderBaseType::UInt:  BufferFormat = PF_R32G32B32A32_UINT; break;
			case ETexDisplay_ShaderBaseType::SInt:  BufferFormat = PF_R32G32B32A32_UINT; break; // NOTE: PF_R32G32B32A32_SINT is not available
			case ETexDisplay_ShaderBaseType::Float: BufferFormat = PF_A32B32G32R32F; break;
			default: checkNoEntry(); break;
			}

			auto TextureSRVDesc = FRHIViewDesc::CreateTextureSRV()
				.SetDimensionFromTexture(ViewExtension->TextureToDisplay->GetRHI())
				.SetMipRange(0, TextureDesc.NumMips)
				.SetArrayRange(0, TextureDesc.ArraySize)
				.SetFormat((IsStencilFormat(TextureDesc.Format) && InPreviewOptions.bDisplayStencil) ? PF_X24_G8 : TextureDesc.Format);

			const int32 HoveredTexCoordX = InPreviewOptions.TextureInspectorCursorPosition.X >> InPreviewOptions.CurrentMip;
			const int32 HoveredTexCoordY = InPreviewOptions.TextureInspectorCursorPosition.Y >> InPreviewOptions.CurrentMip;

			SetComputePipelineState(RHICmdList, ComputeShader.GetComputeShader());
			SetShaderParametersLegacyCS(
				RHICmdList, ComputeShader,
				RHICmdList.CreateUnorderedAccessView(GPixelValueDestBuffer->VertexBufferRHI, FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(BufferFormat)),
				RHICmdList.CreateShaderResourceView(ViewExtension->TextureToDisplay->GetRHI(), TextureSRVDesc),
				FIntVector3(TextureDesc.Extent.X, TextureDesc.Extent.Y, TextureDesc.Depth),
				InPreviewOptions.CurrentMip,
				TextureDesc.bIsCubemap ? InPreviewOptions.CurrentFace : InPreviewOptions.CurrentArraySlice,
				FIntPoint(HoveredTexCoordX, HoveredTexCoordY));

			RHICmdList.DispatchComputeShader(1, 1, 1);

			FTextureMetadata& TextureInfo = GetTextureMetadata(/*bOnRenderThread=*/true);
			uint8* Dst = TextureInfo.SelectedPixelValue;
			{
				FRHIGPUBufferReadback Readback(TEXT("TexDisplay::CurrentPixelReadback"));
				uint32 NumBytes = sizeof(FUintVector4);
				Readback.EnqueueCopy(RHICmdList, GPixelValueDestBuffer->VertexBufferRHI, NumBytes);
				RHICmdList.BlockUntilGPUIdle();
				RHICmdList.FlushResources();
				RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

				if (const uint8* SrcRawBuffer = (const uint8*)Readback.Lock(NumBytes))
				{
					if (BaseType == ETexDisplay_ShaderBaseType::UInt)
					{
						FUintVector4 value; FMemory::Memcpy(&value, SrcRawBuffer, sizeof(value));
						FMemory::Memcpy(Dst, &value, sizeof(FUintVector4));
					}
					else if (BaseType == ETexDisplay_ShaderBaseType::SInt)
					{
						FIntVector4 value; FMemory::Memcpy(&value, SrcRawBuffer, sizeof(value));
						FMemory::Memcpy(Dst, &value, sizeof(FIntVector4));
					}
					else if (BaseType == ETexDisplay_ShaderBaseType::Float)
					{
						FVector4f value; FMemory::Memcpy(&value, SrcRawBuffer, sizeof(value));
						FMemory::Memcpy(Dst, &value, sizeof(FVector4f));
					}

					Readback.Unlock();
				}
			}
		}
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

		FRHICommandListImmediate& RHICmdList = *(FRHICommandListImmediate*)immediate_command_list;
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
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

		auto TextureSRVDesc = FRHIViewDesc::CreateTextureSRV()
			.SetDimensionFromTexture(ViewExtension->TextureToDisplay->GetRHI())
			.SetMipRange(0, TextureDesc.NumMips)
			.SetArrayRange(0, TextureDesc.ArraySize)
			.SetFormat((IsStencilFormat(TextureDesc.Format) && PreviewParams.Options.bDisplayStencil) ? PF_X24_G8 : TextureDesc.Format);

		SetShaderParametersLegacyPS(
			RHICmdList, PixelShader,
			RHICmdList.CreateShaderResourceView(ViewExtension->TextureToDisplay->GetRHI(), TextureSRVDesc),
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

		RHICmdList.SetViewport(0.f, 0.f, 0.f, PreviewParams.ViewportSize.x, PreviewParams.ViewportSize.y, 1.f);
		RHICmdList.SetScissorRect(true, PreviewParams.ClipRectMin.x, PreviewParams.ClipRectMin.y, PreviewParams.ClipRectMax.x, PreviewParams.ClipRectMax.y);

		UE::Renderer::PostProcess::DrawRectangle(
			RHICmdList,
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
					InOutTexturePreviewOptions.bRequestMinMaxTextureValues = true;
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
			if (InOutTexturePreviewOptions.RequestedZoomLevel == FTexturePreviewOptions::ERequestedZoomLevel::OneToOne)
			{
				InOutTexturePreviewOptions.CanvasZoomPercentage = 100.f;
				InOutTexturePreviewOptions.CanvasScrollOffset = FVector2f(0.f, 0.f);
			}
			else if (InOutTexturePreviewOptions.RequestedZoomLevel == FTexturePreviewOptions::ERequestedZoomLevel::Fit)
			{
				InOutTexturePreviewOptions.CanvasZoomPercentage = FMath::Min(256.f, ConstrainedCanvasSize.x / (float)InTextureInfo.SizeX) * 100.f;
				InOutTexturePreviewOptions.CanvasScrollOffset = FVector2f(0.f, 0.f);
			}

			InOutTexturePreviewOptions.RequestedZoomLevel = FTexturePreviewOptions::ERequestedZoomLevel::None;
		}
		ConstrainedCanvasSize = ImVec2(InTextureInfo.SizeX, InTextureInfo.SizeY) * (InOutTexturePreviewOptions.CanvasZoomPercentage / 100.f);

		InOutTexturePreviewOptions.TextureInspectorRect = FIntVector4::ZeroValue;
		
		ImGui::InvisibleButton("TextureCanvas", InCanvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

		const bool bIsCanvasHovered = ImGui::IsItemHovered();
		const bool bIsCanvasClicked = ImGui::IsItemActive();
		if (bIsCanvasHovered && (FMath::Abs(ImGui::GetIO().MouseWheel) > KINDA_SMALL_NUMBER))
		{
			InOutTexturePreviewOptions.CanvasZoomPercentage += 8.f * (ImGui::GetIO().MouseWheel > 0 ? 1 : -1);
			InOutTexturePreviewOptions.CanvasZoomPercentage = FMath::Clamp(InOutTexturePreviewOptions.CanvasZoomPercentage, 5.f, 25600.f);
		}
		if (bIsCanvasClicked && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f))
		{
			InOutTexturePreviewOptions.CanvasScrollOffset.X += ImGui::GetIO().MouseDelta.x;
			InOutTexturePreviewOptions.CanvasScrollOffset.Y += ImGui::GetIO().MouseDelta.y;
		}

		if (ConstrainedCanvasSize.x < InCanvasSize.x || ConstrainedCanvasSize.y < InCanvasSize.y)
		{
			InOutTexturePreviewOptions.CanvasScrollOffset = FVector2f(0.f, 0.f);
		}
		else
		{
			const ImRect TranslatedCanvas = ImRect(
				ImVec2(InOutTexturePreviewOptions.CanvasScrollOffset.X, InOutTexturePreviewOptions.CanvasScrollOffset.Y),
				ConstrainedCanvasSize + ImVec2(InOutTexturePreviewOptions.CanvasScrollOffset.X, InOutTexturePreviewOptions.CanvasScrollOffset.Y));

			if (TranslatedCanvas.Min.x > 0)
			{
				InOutTexturePreviewOptions.CanvasScrollOffset.X = 0.f;
			}
			else if (TranslatedCanvas.Max.x < InCanvasSize.x)
			{
				InOutTexturePreviewOptions.CanvasScrollOffset.X = InCanvasSize.x - ConstrainedCanvasSize.x;
			}

			if (TranslatedCanvas.Min.y > 0)
			{
				InOutTexturePreviewOptions.CanvasScrollOffset.Y = 0.f;
			}
			else if (TranslatedCanvas.Max.y < InCanvasSize.y)
			{
				InOutTexturePreviewOptions.CanvasScrollOffset.Y = InCanvasSize.y - ConstrainedCanvasSize.y;
			}
		}

		InOutTexturePreviewOptions.UVScaleAndOffset =
		FVector4f
		{
			ImGui::GetItemRectSize().x / ConstrainedCanvasSize.x,
			ImGui::GetItemRectSize().y / ConstrainedCanvasSize.y,
			-InOutTexturePreviewOptions.CanvasScrollOffset.X / ConstrainedCanvasSize.x,
			-InOutTexturePreviewOptions.CanvasScrollOffset.Y / ConstrainedCanvasSize.y
		};

		//if (bIsCanvasHovered)
		{
			ImVec2 RelativeMousePos = (ImGui::GetIO().MousePos - ImGui::GetItemRectMin());
			RelativeMousePos.x = FMath::Clamp(RelativeMousePos.x, 0.f, ImGui::GetItemRectSize().x);
			RelativeMousePos.y = FMath::Clamp(RelativeMousePos.y, 0.f, ImGui::GetItemRectSize().y);

			FVector2f Scale = FVector2f(InTextureInfo.SizeX, InTextureInfo.SizeY) / FVector2f(ConstrainedCanvasSize.x, ConstrainedCanvasSize.y);
			FVector2f CursorPos = (FVector2f(RelativeMousePos.x, RelativeMousePos.y) - InOutTexturePreviewOptions.CanvasScrollOffset) * Scale;
			InOutTexturePreviewOptions.CursorPosition = FIntPoint(CursorPos.X, CursorPos.Y);
		}

		if (bIsCanvasClicked && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.f))
		{
			ImVec2 RelativeMousePos = (ImGui::GetIO().MousePos - ImGui::GetItemRectMin());
			RelativeMousePos.x = FMath::Clamp(RelativeMousePos.x, 0.f, ImGui::GetItemRectSize().x);
			RelativeMousePos.y = FMath::Clamp(RelativeMousePos.y, 0.f, ImGui::GetItemRectSize().y);
			
			FVector2f Scale = FVector2f(InTextureInfo.SizeX, InTextureInfo.SizeY) / FVector2f(ConstrainedCanvasSize.x, ConstrainedCanvasSize.y);
			FVector2f CursorPos = (FVector2f(RelativeMousePos.x, RelativeMousePos.y) - InOutTexturePreviewOptions.CanvasScrollOffset) * Scale;
			InOutTexturePreviewOptions.TextureInspectorCursorPosition = FIntPoint(CursorPos.X, CursorPos.Y);

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

			InOutTexturePreviewOptions.TextureInspectorRect.X = AbsoluteMousePos.x + TextureInspectorOffset.x;
			InOutTexturePreviewOptions.TextureInspectorRect.Y = AbsoluteMousePos.y + TextureInspectorOffset.y;
			InOutTexturePreviewOptions.TextureInspectorRect.Z = AbsoluteMousePos.x + TextureInspectorOffset.x + TextureInspectorSize;
			InOutTexturePreviewOptions.TextureInspectorRect.W = AbsoluteMousePos.y + TextureInspectorOffset.y + TextureInspectorSize;

			const int32 HoveredTexCoordX = InOutTexturePreviewOptions.TextureInspectorCursorPosition.X >> InOutTexturePreviewOptions.CurrentMip;
			const int32 HoveredTexCoordY = InOutTexturePreviewOptions.TextureInspectorCursorPosition.Y >> InOutTexturePreviewOptions.CurrentMip;
			
			ImGui::SetNextWindowPos(ImVec2(InOutTexturePreviewOptions.TextureInspectorRect.X + TextureInspectorInfoWidgetOffsetX, InOutTexturePreviewOptions.TextureInspectorRect.Y), ImGuiCond_Always);
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
			ENQUEUE_RENDER_COMMAND(ImGuiTexDisplay_Flip)(
				[bRequestNewTexture, PreviewOptions=TexturePreviewOptions, TextureMetadataReadIndex=((TextureMetadataReadIndex_GT + 1) % 2)](FRHICommandListImmediate& RHICmdList)
				{
					SCOPED_DRAW_EVENT(RHICmdList, ImGuiTexDisplay_Flip);

					TextureMetadataReadIndex_RT = TextureMetadataReadIndex;
					
					if (bRequestNewTexture)
					{
						ViewExtension->SetTextureToDisplay(ANSI_TO_TCHAR(*VisTextureName));
					}

					ReadCurrentPixelValue(RHICmdList, PreviewOptions);
				});

			if (TexturePreviewOptions.bRequestMinMaxTextureValues)
			{
				TexturePreviewOptions.bRequestMinMaxTextureValues = false;

				FVector2D Range;
				ENQUEUE_RENDER_COMMAND(ImGuiTexDisplay_GetMinMaxValues)(
					[&Range, PreviewOptions=TexturePreviewOptions](FRHICommandListImmediate& RHICmdList)
					{
						SCOPED_DRAW_EVENT(RHICmdList, ImGuiTexDisplay_GetMinMaxValues);

						Range = GetMinMaxTextureValue(RHICmdList, PreviewOptions);
					});
				FlushRenderingCommands();

				TexturePreviewOptions.RangeValueMin = TexturePreviewOptions.RangeMin = Range.X;
				TexturePreviewOptions.RangeValueMax = TexturePreviewOptions.RangeMax = Range.Y;
			}
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
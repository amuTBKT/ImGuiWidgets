// Copyright 2025 Amit Kumar Mehar. All Rights Reserved.

#include "ImGuiTextureVisualizer.h"

#include "ImGuiWidgets.h"
#include "ImGuiSubsystem.h"
#include "ImGuiTextureDisplayShaders.h"
#include "ImGuiTextureVisualizerUtils.h"

#include "Engine/Engine.h"
#include "Engine/Texture.h"
#include "RHICommandList.h"
#include "RHIGPUReadback.h"
#include "TextureResource.h"
#include "RHIStaticStates.h"
#include "ScreenRendering.h"
#include "PipelineStateCache.h"
#include "CommonRenderResources.h"
#include "RenderCaptureInterface.h"
#include "PostProcess/DrawRectangle.h"

// override setup
namespace ImGuiTextureVisualizer
{
	static const FTextureResource* TextureResourceOverride = nullptr;
	static TWeakObjectPtr<const UTexture> TextureOverride = nullptr;
	static FTextureRHIRef TextureRHIOverride = nullptr;
	static FAnsiString TextureOverrideName;

	void SetTextureOverride_GameThread(const UTexture* Texture)
	{
		ClearTextureOverride_GameThread();

		if (Texture)
		{
			TextureOverride = Texture;

			ENQUEUE_RENDER_COMMAND(SetTextureOverrideName)(
				[TextureName=FAnsiString(TCHAR_TO_ANSI(*Texture->GetName()))](FRHICommandListImmediate& RHICmdList)
				{
					TextureOverrideName = TextureName;
				});
		}
	}

	void SetTextureOverride_GameThread(const FString& DisplayName, const FTextureResource* TextureResource)
	{
		ClearTextureOverride_GameThread();

		if (TextureResourceOverride)
		{
			TextureResourceOverride = TextureResource;
			
			ENQUEUE_RENDER_COMMAND(SetTextureOverrideName)(
				[TextureName = FAnsiString(TCHAR_TO_ANSI(*DisplayName))](FRHICommandListImmediate& RHICmdList)
				{
					TextureOverrideName = TextureName;
				});
		}
	}

	void SetTextureOverride_RenderThread(const FString& DisplayName, FRHITexture* TextureResource)
	{
		TextureRHIOverride = TextureResource;
		TextureOverrideName = TCHAR_TO_ANSI(*DisplayName);
	}

	void ClearTextureOverride_GameThread()
	{
		TextureOverride.Reset();
		TextureResourceOverride = nullptr;
		
		ENQUEUE_RENDER_COMMAND(SetTextureOverrideName)(
			[](FRHICommandListImmediate& RHICmdList)
			{
				TextureRHIOverride = nullptr;
				TextureOverrideName.Reset();
			});
	}
}

// widget logic
namespace ImGuiTextureVisualizer
{
	FVertexBuffer* GPixelValueDestBuffer = new TGlobalResource<FPixelValueDestBuffer, FRenderResource::EInitPhase::Default>;

	// Texture info populated by the Render thread
	struct FTextureInfo
	{
		int32		 SizeX		= 1;
		int32		 SizeY		= 1;
		int32		 SizeZ		= 0;
		int32		 ArraySize	= 1;
		EPixelFormat Format		= PF_Unknown;
		uint8		 NumMips	= 1;
		uint8		 bIsArray	: 1 = 0;
		uint8		 bIsCubemap : 1 = 0;

		// read back value for single pixel
		FUintVector4 SelectedPixelValue = FUintVector4::ZeroValue;

		// since we can override texture from Render thread,
		// kind of need to keep it in sync b/w Render and Game thread
		FAnsiString TextureOverrideName;

		void Reset()
		{
			SizeX = SizeY = ArraySize = NumMips = 1;
			SizeZ = 0;
			Format = PF_Unknown;
			bIsArray = false;
			bIsCubemap = false;

			SelectedPixelValue = FUintVector4::ZeroValue;
			
			TextureOverrideName.Reset();
		}

		bool IsValid() const
		{
			return Format != PF_Unknown;
		}

		int32 GetSizeX(int32 MipIndex) const
		{
			return FMath::Max(1, SizeX >> MipIndex);
		}

		int32 GetSizeY(int32 MipIndex) const
		{
			return FMath::Max(1, SizeY >> MipIndex);
		}
	};
	static FTextureInfo DoubleBufferedTextureInfo[2];
	static uint8 TextureInfoReadIndex_RT = 0;
	static uint8 TextureInfoReadIndex_GT = 0;
	FTextureInfo& GetTextureInfo(bool bOnRenderThread)
	{
		return DoubleBufferedTextureInfo[bOnRenderThread ? TextureInfoReadIndex_RT : TextureInfoReadIndex_GT];
	}

	struct FTexturePreviewOptions
	{
		enum class ERequestedZoomLevel
		{
			None,
			OneToOne,
			Fit
		};

		int32 CurrentArraySlice		= 0;
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

		float CanvasScale = 1.f;
		FVector2f CanvasCenter = FVector2f(0.f, 0.f);
		FVector4f UVScaleAndOffset	 = FVector4f(1.f, 1.f, 0.f, 0.f);
		ERequestedZoomLevel RequestedZoomLevel = ERequestedZoomLevel::None;
		
		FLinearColor BackgroundColor = FLinearColor(0.f, 0.f, 0.f, 0.f);

		bool bRequestMinMaxTextureValues = false;

		FIntPoint CursorPosition = FIntPoint::ZeroValue;

		FIntPoint TextureInspectorCursorPosition = FIntPoint::ZeroValue;
		FIntVector4 TextureInspectorRect = FIntVector4::ZeroValue;

		// if valid, prefer this over the textures collected from GraphBuilder
		const FTextureResource* TextureResourceOverride = nullptr;

		TOptional<FUintVector4> LastSelectedPixelValue;

		void Reset()
		{
			CurrentArraySlice = 0;
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

			// reset range
			RangeMin = 0.;
			RangeValueMin = 0.;
			RangeMax = 1.;
			RangeValueMax = 1.;

			TextureResourceOverride = nullptr;

			LastSelectedPixelValue.Reset();
		}
	};
	static FTexturePreviewOptions TexturePreviewOptions;
	
	static TArray<FAnsiString> AvailableTextures;
	static TSharedPtr<FTextureCollectorSceneViewExtension> ViewExtension = nullptr;

	static TArray<TSharedPtr<FRHIGPUBufferReadback, ESPMode::NotThreadSafe>> QueuedReadbackBuffers;
	static TArray<TSharedPtr<FRHIGPUBufferReadback, ESPMode::NotThreadSafe>> AvailableReadbackBuffers;
	TSharedPtr<FRHIGPUBufferReadback, ESPMode::NotThreadSafe> GetReadbackBufferForWriting()
	{
		if (AvailableReadbackBuffers.Num() > 0)
		{
			QueuedReadbackBuffers.Add(AvailableReadbackBuffers.Pop());
		}
		else
		{
			QueuedReadbackBuffers.Add(MakeShared<FRHIGPUBufferReadback, ESPMode::NotThreadSafe>(TEXT("TexDisplay::CurrentPixelReadback")));
		}
		return QueuedReadbackBuffers.Last();
	}
	TSharedPtr<FRHIGPUBufferReadback, ESPMode::NotThreadSafe> GetReadbackBufferForReading()
	{
		TSharedPtr<FRHIGPUBufferReadback, ESPMode::NotThreadSafe> Buffer = nullptr;
		for (auto Itr = QueuedReadbackBuffers.CreateIterator(); Itr; ++Itr)
		{
			if ((*Itr)->IsReady())
			{
				Buffer = *Itr;
				AvailableReadbackBuffers.Add(*Itr);
				Itr.RemoveCurrent();
			}
		}
		return Buffer;
	}

	static void Initialize()
	{
		ViewExtension = FSceneViewExtensions::NewExtension<FTextureCollectorSceneViewExtension>();
		FCoreDelegates::OnEnginePreExit.AddLambda(
			[]()
			{
				ViewExtension.Reset();
				QueuedReadbackBuffers.Reset();
				AvailableReadbackBuffers.Reset();
			});
	}

	FRHITexture* GetTextureToDisplay(const FTexturePreviewOptions& InPreviewOptions)
	{
		if (TextureRHIOverride)
		{
			return TextureRHIOverride.GetReference();
		}

		FRHITexture* TextureToDisplay = nullptr;
		if (InPreviewOptions.TextureResourceOverride && InPreviewOptions.TextureResourceOverride->GetTextureReference())
		{
			TextureToDisplay = InPreviewOptions.TextureResourceOverride->TextureRHI;
		}
		else
		{
			TextureToDisplay = ViewExtension->TextureToDisplay ? ViewExtension->TextureToDisplay->GetRHI() : nullptr;
		}
		return TextureToDisplay;
	}

	static FVector2D GetMinMaxTextureValue(FRHICommandListImmediate& RHICmdList, const FTexturePreviewOptions& InPreviewOptions)
	{
		FRHITexture* TextureToDisplay = GetTextureToDisplay(InPreviewOptions);
		if (!TextureToDisplay)
		{
			return FVector2D(0.f, 1.f);
		}
		const FPooledRenderTargetDesc TextureDesc = Translate(TextureToDisplay->GetDesc());

		const FIntVector3 HistogramTextureExtents =
		{
			TextureDesc.Extent.X >> InPreviewOptions.CurrentMip,
			TextureDesc.Extent.Y >> InPreviewOptions.CurrentMip,
			TextureDesc.Depth
		};
		const uint64 HistogramTextureExtentMax = HistogramTextureExtents.GetMax();
		const uint64 BlockPixSize = FTextureDisplay_TileMinMaxCS::HGRAM_PIXELS_PER_TILE * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK;
		const uint64 BlockCount = FMath::Max(1ull, uint64(HistogramTextureExtentMax * HistogramTextureExtentMax) / (BlockPixSize * BlockPixSize));

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

				ETextureDimension TextureDimension = TextureToDisplay->GetDesc().Dimension;
				if (TextureDimension == ETextureDimension::TextureCube || TextureDimension == ETextureDimension::TextureCubeArray)
				{
					TextureDimension = ETextureDimension::Texture2DArray;
				}

				auto TextureSRVDesc = FRHIViewDesc::CreateTextureSRV()
					.SetDimension(TextureDimension)
					.SetMipRange(0, TextureDesc.NumMips)
					.SetArrayRange(0, TextureDesc.IsCubemap() ? TextureDesc.ArraySize * 6 : TextureDesc.ArraySize)
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
					RHICmdList.CreateShaderResourceView(TextureToDisplay, TextureSRVDesc),
					HistogramTextureExtents,
					InPreviewOptions.CurrentMip,
					InPreviewOptions.CurrentArraySlice);

				const int32 ThreadGroupCountX = FMath::DivideAndRoundUp(HistogramTextureExtents.X, FTextureDisplay_TileMinMaxCS::HGRAM_PIXELS_PER_TILE * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK);
				const int32 ThreadGroupCountY = FMath::DivideAndRoundUp(HistogramTextureExtents.Y, FTextureDisplay_TileMinMaxCS::HGRAM_PIXELS_PER_TILE * FTextureDisplay_TileMinMaxCS::HGRAM_TILES_PER_BLOCK);

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
					HistogramTextureExtents);

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
		FRHITexture* TextureToDisplay = GetTextureToDisplay(InPreviewOptions);
		if (!TextureToDisplay)
		{
			return;
		}
		const FPooledRenderTargetDesc TextureDesc = Translate(TextureToDisplay->GetDesc());

		ETexDisplay_ResourceType ResType;
		ETexDisplay_ShaderBaseType BaseType;
		PixelFormatUtils::DetermineShaderTypesForTexture(TextureDesc, InPreviewOptions.bDisplayStencil, ResType, BaseType);

		if (auto ReadBuffer = GetReadbackBufferForReading())
		{
			if (const uint8* SrcRawBuffer = (const uint8*)ReadBuffer->Lock(sizeof(FUintVector4)))
			{
				FTextureInfo& TextureInfo = GetTextureInfo(/*bOnRenderThread=*/true);
				FMemory::Memcpy(&TextureInfo.SelectedPixelValue, SrcRawBuffer, sizeof(FUintVector4));

				ReadBuffer->Unlock();
			}
		}

		auto WriteBuffer = ((InPreviewOptions.TextureInspectorRect.Z - InPreviewOptions.TextureInspectorRect.X) > 0) ? GetReadbackBufferForWriting() : nullptr;
		if (WriteBuffer)
		{
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

			ETextureDimension TextureDimension = TextureToDisplay->GetDesc().Dimension;
			if (TextureDimension == ETextureDimension::TextureCube || TextureDimension == ETextureDimension::TextureCubeArray)
			{
				TextureDimension = ETextureDimension::Texture2DArray;
			}

			auto TextureSRVDesc = FRHIViewDesc::CreateTextureSRV()
				.SetDimension(TextureDimension)
				.SetMipRange(0, TextureDesc.NumMips)
				.SetArrayRange(0, TextureDesc.IsCubemap() ? TextureDesc.ArraySize * 6 : TextureDesc.ArraySize)
				.SetFormat((IsStencilFormat(TextureDesc.Format) && InPreviewOptions.bDisplayStencil) ? PF_X24_G8 : TextureDesc.Format);

			const int32 HoveredTexCoordX = InPreviewOptions.TextureInspectorCursorPosition.X >> InPreviewOptions.CurrentMip;
			const int32 HoveredTexCoordY = InPreviewOptions.TextureInspectorCursorPosition.Y >> InPreviewOptions.CurrentMip;

			SetComputePipelineState(RHICmdList, ComputeShader.GetComputeShader());
			SetShaderParametersLegacyCS(
				RHICmdList, ComputeShader,
				RHICmdList.CreateUnorderedAccessView(GPixelValueDestBuffer->VertexBufferRHI, FRHIViewDesc::CreateBufferUAV().SetType(FRHIViewDesc::EBufferType::Typed).SetFormat(BufferFormat)),
				RHICmdList.CreateShaderResourceView(TextureToDisplay, TextureSRVDesc),
				FIntVector3(TextureDesc.Extent.X, TextureDesc.Extent.Y, TextureDesc.Depth),
				InPreviewOptions.CurrentMip,
				InPreviewOptions.CurrentArraySlice,
				FIntPoint(HoveredTexCoordX, HoveredTexCoordY));

			RHICmdList.DispatchComputeShader(1, 1, 1);

			WriteBuffer->EnqueueCopy(RHICmdList, GPixelValueDestBuffer->VertexBufferRHI, sizeof(FUintVector4));
		}
	}

	struct FTexturePreviewUserData
	{
		ImVec2 ViewportSize;
		ImVec2 ClipRectMin;
		ImVec2 ClipRectMax;
		FTexturePreviewOptions Options;
	};
	static void TexturePreviewCallback(void* immediate_command_list, void* user_data, size_t user_data_size)
	{
		if (!ensure(user_data && (sizeof(FTexturePreviewUserData) == user_data_size)))
		{
			return;
		}

		FRHICommandListImmediate& RHICmdList = *(FRHICommandListImmediate*)immediate_command_list;
		const FTexturePreviewUserData& PreviewParams = *(const FTexturePreviewUserData*)user_data;

		FRHITexture* TextureToDisplay = GetTextureToDisplay(PreviewParams.Options);
		if (!TextureToDisplay)
		{
			return;
		}
		const FPooledRenderTargetDesc TextureDesc = Translate(TextureToDisplay->GetDesc());

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

		ETextureDimension TextureDimension = TextureToDisplay->GetDesc().Dimension;
		if (TextureDimension == ETextureDimension::TextureCube || TextureDimension == ETextureDimension::TextureCubeArray)
		{
			TextureDimension = ETextureDimension::Texture2DArray;
		}

		auto TextureSRVDesc = FRHIViewDesc::CreateTextureSRV()
			.SetDimension(TextureDimension)
			.SetMipRange(0, TextureDesc.NumMips)
			.SetArrayRange(0, TextureDesc.IsCubemap() ? TextureDesc.ArraySize * 6 : TextureDesc.ArraySize)
			.SetFormat((IsStencilFormat(TextureDesc.Format) && PreviewParams.Options.bDisplayStencil) ? PF_X24_G8 : TextureDesc.Format);

		SetShaderParametersLegacyPS(
			RHICmdList, PixelShader,
			RHICmdList.CreateShaderResourceView(TextureToDisplay, TextureSRVDesc),
			TStaticSamplerState<SF_Point>::GetRHI(),
			FIntVector3(TextureDesc.Extent.X, TextureDesc.Extent.Y, TextureDesc.Depth),
			TextureVisShowFlags,
			PreviewParams.Options.CurrentMip,
			PreviewParams.Options.CurrentArraySlice,
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

		FTextureInfo& TextureInfo = GetTextureInfo(/*bOnRenderThread=*/true);
		TextureInfo.SizeX = TextureDesc.Extent.X;
		TextureInfo.SizeY = TextureDesc.Extent.Y;
		TextureInfo.SizeZ = TextureDesc.Is3DTexture() ? TextureDesc.Depth : 0;
		TextureInfo.Format = TextureDesc.Format;
		TextureInfo.NumMips = TextureDesc.NumMips;
		TextureInfo.bIsArray = TextureDesc.IsArray();
		TextureInfo.bIsCubemap = TextureDesc.IsCubemap();
		TextureInfo.TextureOverrideName = TextureOverrideName;
		
		if (TextureDesc.IsCubemap())
		{
			TextureInfo.ArraySize = TextureDesc.ArraySize * 6;
		}
		else if (TextureDesc.IsArray())
		{
			TextureInfo.ArraySize = TextureDesc.ArraySize;
		}
		else
		{
			TextureInfo.ArraySize = 1;
		}
	}

	static bool IsTextureOverrideValid()
	{
		return !GetTextureInfo(/*bOnRenderThread=*/false).TextureOverrideName.IsEmpty();
	}

	static bool DrawTextureList(FImGuiTickContext* Context, FAnsiString& InOutSelectedTextureName)
	{
		UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();

		static FImGuiTextFilter SearchFilter = FImGuiTextFilter::MakeWidget(64u);

		const float GlobalScale = ImGui::GetStyle().FontScaleMain;
		const FAnsiString PreviouslySelectedTextureName = InOutSelectedTextureName;
		
		const bool bIsUsingTextureOverride = IsTextureOverrideValid();

		ImGui::BeginDisabled(bIsUsingTextureOverride);

		ImGui::SetNextItemWidth(450.f * GlobalScale);
		ImGui::SetNextWindowSize(ImVec2(450.f * GlobalScale, 200.f * GlobalScale), ImGuiCond_Always);
		const bool bShowTextureList = ImGui::BeginCombo("##TextureList", InOutSelectedTextureName.IsEmpty() ? "Select a Texture..." : *InOutSelectedTextureName);
		const ImVec2 ComboBoxSize = ImGui::GetItemRectSize();
		if (bShowTextureList)
		{
			if (ImGui::BeginChild("TextureWidgetArea", ImGui::GetContentRegionAvail(), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse))
			{
				// section pinned at the top (doesn't scroll)
				if (ImGui::BeginChild("FilteringArea", ImVec2(ImGui::GetContentRegionAvail().x, 0.f), ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse))
				{
					SearchFilter.Draw(Context, "##Filter", "Search Textures", ImGui::GetContentRegionAvail().x, ImGui::IsWindowAppearing());

					if (ImGui::IsWindowAppearing())
					{
						AvailableTextures.Reset();

						FlushRenderingCommands();
						for (const FString& Texture : ViewExtension->AvailableTextures)
						{
							FAnsiString TextureName = TCHAR_TO_ANSI(*Texture);
							if (!TextureName.IsEmpty())
							{
								AvailableTextures.AddUnique(MoveTemp(TextureName));
							}
						}
					}
				}
				ImGui::EndChild();

				if (ImGui::BeginListBox("ListView", ImGui::GetContentRegionAvail()))
				{
					for (const auto& TextureName : AvailableTextures)
					{
						if (!SearchFilter.PassFilter(TextureName))
						{
							continue;
						}

						const bool bIsSelected = TextureName.Equals(PreviouslySelectedTextureName);
						if (ImGui::Selectable(*TextureName, bIsSelected))
						{
							InOutSelectedTextureName = TextureName;

							ImGui::CloseCurrentPopup();
							break;
						}

						if (ImGui::IsWindowAppearing() && bIsSelected)
						{
							ImGui::ScrollToItem();
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndListBox();
				}
			}
			ImGui::EndChild();

			ImGui::EndCombo();
		}

		ImGui::EndDisabled();

		// reset icon
		if (!bShowTextureList && !InOutSelectedTextureName.IsEmpty())
		{
			ImGui::SameLine();
			
			const FImGuiImageBindingParams ResetToDefaultIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.ResetToDefault"), ComboBoxSize.y);

			ImGui::PushStyleColor(ImGuiCol_Button, 0xBFFFFFFF);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0xFFFFFFFF);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0xFFFFFFFF);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));

			if (FImGui::TransparentImageButton("ResetTexture", ResetToDefaultIcon))
			{
				InOutSelectedTextureName.Reset();
				if (bIsUsingTextureOverride)
				{
					ClearTextureOverride_GameThread();
				}
			}
			ImGui::SetItemTooltip(bIsUsingTextureOverride ? "Clear Texture Override" : "Reset");
			
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(3);
		}

		return !InOutSelectedTextureName.Equals(PreviouslySelectedTextureName);
	}

	static void DrawTextureControls(FImGuiTickContext* Context, const FTextureInfo& InTextureInfo, FTexturePreviewOptions& InOutTexturePreviewOptions)
	{
		UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();

		const float GlobalScale = ImGui::GetStyle().FontScaleMain;
		const float ControlPadding = 10.f * GlobalScale;
		const float ScrollInput = FMath::Abs(ImGui::GetIO().MouseWheel) > KINDA_SMALL_NUMBER ? (ImGui::GetIO().MouseWheel > 0.f ? -1.f : 1.f) : 0.f;

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
			if (ImGui::BeginCombo("##Zoom", *FAnsiString::Printf("%i%%", int32(InOutTexturePreviewOptions.CanvasScale * 100.f)), ImGuiComboFlags_None))
			{
				static const int32 AvailableZoomLevels[] = { 10, 25, 75, 50, 100, 200, 400, 800 };
				for (int32 Index = 0; Index < UE_ARRAY_COUNT(AvailableZoomLevels); Index++)
				{
					if (ImGui::Selectable(*FAnsiString::Printf("%i%%", AvailableZoomLevels[Index])))
					{
						InOutTexturePreviewOptions.CanvasScale = (float)AvailableZoomLevels[Index] / 100.f;
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
			InOutTexturePreviewOptions.CurrentMip = FMath::Clamp(InOutTexturePreviewOptions.CurrentMip, 0, InTextureInfo.NumMips - 1);

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

			if (ImGui::IsItemHovered() && (InTextureInfo.NumMips > 1))
			{
				InOutTexturePreviewOptions.CurrentMip = FMath::Clamp(InOutTexturePreviewOptions.CurrentMip + ScrollInput, 0, (int32)InTextureInfo.NumMips - 1);
			}
		}

		ImGui::SameLine();
		ImGui::Dummy(ImVec2(ControlPadding, 0));
		ImGui::SameLine();

		// texture slice/face controls
		{
			const int32 ArraySize = FMath::Max(InTextureInfo.ArraySize, InTextureInfo.SizeZ);
			InOutTexturePreviewOptions.CurrentArraySlice = FMath::Clamp(InOutTexturePreviewOptions.CurrentArraySlice, 0, ArraySize - 1);

			if (InTextureInfo.bIsCubemap)
			{
				static const char* FaceNames[6] = { "X+", "X-", "Y+", "Y-", "Z+", "Z-" };

				ImGui::Text("Face"); ImGui::SameLine(); ImGui::SetNextItemWidth(128.f * GlobalScale);
				if (ImGui::BeginCombo("##Face", *FAnsiString::Printf("%s", FaceNames[InOutTexturePreviewOptions.CurrentArraySlice % 6]), ImGuiComboFlags_None))
				{
					for (int32 FaceIndex = 0; FaceIndex < ArraySize; FaceIndex++)
					{
						const bool bIsSelected = (InOutTexturePreviewOptions.CurrentArraySlice == FaceIndex);
						if (ImGui::Selectable(FaceNames[FaceIndex % 6], bIsSelected))
						{
							InOutTexturePreviewOptions.CurrentArraySlice = FaceIndex;
						}
						if (bIsSelected)
						{
							ImGui::SetItemDefaultFocus();
						}
					}
					ImGui::EndCombo();
				}
			}
			else
			{
				ImGui::Text("Slice"); ImGui::SameLine(); ImGui::SetNextItemWidth(128.f * GlobalScale);
				ImGui::BeginDisabled(ArraySize <= 1);
				if (ImGui::BeginCombo("##Slice", *FAnsiString::Printf("Slice %i", InOutTexturePreviewOptions.CurrentArraySlice), ImGuiComboFlags_None))
				{
					for (int32 SliceIndex = 0; SliceIndex < ArraySize; SliceIndex++)
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
			}

			if ((ArraySize > 1) && ImGui::IsItemHovered())
			{
				InOutTexturePreviewOptions.CurrentArraySlice = FMath::Clamp(InOutTexturePreviewOptions.CurrentArraySlice + ScrollInput, 0, ArraySize - 1);
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
				const FImGuiImageBindingParams CheckerboardIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.CheckerPattern"), CurrentLineHeight);

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
				const FImGuiImageBindingParams ZoomToTextureIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.Find"), CurrentLineHeight);

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
				const FImGuiImageBindingParams ZoomToFitIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.FrameSelected"), CurrentLineHeight);

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
				const FImGuiImageBindingParams ResetToDefaultIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.ResetToDefault"), CurrentLineHeight);

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
		FImGuiTickContext* Context,
		ImVec2 InCanvasSize,
		const FTextureInfo& InTextureInfo,
		FTexturePreviewOptions& InOutTexturePreviewOptions)
	{
		ImVec2 ConstrainedCanvasSize = ConstrainCanvasToAspectRatio(InTextureInfo.SizeX, InTextureInfo.SizeY, InCanvasSize);
		if (InTextureInfo.IsValid())
		{
			if (InOutTexturePreviewOptions.RequestedZoomLevel == FTexturePreviewOptions::ERequestedZoomLevel::OneToOne)
			{
				InOutTexturePreviewOptions.CanvasScale = 1.f;
				InOutTexturePreviewOptions.CanvasCenter = FVector2f(0.f, 0.f);
			}
			else if (InOutTexturePreviewOptions.RequestedZoomLevel == FTexturePreviewOptions::ERequestedZoomLevel::Fit)
			{
				InOutTexturePreviewOptions.CanvasScale = FMath::Min(256.f, ConstrainedCanvasSize.x / (float)InTextureInfo.SizeX);
				InOutTexturePreviewOptions.CanvasCenter = FVector2f(0.f, 0.f);
			}

			InOutTexturePreviewOptions.RequestedZoomLevel = FTexturePreviewOptions::ERequestedZoomLevel::None;
		}
		ConstrainedCanvasSize = ImVec2(InTextureInfo.SizeX, InTextureInfo.SizeY) * (InOutTexturePreviewOptions.CanvasScale);

		InOutTexturePreviewOptions.TextureInspectorRect = FIntVector4::ZeroValue;
		
		ImGui::InvisibleButton("TextureCanvas", InCanvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

		const bool bIsCanvasHovered = ImGui::IsItemHovered();
		const bool bIsCanvasClicked = ImGui::IsItemActive();
		if (bIsCanvasHovered && (FMath::Abs(ImGui::GetIO().MouseWheel) > KINDA_SMALL_NUMBER))
		{
			const float Zoom = (ImGui::GetIO().MouseWheel > 0.f ? 1.1f : 0.9f);

			ImVec2 RelativeMousePos = (Context->ImGuiContext->MouseLastValidPos - ImGui::GetItemRectMin());
			FVector2f ZoomPivot =
			{
				FMath::Clamp(RelativeMousePos.x, 0.f, ImGui::GetItemRectSize().x),
				FMath::Clamp(RelativeMousePos.y, 0.f, ImGui::GetItemRectSize().y)
			};
			ZoomPivot = (ZoomPivot - InOutTexturePreviewOptions.CanvasCenter) / (InOutTexturePreviewOptions.CanvasScale);

			InOutTexturePreviewOptions.CanvasCenter += ZoomPivot * (InOutTexturePreviewOptions.CanvasScale);

			InOutTexturePreviewOptions.CanvasScale *= Zoom;
			InOutTexturePreviewOptions.CanvasScale = FMath::Clamp(InOutTexturePreviewOptions.CanvasScale, 0.1f, 256.f);

			InOutTexturePreviewOptions.CanvasCenter -= ZoomPivot * (InOutTexturePreviewOptions.CanvasScale);
			
			ConstrainedCanvasSize = ImVec2(InTextureInfo.SizeX, InTextureInfo.SizeY) * (InOutTexturePreviewOptions.CanvasScale);
		}
		if (bIsCanvasClicked && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f))
		{
			InOutTexturePreviewOptions.CanvasCenter.X += ImGui::GetIO().MouseDelta.x;
			InOutTexturePreviewOptions.CanvasCenter.Y += ImGui::GetIO().MouseDelta.y;
		}

		// clamp scrolling to widget borders
		{
			if (ConstrainedCanvasSize.x <= InCanvasSize.x)
			{
				InOutTexturePreviewOptions.CanvasCenter.X = 0.f;
			}
			if (ConstrainedCanvasSize.y <= InCanvasSize.y)
			{
				InOutTexturePreviewOptions.CanvasCenter.Y = 0.f;
			}

			const ImRect TranslatedCanvas = ImRect(
				ImVec2(InOutTexturePreviewOptions.CanvasCenter.X, InOutTexturePreviewOptions.CanvasCenter.Y),
				ConstrainedCanvasSize + ImVec2(InOutTexturePreviewOptions.CanvasCenter.X, InOutTexturePreviewOptions.CanvasCenter.Y));

			if (ConstrainedCanvasSize.x > InCanvasSize.x)
			{
				if (TranslatedCanvas.Min.x > 0)
				{
					InOutTexturePreviewOptions.CanvasCenter.X = 0.f;
				}
				else if (TranslatedCanvas.Max.x < InCanvasSize.x)
				{
					InOutTexturePreviewOptions.CanvasCenter.X = InCanvasSize.x - ConstrainedCanvasSize.x;
				}
			}

			if (ConstrainedCanvasSize.y > InCanvasSize.y)
			{
				if (TranslatedCanvas.Min.y > 0)
				{
					InOutTexturePreviewOptions.CanvasCenter.Y = 0.f;
				}
				else if (TranslatedCanvas.Max.y < InCanvasSize.y)
				{
					InOutTexturePreviewOptions.CanvasCenter.Y = InCanvasSize.y - ConstrainedCanvasSize.y;
				}
			}
		}

		InOutTexturePreviewOptions.UVScaleAndOffset =
		FVector4f
		{
			ImGui::GetItemRectSize().x / ConstrainedCanvasSize.x,
			ImGui::GetItemRectSize().y / ConstrainedCanvasSize.y,
			-InOutTexturePreviewOptions.CanvasCenter.X / ConstrainedCanvasSize.x,
			-InOutTexturePreviewOptions.CanvasCenter.Y / ConstrainedCanvasSize.y
		};

		{
			ImVec2 RelativeMousePos = (Context->ImGuiContext->MouseLastValidPos - ImGui::GetItemRectMin());
			RelativeMousePos.x = FMath::Clamp(RelativeMousePos.x, 0.f, ImGui::GetItemRectSize().x);
			RelativeMousePos.y = FMath::Clamp(RelativeMousePos.y, 0.f, ImGui::GetItemRectSize().y);

			FVector2f Scale = FVector2f(InTextureInfo.SizeX, InTextureInfo.SizeY) / FVector2f(ConstrainedCanvasSize.x, ConstrainedCanvasSize.y);
			FVector2f CursorPos = (FVector2f(RelativeMousePos.x, RelativeMousePos.y) - InOutTexturePreviewOptions.CanvasCenter) * Scale;
			InOutTexturePreviewOptions.CursorPosition = FIntPoint(CursorPos.X, CursorPos.Y);

			if (bIsCanvasClicked && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.f))
			{
				InOutTexturePreviewOptions.LastSelectedPixelValue = InTextureInfo.SelectedPixelValue;

				CursorPos.X = FMath::Clamp(CursorPos.X, 0, InTextureInfo.GetSizeX(0) - 1);
				CursorPos.Y = FMath::Clamp(CursorPos.Y, 0, InTextureInfo.GetSizeY(0) - 1);

				InOutTexturePreviewOptions.TextureInspectorCursorPosition = FIntPoint(CursorPos.X, CursorPos.Y);

				const float TextureInspectorSize = 144.f;
				const float TextureInspectorInfoWidgetSize = 200.f;
				const float AvailableSpaceLeft = RelativeMousePos.x;
				const float AvailableSpaceRight = ImGui::GetItemRectSize().x - RelativeMousePos.x;
				const float AvailableSpaceTop = RelativeMousePos.y;
				const float AvailableSpaceBottom = ImGui::GetItemRectSize().y - RelativeMousePos.y;

				ImVec2 AbsoluteMousePos = RelativeMousePos + ImGui::GetItemRectMin();

				ImVec2 TextureInspectorOffset = ImVec2(32.f, -TextureInspectorSize);
				float TextureInspectorInfoWidgetOffsetX = TextureInspectorSize;
				if (AvailableSpaceTop < TextureInspectorSize)
				{
					TextureInspectorOffset.y += (TextureInspectorSize - AvailableSpaceTop);
				}
				if (AvailableSpaceRight < (TextureInspectorSize + TextureInspectorInfoWidgetSize))
				{
					TextureInspectorOffset.x -= ((TextureInspectorSize + TextureInspectorInfoWidgetSize) - AvailableSpaceRight);
				}

				InOutTexturePreviewOptions.TextureInspectorRect.X = AbsoluteMousePos.x + TextureInspectorOffset.x;
				InOutTexturePreviewOptions.TextureInspectorRect.Y = AbsoluteMousePos.y + TextureInspectorOffset.y;
				InOutTexturePreviewOptions.TextureInspectorRect.Z = AbsoluteMousePos.x + TextureInspectorOffset.x + TextureInspectorSize;
				InOutTexturePreviewOptions.TextureInspectorRect.W = AbsoluteMousePos.y + TextureInspectorOffset.y + TextureInspectorSize;

				const int32 HoveredTexCoordX = InOutTexturePreviewOptions.TextureInspectorCursorPosition.X >> InOutTexturePreviewOptions.CurrentMip;
				const int32 HoveredTexCoordY = InOutTexturePreviewOptions.TextureInspectorCursorPosition.Y >> InOutTexturePreviewOptions.CurrentMip;

				ImGui::SetNextWindowPos(ImVec2(InOutTexturePreviewOptions.TextureInspectorRect.X + TextureInspectorInfoWidgetOffsetX, InOutTexturePreviewOptions.TextureInspectorRect.Y), ImGuiCond_Always);
				ImGui::SetNextWindowSize(ImVec2(TextureInspectorInfoWidgetSize, TextureInspectorSize), ImGuiCond_Always);
				if (ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePrevious, ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoInputs))
				{
					ImGui::Text("UV: %.4f %.4f",
						(float)HoveredTexCoordX / (float)InTextureInfo.GetSizeX(InOutTexturePreviewOptions.CurrentMip),
						(float)HoveredTexCoordY / (float)InTextureInfo.GetSizeY(InOutTexturePreviewOptions.CurrentMip));
					ImGui::Text("Coord: %i %i", HoveredTexCoordX, HoveredTexCoordY);
					ImGui::Separator();
					ImGui::TextUnformatted(*PixelFormatUtils::GetPixelValueAsString((uint8*)&InTextureInfo.SelectedPixelValue, InTextureInfo.Format, InOutTexturePreviewOptions.bDisplayStencil));
					ImGui::EndTooltip();
				}

				// NOTE: adjust for viewport offset
				ImVec2 WindowPos = ImGui::GetWindowPos();
				InOutTexturePreviewOptions.TextureInspectorRect -= FIntVector4(WindowPos.x, WindowPos.y, WindowPos.x, WindowPos.y);
			}
		}
	}

	static void Tick(FImGuiTickContext* Context)
	{
		FImGuiTickScope Scope{ Context };

		// TODO: Find a better/reliable way to check if docknode is already active
#if WITH_EDITOR //dockspace already created if not using standalone widgets
		ImGuiDockNodeFlags DockingFlags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoTabBar;
		const ImGuiID MainDockSpaceID = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), DockingFlags);
		ImGui::SetNextWindowDockID(MainDockSpaceID, ImGuiCond_Always);
#endif

		if (ImGui::Begin("TextureVisualizer", nullptr, ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoScrollbar))
		{
			static FAnsiString VisTextureName;
			static bool bWasTextureOverrideValid = false;

			bool bRequestNewTexture = false;
			if (IsTextureOverrideValid())
			{
				bWasTextureOverrideValid = true;
				if (!VisTextureName.Equals(GetTextureInfo(/*bOnRenderThread=*/false).TextureOverrideName))
				{
					VisTextureName = GetTextureInfo(/*bOnRenderThread=*/false).TextureOverrideName;
					TexturePreviewOptions.Reset();

					// NOTE: not resetting texture info here as the data might be coming from the Render thread
				}
			}
			else if (bWasTextureOverrideValid)
			{
				ClearTextureOverride_GameThread();

				bWasTextureOverrideValid = false;
				VisTextureName.Reset();
				bRequestNewTexture = true;
			}

			bRequestNewTexture |= DrawTextureList(Context, VisTextureName);
			if (bRequestNewTexture)
			{
				FlushRenderingCommands();

				GetTextureInfo(/*bOnRenderThread=*/false).Reset();
				GetTextureInfo(/*bOnRenderThread=*/true).Reset();
				TexturePreviewOptions.Reset();
			}

			const FTextureInfo& TextureInfo = GetTextureInfo(/*bOnRenderThread=*/false);

			ImGui::Separator();

			auto PrevPreviewOptions = TexturePreviewOptions;
			DrawTextureControls(Context, TextureInfo, TexturePreviewOptions);

			ImGui::Separator();

			const int32 TextureDetailsWidgetHeight = 50.f * ImGui::GetStyle().FontScaleMain;

			const ImVec2 TextureCanvasSize = ImGui::GetContentRegionAvail() - ImVec2(0.f, TextureDetailsWidgetHeight);
			if (!VisTextureName.IsEmpty() && TextureInfo.Format != PF_Unknown)
			{
				DrawTextureCanvas(Context, TextureCanvasSize, TextureInfo, TexturePreviewOptions);
			}

			TexturePreviewOptions.TextureResourceOverride = nullptr;
			if (TextureOverride.IsValid())
			{
				TexturePreviewOptions.TextureResourceOverride = TextureOverride->GetResource();
			}
			else if (TextureResourceOverride)
			{
				TexturePreviewOptions.TextureResourceOverride = TextureResourceOverride;
			}

			FTexturePreviewUserData Params;
			Params.ViewportSize = ImGui::GetIO().DisplaySize;
			Params.ClipRectMin = ImGui::GetItemRectMin() - ImGui::GetWindowPos();
			Params.ClipRectMax = ImGui::GetItemRectMax() - ImGui::GetWindowPos();
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
				const int32 HoveredTexCoordX = TexturePreviewOptions.CursorPosition.X >> TexturePreviewOptions.CurrentMip;
				const int32 HoveredTexCoordY = TexturePreviewOptions.CursorPosition.Y >> TexturePreviewOptions.CurrentMip;

				if (TextureInfo.SizeZ > 0)
				{
					FAnsiString FormatName = TCHAR_TO_ANSI(GPixelFormats[TextureInfo.Format].Name);
					ImGui::Text("%s - %ix%ix%i %i mips - %s , Hovered Pixel - %i, %i (%f, %f)",
						*VisTextureName, TextureInfo.SizeX, TextureInfo.SizeY, TextureInfo.SizeZ, TextureInfo.NumMips, *FormatName,
						HoveredTexCoordX,
						HoveredTexCoordY,
						(float)HoveredTexCoordX / (float)TextureInfo.GetSizeX(TexturePreviewOptions.CurrentMip),
						(float)HoveredTexCoordY / (float)TextureInfo.GetSizeY(TexturePreviewOptions.CurrentMip));
				}
				else
				{
					FAnsiString FormatName = TCHAR_TO_ANSI(GPixelFormats[TextureInfo.Format].Name);
					ImGui::Text("%s - %ix%i %i mips - %s , Hovered Pixel - %i, %i (%f, %f)",
						*VisTextureName, TextureInfo.SizeX, TextureInfo.SizeY, TextureInfo.NumMips, *FormatName,
						HoveredTexCoordX,
						HoveredTexCoordY,
						(float)HoveredTexCoordX / (float)TextureInfo.GetSizeX(TexturePreviewOptions.CurrentMip),
						(float)HoveredTexCoordY / (float)TextureInfo.GetSizeY(TexturePreviewOptions.CurrentMip));
				}

				if (TexturePreviewOptions.LastSelectedPixelValue.IsSet())
				{
					ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = ImGui::GetStyle().FramePadding.y;
					ImGui::TextUnformatted("Last Selected Pixel");
					ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = 0.f;

					ImGui::SameLine();
					FAnsiString PixelValueAsString = PixelFormatUtils::GetPixelValueAsStringInline((uint8*)&TexturePreviewOptions.LastSelectedPixelValue.GetValue(), TextureInfo.Format, TexturePreviewOptions.bDisplayStencil);
					ImGui::InputText("##PixelValue", (ANSICHAR*)*PixelValueAsString, PixelValueAsString.Len() + 1, ImGuiInputTextFlags_ReadOnly);
				}
				else
				{
					ImGui::TextUnformatted("Right click to inspect pixel");
				}
			}

			TextureInfoReadIndex_GT = (TextureInfoReadIndex_GT + 1) % 2;
			ENQUEUE_RENDER_COMMAND(ImGuiTexDisplay_Flip)(
				[bRequestNewTexture, PreviewOptions=TexturePreviewOptions, TextureInfoReadIndex=((TextureInfoReadIndex_GT + 1) % 2)](FRHICommandListImmediate& RHICmdList)
				{
					SCOPED_DRAW_EVENT(RHICmdList, ImGuiTexDisplay_Flip);

					TextureInfoReadIndex_RT = TextureInfoReadIndex;
					
					if (bRequestNewTexture)
					{
						ViewExtension->SetTextureNameToDisplay(ANSI_TO_TCHAR(*VisTextureName));
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
		.WidgetDescription = "Tool for visualizing textures."
	};
	IMGUI_REGISTER_STANDALONE_WIDGET(Params);
}

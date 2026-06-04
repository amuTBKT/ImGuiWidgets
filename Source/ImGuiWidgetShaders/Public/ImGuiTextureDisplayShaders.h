// Copyright 2026 Amit Kumar Mehar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ShaderParameterStruct.h"
#include "ShaderParameterUtils.h"
#include "GlobalShader.h"
#include "Shader.h"

enum class ETexDisplay_ShowFlag
{
	RedChannelMask		= (1u << 0),
	GreenChannelMask	= (1u << 1),
	BlueChannelMask		= (1u << 2),
	AlphaChannelMask	= (1u << 3),
	DepthChannelMask	= (1u << 4),
	StencilChannelMask	= (1u << 5),
	sRGBMask			= (1u << 6),
};

enum class ETexDisplay_ResourceType
{
	Tex1D,
	Tex2D,
	Tex3D,
	Depth,
	Stencil,
	DepthMS,
	StencilMS,
	Tex2DMS,
	MAX
};

enum class ETexDisplay_ShaderBaseType
{
	UInt,
	SInt,
	Float,
	MAX
};

class IMGUIWIDGETSHADERS_API FTextureDisplayPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FTextureDisplayPS, Global);

	class FResourceType		: SHADER_PERMUTATION_ENUM_CLASS("SHADER_RESTYPE", ETexDisplay_ResourceType);
	class FShaderBaseType	: SHADER_PERMUTATION_ENUM_CLASS("SHADER_BASETYPE", ETexDisplay_ShaderBaseType);
	using FPermutationDomain = TShaderPermutationDomain<FResourceType, FShaderBaseType>;

public:
	FTextureDisplayPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		TextureParam.Bind(Initializer.ParameterMap, TEXT("Texture"));
		TextureSamplerParam.Bind(Initializer.ParameterMap, TEXT("TextureSampler"));

		ShowFlagsParam.Bind(Initializer.ParameterMap, TEXT("ShowFlags"));
		TextureExtentsParam.Bind(Initializer.ParameterMap, TEXT("TextureExtents"));
		BackgroundColorParam.Bind(Initializer.ParameterMap, TEXT("BackgroundColor"));
		UVScaleAndOffsetParam.Bind(Initializer.ParameterMap, TEXT("UVScaleAndOffset"));
		TextureInspectorRectParam.Bind(Initializer.ParameterMap, TEXT("TextureInspectorRect"));
		TextureInspectorCursorParam.Bind(Initializer.ParameterMap, TEXT("TextureInspectorCursor"));
		CursorInfoParam.Bind(Initializer.ParameterMap, TEXT("CursorInfo"));
		RangeMin_InvRangeSizeParam.Bind(Initializer.ParameterMap, TEXT("RangeMin_InvRangeSize"));
		CurrentMip_ArraySliceOrDepthParam.Bind(Initializer.ParameterMap, TEXT("CurrentMip_ArraySliceOrDepth"));
	}
	FTextureDisplayPS() {}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		const auto ResType = PermutationVector.Get<FResourceType>();
		const auto ShaderBaseType = PermutationVector.Get<FShaderBaseType>();

		if (ResType == ETexDisplay_ResourceType::Stencil || ResType == ETexDisplay_ResourceType::StencilMS)
		{
			return ShaderBaseType == ETexDisplay_ShaderBaseType::UInt;
		}
		else if (ResType == ETexDisplay_ResourceType::Depth || ResType == ETexDisplay_ResourceType::DepthMS)
		{
			return ShaderBaseType == ETexDisplay_ShaderBaseType::Float;
		}

		return true;
	}

	void SetParameters(
		FRHIBatchedShaderParameters& BatchedParameters,
		FRHIShaderResourceView* TextureSRV,
		FRHISamplerState* SamplerState,
		FIntVector3 TextureExtents,
		uint32 ShowFlags,
		uint8 MipToDisplay,
		uint32 ArraySliceOrDepthToDisplay,
		FVector2f RangeMin_InvRangeSize,
		FVector4f UVScaleAndOffset,
		uint32 BackgroundColor,
		FIntVector4 CursorInfo,
		FIntPoint TextureInspectorCursor,
		FIntVector4 TextureInspectorRect)
	{
		if (TextureParam.IsBound())
		{
			SetSRVParameter(BatchedParameters, TextureParam, TextureSRV);
		}
		if (TextureSamplerParam.IsBound())
		{
			SetSamplerParameter(BatchedParameters, TextureSamplerParam, SamplerState);
		}

		SetShaderValue(BatchedParameters, ShowFlagsParam, ShowFlags);
		SetShaderValue(BatchedParameters, TextureExtentsParam, TextureExtents);
		SetShaderValue(BatchedParameters, BackgroundColorParam, BackgroundColor);
		SetShaderValue(BatchedParameters, UVScaleAndOffsetParam, UVScaleAndOffset);
		SetShaderValue(BatchedParameters, TextureInspectorRectParam, TextureInspectorRect);
		SetShaderValue(BatchedParameters, TextureInspectorCursorParam, TextureInspectorCursor);
		SetShaderValue(BatchedParameters, CursorInfoParam, CursorInfo);
		SetShaderValue(BatchedParameters, RangeMin_InvRangeSizeParam, RangeMin_InvRangeSize);
		SetShaderValue(BatchedParameters, CurrentMip_ArraySliceOrDepthParam, (uint32(MipToDisplay) << 24) | ArraySliceOrDepthToDisplay);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, TextureParam);
	LAYOUT_FIELD(FShaderResourceParameter, TextureSamplerParam);

	LAYOUT_FIELD(FShaderParameter, ShowFlagsParam);
	LAYOUT_FIELD(FShaderParameter, TextureExtentsParam);
	LAYOUT_FIELD(FShaderParameter, BackgroundColorParam);
	LAYOUT_FIELD(FShaderParameter, UVScaleAndOffsetParam);
	LAYOUT_FIELD(FShaderParameter, TextureInspectorRectParam);
	LAYOUT_FIELD(FShaderParameter, TextureInspectorCursorParam);
	LAYOUT_FIELD(FShaderParameter, CursorInfoParam);
	LAYOUT_FIELD(FShaderParameter, RangeMin_InvRangeSizeParam);
	LAYOUT_FIELD(FShaderParameter, CurrentMip_ArraySliceOrDepthParam);
};

class IMGUIWIDGETSHADERS_API FTextureDisplay_CopyPixelValueCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FTextureDisplay_CopyPixelValueCS, Global);

	class FResourceType : SHADER_PERMUTATION_ENUM_CLASS("SHADER_RESTYPE", ETexDisplay_ResourceType);
	class FShaderBaseType : SHADER_PERMUTATION_ENUM_CLASS("SHADER_BASETYPE", ETexDisplay_ShaderBaseType);
	using FPermutationDomain = TShaderPermutationDomain<FResourceType, FShaderBaseType>;

public:
	FTextureDisplay_CopyPixelValueCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		DestBufferParam.Bind(Initializer.ParameterMap, TEXT("DestBuffer"));

		TextureParam.Bind(Initializer.ParameterMap, TEXT("Texture"));
		TextureExtentsParam.Bind(Initializer.ParameterMap, TEXT("TextureExtents"));
		TextureCoordsParam.Bind(Initializer.ParameterMap, TEXT("TextureCoords"));
		CurrentMip_ArraySliceOrDepthParam.Bind(Initializer.ParameterMap, TEXT("CurrentMip_ArraySliceOrDepth"));
	}
	FTextureDisplay_CopyPixelValueCS() {}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		const auto ResType = PermutationVector.Get<FResourceType>();
		const auto ShaderBaseType = PermutationVector.Get<FShaderBaseType>();

		if (ResType == ETexDisplay_ResourceType::Stencil || ResType == ETexDisplay_ResourceType::StencilMS)
		{
			return ShaderBaseType == ETexDisplay_ShaderBaseType::UInt;
		}
		else if (ResType == ETexDisplay_ResourceType::Depth || ResType == ETexDisplay_ResourceType::DepthMS)
		{
			return ShaderBaseType == ETexDisplay_ShaderBaseType::Float;
		}

		return true;
	}

	void SetParameters(
		FRHIBatchedShaderParameters& BatchedParameters,
		FRHIUnorderedAccessView* DestBufferUAV,
		FRHIShaderResourceView* TextureSRV,
		FIntVector3 TextureExtents,
		uint8 MipToDisplay,
		uint32 ArraySliceOrDepthToDisplay,
		FIntPoint TextureCoords)
	{
		if (DestBufferParam.IsBound())
		{
			SetUAVParameter(BatchedParameters, DestBufferParam, DestBufferUAV);
		}
		if (TextureParam.IsBound())
		{
			SetSRVParameter(BatchedParameters, TextureParam, TextureSRV);
		}

		SetShaderValue(BatchedParameters, TextureExtentsParam, TextureExtents);
		SetShaderValue(BatchedParameters, TextureCoordsParam, TextureCoords);
		SetShaderValue(BatchedParameters, CurrentMip_ArraySliceOrDepthParam, (uint32(MipToDisplay) << 24) | ArraySliceOrDepthToDisplay);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, DestBufferParam);
	LAYOUT_FIELD(FShaderResourceParameter, TextureParam);

	LAYOUT_FIELD(FShaderParameter, TextureExtentsParam);
	LAYOUT_FIELD(FShaderParameter, TextureCoordsParam);
	LAYOUT_FIELD(FShaderParameter, CurrentMip_ArraySliceOrDepthParam);
};

class IMGUIWIDGETSHADERS_API FTextureDisplay_TileMinMaxCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FTextureDisplay_TileMinMaxCS, Global);

	class FResourceType : SHADER_PERMUTATION_ENUM_CLASS("SHADER_RESTYPE", ETexDisplay_ResourceType);
	class FShaderBaseType : SHADER_PERMUTATION_ENUM_CLASS("SHADER_BASETYPE", ETexDisplay_ShaderBaseType);
	using FPermutationDomain = TShaderPermutationDomain<FResourceType, FShaderBaseType>;

public:
	FTextureDisplay_TileMinMaxCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		MinMaxDestParam.Bind(Initializer.ParameterMap, TEXT("MinMaxDest"));
		
		TextureParam.Bind(Initializer.ParameterMap, TEXT("Texture"));		
		TextureExtentsParam.Bind(Initializer.ParameterMap, TEXT("TextureExtents"));
		CurrentMip_ArraySliceOrDepthParam.Bind(Initializer.ParameterMap, TEXT("CurrentMip_ArraySliceOrDepth"));
	}
	FTextureDisplay_TileMinMaxCS() {}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		const auto ResType = PermutationVector.Get<FResourceType>();
		const auto ShaderBaseType = PermutationVector.Get<FShaderBaseType>();

		if (ResType == ETexDisplay_ResourceType::Stencil || ResType == ETexDisplay_ResourceType::StencilMS)
		{
			return ShaderBaseType == ETexDisplay_ShaderBaseType::UInt;
		}
		else if (ResType == ETexDisplay_ResourceType::Depth || ResType == ETexDisplay_ResourceType::DepthMS)
		{
			return ShaderBaseType == ETexDisplay_ShaderBaseType::Float;
		}

		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("HGRAM_PIXELS_PER_TILE"), HGRAM_PIXELS_PER_TILE);
		OutEnvironment.SetDefine(TEXT("HGRAM_TILES_PER_BLOCK"), HGRAM_TILES_PER_BLOCK);
	}

	void SetParameters(
		FRHIBatchedShaderParameters& BatchedParameters,
		FRHIUnorderedAccessView* MinMaxDestUAV,
		FRHIShaderResourceView* TextureSRV,
		FIntVector3 TextureExtents,
		uint8 MipToDisplay,
		uint32 ArraySliceOrDepthToDisplay)
	{
		if (MinMaxDestParam.IsBound())
		{
			SetUAVParameter(BatchedParameters, MinMaxDestParam, MinMaxDestUAV);
		}
		if (TextureParam.IsBound())
		{
			SetSRVParameter(BatchedParameters, TextureParam, TextureSRV);
		}

		SetShaderValue(BatchedParameters, TextureExtentsParam, TextureExtents);
		SetShaderValue(BatchedParameters, CurrentMip_ArraySliceOrDepthParam, (uint32(MipToDisplay) << 24) | ArraySliceOrDepthToDisplay);
	}

public:
	static const inline int32 HGRAM_PIXELS_PER_TILE = 64;
	static const inline int32 HGRAM_TILES_PER_BLOCK = 10;

private:
	LAYOUT_FIELD(FShaderResourceParameter, MinMaxDestParam);

	LAYOUT_FIELD(FShaderResourceParameter, TextureParam);
	LAYOUT_FIELD(FShaderParameter, TextureExtentsParam);
	LAYOUT_FIELD(FShaderParameter, CurrentMip_ArraySliceOrDepthParam);
};

class IMGUIWIDGETSHADERS_API FTextureDisplay_ResultMinMaxCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FTextureDisplay_ResultMinMaxCS, Global);

	class FShaderBaseType : SHADER_PERMUTATION_ENUM_CLASS("SHADER_BASETYPE", ETexDisplay_ShaderBaseType);
	using FPermutationDomain = TShaderPermutationDomain<FShaderBaseType>;

public:
	FTextureDisplay_ResultMinMaxCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		MinMaxResultSourceParam.Bind(Initializer.ParameterMap, TEXT("MinMaxResultSource"));
		MinMaxResultDestParam.Bind(Initializer.ParameterMap, TEXT("MinMaxResultDest"));
		HistogramTextureResolutionParam.Bind(Initializer.ParameterMap, TEXT("HistogramTextureResolution"));
	}
	FTextureDisplay_ResultMinMaxCS()
	{
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("HGRAM_PIXELS_PER_TILE"), HGRAM_PIXELS_PER_TILE);
		OutEnvironment.SetDefine(TEXT("HGRAM_TILES_PER_BLOCK"), HGRAM_TILES_PER_BLOCK);
	}

	void SetParameters(
		FRHIBatchedShaderParameters& BatchedParameters,
		FRHIUnorderedAccessView* MinMaxResultDest,
		FRHIShaderResourceView* MinMaxResultSource,
		FIntVector3 HistogramTextureExtents)
	{
		if (MinMaxResultDestParam.IsBound())
		{
			SetUAVParameter(BatchedParameters, MinMaxResultDestParam, MinMaxResultDest);
		}
		if (MinMaxResultSourceParam.IsBound())
		{
			SetSRVParameter(BatchedParameters, MinMaxResultSourceParam, MinMaxResultSource);
		}
		SetShaderValue(BatchedParameters, HistogramTextureResolutionParam, HistogramTextureExtents);
	}

public:
	static const inline int32 HGRAM_PIXELS_PER_TILE = 64;
	static const inline int32 HGRAM_TILES_PER_BLOCK = 10;

private:
	LAYOUT_FIELD(FShaderResourceParameter, MinMaxResultSourceParam);
	LAYOUT_FIELD(FShaderResourceParameter, MinMaxResultDestParam);
	LAYOUT_FIELD(FShaderParameter, HistogramTextureResolutionParam);
};

// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#if WITH_IMGUI

#include "ImGuiAssetPicker.h"
#include "ContentBrowserDataUtils.h"

namespace FImGuiContentBrowserUtils
{
	bool bShowProjectConent = true;
	bool bShowEngineConent = true;
	bool bShowPluginConent = true;
	bool bShowDeveloperConent = true;

	bool FilterAsset(FName AssetPath)
	{
		EContentBrowserItemAttributeFilter ContentBrowserItemFilter = EContentBrowserItemAttributeFilter::IncludeNone;
		if (bShowProjectConent)
		{
			ContentBrowserItemFilter |= EContentBrowserItemAttributeFilter::IncludeProject;
		}
		if (bShowEngineConent)
		{
			ContentBrowserItemFilter |= EContentBrowserItemAttributeFilter::IncludeEngine;
		}
		if (bShowPluginConent)
		{
			ContentBrowserItemFilter |= EContentBrowserItemAttributeFilter::IncludePlugins;
		}
		if (bShowDeveloperConent)
		{
			ContentBrowserItemFilter |= EContentBrowserItemAttributeFilter::IncludeDeveloper;
		}

		FNameBuilder PathBufferStr(AssetPath);
		return ContentBrowserDataUtils::PathPassesAttributeFilter(PathBufferStr, 0, ContentBrowserItemFilter);
	}

}

#endif //#if WITH_IMGUI
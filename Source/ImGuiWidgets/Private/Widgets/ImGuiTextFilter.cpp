// Copyright 2026 Amit Kumar Mehar. All Rights Reserved.

#include "ImGuiWidgets.h"

#include "ImGuiSubsystem.h"
#include "String/ParseTokens.h"

namespace ImGuiTextureFilter_Private
{
	void AutoActivateNextItem(const char* ItemLabel)
	{
		const ImGuiID ItemId = ImGui::GetCurrentWindow()->GetID(ItemLabel);
		ImGui::PushID(ItemId);

		const ImGuiID StorageId = ImGui::GetCurrentWindow()->GetID("AutoActivate");
		const bool bHadFocus = ImGui::GetStateStorage()->GetBool(StorageId);
		const bool bHasFocus = (ImGui::GetFocusID() == ItemId);

		if (bHasFocus && !bHadFocus)
		{
			ImGui::SetKeyboardFocusHere();
		}

		ImGui::GetStateStorage()->SetBool(StorageId, bHasFocus);
		ImGui::PopID();
	}
}

FImGuiTextFilter FImGuiTextFilter::MakeWidget(uint32 MaxLength)
{
	FImGuiTextFilter Widget = {};
	Widget.FilterStringBuffer.Reserve(MaxLength);
	Widget.FilterStringBuffer_ANSI.Reserve(MaxLength);
	Widget.Reset();
	return Widget;
}

void FImGuiTextFilter::Reset()
{
	if (!ensure(FilterStringBuffer_ANSI.Max() > 0))
	{
		return;
	}

	FilterStringBuffer.Reset();
	FilterKeywordTokens.Reset();
	FilterKeywordTokens_ANSI.Reset();
	FilterStringBuffer_ANSI.Reset();
	FilterStringBuffer_ANSI.GetData()[0] = '\0';

	SearchIconTint = 0.75f;
	ClearIconTint = 0.75f;
}

bool FImGuiTextFilter::Draw(FImGuiTickContext* Context, const char* Label, const char* HintText, float WidgetWidth, bool bSetFocus, bool bActivateOnNavFocus)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("TextFilter::Draw"), STAT_ImGuiTextFilter_Draw, STATGROUP_ImGui);

	const int32 MaxLength = FilterStringBuffer_ANSI.Max();
	if (MaxLength == 0)
	{
		FImGui::DrawWarningMessageBox(Context, 4.f, ImVec4(1.f, 0.f, 0.f, 1.f), *FAnsiString::Printf("TextFilter('%s') not initialized!", Label));
		return false;
	}

	FImGui::EnsureValidImGuiContext(Context);

	UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
	const FImGuiImageBindingParams SearchIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.Search"), ImGui::GetFontSize());
	const FImGuiImageBindingParams ClearIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("ImIcon.Cross"), ImGui::GetFontSize());

	FImGuiNamedScope WidgetScope{ Label };

	bool bFilterChanged = false;
	bool bSearchBoxHasFocus = false;

	ImGui::BeginGroup();
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));

		// blend button with input text widget
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		if (FilterKeywordTokens_ANSI.IsEmpty())
		{
			ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
			if (FImGui::ImageButton("Search", SearchIcon, ImVec4(0, 0, 0, 0), ImVec4(SearchIconTint, SearchIconTint, SearchIconTint, 1.f)))
			{
				// set focus to input text
				bSetFocus = true;
			}
			ImGui::PopItemFlag();
		}
		else
		{
			if (FImGui::ImageButton("ClearFilter", ClearIcon, ImVec4(0, 0, 0, 0), ImVec4(ClearIconTint, ClearIconTint, ClearIconTint, 1.f)))
			{
				bFilterChanged = true;
				Reset();

				// set focus to input text
				bSetFocus = true;
			}
			ClearIconTint = ImGui::IsItemHovered() ? 1.f : 0.75f;
		}
		ImGui::PopStyleColor(3);

		ImGui::SameLine();

		const float SearchBoxWidth = (WidgetWidth > 0 ? WidgetWidth : ImGui::CalcItemWidth()) - SearchIcon.Size.x - ImGui::GetStyle().FramePadding.x * 2.f;
		if (SearchBoxWidth > KINDA_SMALL_NUMBER)
		{
			ImGui::SetNextItemWidth(SearchBoxWidth);
		}
		if (bSetFocus)
		{
			ImGui::SetKeyboardFocusHere();
		}

		if (bActivateOnNavFocus)
		{
			ImGuiTextureFilter_Private::AutoActivateNextItem("##Filter");
		}
		char* TextBuffer = FilterStringBuffer_ANSI.GetData();
		ImGui::PushStyleColor(ImGuiCol_NavCursor, 0); //not ideal but NavCursor has become very annoying now (shows even when using `ImGui::SetKeyboardFocusHere`) after ImGui commit: 1566c96ccd5faa7fddf34329687ac16796bebabc
		const bool bInputTextChanged = HintText ? ImGui::InputTextWithHint("##Filter", HintText, TextBuffer, MaxLength) : ImGui::InputText("##Filter", TextBuffer, MaxLength);
		ImGui::PopStyleColor();
		if (bInputTextChanged)
		{
			bFilterChanged = true;

			FilterStringBuffer.SetNumUninitialized(FPlatformString::ConvertedLength<TCHAR>((const UTF8CHAR*)TextBuffer));
			FPlatformString::Convert((TCHAR*)FilterStringBuffer.GetData(), FilterStringBuffer.Num(), (const UTF8CHAR*)TextBuffer, FCStringAnsi::Strlen(TextBuffer) + 1);

			// patch ansi string buffer
			FilterStringBuffer_ANSI.SetNumUnsafeInternal(FCStringAnsi::Strlen(TextBuffer));
			FilterStringBuffer_ANSI.GetData()[FilterStringBuffer_ANSI.Num()] = '\0';

			FilterKeywordTokens.Reset();
			FilterKeywordTokens_ANSI.Reset();
			if (FilterStringBuffer_ANSI.Num() > 0)
			{
				static const FAnsiStringView Delimiter = FAnsiStringView{ " " };
				const FAnsiStringView SourceStringView = GetFilterStringANSI();

				UE::String::EParseTokensOptions ParseOptions = UE::String::EParseTokensOptions::IgnoreCase | UE::String::EParseTokensOptions::SkipEmpty;
				UE::String::ParseTokens(SourceStringView, Delimiter,
					[&](FAnsiStringView Token)
					{
						Token.TrimStartAndEndInline();

						uint16 StartPosition_ANSI = (uint16)std::distance(SourceStringView.GetData(), Token.GetData());
						uint16 CharCount_ANSI = (uint16)Token.Len();
						if (CharCount_ANSI > 0)
						{
							FilterKeywordTokens_ANSI.Emplace(StartPosition_ANSI, CharCount_ANSI);

							uint16 StartPosition = FPlatformString::ConvertedLength<TCHAR>((const UTF8CHAR*)SourceStringView.GetData(), StartPosition_ANSI);
							uint16 CharCount = FPlatformString::ConvertedLength<TCHAR>((const UTF8CHAR*)SourceStringView.GetData() + StartPosition_ANSI, CharCount_ANSI);
							FilterKeywordTokens.Emplace(StartPosition, CharCount);
						}
					},
					ParseOptions);
			}
		}
		bSearchBoxHasFocus = ImGui::IsItemActive() || (Context->ImguiContext->NavId == ImGui::GetItemID()); //account for nav focus as NavCursor is disabled
		SearchIconTint = bSearchBoxHasFocus ? 1.f : 0.75f;

		ImGui::PopStyleVar(1);
	}
	ImGui::EndGroup();

	if (bSearchBoxHasFocus)
	{
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_FrameBgActive), 0.f, ImDrawFlags_None, 1.f);
	}

	return bFilterChanged;
}

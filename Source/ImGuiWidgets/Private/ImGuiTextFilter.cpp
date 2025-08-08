// Copyright 2024 Amit Kumar Mehar. All Rights Reserved.

#include "ImGuiCommonWidgets.h"

#include "ImGuiSubsystem.h"
#include "String/ParseTokens.h"

FImGuiTextFilter FImGuiTextFilter::MakeWidget(uint32 MaxLength)
{
	FImGuiTextFilter Widget = {};
	Widget.FilterStringBuffer.Reserve(MaxLength);
	Widget.FilterStringBuffer_ANSI.Reserve(MaxLength);
	Widget.Reset();
	return Widget;
}

bool FImGuiTextFilter::Draw(ImGuiContext* Context, const char* Label, const char* HintText, float WidgetWidth, bool bSetFocus)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("TextFilter::Draw"), STAT_ImGuiTextFilter_Draw, STATGROUP_ImGui);

	const int32 MaxLength = FilterStringBuffer_ANSI.Max();
	if (MaxLength == 0)
	{
		FImGui::AddWarningMessageBox(Context, 4.f, ImVec4(1.f, 0.f, 0.f, 1.f), *FAnsiString::Printf("TextFilter('%s') not initialized!", Label));
		return false;
	}

	FImGui::EnsureValidImGuiContext(Context);

	UImGuiSubsystem* ImGuiSubsystem = UImGuiSubsystem::Get();
	const FImGuiImageBindingParams SearchIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icons.Search"), FVector2D(ImGui::GetFontSize()), 1.f);
	const FImGuiImageBindingParams ClearIcon = ImGuiSubsystem->RegisterOneFrameResource(IMGUI_ICON("Icons.X"), FVector2D(ImGui::GetFontSize()), 1.f);

	FImGuiNamedWidgetScope WidgetScope{ Label };

	bool bFilterChanged = false;
	bool bSearchBoxHasFocus = false;

	ImGui::BeginGroup();
	{
		// blend button with input text widget
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));

		if (FilterKeywordTokens.IsEmpty())
		{
			if (FImGui::ImageButton("Search", SearchIcon, ImVec4(0, 0, 0, 0), ImVec4(SearchIconTint, SearchIconTint, SearchIconTint, 1.f)))
			{
				// set focus to input text
				bSetFocus = true;
			}
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

		ImGui::SameLine();

		const float SearchBoxWidth = WidgetWidth - SearchIcon.Size.x - ImGui::GetStyle().FramePadding.x * 2.f;
		if (SearchBoxWidth > KINDA_SMALL_NUMBER)
		{
			ImGui::SetNextItemWidth(SearchBoxWidth);
		}
		if (bSetFocus)
		{
			ImGui::SetKeyboardFocusHere();
		}

		char* TextBuffer = FilterStringBuffer_ANSI.GetData();
		const bool bInputTextChanged = HintText ? ImGui::InputTextWithHint("##Filter", HintText, TextBuffer, MaxLength) : ImGui::InputText("##Filter", TextBuffer, MaxLength);
		if (bInputTextChanged)
		{
			bFilterChanged = true;

			const ANSICHAR* Src = TextBuffer;
			FilterStringBuffer.Reset();
			while (*Src)
			{
				FilterStringBuffer.Add(CharCast<TCHAR>(*Src++));
			}
			// patch ansi string buffer
			FilterStringBuffer_ANSI.SetNum(FilterStringBuffer.Num(), EAllowShrinking::No);
			FilterStringBuffer_ANSI.GetData()[FilterStringBuffer_ANSI.Num()] = '\0';

			FilterKeywordTokens.Reset();
			if (FilterStringBuffer_ANSI.Num() > 0)
			{
				static const FAnsiStringView Delimiter = FAnsiStringView{ " " };
				const FAnsiStringView SourceStringView = GetFilterStringANSI();

				UE::String::EParseTokensOptions ParseOptions = UE::String::EParseTokensOptions::IgnoreCase | UE::String::EParseTokensOptions::SkipEmpty;
				UE::String::ParseTokens(SourceStringView, Delimiter,
					[&](FAnsiStringView Token)
					{
						Token.TrimStartAndEndInline();

						int16 StartPosition = (int16)std::distance(SourceStringView.GetData(), Token.GetData());
						int16 CharCount = (int16)Token.Len();
						if (CharCount > 0)
						{
							FilterKeywordTokens.Emplace(StartPosition, CharCount);
						}
					},
					ParseOptions);
			}
		}
		bSearchBoxHasFocus = ImGui::IsItemActive();
		SearchIconTint = bSearchBoxHasFocus ? 1.f : 0.75f;

		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(3);
	}
	ImGui::EndGroup();

	if (bSearchBoxHasFocus)
	{
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_FrameBgActive), 0.f, ImDrawFlags_None, 1.f);
	}

	return bFilterChanged;
}

void FImGuiTextFilter::Reset()
{
	if (!ensure(FilterStringBuffer_ANSI.Max() > 0))
	{
		return;
	}

	FilterStringBuffer.Reset();
	FilterKeywordTokens.Reset();
	FilterStringBuffer_ANSI.Reset();
	FilterStringBuffer_ANSI.GetData()[0] = '\0';

	SearchIconTint = 0.75f;
	ClearIconTint = 0.75f;
}

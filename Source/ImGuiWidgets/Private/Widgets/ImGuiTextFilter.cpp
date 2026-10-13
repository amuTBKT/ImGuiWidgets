// Copyright 2026 Amit Kumar Mehar. All Rights Reserved.

#include "ImGuiWidgets.h"
#include "ImGuiSubsystem.h"
#include "String/ParseTokens.h"

FImGuiTextFilter FImGuiTextFilter::MakeWidget(uint32 MaxLength)
{
	static const FSearchBoxStyle DefaultStyle = []()
		{
			auto Style = FAppStyle::Get().GetWidgetStyle<FSearchBoxStyle>(IMGUI_FNAME("SearchBox"));
			// removed the HalfHeightRadius corners for now
			Style.TextBoxStyle = FAppStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
			return Style;
		}();

	FImGuiTextFilter Widget = {};
	Widget.SlateStyle = DefaultStyle;
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
}

bool FImGuiTextFilter::Draw(FImGuiTickContext* Context, const char* Label, const char* HintText, bool bSetFocus, bool bActivateOnNavFocus)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("TextFilter::Draw"), STAT_ImGuiTextFilter_Draw, STATGROUP_ImGui);

	const int32 MaxLength = FilterStringBuffer_ANSI.Max();
	if (MaxLength == 0)
	{
		TAnsiStringBuilder<128> StringBuilder;
		StringBuilder.Appendf("TextFilter('%s') not initialized!", Label);
		FImGui::DrawWarningMessageBox(Context, 4.f, ImVec4(1.f, 0.f, 0.f, 1.f), *StringBuilder);
		return false;
	}
	if (!SlateStyle.IsSet())
	{
		TAnsiStringBuilder<128> StringBuilder;
		StringBuilder.Appendf("TextFilter('%s') invalid style!", Label);
		FImGui::DrawWarningMessageBox(Context, 4.f, ImVec4(1.f, 0.f, 0.f, 1.f), *StringBuilder);
		return false;
	}

	FImGuiTickScope TickScope{ Context };

	char* TextBuffer = FilterStringBuffer_ANSI.GetData();
	bool bFilterChanged = FImGui::SearchTextBox(Context, Label, HintText, TextBuffer, MaxLength, SlateStyle.GetValue(), bSetFocus, bActivateOnNavFocus);
	if (bFilterChanged)
	{
		OnFilterChanged();
	}
	return bFilterChanged;
}

void FImGuiTextFilter::SetText(const char* Text)
{
	if (!Text || FCStringAnsi::Strlen(Text) == 0)
	{
		Reset();
		return;
	}

	const int32 MaxLength = FilterStringBuffer_ANSI.Max();
	FMemory::Memcpy(FilterStringBuffer_ANSI.GetData(), Text, FMath::Min(FCStringAnsi::Strlen(Text) + 1, MaxLength));
	OnFilterChanged();
}

void FImGuiTextFilter::OnFilterChanged()
{
	char* TextBuffer = FilterStringBuffer_ANSI.GetData();
	if (FCStringAnsi::Strlen(TextBuffer) == 0)
	{
		Reset();
	}
	else
	{
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
}

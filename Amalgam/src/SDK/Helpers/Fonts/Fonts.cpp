#include "Fonts.h"

//micross

void CFonts::Reload(float flDPI)
{
	m_mapFonts[FONT_ESP] = { "Tahoma", int(12.f * flDPI), FONTFLAG_DROPSHADOW, 0 };
	m_mapFonts[FONT_ESP_FLAG] = { "Small Fonts", int(9.f * flDPI), FONTFLAG_DROPSHADOW, 0 };
	m_mapFonts[FONT_ESP_HP] = { "Small Fonts", int(10.f * flDPI), FONTFLAG_DROPSHADOW, 0 };
	m_mapFonts[FONT_INDICATORS] = { "micross", int(13.f * flDPI), FONTFLAG_OUTLINE, 0 };

	for (auto& [_, fFont] : m_mapFonts)
	{
		I::MatSystemSurface->SetFontGlyphSet
		(
			fFont.m_dwFont = I::MatSystemSurface->CreateFont(),
			fFont.m_szName,		//name
			fFont.m_nTall,		//tall
			fFont.m_nWeight,	//weight
			0,					//blur
			0,					//scanlines
			fFont.m_nFlags		//flags
		);
	}
}

const Font_t& CFonts::GetFont(EFonts eFont)
{
	return m_mapFonts[eFont];
}
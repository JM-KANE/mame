// license:BSD-3-Clause
// copyright-holders:Allard van der Bas
/***************************************************************************

  video.c

  Functions to emulate the video hardware of the machine.

***************************************************************************/

#include "emu.h"
#include "video/resnet.h"
#include "mikie.h"

/***************************************************************************

  Convert the color PROMs into a more useable format.

  Mikie has three 256x4 palette PROMs (one per gun) and two 256x4 lookup
  table PROMs (one for characters, one for sprites).
  I don't know for sure how the palette PROMs are connected to the RGB
  output, but it's probably the usual:

  bit 3 -- 220 ohm resistor  -- RED/GREEN/BLUE
        -- 470 ohm resistor  -- RED/GREEN/BLUE
        -- 1  kohm resistor  -- RED/GREEN/BLUE
  bit 0 -- 2.2kohm resistor  -- RED/GREEN/BLUE

***************************************************************************/

void mikie_state::mikie_palette(palette_device &palette) const
{
	uint8_t const *color_prom = memregion("proms")->base();
	static constexpr int resistances[4] = { 2200, 1000, 470, 220 };

	// compute the color output resistor weights
	double rweights[4], gweights[4], bweights[4];
	compute_resistor_weights(0, 255, -1.0,
			4, resistances, rweights, 470, 0,
			4, resistances, gweights, 470, 0,
			4, resistances, bweights, 470, 0);

	// create a lookup table for the palette
	for (int i = 0; i < 0x100; i++)
	{
		int bit0, bit1, bit2, bit3;

		// red component
		bit0 = BIT(color_prom[i + 0x000], 0);
		bit1 = BIT(color_prom[i + 0x000], 1);
		bit2 = BIT(color_prom[i + 0x000], 2);
		bit3 = BIT(color_prom[i + 0x000], 3);
		int const r = combine_weights(rweights, bit0, bit1, bit2, bit3);

		// green component
		bit0 = BIT(color_prom[i + 0x100], 0);
		bit1 = BIT(color_prom[i + 0x100], 1);
		bit2 = BIT(color_prom[i + 0x100], 2);
		bit3 = BIT(color_prom[i + 0x100], 3);
		int const g = combine_weights(gweights, bit0, bit1, bit2, bit3);

		// blue component
		bit0 = BIT(color_prom[i + 0x200], 0);
		bit1 = BIT(color_prom[i + 0x200], 1);
		bit2 = BIT(color_prom[i + 0x200], 2);
		bit3 = BIT(color_prom[i + 0x200], 3);
		int const b = combine_weights(bweights, bit0, bit1, bit2, bit3);

		palette.set_indirect_color(i, rgb_t(r, g, b));
	}

	// color_prom now points to the beginning of the lookup table,
	color_prom += 0x300;

	// characters use colors 0x10-0x1f of each 0x20 color bank, while sprites use colors 0-0x0f
	for (int i = 0; i < 0x200; i++)
	{
		for (int j = 0; j < 8; j++)
		{
			uint8_t const ctabentry = (j << 5) | ((~i & 0x100) >> 4) | (color_prom[i] & 0x0f);
			m_palette->set_pen_indirect(((i & 0x100) << 3) | (j << 8) | (i & 0xff), ctabentry);
		}
	}
}

void mikie_state::mikie_videoram_w(offs_t offset, uint8_t data)
{
	m_videoram[offset] = data;
	m_bg_tilemap->mark_tile_dirty(offset);
}

void mikie_state::mikie_colorram_w(offs_t offset, uint8_t data)
{
	m_colorram[offset] = data;
	m_bg_tilemap->mark_tile_dirty(offset);
}

void mikie_state::mikie_palettebank_w(uint8_t data)
{
	if (m_palettebank != (data & 0x07))
	{
		m_palettebank = data & 0x07;
		machine().tilemap().mark_all_dirty();
	}
}

WRITE_LINE_MEMBER(mikie_state::flipscreen_w)
{
	flip_screen_set(state);
	machine().tilemap().mark_all_dirty();
}

TILE_GET_INFO_MEMBER(mikie_state::get_bg_tile_info)
{
	int code = m_videoram[tile_index] + ((m_colorram[tile_index] & 0x20) << 3);
	int color = (m_colorram[tile_index] & 0x0f) + 16 * m_palettebank;
	int flags = ((m_colorram[tile_index] & 0x40) ? TILE_FLIPX : 0) | ((m_colorram[tile_index] & 0x80) ? TILE_FLIPY : 0);
	if (m_colorram[tile_index] & 0x10)
		tileinfo.category = 1;
	else
		tileinfo.category = 0;

	tileinfo.set(0, code, color, flags);


}

void mikie_state::video_start()
{
	m_bg_tilemap = &machine().tilemap().create(*m_gfxdecode, tilemap_get_info_delegate(*this, FUNC(mikie_state::get_bg_tile_info)), TILEMAP_SCAN_ROWS, 8, 8, 32, 32);
}

void mikie_state::draw_sprites(bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	uint8_t *spriteram = m_spriteram;
	int offs;

	for (offs = 0; offs < m_spriteram.bytes(); offs += 4)
	{
		int gfxbank = (spriteram[offs + 2] & 0x40) ? 2 : 1;
		int code = (spriteram[offs + 2] & 0x3f) + ((spriteram[offs + 2] & 0x80) >> 1) + ((spriteram[offs] & 0x40) << 1);
		int color = (spriteram[offs] & 0x0f) + 16 * m_palettebank;
		int sx = spriteram[offs + 3];
		int sy = 244 - spriteram[offs + 1];
		int flipx = ~spriteram[offs] & 0x10;
		int flipy = spriteram[offs] & 0x20;

		if (flip_screen())
		{
			sy = 242 - sy;
			flipy = !flipy;
		}


			m_gfxdecode->gfx(gfxbank)->transpen(bitmap,cliprect,
			code, color,
			flipx,flipy,
			sx,sy, 0);
	}
}

uint32_t mikie_state::screen_update_mikie(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	m_bg_tilemap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_CATEGORY(0), 0);
	draw_sprites(bitmap, cliprect);
	m_bg_tilemap->draw(screen, bitmap, cliprect, TILEMAP_DRAW_CATEGORY(1), 0);
	return 0;
}

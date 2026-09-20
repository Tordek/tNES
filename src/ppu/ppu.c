#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "ppu/ppu.h"

static void ic_2c02_inc_x(struct ic_2c02_registers *ppu);
static void ic_2c02_inc_y(struct ic_2c02_registers *ppu);

enum ic_2c02_mmapped_registers
{
  mmapped_ppuctrl = 0x2000,
  mmapped_ppumask = 0x2001,
  mmapped_ppustatus = 0x2002,
  mmapped_oamaddr = 0x2003,
  mmapped_oamdata = 0x2004,
  mmapped_ppuscroll = 0x2005,
  mmapped_ppuaddr = 0x2006,
  mmapped_ppudata = 0x2007,
};

void ic_2c02_init(struct ic_2c02_registers *ppu)
{
  *ppu = (struct ic_2c02_registers){
      .status = 0,

      .do_nmi = 0,

      .clock = 2,
      .mask = 0,

      .pixel = 0,
      .scanline = -1,

      // Background registers
      .vram_increment = 0,
      .vram_address = 0,
      .t = 0,
      .fine_x = 0,
      .w = 0, // Read latch
      .pattern_hi = 0,
      .pattern_lo = 0,
      .palette_hi = 0,
      .palette_lo = 0,
      .palette_next = 0,
      .background = 0,

      .pattern_hi_next = 0,
      .pattern_lo_next = 0,
      .nametable_byte_next = 0,
      .attribute_byte = 0,
      .attribute_byte_next = 0,

      // Sprites
      .sprite_pattern_table = 0,
      .primary_oam.raw = {0},
      .secondary_oam.raw = {0},
      .sprite_pattern_hi = {0},
      .sprite_pattern_lo = {0},
      .sprite_attributes = {0},
      .sprite_id = {0},
      .sprite_x = {0},
      .secondary_oam_write_enable = 0,
      .tall_sprites = 0,

      .secondary_oam_index = 0,
      .n = 0,

      .oam_addr = 0,
      .oam_state = 0,

      .m = 0,
      .waste = 0,

      .ppudata_read = 0,
      // .palette = {0},

      .screen = {{0}},
      // .mapper = NULL,
  };
}

void ic_2c02_reset(struct ic_2c02_registers *ppu)
{
  ppu->palette_next = 0;
  ppu->pattern_hi = 0;
  ppu->pattern_lo = 0;
  ppu->status = 0;
  ppu->vram_increment = 1;
  ppu->pixel = 0;
  ppu->scanline = -1;
}

int ic_2c02_clock(struct ic_2c02_registers *ppu, struct ic_2c02_bus *bus)
{
  ppu->clock++;
  int scanline = ppu->scanline;
  int pixel = ppu->pixel;
  if (ppu->mask & 0x18)
  {
    if (-1 < scanline && scanline < 240 && 0 < pixel && pixel < 257)
    {
      int8_t color_abs = 0;

      if (ppu->mask & 0x08)
      { // BG
        uint8_t color_id = ((ppu->pattern_lo << ppu->fine_x) & 0x8000) >> 15;
        color_id |= ((ppu->pattern_hi << ppu->fine_x) & 0x8000) >> 14;
        uint8_t palette = ((ppu->palette_lo << ppu->fine_x) & 0x80) >> 5;
        palette |= ((ppu->palette_hi << ppu->fine_x) & 0x80) >> 4;

        color_abs = color_id ? palette | color_id : 0;
      }

      if (ppu->mask & 0x10)
      { // Sprites
        int sprite_drawn = 0;
        for (int i = 0; i < 8; i++)
        {
          if (ppu->sprite_x[i] > 0)
          {
            ppu->sprite_x[i]--;
            continue;
          }

          uint8_t sprite_color = 0;
          if (ppu->sprite_attributes[i] & 0x40)
          {
            sprite_color |= ppu->sprite_pattern_lo[i] & 0x01;
            sprite_color |= (ppu->sprite_pattern_hi[i] & 0x01) << 1;
            ppu->sprite_pattern_hi[i] >>= 1;
            ppu->sprite_pattern_lo[i] >>= 1;
          }
          else
          {
            sprite_color |= (ppu->sprite_pattern_lo[i] & 0x80) >> 7;
            sprite_color |= (ppu->sprite_pattern_hi[i] & 0x80) >> 6;
            ppu->sprite_pattern_hi[i] <<= 1;
            ppu->sprite_pattern_lo[i] <<= 1;
          }

          if (ppu->sprite_id[i] == 0 && sprite_color && color_abs)
          {
            ppu->status |= 0x40;
          }

          if (!sprite_drawn && sprite_color && ((ppu->sprite_attributes[i] & 0x20) == 0 || color_abs == 0))
          {
            uint8_t palette = (ppu->sprite_attributes[i] << 2) & 0x0c;

            color_abs = 0x10 | palette | sprite_color;
            sprite_drawn = 1;
          }
        }
      }

      uint8_t palette_color;
      bus->read(&palette_color, bus->context, 0x3f00 + color_abs);
      // uint8_t palette_color = ppu->palette[color_abs];
      if (ppu->mask & 0x01)
      {
        palette_color &= 0x30;
      }

      ppu->screen[scanline][pixel - 1] = palette_color;
    }

    if ((2 <= pixel && pixel <= 257) || (322 <= pixel && pixel <= 337))
    {
      ppu->pattern_lo <<= 1;
      ppu->pattern_hi <<= 1;
      ppu->palette_hi = (ppu->palette_hi << 1) | ((ppu->palette_next & 0x02) >> 1);
      ppu->palette_lo = (ppu->palette_lo << 1) | ((ppu->palette_next & 0x01));
    }

    if (scanline < 240)
    {
      uint16_t tile_address = 0x2000 | (ppu->vram_address & 0x0FFF);
      uint16_t attribute_address = 0x23C0 | (ppu->vram_address & 0x0c00) | ((ppu->vram_address >> 4) & 0x0038) | ((ppu->vram_address >> 2) & 0x0007);

      if (257 <= pixel && pixel <= 320)
      {
        ppu->oam_addr = 0x00;
      }
      else if (pixel == 338 || pixel == 340)
      {
        // Garbage NT read.
        uint8_t discard;
        bus->read(&discard, bus->context, tile_address);
      }
      else if (pixel > 0)
      {
        switch (pixel & 0x07)
        {
        case 0x02:
        {
          uint8_t val;
          // Load NT byte
          // Shift 4 bits now to save 2 shifts later.
          bus->read(&val, bus->context, tile_address);
          ppu->nametable_byte_next = val << 4;
        }
        break;
        case 0x04:
          // Load AT byte
          bus->read(&ppu->attribute_byte_next, bus->context, attribute_address);
          break;
        case 0x06:
        {
          // Low BG tile
          uint8_t val;
          bus->read(&val, bus->context, ppu->background | ppu->nametable_byte_next | (ppu->vram_address >> 12));
          ppu->pattern_lo_next = val;
        }
        break;
        case 0x00:
        {
          // High BG tile
          uint8_t val;
          bus->read(&val, bus->context, ppu->background | ppu->nametable_byte_next | (ppu->vram_address >> 12) | 0x08);
          ppu->pattern_hi_next = val;

          ppu->pattern_lo |= ppu->pattern_lo_next;
          ppu->pattern_hi |= ppu->pattern_hi_next;
          ppu->attribute_byte = ppu->attribute_byte_next;

          switch (ppu->vram_address & 0x0042)
          {
          case 0x0000:
            ppu->palette_next = ppu->attribute_byte_next;
            break;
          case 0x0002:
            ppu->palette_next = ppu->attribute_byte_next >> 2;
            break;
          case 0x0040:
            ppu->palette_next = ppu->attribute_byte_next >> 4;
            break;
          case 0x0042:
            ppu->palette_next = ppu->attribute_byte_next >> 6;
            break;
          }

          ic_2c02_inc_x(ppu);
        }
        break;
        }
      }

      if (ppu->pixel == 256)
      {
        ic_2c02_inc_y(ppu);
      }

      if (ppu->pixel == 257)
      {
        ppu->vram_address &= 0xFBE0;
        ppu->vram_address |= ppu->t & 0x041f;
      }

      if (ppu->scanline == -1 && 280 <= ppu->pixel && ppu->pixel <= 304)
      {
        ppu->vram_address &= 0x041f;
        ppu->vram_address |= ppu->t & 0xfbe0;
      }
    }
  }
  else
  {
    if (-1 < scanline && scanline < 240 && 0 < pixel && pixel < 257)
    {
      if (ppu->vram_address >= 0x3f00)
      {
        bus->read(&ppu->screen[scanline][pixel - 1], bus->context, ppu->vram_address & 0x3f1F);
      }
    }
  }

  // Sprite evaluation
  if (0 < pixel && pixel < 65)
  {
    ppu->secondary_oam.raw[pixel & 0x1f] = 0xFF;
    ppu->secondary_oam_index = 0;
    ppu->n = 0;
    ppu->oam_state = 1;
    ppu->waste = 1;
  }
  else if (pixel < 256)
  {
    switch (ppu->oam_state)
    {
    case 1:
    {
      // 1
      struct oam_value cur_oam = ppu->primary_oam.data[ppu->n];
      uint8_t sprite_y = cur_oam.y;
      if (ppu->secondary_oam_index < 8)
      {
        ppu->secondary_oam.data[ppu->secondary_oam_index].y = sprite_y;
        // 1a
        if (sprite_y <= scanline && scanline < (sprite_y + 8))
        {
          ppu->secondary_oam.data[ppu->secondary_oam_index] = ppu->primary_oam.data[ppu->n];
          ppu->sprite_id[ppu->secondary_oam_index] = ppu->n;
          ppu->secondary_oam_index++;
        }
      }
      ppu->n++;
      ppu->oam_state = 2;
    }
    break;

    case 2:
    {
      if (ppu->n == 64)
      {
        ppu->n = 0;
        ppu->oam_state = 4;
      }
      else if (ppu->secondary_oam_index < 8)
      {
        ppu->oam_state = 1;
      }
      else if (ppu->secondary_oam_index == 8)
      {
        ppu->m = 0;
        ppu->oam_state = 3;
      }
    }
    break;

    case 3:
    {
      uint8_t y = ppu->primary_oam.raw[ppu->n * 4 + ppu->m];
      if (ppu->waste || (y <= scanline && scanline < (y + 8)))
      {
        ppu->waste = 1;
        ppu->status |= 0x20;
        ppu->m++;
        if (ppu->m == 4)
        {
          ppu->m = 0;
          ppu->n++;
          ppu->waste = 0;
        }
      }
      else
      {
        ppu->m = (ppu->m + 1) & 0x07;
        ppu->n++;
      }

      if (ppu->n == 64)
      {
        ppu->n = 0;
        ppu->oam_state = 4;
      }
    }
    break;

    case 4:
      ppu->n = (ppu->n + 1) & 0x3f;
    }
  }
  else if (pixel < 265)
  { // For absolute cycle-accuracy, do this in different cycles.
    struct oam_value sprite = ppu->secondary_oam.data[pixel & 0x07];
    bus->read(&ppu->sprite_pattern_lo[pixel & 0x07], bus->context, ppu->sprite_pattern_table | (sprite.tile_index << 4) | (scanline - sprite.y));
    bus->read(&ppu->sprite_pattern_hi[pixel & 0x07], bus->context, ppu->sprite_pattern_table | (sprite.tile_index << 4) | 8 | (scanline - sprite.y));
    ppu->sprite_attributes[pixel & 0x07] = sprite.attributes;
    ppu->sprite_x[pixel & 0x07] = sprite.x;
  }
  else if (pixel == 340)
  {
  }

  ppu->pixel++;

  if (ppu->pixel > 340)
  {
    ppu->pixel = 0;
    ppu->scanline += 1;

    if (ppu->scanline > 260)
    {
      ppu->scanline = -1;
    }
  }

  if (ppu->scanline == -1 && ppu->pixel == 1)
  {
    ppu->status &= 0x7f; // Exit VBlank
    ppu->status &= 0xbf;
  }

  if (ppu->scanline == 241 && ppu->pixel == 1)
  {
    ppu->status |= 0x80; // Enter VBlank
    return 1;
  }

  return 0;
}

void ic_2c02_mmapped_read(uint8_t *restrict data, struct ic_2c02_registers *ppu, struct ic_2c02_bus *bus, uint16_t address)
{
  switch (address)
  {
  case mmapped_ppustatus:
  {
    *data = (ppu->status & 0xe0) | (*data & 0x1f);
    ppu->status &= 0x7F;
    ppu->w = 0;
    return;
  }
  case mmapped_oamdata:
    *data = ppu->primary_oam.raw[ppu->oam_addr];
    break;
  case mmapped_ppudata:
  {
    uint8_t result;
    if (ppu->vram_address < 0x3F00)
    {
      result = ppu->ppudata_read;
    }
    else
    {
      // result = ppu->palette[ppu->vram_address & 0x001F];
      bus->read(&result, bus->context, 0x3f00 + (ppu->vram_address & 0x001F));
      if (ppu->mask & 0x01)
      {
        result &= 0x30;
      }
    }
    bus->read(&ppu->ppudata_read, bus->context, ppu->vram_address & 0x3FFF);
    if (ppu->mask & 0x18 && ppu->scanline < 240)
    {
      ic_2c02_inc_x(ppu);
      ic_2c02_inc_y(ppu);
    }
    else
    {
      ppu->vram_address += ppu->vram_increment;
    }
    *data = result;
    return;
  }
  default:
    printf("Mmapped reg read not implemented: %x\n", address);
    // return 0;
  }
}

void ic_2c02_mmapped_write(struct ic_2c02_registers *ppu, struct ic_2c02_bus *bus, uint16_t address, uint8_t value)
{
  switch (address)
  {
  case mmapped_ppuctrl:
    ppu->t &= 0xF3FF;
    ppu->t |= (value & 0x03) << 10;

    if (value & 0x04)
    {
      ppu->vram_increment = 32;
    }
    else
    {
      ppu->vram_increment = 1;
    }

    ppu->sprite_pattern_table = (value & 0x08) << 9;

    ppu->background = (value & 0x10) << 8;

    ppu->tall_sprites = (value & 0x20);

    if (value & 0x40)
    {
      //
    }

    ppu->do_nmi = value & 0x80;

    break;
  case mmapped_ppumask:
    ppu->mask = value;
    break;
  case mmapped_oamaddr:
    ppu->oam_addr = value;
    break;
  case mmapped_oamdata:
    ppu->primary_oam.raw[ppu->oam_addr++] = value;
    break;
  case mmapped_ppuscroll:
    if (ppu->w == 0)
    {
      ppu->t &= 0xFFE0;
      ppu->t |= value >> 3;
      ppu->fine_x = value & 0x07;
      ppu->w = 1;
    }
    else
    {
      ppu->t &= 0x0C1F;
      ppu->t |= (value & 0xF8) << 2;
      ppu->t |= (value & 0x07) << 12;
      ppu->w = 0;
    }
    break;
  case mmapped_ppuaddr:
    if (ppu->w == 0)
    {
      ppu->t &= 0x00FF;
      ppu->t |= (value & 0x3F) << 8;
      ppu->w = 1;
    }
    else
    {
      ppu->t &= 0xFF00;
      ppu->t |= value;
      ppu->vram_address = ppu->t;
      ppu->w = 0;
    }
    break;
  case mmapped_ppudata:
    bus->write(bus->context, ppu->vram_address & 0x3fff, value);

    if (ppu->mask & 0x18 && ppu->scanline < 240)
    {
      ic_2c02_inc_x(ppu);
      ic_2c02_inc_y(ppu);
    }
    else
    {
      ppu->vram_address += ppu->vram_increment;
    }
    break;
  default:
    printf("Mmapped reg not implemented: %x\n", address);
  }
}

static void ic_2c02_inc_x(struct ic_2c02_registers *ppu)
{
  if ((ppu->vram_address & 0x001F) == 0x001F)
  {
    ppu->vram_address &= 0xFFE0;
    ppu->vram_address ^= 0x0400;
  }
  else
  {
    ppu->vram_address++;
  }
}

static void ic_2c02_inc_y(struct ic_2c02_registers *ppu)
{
  if ((ppu->vram_address & 0x7000) == 0x7000)
  {
    ppu->vram_address &= 0x0fff;
    uint16_t y = (ppu->vram_address & 0x03e0) >> 5;
    if (y == 29)
    {
      y = 0;
      ppu->vram_address ^= 0x0800;
    }
    else if (y == 31)
    {
      y = 0;
    }
    else
    {
      y++;
    }
    ppu->vram_address = (ppu->vram_address & 0xFC1F) | (y << 5);
  }
  else
  {
    ppu->vram_address += 0x1000;
  }
}

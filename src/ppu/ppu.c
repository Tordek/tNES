#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "ppu/ppu.h"

static void ic_2c02_inc_x(struct ic_2c02_registers *ppu);
static void ic_2c02_inc_y(struct ic_2c02_registers *ppu);

enum ic_2c02_mmapped_registers
{
  mmapped_ppuctrl = 0x00,
  mmapped_ppumask = 0x01,
  mmapped_ppustatus = 0x02,
  mmapped_oamaddr = 0x03,
  mmapped_oamdata = 0x04,
  mmapped_ppuscroll = 0x05,
  mmapped_ppuaddr = 0x06,
  mmapped_ppudata = 0x07,
};

void ppu_bus_read_internal(
    uint8_t *restrict result,
    struct ic_2c02_registers *ppu,
    struct ic_2c02_bus *bus,
    uint16_t address)
{
  bus->read(&ppu->ppubus_data, bus->context, address);

  // The first 0x3f00 are handled by the cartridge
  if (address < 0x3f00)
  {
    *result = ppu->ppubus_data;
  }
  // The last 0x100 are 8 0x20 mirrors of palette ram
  else if (address < 0x4000)
  {
    *result = ppu->palette[address & 0x1f];
  }
}

void ppu_bus_write_internal(struct ic_2c02_registers *ppu, struct ic_2c02_bus *bus, uint16_t address, uint8_t data)
{
  // The first 0x3f00 are handled by the cartridge
  if (address < 0x3f00)
  {
    bus->write(bus->context, address, data);
  }
  // The last 0x100 are 8 0x20 mirrors of palette ram
  else if (address < 0x4000)
  {
    uint8_t palette_pos = address & 0x1f;

    if ((palette_pos & 0x03) == 0)
    {
      ppu->palette[palette_pos & 0x0F] = data;
      ppu->palette[palette_pos | 0x10] = data;
    }
    else
    {
      ppu->palette[palette_pos] = data;
    }
  }
}

void ic_2c02_init(struct ic_2c02_registers *ppu)
{
  *ppu = (struct ic_2c02_registers){
      .status = 0,

      .do_nmi = 0,

      .clock = 2,
      .mask = 0,

      .dot = 0,
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

      .ppudata_buffer = 0,
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
  ppu->dot = 0;
  ppu->scanline = -1;
}

int ic_2c02_clock(struct ic_2c02_registers *ppu, struct ic_2c02_bus *bus)
{
  uint8_t discard;

  ppu->clock++;
  int scanline = ppu->scanline;
  int dot = ppu->dot;
  if (ppu->mask & 0x18)
  {
    if (-1 < scanline && scanline < 240 && 0 < dot && dot < 257)
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

      if (ppu->mask & 0x18)
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
      ppu_bus_read_internal(&palette_color, ppu, bus, 0x3f00 + color_abs);
      // uint8_t palette_color = ppu->palette[color_abs];
      if (ppu->mask & 0x01)
      {
        palette_color &= 0x30;
      }

      ppu->screen[scanline][dot - 1] = palette_color;
    }

    if ((2 <= dot && dot <= 257) || (322 <= dot && dot <= 337))
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

      if (257 <= dot && dot <= 320)
      {
        ppu->oam_addr = 0x00;
      }
      else if (dot == 338 || dot == 340)
      {
        // Garbage NT read.
        ppu_bus_read_internal(&discard, ppu, bus, tile_address);
      }
      else if (dot > 0)
      {
        switch (dot & 0x07)
        {
        case 0x02:
        {
          uint8_t val;
          // Load NT byte
          // Shift 4 bits now to save 2 shifts later.
          ppu_bus_read_internal(&val, ppu, bus, tile_address);
          ppu->nametable_byte_next = val << 4;
        }
        break;
        case 0x04:
          // Load AT byte
          ppu_bus_read_internal(&ppu->attribute_byte_next, ppu, bus, attribute_address);
          break;
        case 0x06:
        {
          // Low BG tile
          uint8_t val;
          ppu_bus_read_internal(&val, ppu, bus, ppu->background | ppu->nametable_byte_next | (ppu->vram_address >> 12));
          ppu->pattern_lo_next = val;
        }
        break;
        case 0x00:
        {
          // High BG tile
          uint8_t val;
          ppu_bus_read_internal(&val, ppu, bus, ppu->background | ppu->nametable_byte_next | (ppu->vram_address >> 12) | 0x08);
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

      if (ppu->dot == 256)
      {
        ic_2c02_inc_y(ppu);
      }

      if (ppu->dot == 257)
      {
        ppu->vram_address &= 0xFBE0;
        ppu->vram_address |= ppu->t & 0x041f;
      }

      if (ppu->scanline == -1 && 280 <= ppu->dot && ppu->dot <= 304)
      {
        ppu->vram_address &= 0x041f;
        ppu->vram_address |= ppu->t & 0xfbe0;
      }
    }
  }
  else
  {
    if (-1 < scanline && scanline < 240 && 0 < dot && dot < 257)
    {
      if (ppu->vram_address >= 0x3f00)
      {
        ppu_bus_read_internal(&ppu->screen[scanline][dot - 1], ppu, bus, ppu->vram_address & 0x3f1F);
      }
    }
  }

  // Sprite evaluation
  if (ppu->mask & 0x18)
    if (0 < dot && dot < 65)
    {
      ppu->secondary_oam.raw[dot & 0x1f] = 0xFF;
      ppu->secondary_oam_index = 0;
      ppu->n = 0;
      ppu->oam_state = 1;
      ppu->waste = 1;
    }
    else if (dot < 256)
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
    else if (dot < 265)
    { // For absolute cycle-accuracy, do this in different cycles.
      struct oam_value sprite = ppu->secondary_oam.data[dot & 0x07];
      ppu_bus_read_internal(&ppu->sprite_pattern_lo[dot & 0x07], ppu, bus, ppu->sprite_pattern_table | (sprite.tile_index << 4) | (scanline - sprite.y));
      ppu_bus_read_internal(&ppu->sprite_pattern_hi[dot & 0x07], ppu, bus, ppu->sprite_pattern_table | (sprite.tile_index << 4) | 8 | (scanline - sprite.y));
      ppu->sprite_attributes[dot & 0x07] = sprite.attributes;
      ppu->sprite_x[dot & 0x07] = sprite.x;
    }
    else if (dot == 340)
    {
    }

  ppu->dot++;

  if (ppu->dot > 340)
  {
    ppu->dot = 0;
    ppu->scanline += 1;

    if (ppu->scanline > 260)
    {
      ppu->scanline = -1;
    }
  }

  if (ppu->scanline == -1 && ppu->dot == 1)
  {
    ppu->status &= 0x1f; // Exit VBlank
  }

  if (ppu->scanline == 241 && ppu->dot == 1)
  {
    ppu->status |= 0x80; // Enter VBlank
    return 1;
  }

  return 0;
}

void ic_2c02_mmapped_read(uint8_t *restrict data, struct ic_2c02_registers *ppu, struct ic_2c02_bus *bus, uint16_t address)
{
  uint8_t dummy;
  switch (address)
  {
  case mmapped_ppuctrl:
  case mmapped_ppumask:
  case mmapped_oamaddr:
  case mmapped_ppuscroll:
  case mmapped_ppuaddr:
    *data = ppu->ppubus_data;
    ppu_bus_read_internal(&dummy, ppu, bus, ppu->vram_address & 0x3FFF);
    break;
  case mmapped_ppustatus:
  {
    *data = (ppu->status & 0xe0) | (ppu->ppubus_data & 0x1f);
    ppu_bus_read_internal(&dummy, ppu, bus, ppu->vram_address & 0x3FFF);
    ppu->ppudata_buffer = ppu->ppubus_data;
    ppu->ppubus_data = *data;
    ppu->status &= 0x7F;
    ppu->w = 0;
    return;
  }
  case mmapped_oamdata:
    *data = ppu->primary_oam.raw[ppu->oam_addr];
    ppu_bus_read_internal(&dummy, ppu, bus, ppu->vram_address & 0x3FFF);
    ppu->ppubus_data = *data;
    break;
  case mmapped_ppudata:
  {

    if (ppu->vram_address < 0x3F00)
    {
      uint8_t result;
      ppu_bus_read_internal(&result, ppu, bus, ppu->vram_address & 0x3FFF);
      *data = ppu->ppudata_buffer;
      ppu->ppudata_buffer = ppu->ppubus_data;
    }
    else
    {
      *data = ppu->ppubus_data;
      uint8_t result;
      ppu_bus_read_internal(&result, ppu, bus, ppu->vram_address & 0x3FFF);
      ppu->ppudata_buffer = ppu->ppubus_data;
      if (ppu->mask & 0x01)
      {
        result &= 0x30;
      }
      *data = (*data & 0xc0) | (result & 0x3f);
      ppu->ppubus_data = *data;
    }
    if (ppu->mask & 0x18 && ppu->scanline < 240)
    {
      ic_2c02_inc_x(ppu);
      ic_2c02_inc_y(ppu);
    }
    else
    {
      ppu->vram_address += ppu->vram_increment;
    }
    return;
  }
  default:
    __builtin_unreachable();
    break;
  }
}

void ic_2c02_mmapped_write(struct ic_2c02_registers *ppu, struct ic_2c02_bus *bus, uint16_t address, uint8_t value)
{
  ppu->ppubus_data = value;
  switch (address)
  {
  case mmapped_ppustatus:
    break;
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
    ppu_bus_write_internal(ppu, bus, ppu->vram_address & 0x3fff, value);

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
    printf("Mmapped reg write not implemented: %x\n", address);
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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "rom/rom.h"

enum tnes_ines_version detect_version(uint8_t header[static 16])
{
  if ((header[7] & 0x0c) == 0x08)
  {
    return TNES_INES_NES_20;
  }
  else if ((header[7] & 0x0c) == 0x04)
  {
    return TNES_INES_ARCHAIC;
  }
  else if ((header[7] & 0x0c) == 0x00 && memcmp(header + 12, "\0\0\0\0", 4) == 0)
  {
    return TNES_INES;
  }
  else
  {
    return TNES_INES_ARCHAIC;
  }
}

int read_rom(struct nes_rom *rom, FILE *rom_file)
{
  uint8_t header_data[16];
  size_t rc = fread(header_data, 16, 1, rom_file);
  if (rc != 1)
  {
    return READ_ROM_FREAD_ERROR;
  }

  if (memcmp(header_data, "NES\x1A", 3) != 0)
  {
    return READ_ROM_INVALID_HEADER;
  }

  rom->version = detect_version(header_data);

  rom->prg_rom_size = header_data[4];
  rom->chr_rom_size = header_data[5];

  rom->mirroring = !!(header_data[6] & 0x01);
  rom->non_volatile_memory = !!(header_data[6] & 0x02);
  rom->has_trainer = !!(header_data[6] & 0x04);
  rom->four_screen_mode = !!(header_data[6] & 0x08);

  switch (rom->version)
  {

  case TNES_INES_ARCHAIC:
  {
    rom->mapper_id = header_data[6] >> 4;
  }

  case TNES_INES:
  {
    // TODO: Support these options.
    // rom->prg_ram_size |= (header_data[8] || 1 * 8192);
    // rom->has_vs_unisystem = !!(header_data[7] & 0x01);
    rom->mapper_id = (header_data[7] & 0xf0) | (header_data[6] >> 4);
    // rom->tv_system = (header_data[0] & 0x01) << 1;
  }
  break;

  case TNES_INES_NES_20:
  {
    rom->mapper_id = (header_data[8] & 0x0F) << 8 | (header_data[7] & 0xf0) | (header_data[6] >> 4);
    rom->submapper_id = header_data[8] >> 4;

    rom->prg_rom_size |= (header_data[9] & 0x0F) << 8;
    rom->chr_rom_size |= (header_data[9] & 0xF0) << 4;
    // rom->has_vs_unisystem = !!(header_data[7] & 0x01);
    // rom->has_playchoice = !!(header_data[7] & 0x02);
    // rom->tv_system = (header_data[0] & 0x03);
    // if (rom->tv_system == 0x01)
    // {
    //   rom->tv_system = 0x03
    // }
  }
  break;

  default:
    break;
  }

  rom->prg_rom_size *= 0x4000u;
  rom->prg_rom = malloc(rom->prg_rom_size);
  fread(rom->prg_rom, rom->prg_rom_size, 1, rom_file);

  if (rom->chr_rom_size > 0)
  {
    rom->chr_rom_size *= 0x2000u;
    rom->chr_rom = malloc(rom->chr_rom_size);
    fread(rom->chr_rom, rom->chr_rom_size, 1, rom_file);
  }
  else
  {
    rom->chr_rom_size = 0x2000u;
    rom->chr_rom = malloc(0x20000u);
  }

  return READ_ROM_SUCCESS;
}

void free_rom(struct nes_rom *rom)
{
  if (rom->prg_rom != NULL)
  {
    free(rom->prg_rom);
  }

  if (rom->chr_rom != NULL)
  {
    free(rom->chr_rom);
  }
}

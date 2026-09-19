#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "cpu/ic_6502.h"
#include "ppu/ppu.h"
#include "rom/rom.h"
#include "cartridge/cartridge.h"

struct mapper_0_cartridge
{
  struct cartridge base;
  uint8_t *prg_ram;
  uint16_t nametable_mirroring;
  uint16_t prg_rom_mirroring;
  uint8_t const *prg_rom;
  uint8_t *chr_rom;
};

void cartridge0_read_cpu_bus(uint8_t *restrict data, void *ctx, uint16_t address)
{
  struct mapper_0_cartridge *cartridge = (struct mapper_0_cartridge *)ctx;

  // Mapper 0 ignores everything outside its space.
  if (address < 0x6000)
  {
    // NOP
  }
  else if (address < 0x8000)
  {
    *data = cartridge->prg_ram[address & 0x1fff];
  }
  else
  {
    *data = cartridge->prg_rom[address & cartridge->prg_rom_mirroring];
  }
}

void cartridge0_write_cpu_bus(void *ctx, uint16_t address, uint8_t data)
{
  struct mapper_0_cartridge *cartridge = (struct mapper_0_cartridge *)ctx;

  if (address < 0x6000)
  {
    // NOP
  }
  else if (address < 0x8000)
  {
    cartridge->prg_ram[address - 0x6000] = data;
  }
  else
  {
    // prg_rom is not writable; ignore.
  }
}

void cartridge0_read_ppu_bus(uint8_t *restrict data, void *ctx, uint8_t *ppu_ram, uint16_t address)
{
  struct mapper_0_cartridge *cartridge = (struct mapper_0_cartridge *)ctx;
  if (address < 0x2000)
  {
    *data = cartridge->chr_rom[address];
  }
  else if (address < 0x4000)
  {
    *data = ppu_ram[address & 0x7ff];
  }
}

void cartridge0_write_ppu_bus(void *ctx, uint8_t *ppu_ram, uint16_t address, uint8_t data)
{
  // TODO: Nametable mirroring.
  if (address < 0x2000)
  {
    // NOP
  }
  else if (address < 0x4000)
  {
    ppu_ram[address & 0x7ff] = data;
  }
}

struct cartridge *cartridge_builder(struct nes_rom *rom)
{
  switch (rom->mapper_id)
  {
  case 0:
  {
    struct mapper_0_cartridge *cartridge = malloc(sizeof(struct mapper_0_cartridge));
    if (!cartridge)
    {
      return NULL;
    }
    *cartridge = (struct mapper_0_cartridge){
        .base = {
            .cpu_read = cartridge0_read_cpu_bus,
            .cpu_write = cartridge0_write_cpu_bus,
            .ppu_read = cartridge0_read_ppu_bus,
            .ppu_write = cartridge0_write_ppu_bus,
        },

        .prg_rom_mirroring = rom->prg_rom_size == 0x4000 ? 0x3fff : 0x7fff,
        .prg_rom = rom->prg_rom,
        .chr_rom = rom->chr_rom,
    };
    if (rom->prg_ram_size)
    {
      cartridge->prg_ram = malloc(rom->prg_ram_size);
    }
    return (struct cartridge *)cartridge;
  }
  default:
    assert(0 && "Mapper not implemented");
  }
}

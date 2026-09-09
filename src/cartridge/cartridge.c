#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include "cpu/cpu.h"
#include "ppu/ppu.h"
#include "rom/rom.h"
#include "cartridge/cartridge.h"

struct mapper_0_cartridge
{
  struct cartridge base;
  uint8_t prg_ram[0x8000];
  uint16_t nametable_mirroring;
  uint16_t prg_rom_mirroring;
  uint8_t const *prg_rom;
  uint8_t *chr_rom;
};

uint8_t cartridge0_read_cpu_bus(void *bus, uint16_t address)
{
  struct mapper_0_cartridge *cartridge = (struct mapper_0_cartridge *)bus;

  // Mapper 0 ignores everything outside its space.
  if (address < 0x6000)
  {
    // NOP
    return 0xff;
  }
  else if (address < 0x8000)
  {
    return cartridge->prg_ram[address]; // TODO: Mirroring.
  }
  else
  {
    return cartridge->prg_rom[address & cartridge->prg_rom_mirroring];
  }
}

void cartridge0_write_cpu_bus(void *bus, uint16_t address, uint8_t data)
{
  struct mapper_0_cartridge *cartridge = (struct mapper_0_cartridge *)bus;

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

uint8_t cartridge0_read_ppu_bus(void *bus, uint16_t address)
{
  struct mapper_0_cartridge *cartridge = (struct mapper_0_cartridge *)bus;
  return cartridge->chr_rom[address];
}

void cartridge0_write_ppu_bus(void *bus, uint16_t address, uint8_t data)
{
  // NOP because Mapper 0 only has PPU ROM.
}

struct cartridge *cartridge_builder(struct nes_rom *rom)
{
  switch (rom->mapper_id)
  {
  case 0:
  {
    struct mapper_0_cartridge *cartridge = malloc(sizeof(struct mapper_0_cartridge));
    *cartridge = (struct mapper_0_cartridge){
        .base = {
            .cpu_read = cartridge0_read_cpu_bus,
            .cpu_write = cartridge0_write_cpu_bus,
            .ppu_read = cartridge0_read_ppu_bus,
            .ppu_write = cartridge0_write_ppu_bus,
        },

        .prg_rom_mirroring = rom->prg_rom_size == 0x4000 ? 0x3fff : 0x7fff,
        .prg_rom = rom->prg_rom,
    };
    return (struct cartridge *)cartridge;
  }
  default:
    assert(0 && "Mapper not implemented");
  }
}

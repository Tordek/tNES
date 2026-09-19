#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "rom/rom.h"
#include "cartridge/cartridge.h"
#include "cpu/ic_6502.h"
#include "cpu/ic_rp2a03.h"
#include "ppu/ppu.h"
#include "machine/machine.h"
#include "sdl/sdl.h"

int main(int argc, char *argv[])
{
  if (argc != 2)
  {
    printf("Usage: %s rom\n", argv[0]);
    return -1;
  }

  struct nes_rom rom;
  FILE *rom_file = fopen(argv[1], "r");
  if (!rom_file)
  {
    fprintf(stderr, "File not found: %s\n", argv[1]);
    return 1;
  }

  int rc = read_rom(&rom, rom_file);
  if (rc)
  {
    fprintf(stderr, "Not a iNES/NES2.0 file\n");
    return rc;
  }

  printf("ROM info: %s\n", argv[1]);
  printf("Mapper: %d\n", rom.mapper_id);
  printf("PRG_ROM size: %d bytes\n", rom.prg_rom_size);
  printf("CHR_ROM size: %d bytes\n", rom.chr_rom_size);

  struct cartridge *cartridge = cartridge_builder(&rom);
  struct ic_rp2a03_registers cpu;
  struct ic_2c02_registers ppu;

  cpu.ic_6502.instruction = 0x00;
  cpu.ic_6502.page_jump = 0;
  cpu.ic_6502.nmi_requested = false;

  struct tnes_machine machine = {
      .cpu = &cpu,
      .ppu = &ppu,
      .cartridge = cartridge,
      .cycles = 0,
      .reset = true,
      .cpu_bus = {
          .context = &machine,
          .read = cpu_bus_read,
          .write = cpu_bus_write,
      },
      .ppu_bus = {
          .context = &machine,
          .read = ppu_bus_read,
          .write = ppu_bus_write,
      },
  };

  printf("Starting SDL...\n");
  struct renderer_state *state = initialize_sdl(&machine);
  printf("SDL initialized.\n");

  if (state == NULL)
  {
    return -1;
  }

  ic_2c02_init(&ppu);

  // int max_cycles = 1000000;
  while (1)
  {
    int hsync = tick_machine(&machine);

    if (hsync)
    {
      if (handle_inputs(state))
      {
        break;
      }
      render(state);
    }
  }

  return 0;
}

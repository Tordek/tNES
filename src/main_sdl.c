#include <stdint.h>
#include <stdio.h>

#include "rom/rom.h"
#include "cartridge/cartridge.h"
#include "machine/machine.h"
#include "sdl/sdl.h"
#include "cpu/cpu.h"
#include "ppu/ppu.h"

int main(int argc, char *argv[])
{
  if (argc != 2)
  {
    printf("Usage: %s rom\n", argv[0]);
    return -1;
  }

  struct nes_rom rom;
  FILE *rom_file = fopen(argv[1], "r");
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

  struct tnes_machine machine;
  struct cartridge *cartridge = cartridge_builder(&rom);
  struct ic_6502_registers cpu;
  struct ppu ppu;
  machine.cpu = &cpu;
  machine.ppu = &ppu;
  machine.cartridge = cartridge;

  printf("Starting SDL...\n");
  struct renderer_state *state = initialize_sdl(&machine);
  printf("SDL initialized.\n");

  if (state == NULL)
  {
    return -1;
  }

  // ic_2c02_reset(&ppu);
  // ic_6502_reset(&cpu);

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

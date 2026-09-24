#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "rom/rom.h"
#include "cartridge/cartridge.h"
#include "cpu/ic_6502.h"
#include "cpu/ic_rp2a03.h"
#include "ppu/ppu.h"
#include "machine/machine.h"
#include "sdl/sdl.h"

void mrand(void *target, size_t count)
{
  char *ref = target;
  for (size_t i = 0; i < count; i++)
  {
    ref[i] = rand();
  }
}

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
  printf("PRG_ROM size: %x (%d) bytes\n", rom.prg_rom_size, rom.prg_rom_size);
  printf("CHR_ROM size: %x (%d) bytes\n", rom.chr_rom_size, rom.chr_rom_size);

  struct cartridge *cartridge = cartridge_builder(&rom);
  if (!cartridge)
  {
    printf("Failed to load cartridge\n");
    return 1;
  }

  struct tnes_machine machine;
  mrand(&machine, sizeof(machine));
  init_machine(&machine);
  machine.cartridge = cartridge;
  machine.reset = true;

  printf("Starting SDL...\n");
  struct renderer_state *state = initialize_sdl(&machine);
  printf("SDL initialized.\n");

  if (state == NULL)
  {
    return -1;
  }

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

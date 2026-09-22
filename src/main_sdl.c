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
  printf("PRG_ROM size: %d bytes\n", rom.prg_rom_size);
  printf("CHR_ROM size: %d bytes\n", rom.chr_rom_size);

  struct cartridge *cartridge = cartridge_builder(&rom);
  if (!cartridge)
  {
    printf("Failed to load cartridge\n");
    return 1;
  }
  struct ic_rp2a03_registers cpu;
  struct ic_2c02_registers ppu;

  mrand(&cpu, sizeof(cpu));
  mrand(&ppu, sizeof(ppu));

  cpu.ic_6502.instruction = 0x00;
  cpu.ic_6502.page_jump = 0;
  cpu.ic_6502.nmi_requested = false;
  cpu.cycles = 0;
  cpu.player1 = (struct controller){0};
  cpu.player2 = (struct controller){0};
  cpu.dma_write_time = 0;

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

  cpu.ic_6502.instruction = 0;
  mrand(&machine.main_ram, sizeof(machine.main_ram));

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

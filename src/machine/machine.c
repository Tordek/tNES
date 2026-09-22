#include <stdint.h>
#include <stdbool.h>

#include "cpu/ic_6502.h"
#include "cpu/ic_rp2a03.h"
#include "ppu/ppu.h"
#include "rom/rom.h"
#include "cartridge/cartridge.h"
#include "machine/machine.h"
#include "machine/machine_debug.h"

void cpu_bus_read(uint8_t *restrict result, void *device, uint16_t address)
{
  struct tnes_machine *machine = (struct tnes_machine *)device;
  *result = machine->cpu_bus_data;

  // The cartridge sees all reads
  machine->cartridge->cpu_read(result, machine->cartridge, address);

  // The first 0x2000 is mirrored as 4 0x800-byte blocks of main RAM.
  if (address < 0x2000)
  {
    *result = machine->main_ram[address & 0x07ff];
  }
  // The next 0x2000 is mirrored as 0x400 8-byte blocks of PPU registers.
  else if (address < 0x4000)
  {
    ic_2c02_mmapped_read(result, machine->ppu, &machine->ppu_bus, address & 0x07);
  }
  // 0x20 bytes of APU and Controller handling.
  else if (address < 0x4020)
  {
    ic_rp2a03_mmapped_read(result, machine->cpu, address & 0x001f);
  }

  machine->cpu_bus_data = *result;
}

void cpu_bus_write(void *device, uint16_t address, uint8_t data)
{
  struct tnes_machine *machine = (struct tnes_machine *)device;
  machine->cpu_bus_data = data;

  // The cartridge sees all writes, even if it doesn't interact with them.
  machine->cartridge->cpu_write(machine->cartridge, address, data);

  // The first 0x2000 is mirrored as 4 0x800-byte blocks of main RAM.
  if (address < 0x2000)
  {
    machine->main_ram[address & 0x07ff] = data;
  }
  // The next 0x2000 is mirrored as 0x400 8-byte blocks of PPU registers.
  else if (address < 0x4000)
  {
    ic_2c02_mmapped_write(machine->ppu, &machine->ppu_bus, address & 0x07, data);
  }
  // 0x20 bytes of APU and Controller handling.
  else if (address < 0x4020)
  {
    ic_rp2a03_mmapped_write(machine->cpu, address & 0x001f, data);
  }
}

// TODO: Handle PPU buffer here.
void ppu_bus_read(uint8_t *result, void *device, uint16_t address)
{
  struct tnes_machine *machine = (struct tnes_machine *)device;

  *result = machine->ppu_bus_data;

  // The first 0x3f00 are handled by the cartridge
  if (address < 0x3f00)
  {
    machine->cartridge->ppu_read(result, machine->cartridge, machine->ppu_ram, address);
  }
  // The last 0x100 are 8 0x20 mirrors of palette ram
  else if (address < 0x4000)
  {
    *result = machine->ppu->palette[address & 0x1f];
  }

  machine->ppu_bus_data = *result;
}

void ppu_bus_write(void *device, uint16_t address, uint8_t data)
{
  struct tnes_machine *machine = (struct tnes_machine *)device;
  machine->ppu_bus_data = data;

  // The first 0x3f00 are handled by the cartridge
  if (address < 0x3f00)
  {
    machine->cartridge->ppu_write(machine->cartridge, machine->ppu_ram, address, data);
  }
  // The last 0x100 are 8 0x20 mirrors of palette ram
  else if (address < 0x4000)
  {
    uint8_t palette_pos = address & 0x1f;
    if ((palette_pos & 0x03) == 0)
    {
      machine->ppu->palette[palette_pos & 0x0F] = data;
      machine->ppu->palette[palette_pos | 0x10] = data;
    }
    else
    {
      machine->ppu->palette[palette_pos] = data;
    }
  }
}

int tick_machine(struct tnes_machine *machine)
{
  if (machine->reset)
  {
    ic_2c02_reset(machine->ppu);
  }

  int vblank = ic_2c02_clock(machine->ppu, &machine->ppu_bus);

  if (vblank && machine->ppu->do_nmi)
  {
    ic_rp2a03_nmi(machine->cpu);
  }

  if (machine->cycles % 3 == 0)
  {
    if (machine->cpu->ic_6502.cycle == 0)
    {
      // print_status(machine);
    }
    ic_rp2a03_tick(machine->cpu, &machine->cpu_bus, false /* machine->apu.irq || machine->cartridge.irq */, machine->reset);
  }

  machine->reset = false;
  machine->cycles++;
  return vblank;
}

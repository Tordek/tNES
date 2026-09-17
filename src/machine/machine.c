#include <stdint.h>
#include <stdbool.h>
#include "cpu/cpu.h"
#include "ppu/ppu.h"
#include "rom/rom.h"
#include "machine/machine.h"
#include "cartridge/cartridge.h"

uint8_t cpu_bus_read(void *device, uint16_t address)
{
  struct tnes_machine *machine = (struct tnes_machine *)device;

  // The cartridge sees all reads
  uint8_t cartridge_response = machine->cartridge->cpu_read(machine->cartridge, address);

  // The first 0x2000 is mirrored as 4 0x800-byte blocks of main RAM.
  if (address < 0x2000)
  {
    return machine->main_ram[address & 0x07ff];
  }
  // The next 0x2000 is mirrored as 0x400 8-byte blocks of PPU registers.
  else if (address < 0x4000)
  {
    struct ic_2c02_bus bus = {
        .context = device,
        .read = &ppu_bus_read,
        .write = &ppu_bus_write};
    return ic_2c02_mmapped_read(machine->ppu, &bus, address & 0x2007);
  }
  // 0x20 bytes of APU and Controller handling.
  else if (address < 0x4020)
  {
    // if (address == 0x4016 || address == 0x4017)
    // {
    //   // TODO: ?
    //   return controllers_read(machine->base.controllers, address & 0x0001);
    // }
    // return ic_rp2a03_read(machine->base.apu, address & 0x001f);
    return 0;
  }
  // And the rest is for the Mapper to handle
  else
  {
    return cartridge_response;
  }
}

void cpu_bus_write(void *device, uint16_t address, uint8_t data)
{
  struct tnes_machine *machine = (struct tnes_machine *)device;

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
    struct ic_2c02_bus bus = {
        .context = device,
        .read = &ppu_bus_read,
        .write = &ppu_bus_write};
    ic_2c02_mmapped_write(machine->ppu, &bus, address & 0x2007, data);
  }
  // 0x20 bytes of APU and Controller handling.
  else if (address < 0x4020)
  {
    // cartridge->base.dma_page = data << 8;
    // cartridge->base.dma_write_time = 513 + (cartridge->base.cycles & 0x01);
    // if (address == 0x4016 || address == 0x4017)
    // {
    //   // TODO: ?
    //   return controllers_read(machine->base.controllers, address & 0x0001);
    // }
    // return ic_rp2a03_read(machine->base.apu, address & 0x001f);
  }
}

uint8_t ppu_bus_read(void *device, uint16_t address)
{
  struct tnes_machine *machine = (struct tnes_machine *)device;

  // The cartridge sees all reads
  uint8_t cartridge_response = machine->cartridge->ppu_read(machine->cartridge, machine->ppu_ram, address);

  // The first 0x3f00 are handled by the cartridge
  if (address < 0x3f00)
  {
    return cartridge_response;
  }
  // The last 0x100 are 8 0x20 mirrors of palette ram
  else
  {
    // return machine->palette_ram[address & 0x1f];

    return machine->palette_ram[address & 0x1f];
  }
}

void ppu_bus_write(void *device, uint16_t address, uint8_t data)
{
  struct tnes_machine *machine = (struct tnes_machine *)device;

  // The cartridge sees all writes
  machine->cartridge->ppu_write(machine->cartridge, machine->ppu_ram, address, data);

  // The first 0x3f00 are handled by the cartridge
  if (address < 0x3f00)
  {
    // NOP
  }
  // The last 0x100 are 8 0x20 mirrors of palette ram
  else
  {
    printf("color %d\n", data);
    // machine->palette_ram[address & 0x1f] = data;
    // machine->ppu->palette[address & 0x1f] = data;
    uint8_t palette_pos = address & 0x1f;
    if ((palette_pos & 0x03) == 0)
    {
      machine->palette_ram[palette_pos & 0x0F] = data;
      machine->palette_ram[palette_pos | 0x10] = data;
    }
    else
    {
      machine->palette_ram[palette_pos] = data;
    }
  }
}

int tick_machine(struct tnes_machine *machine)
{
  if (machine->reset)
  {
    ic_2c02_reset(machine->ppu);
  }

  struct ic_2c02_bus ppu_bus =
      {
          .context = machine,
          .read = ppu_bus_read,
          .write = ppu_bus_write};
  int vblank = ic_2c02_clock(machine->ppu, &ppu_bus);

  if (vblank && machine->ppu->do_nmi)
  {
    nmi(machine->cpu);
  }

  if (machine->cycles % 3 == 0)
  {
    struct ic_6502_bus cpu_bus =
        {
            .context = machine,
            .read = cpu_bus_read,
            .write = cpu_bus_write};

    tick_cpu(machine->cpu, &cpu_bus, false /* machine->apu.irq || machine->cartridge.irq */, machine->reset);
  }

  machine->reset = false;
  machine->cycles++;
  return vblank;
}

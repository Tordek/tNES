#include <stdint.h>
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
    // return ic_2c02_read(machine->base.ppu, address & 0x0007);
    return 0;
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
    // return ic_2c02_write(machine->base.ppu, address & 0x0007);
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
  uint8_t cartridge_response = machine->cartridge->ppu_read(machine->cartridge, address);

  // The first 0x3f00 are handled by the cartridge
  if (address < 0x3f00)
  {
    return cartridge_response;
  }
  // The last 0x100 are 8 0x20 mirrors of palette ram
  else
  {
    return machine->palette_ram[address & 0x1f];
  }
}

void ppu_bus_write(void *device, uint16_t address, uint8_t data)
{
  struct tnes_machine *machine = (struct tnes_machine *)device;

  // The cartridge sees all writes
  machine->cartridge->ppu_write(machine->cartridge, address, data);

  // The first 0x3f00 are handled by the cartridge
  if (address < 0x3f00)
  {
    // NOP
  }
  // The last 0x100 are 8 0x20 mirrors of palette ram
  else
  {
    machine->palette_ram[address & 0x1f] = data;
  }
}

int tick_machine(struct tnes_machine *machine)
{
  /* data */
  struct ic_6502_bus cpu_bus =
      {
          .context = machine,
          .read = cpu_bus_read,
          .write = cpu_bus_write};

  tick_cpu(machine->cpu, &cpu_bus, false /* machine->apu.irq || machine->cartridge.irq */, machine->reset);
  machine->reset = false;
  machine->cycles++;
  machine->ppu->clock += 3;
  return 1;
}

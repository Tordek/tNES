#include <stdint.h>
#include <stdbool.h>

#include "cpu/ic_6502.h"
#include "cpu/ic_rp2a03.h"
#include "ppu/ppu.h"
#include "rom/rom.h"
#include "cartridge/cartridge.h"
#include "machine/machine.h"
#include "machine/machine_debug.h"

#define CPU_RATE 1789773.0
#define AUDIO_RATE 48000.0

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
    machine->cpu_bus_data = *result;
  }
  // The next 0x2000 is mirrored as 0x400 8-byte blocks of PPU registers.
  else if (address < 0x4000)
  {
    ic_2c02_mmapped_read(result, &machine->ppu, &machine->ppu_bus, address & 0x07);
    machine->cpu_bus_data = *result;
  }
  // 0x20 bytes of APU and Controller handling.
  else if (address < 0x4020)
  {
    ic_rp2a03_mmapped_read(result, &machine->cpu, address & 0x001f);
  }
  else
  {
    machine->cpu_bus_data = *result;
  }
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
    ic_2c02_mmapped_write(&machine->ppu, &machine->ppu_bus, address & 0x07, data);
  }
  // 0x20 bytes of APU and Controller handling.
  else if (address < 0x4020)
  {
    ic_rp2a03_mmapped_write(&machine->cpu, address & 0x001f, data);
  }
}

void ppu_bus_read(uint8_t *result, void *device, uint16_t address)
{
  struct tnes_machine *machine = (struct tnes_machine *)device;

  machine->cartridge->ppu_read(result, machine->cartridge, machine->ppu_ram, address);
}

void ppu_bus_write(void *device, uint16_t address, uint8_t data)
{
  struct tnes_machine *machine = (struct tnes_machine *)device;

  machine->cartridge->ppu_write(machine->cartridge, machine->ppu_ram, address, data);
}

void init_machine(struct tnes_machine *machine)
{
  machine->cycles = 0;
  machine->cpu_bus = (struct ic_6502_bus){
      .context = machine,
      .read = cpu_bus_read,
      .write = cpu_bus_write,
  };
  machine->ppu_bus = (struct ic_2c02_bus){
      .context = machine,
      .read = ppu_bus_read,
      .write = ppu_bus_write,
  };

  machine->audio_start = 0;
  machine->audio_end = 0;
  machine->sample_count = 0;

  ic_rp2a03_init(&machine->cpu);
  ic_2c02_init(&machine->ppu);
}

int tick_machine(struct tnes_machine *machine)
{
  size_t next =
      (machine->audio_end + 1) % AUDIO_BUFFER_SIZE;

  if (next == machine->audio_start)
    return false; // buffer full

  if (machine->reset)
  {
    ic_2c02_reset(&machine->ppu);
  }

  int vblank = ic_2c02_clock(&machine->ppu, &machine->ppu_bus);

  if (vblank && machine->ppu.do_nmi)
  {
    ic_rp2a03_nmi(&machine->cpu);
  }

  if (machine->cycles % 3 == 0)
  {
    if (machine->cpu.ic_6502.cycle == 0)
    {
      // print_status(machine);
    }
    ic_rp2a03_tick(&machine->cpu, &machine->cpu_bus, false /* machine->apu.irq || machine->cartridge.irq */, machine->reset);

    machine->sample_count += AUDIO_RATE;
    if (machine->sample_count >= CPU_RATE)
    {
      machine->sample_count -= CPU_RATE;

      uint8_t pulse_group = machine->cpu.pulse[0].value + machine->cpu.pulse[1].value;
      float pulse_out = pulse_group == 0 ? 0 : 95.88 / (8128.0 / pulse_group + 100);
      float tnd_group = machine->cpu.triangle_value / 8227.0 + (machine->cpu.noise_value / 12241.0) + 0;
      float tnd_out = tnd_group == 0 ? 0 : 159.79 / (1 / tnd_group + 100);

      machine->audio_samples[machine->audio_end++] = pulse_out + tnd_out;
      machine->audio_end %= AUDIO_BUFFER_SIZE;
    }
  }

  machine->reset = false;
  machine->cycles++;
  return vblank;
}

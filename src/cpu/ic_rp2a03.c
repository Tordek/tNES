#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cpu/ic_6502.h"
#include "cpu/ic_rp2a03.h"

void ic_rp2a03_tick(struct ic_rp2a03_registers *cpu, struct ic_6502_bus *bus, bool irq, bool reset)
{
  if (cpu->dma_write_time > 512)
  {
    uint8_t val;
    bus->read(&val, bus->context, cpu->dma_page);
    cpu->dma_write_time--;
  }
  else if (cpu->dma_write_time > 0)
  {
    cpu->dma_write_time--;
    // TODO: Read and write on separate cycles.
    if (cpu->dma_write_time % 2 == 0)
    {
      uint16_t byte = 255 - (cpu->dma_write_time >> 1);
      uint8_t val;
      bus->read(&val, bus->context, cpu->dma_page | byte);
      bus->write(bus->context, 0x2004, val);
    }
  }
  else
  {
    ic_6502_tick(&cpu->ic_6502, bus, irq, reset);
  }
}

void ic_rp2a03_mmapped_read(uint8_t *restrict data, struct ic_rp2a03_registers *cpu, uint16_t address)
{
  switch (address)
  {
  case 0x16:
  {
    if (cpu->player1.strobe)
    {
      cpu->player1.latch = cpu->player1.buttons.raw;
    }

    uint8_t read = cpu->player1.latch & 0x01;
    cpu->player1.latch = (cpu->player1.latch >> 1) | 0x80;
    *data = read;
    return;
  }
  case 0x17:
  {
    if (cpu->player2.strobe)
    {
      cpu->player2.latch = cpu->player2.buttons.raw;
    }

    *data = cpu->player2.latch & 0x01;
    cpu->player2.latch = (cpu->player2.latch >> 1) | 0x80;
    return;
  }
  default:
    // Open bus
  }
}

void ic_rp2a03_mmapped_write(struct ic_rp2a03_registers *cpu, uint16_t address, uint8_t data)
{
  switch (address)
  {
  case 0x14:
    cpu->dma_page = data << 8;
    cpu->dma_write_time = 513 + (cpu->cycles & 0x01);
    break;
  case 0x16:
  {
    cpu->player1.strobe = data & 0x01;
    if (cpu->player1.strobe)
    {
      cpu->player1.latch = cpu->player1.buttons.raw;
    }
  }
  break;
  case 0x17:
  {
    cpu->player2.strobe = data & 0x01;
    if (cpu->player2.strobe)
    {
      cpu->player2.latch = cpu->player2.buttons.raw;
    }
  }
  break;
  }
}

void ic_rp2a03_nmi(struct ic_rp2a03_registers *cpu)
{
  ic_6502_nmi(&cpu->ic_6502);
}

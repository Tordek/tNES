#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cpu/ic_6502.h"
#include "cpu/ic_rp2a03.h"

void ic_rp2a03_tick(struct ic_rp2a03_registers *cpu, struct ic_6502_bus *bus, bool irq, bool reset)
{
  ic_6502_tick(&cpu->ic_6502, bus, irq, reset);
}

uint8_t ic_rp2a03_mmapped_read(struct ic_rp2a03_registers *cpu, uint16_t address)
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
    return read;
  }
  case 0x17:
  {
    if (cpu->player2.strobe)
    {
      cpu->player2.latch = cpu->player2.buttons.raw;
    }

    uint8_t read = cpu->player2.latch & 0x01;
    cpu->player2.latch = (cpu->player2.latch >> 1) | 0x80;
    return read;
  }
  default:
    return 0;
  }
}

void ic_rp2a03_mmapped_write(struct ic_rp2a03_registers *cpu, uint16_t address, uint8_t data)
{
  switch (address)
  {
  case 0x16:
  {
    cpu->player1.strobe = data & 0x01;
    if (cpu->player1.strobe)
    {
      cpu->player1.latch = cpu->player1.buttons.raw;
    }
  }
  case 0x17:
  {
    cpu->player2.strobe = data & 0x01;
    if (cpu->player2.strobe)
    {
      cpu->player2.latch = cpu->player2.buttons.raw;
    }
  }
  }
}

void ic_rp2a03_nmi(struct ic_rp2a03_registers *cpu)
{
  ic_6502_nmi(&cpu->ic_6502);
}

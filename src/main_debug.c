#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rom/rom.h"
#include "cpu/cpu.h"
#include "ppu/ppu.h"
#include "machine/machine.h"
#include "cartridge/cartridge.h"

char const names[256][5] = {
    " BRK",
    " ORA",
    " KIL",
    "*SLO",
    "*NOP",
    " ORA",
    " ASL",
    "*SLO",
    " PHP",
    " ORA",
    " ASL",
    " ANC",
    "*NOP",
    " ORA",
    " ASL",
    "*SLO",
    " BPL",
    " ORA",
    " KIL",
    "*SLO",
    "*NOP",
    " ORA",
    " ASL",
    "*SLO",
    " CLC",
    " ORA",
    "*NOP",
    "*SLO",
    "*NOP",
    " ORA",
    " ASL",
    "*SLO",
    " JSR",
    " AND",
    " KIL",
    "*RLA",
    " BIT",
    " AND",
    " ROL",
    "*RLA",
    " PLP",
    " AND",
    " ROL",
    " ANC",
    " BIT",
    " AND",
    " ROL",
    "*RLA",
    " BMI",
    " AND",
    " KIL",
    "*RLA",
    "*NOP",
    " AND",
    " ROL",
    "*RLA",
    " SEC",
    " AND",
    "*NOP",
    "*RLA",
    "*NOP",
    " AND",
    " ROL",
    "*RLA",
    " RTI",
    " EOR",
    " KIL",
    "*SRE",
    "*NOP",
    " EOR",
    " LSR",
    "*SRE",
    " PHA",
    " EOR",
    " LSR",
    " ALR",
    " JMP",
    " EOR",
    " LSR",
    "*SRE",
    " BVC",
    " EOR",
    " KIL",
    "*SRE",
    "*NOP",
    " EOR",
    " LSR",
    "*SRE",
    " CLI",
    " EOR",
    "*NOP",
    "*SRE",
    "*NOP",
    " EOR",
    " LSR",
    "*SRE",
    " RTS",
    " ADC",
    " KIL",
    "*RRA",
    "*NOP",
    " ADC",
    " ROR",
    "*RRA",
    " PLA",
    " ADC",
    " ROR",
    " ARR",
    " JMP",
    " ADC",
    " ROR",
    "*RRA",
    " BVS",
    " ADC",
    " KIL",
    "*RRA",
    "*NOP",
    " ADC",
    " ROR",
    "*RRA",
    " SEI",
    " ADC",
    "*NOP",
    "*RRA",
    "*NOP",
    " ADC",
    " ROR",
    "*RRA",
    "*NOP",
    " STA",
    " NOP",
    "*SAX",
    " STY",
    " STA",
    " STX",
    "*SAX",
    " DEY",
    " NOP",
    " TXA",
    " XAA",
    " STY",
    " STA",
    " STX",
    "*SAX",
    " BCC",
    " STA",
    " KIL",
    " AHX",
    " STY",
    " STA",
    " STX",
    "*SAX",
    " TYA",
    " STA",
    " TXS",
    " TAS",
    " SHY",
    " STA",
    " SHX",
    " AHX",
    " LDY",
    " LDA",
    " LDX",
    "*LAX",
    " LDY",
    " LDA",
    " LDX",
    "*LAX",
    " TAY",
    " LDA",
    " TAX",
    " LAX",
    " LDY",
    " LDA",
    " LDX",
    "*LAX",
    " BCS",
    " LDA",
    " KIL",
    "*LAX",
    " LDY",
    " LDA",
    " LDX",
    "*LAX",
    " CLV",
    " LDA",
    " TSX",
    " LAS",
    " LDY",
    " LDA",
    " LDX",
    "*LAX",
    " CPY",
    " CMP",
    " NOP",
    "*DCP",
    " CPY",
    " CMP",
    " DEC",
    "*DCP",
    " INY",
    " CMP",
    " DEX",
    " AXS",
    " CPY",
    " CMP",
    " DEC",
    "*DCP",
    " BNE",
    " CMP",
    " KIL",
    "*DCP",
    "*NOP",
    " CMP",
    " DEC",
    "*DCP",
    " CLD",
    " CMP",
    "*NOP",
    "*DCP",
    "*NOP",
    " CMP",
    " DEC",
    "*DCP",
    " CPX",
    " SBC",
    " NOP",
    "*ISB",
    " CPX",
    " SBC",
    " INC",
    "*ISB",
    " INX",
    " SBC",
    " NOP",
    "*SBC",
    " CPX",
    " SBC",
    " INC",
    "*ISB",
    " BEQ",
    " SBC",
    " KIL",
    "*ISB",
    "*NOP",
    " SBC",
    " INC",
    "*ISB",
    " SED",
    " SBC",
    "*NOP",
    "*ISB",
    "*NOP",
    " SBC",
    " INC",
    "*ISB",
};

enum addressing_mode
{
  ADR_IMP,
  ADR_IMM,
  ADR_ACC,
  ADR_ABS,
  ADR_ABSJ,
  ADR_ZP0,
  ADR_ZPY,
  ADR_REL,
  ADR_ZPX,
  ADR_IND,
  ADR_ABX,
  ADR_ABY,
  ADR_IZX,
  ADR_IZY,
};

enum addressing_mode addressing_mode[256] = {
    [0x00] = ADR_IMP,
    [0x01] = ADR_IZX,
    [0x02] = ADR_IMM,
    [0x03] = ADR_IZX,
    [0x04] = ADR_ZP0,
    [0x05] = ADR_ZP0,
    [0x06] = ADR_ZP0,
    [0x07] = ADR_ZP0,
    [0x08] = ADR_IMP,
    [0x09] = ADR_IMM,
    [0x0a] = ADR_ACC,
    [0x0b] = ADR_IMM,
    [0x0c] = ADR_ABS,
    [0x0d] = ADR_ABS,
    [0x0e] = ADR_ABS,
    [0x0f] = ADR_ABS,
    [0x10] = ADR_REL,
    [0x11] = ADR_IZY,
    [0x12] = ADR_IMM,
    [0x13] = ADR_IZY,
    [0x14] = ADR_ZPX,
    [0x15] = ADR_ZPX,
    [0x16] = ADR_ZPX,
    [0x17] = ADR_ZPX,
    [0x18] = ADR_IMP,
    [0x19] = ADR_ABY,
    [0x1a] = ADR_IMP,
    [0x1b] = ADR_ABY,
    [0x1c] = ADR_ABX,
    [0x1d] = ADR_ABX,
    [0x1e] = ADR_ABX,
    [0x1f] = ADR_ABX,
    [0x20] = ADR_ABSJ,
    [0x21] = ADR_IZX,
    [0x22] = ADR_IMM,
    [0x23] = ADR_IZX,
    [0x24] = ADR_ZP0,
    [0x25] = ADR_ZP0,
    [0x26] = ADR_ZP0,
    [0x27] = ADR_ZP0,
    [0x28] = ADR_IMP,
    [0x29] = ADR_IMM,
    [0x2a] = ADR_ACC,
    [0x2b] = ADR_IMM,
    [0x2c] = ADR_ABS,
    [0x2d] = ADR_ABS,
    [0x2e] = ADR_ABS,
    [0x2f] = ADR_ABS,
    [0x30] = ADR_REL,
    [0x31] = ADR_IZY,
    [0x32] = ADR_IMM,
    [0x33] = ADR_IZY,
    [0x34] = ADR_ZPX,
    [0x35] = ADR_ZPX,
    [0x36] = ADR_ZPX,
    [0x37] = ADR_ZPX,
    [0x38] = ADR_IMP,
    [0x39] = ADR_ABY,
    [0x3a] = ADR_IMP,
    [0x3b] = ADR_ABY,
    [0x3c] = ADR_ABX,
    [0x3d] = ADR_ABX,
    [0x3e] = ADR_ABX,
    [0x3f] = ADR_ABX,
    [0x40] = ADR_IMP,
    [0x41] = ADR_IZX,
    [0x42] = ADR_IMM,
    [0x43] = ADR_IZX,
    [0x44] = ADR_ZP0,
    [0x45] = ADR_ZP0,
    [0x46] = ADR_ZP0,
    [0x47] = ADR_ZP0,
    [0x48] = ADR_IMP,
    [0x49] = ADR_IMM,
    [0x4a] = ADR_ACC,
    [0x4b] = ADR_IMM,
    [0x4c] = ADR_ABSJ,
    [0x4d] = ADR_ABS,
    [0x4e] = ADR_ABS,
    [0x4f] = ADR_ABS,
    [0x50] = ADR_REL,
    [0x51] = ADR_IZY,
    [0x52] = ADR_IMM,
    [0x53] = ADR_IZY,
    [0x54] = ADR_ZPX,
    [0x55] = ADR_ZPX,
    [0x56] = ADR_ZPX,
    [0x57] = ADR_ZPX,
    [0x58] = ADR_IMP,
    [0x59] = ADR_ABY,
    [0x5a] = ADR_IMP,
    [0x5b] = ADR_ABY,
    [0x5c] = ADR_ABX,
    [0x5d] = ADR_ABX,
    [0x5e] = ADR_ABX,
    [0x5f] = ADR_ABX,
    [0x60] = ADR_IMP,
    [0x61] = ADR_IZX,
    [0x62] = ADR_IMM,
    [0x63] = ADR_IZX,
    [0x64] = ADR_ZP0,
    [0x65] = ADR_ZP0,
    [0x66] = ADR_ZP0,
    [0x67] = ADR_ZP0,
    [0x68] = ADR_IMP,
    [0x69] = ADR_IMM,
    [0x6a] = ADR_ACC,
    [0x6b] = ADR_IMM,
    [0x6c] = ADR_IND,
    [0x6d] = ADR_ABS,
    [0x6e] = ADR_ABS,
    [0x6f] = ADR_ABS,
    [0x70] = ADR_REL,
    [0x71] = ADR_IZY,
    [0x72] = ADR_IMM,
    [0x73] = ADR_IZY,
    [0x74] = ADR_ZPX,
    [0x75] = ADR_ZPX,
    [0x76] = ADR_ZPX,
    [0x77] = ADR_ZPX,
    [0x78] = ADR_IMP,
    [0x79] = ADR_ABY,
    [0x7a] = ADR_IMP,
    [0x7b] = ADR_ABY,
    [0x7c] = ADR_ABX,
    [0x7d] = ADR_ABX,
    [0x7e] = ADR_ABX,
    [0x7f] = ADR_ABX,
    [0x80] = ADR_IMM,
    [0x81] = ADR_IZX,
    [0x82] = ADR_IMP,
    [0x83] = ADR_IZX,
    [0x84] = ADR_ZP0,
    [0x85] = ADR_ZP0,
    [0x86] = ADR_ZP0,
    [0x87] = ADR_ZP0,
    [0x88] = ADR_IMP,
    [0x89] = ADR_IMP,
    [0x8a] = ADR_IMP,
    [0x8b] = ADR_IMM,
    [0x8c] = ADR_ABS,
    [0x8d] = ADR_ABS,
    [0x8e] = ADR_ABS,
    [0x8f] = ADR_ABS,
    [0x90] = ADR_REL,
    [0x91] = ADR_IZY,
    [0x92] = ADR_IMM,
    [0x93] = ADR_IZY,
    [0x94] = ADR_ZPX,
    [0x95] = ADR_ZPX,
    [0x96] = ADR_ZPY,
    [0x97] = ADR_ZPY,
    [0x98] = ADR_IMP,
    [0x99] = ADR_ABY,
    [0x9a] = ADR_IMP,
    [0x9b] = ADR_ABY,
    [0x9c] = ADR_ABX,
    [0x9d] = ADR_ABX,
    [0x9e] = ADR_ABY,
    [0x9f] = ADR_ABY,
    [0xa0] = ADR_IMM,
    [0xa1] = ADR_IZX,
    [0xa2] = ADR_IMM,
    [0xa3] = ADR_IZX,
    [0xa4] = ADR_ZP0,
    [0xa5] = ADR_ZP0,
    [0xa6] = ADR_ZP0,
    [0xa7] = ADR_ZP0,
    [0xa8] = ADR_IMP,
    [0xa9] = ADR_IMM,
    [0xaa] = ADR_IMP,
    [0xab] = ADR_IMM,
    [0xac] = ADR_ABS,
    [0xad] = ADR_ABS,
    [0xae] = ADR_ABS,
    [0xaf] = ADR_ABS,
    [0xb0] = ADR_REL,
    [0xb1] = ADR_IZY,
    [0xb2] = ADR_IMM,
    [0xb3] = ADR_IZY,
    [0xb4] = ADR_ZPX,
    [0xb5] = ADR_ZPX,
    [0xb6] = ADR_ZPY,
    [0xb7] = ADR_ZPY,
    [0xb8] = ADR_IMP,
    [0xb9] = ADR_ABY,
    [0xba] = ADR_IMP,
    [0xbb] = ADR_ABY,
    [0xbc] = ADR_ABX,
    [0xbd] = ADR_ABX,
    [0xbe] = ADR_ABY,
    [0xbf] = ADR_ABY,
    [0xc0] = ADR_IMM,
    [0xc1] = ADR_IZX,
    [0xc2] = ADR_IMP,
    [0xc3] = ADR_IZX,
    [0xc4] = ADR_ZP0,
    [0xc5] = ADR_ZP0,
    [0xc6] = ADR_ZP0,
    [0xc7] = ADR_ZP0,
    [0xc8] = ADR_IMP,
    [0xc9] = ADR_IMM,
    [0xca] = ADR_IMP,
    [0xcb] = ADR_IMM,
    [0xcc] = ADR_ABS,
    [0xcd] = ADR_ABS,
    [0xce] = ADR_ABS,
    [0xcf] = ADR_ABS,
    [0xd0] = ADR_REL,
    [0xd1] = ADR_IZY,
    [0xd2] = ADR_IMM,
    [0xd3] = ADR_IZY,
    [0xd4] = ADR_ZPX,
    [0xd5] = ADR_ZPX,
    [0xd6] = ADR_ZPX,
    [0xd7] = ADR_ZPX,
    [0xd8] = ADR_IMP,
    [0xd9] = ADR_ABY,
    [0xda] = ADR_IMP,
    [0xdb] = ADR_ABY,
    [0xdc] = ADR_ABX,
    [0xdd] = ADR_ABX,
    [0xde] = ADR_ABX,
    [0xdf] = ADR_ABX,
    [0xe0] = ADR_IMM,
    [0xe1] = ADR_IZX,
    [0xe2] = ADR_IMP,
    [0xe3] = ADR_IZX,
    [0xe4] = ADR_ZP0,
    [0xe5] = ADR_ZP0,
    [0xe6] = ADR_ZP0,
    [0xe7] = ADR_ZP0,
    [0xe8] = ADR_IMP,
    [0xe9] = ADR_IMM,
    [0xea] = ADR_IMP,
    [0xeb] = ADR_IMM,
    [0xec] = ADR_ABS,
    [0xed] = ADR_ABS,
    [0xee] = ADR_ABS,
    [0xef] = ADR_ABS,
    [0xf0] = ADR_REL,
    [0xf1] = ADR_IZY,
    [0xf2] = ADR_IMM,
    [0xf3] = ADR_IZY,
    [0xf4] = ADR_ZPX,
    [0xf5] = ADR_ZPX,
    [0xf6] = ADR_ZPX,
    [0xf7] = ADR_ZPX,
    [0xf8] = ADR_IMP,
    [0xf9] = ADR_ABY,
    [0xfa] = ADR_IMP,
    [0xfb] = ADR_ABY,
    [0xfc] = ADR_ABX,
    [0xfd] = ADR_ABX,
    [0xfe] = ADR_ABX,
    [0xff] = ADR_ABX,
};

void print_status(struct tnes_machine *machine)
{
  struct ic_6502_registers *cpu = machine->cpu;
  struct ppu *ppu = machine->ppu;

  uint8_t opcode = cpu_bus_read(machine, cpu->pc);

  printf("%04X  %02X ", cpu->pc, opcode);
  switch (addressing_mode[opcode])
  {
  case ADR_IMP:
  {
    printf("      %s                             ", names[opcode]);
  }
  break;

  case ADR_ACC:
  {
    printf("      %s A                           ", names[opcode]);
  }
  break;

  case ADR_IMM:
  {
    uint8_t ad = cpu_bus_read(machine, cpu->pc + 1);
    printf("%02X    %s #$%02X                        ", ad, names[opcode], ad);
  }
  break;

  case ADR_ZP0:
  {
    // ZP0
    uint8_t ad = cpu_bus_read(machine, cpu->pc + 1);
    uint8_t val = cpu_bus_read(machine, ad);

    printf("%02X    %s $%02X = %02X                    ", ad, names[opcode], ad, val);
  }
  break;

  case ADR_ZPX:
  {
    uint8_t ad = cpu_bus_read(machine, cpu->pc + 1);
    uint8_t x = (ad + cpu->x) & 0xFF;
    uint8_t data = cpu_bus_read(machine, x);
    printf("%02X    %s $%02X,X @ %02X = %02X             ", ad, names[opcode], ad, x, data);
  }
  break;

  case ADR_ZPY:
  {
    uint8_t ad = cpu_bus_read(machine, cpu->pc + 1);
    uint8_t y = (ad + cpu->y) & 0xFF;
    uint8_t data = cpu_bus_read(machine, y);
    printf("%02X    %s $%02X,Y @ %02X = %02X             ", ad, names[opcode], ad, y, data);
  }
  break;

  case ADR_REL:
  {
    int8_t addr_rel = cpu_bus_read(machine, cpu->pc + 1);
    printf("%02X    %s $%04X                       ", (uint8_t)addr_rel, names[opcode], cpu->pc + addr_rel + 2);
  }
  break;

  case ADR_ABSJ:
  {
    uint16_t lo = cpu_bus_read(machine, cpu->pc + 1);
    uint16_t hi = cpu_bus_read(machine, cpu->pc + 2);
    printf("%02X %02X %s $%04X                       ", lo, hi, names[opcode], hi << 8 | lo);
  }
  break;

  case ADR_ABS:
  {
    uint16_t lo = cpu_bus_read(machine, cpu->pc + 1);
    uint16_t hi = cpu_bus_read(machine, cpu->pc + 2);
    uint8_t val = cpu_bus_read(machine, hi << 8 | lo);
    printf("%02X %02X %s $%04X = %02X                  ", lo, hi, names[opcode], hi << 8 | lo, val);
  }
  break;

  case ADR_ABX:
  {
    uint8_t lo = cpu_bus_read(machine, cpu->pc + 1);
    uint8_t hi = cpu_bus_read(machine, cpu->pc + 2);

    uint16_t base = hi << 8 | lo;
    uint16_t addr = base + cpu->x;
    uint8_t data = cpu_bus_read(machine, addr);

    printf("%02X %02X %s $%04X,X @ %04X = %02X         ", lo, hi, names[opcode], base, addr, data);
  }
  break;

  case ADR_ABY:
  {
    uint8_t lo = cpu_bus_read(machine, cpu->pc + 1);
    uint8_t hi = cpu_bus_read(machine, cpu->pc + 2);

    uint16_t base = hi << 8 | lo;
    uint16_t addr = base + cpu->y;

    uint8_t data = cpu_bus_read(machine, addr);

    printf("%02X %02X %s $%04X,Y @ %04X = %02X         ", lo, hi, names[opcode], base, addr, data);
  }
  break;

  case ADR_IND:
  {
    uint8_t lo = cpu_bus_read(machine, cpu->pc + 1);
    uint8_t hi = cpu_bus_read(machine, cpu->pc + 2);

    uint16_t addr = hi << 8 | lo;

    uint16_t val = cpu_bus_read(machine, addr);
    val |= cpu_bus_read(machine, hi << 8 | ((lo + 1) & 0xff)) << 8;

    printf("%02X %02X %s ($%04X) = %04X              ", lo, hi, names[opcode], addr, val);
  }

  break;

  case ADR_IZX:
  {
    uint8_t ad = cpu_bus_read(machine, cpu->pc + 1);

    uint8_t base = ad + cpu->x;
    uint16_t addr = cpu_bus_read(machine, base);
    addr |= cpu_bus_read(machine, (base + 1) & 0xFF) << 8;

    uint8_t val = cpu_bus_read(machine, addr);
    printf("%02X    %s ($%02X,X) @ %02X = %04X = %02X    ", ad, names[opcode], ad, base, addr, val);
  }

  break;

  case ADR_IZY:
  {
    uint8_t ad = cpu_bus_read(machine, cpu->pc + 1);

    uint16_t base = cpu_bus_read(machine, ad);
    base |= cpu_bus_read(machine, (ad + 1) & 0xFF) << 8;

    uint16_t addr = base + cpu->y;

    uint8_t val = cpu_bus_read(machine, addr);
    printf("%02X    %s ($%02X),Y = %04X @ %04X = %02X  ", ad, names[opcode], ad, base, addr, val);
  }

  break;
  }

  printf("A:%02X X:%02X Y:%02X P:%02X SP:%02X PPU:%3d,%3d CYC:%d\n", cpu->a, cpu->x, cpu->y, cpu->status.raw, cpu->sp, ppu->clock / 341, ppu->clock % 341, machine->cycles);
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
  int rc = read_rom(&rom, rom_file);

  if (rc)
  {
    fprintf(stderr, "Not a iNES/NES2.0 file\n");
    return rc;
  }

  // printf("ROM info: %s\n", argv[1]);
  // printf("Mapper: %d\n", rom.mapper_id);
  // printf("PRG_ROM size: %d bytes\n", rom.prg_rom_size);
  // printf("CHR_ROM size: %d bytes\n", rom.chr_rom_size);

  struct tnes_machine machine;
  struct cartridge *cartridge = cartridge_builder(&rom);
  struct ic_6502_registers cpu;
  struct ppu ppu;
  machine.cpu = &cpu;
  machine.ppu = &ppu;
  machine.cartridge = cartridge;

  // ic_2c02_reset(&ppu);
  machine.reset = true;
  ppu.clock = 0;
  memset(machine.main_ram, 0, sizeof(machine.main_ram));

  // Special Debug mode for NESTEST rom.
  do
  {
    tick_machine(&machine);
  } while (machine.cpu->cycle != 0);

  cpu.pc = 0xc000;

  while (machine.cycles < 26555)
  {
    print_status(&machine);
    do
    {
      tick_machine(&machine);
    } while (machine.cpu->cycle != 0);
  }

  return 0;
}

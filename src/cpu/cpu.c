#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cpu/cpu.h"

void nmi(ic_6502_registers *cpu)
{
  cpu->nmi_requested = true;
}

void update_status(union ic_6502_status *status, uint8_t value)
{
  status->z = (value & 0xff) == 0;
  status->n = (value & 0x80) == 0x80;
}

bool run_break(ic_6502_registers *cpu, struct ic_6502_bus *bus)
{
  switch (cpu->cycle)
  {
  case 0:
    cpu->cycle++;
    // Read and discard instr lo (Advancing PC is handled in normal Instruction path)
    bus->read(bus->context, cpu->pc);
    return false;
  case 1:
    cpu->cycle++;
    // Read and discard instr hi
    bus->read(bus->context, cpu->pc + 1);
    return false;
  case 2:
    if (cpu->state == IC_6502_RESET)
    {
      // Fake-push PC >> 8
      bus->read(bus->context, 0x100 + cpu->sp);
    }
    else
    {
      bus->write(bus->context, 0x100 + cpu->sp, cpu->pc >> 8);
    }
    cpu->sp--;
    cpu->cycle++;
    return false;
  case 3:
    cpu->cycle++;
    // Fake-push PC
    if (cpu->state == IC_6502_RESET)
    {
      // Fake-push PC >> 8
      bus->read(bus->context, 0x100 + cpu->sp);
    }
    else
    {
      bus->write(bus->context, 0x100 + cpu->sp, cpu->pc & 0xff);
    }
    cpu->sp--;
    return false;
  case 4:
    cpu->cycle++;
    if (cpu->state == IC_6502_RESET)
    {
      // Fake-push status
      bus->read(bus->context, 0x100 + cpu->sp);
    }
    else if (cpu->state == IC_6502_INSTRUCTION)
    {
      bus->write(bus->context, 0x100 + cpu->sp, cpu->status.raw | IC_6502_STATUS_B);
    }
    else
    {
      bus->write(bus->context, 0x100 + cpu->sp, cpu->status.raw & ~IC_6502_STATUS_B);
    }
    cpu->sp--;
    cpu->status.i = 1;
    return false;
  case 5:
    switch (cpu->state)
    {
    case IC_6502_RESET:
      cpu->pc = bus->read(bus->context, RES_VECTOR_LO);
      break;
    case IC_6502_NMI:
      cpu->pc = bus->read(bus->context, NMI_VECTOR_LO);
      break;
    case IC_6502_IRQ:
      cpu->pc = bus->read(bus->context, IRQ_VECTOR_LO);
      break;
    case IC_6502_INSTRUCTION: // BRK
      cpu->pc = bus->read(bus->context, IRQ_VECTOR_LO);
      break;
    }
    cpu->cycle++;
    return false;
  case 6:
    switch (cpu->state)
    {
    case IC_6502_RESET:
      cpu->pc |= bus->read(bus->context, RES_VECTOR_HI) << 8;
      break;
    case IC_6502_NMI:
      cpu->pc |= bus->read(bus->context, NMI_VECTOR_HI) << 8;
      break;
    case IC_6502_IRQ:
      cpu->pc |= bus->read(bus->context, IRQ_VECTOR_HI) << 8;
      break;
    case IC_6502_INSTRUCTION: // BRK
      cpu->pc |= bus->read(bus->context, IRQ_VECTOR_HI) << 8;
      break;
    }
    return true;
  default:
    __builtin_unreachable();
  }
}

bool run_instruction(ic_6502_registers *cpu, struct ic_6502_bus *bus)
{
  switch (cpu->cycle)
  {
  case 0:
    cpu->cycle++;
    // Read instruction.
    cpu->instruction = bus->read(bus->context, cpu->pc);
    cpu->pc++;
    return false;

  case 1:
    cpu->cycle++;
    switch (cpu->instruction)
    {
    case NOP_IMP_ea:
      return true;
    case SEI_IMP_78:
      cpu->status.i = 1;
      return true;
    case CLI_IMP_58:
      cpu->status.i = 0;
      return true;
    case SEC_IMP_38:
      cpu->status.c = 1;
      return true;
    case CLC_IMP_18:
      cpu->status.c = 0;
      return true;
    case SED_IMP_f8:
      cpu->status.d = 1;
      return true;
    case CLD_IMP_d8:
      cpu->status.d = 0;
      return true;
    case CLV_IMP_b8:
      cpu->status.v = 0;
      return true;
    case INY_IMP_c8:
      cpu->y++;
      update_status(&cpu->status, cpu->y);
      return true;
    case DEY_IMP_88:
      cpu->y--;
      update_status(&cpu->status, cpu->y);
      return true;
    case INX_IMP_e8:
      cpu->x++;
      update_status(&cpu->status, cpu->x);
      return true;
    case DEX_IMP_ca:
      cpu->x--;
      update_status(&cpu->status, cpu->x);
      return true;
    case TAX_IMP_aa:
      cpu->x = cpu->a;
      update_status(&cpu->status, cpu->x);
      return true;
    case TAY_IMP_a8:
      cpu->y = cpu->a;
      update_status(&cpu->status, cpu->y);
      return true;
    case TYA_IMP_98:
      cpu->a = cpu->y;
      update_status(&cpu->status, cpu->a);
      return true;
    case TXA_IMP_8a:
      cpu->a = cpu->x;
      update_status(&cpu->status, cpu->a);
      return true;
    case TSX_IMP_ba:
      cpu->x = cpu->sp;
      update_status(&cpu->status, cpu->x);
      return true;
    case TXS_IMP_9a:
      cpu->sp = cpu->x;
      return true;
    case TAS_ABY_9b:
      cpu->sp = cpu->a;
      update_status(&cpu->status, cpu->sp);
      return true;

    case PHA_IMP_48:
      cpu->operand = cpu->a;
      return false;
    case PHP_IMP_08:
      union ic_6502_status s = cpu->status;
      s.b = 1;
      s._ = 1;
      cpu->operand = s.raw;
      return false;
    case PLA_IMP_68:
    case PLP_IMP_28:
      cpu->sp++;
      cpu->operand = bus->read(bus->context, 0x100 + cpu->sp);
      return false;
    default:
      break;
    }
    cpu->operand = bus->read(bus->context, cpu->pc);
    cpu->pc++;
    switch (cpu->instruction)
    {
    case LDA_IMM_a9:
      cpu->a = cpu->operand;
      update_status(&cpu->status, cpu->a);
      return true;
    case LDX_IMM_a2:
      cpu->x = cpu->operand;
      update_status(&cpu->status, cpu->x);
      return true;
    case LDY_IMM_a0:
      cpu->y = cpu->operand;
      update_status(&cpu->status, cpu->y);
      return true;
    case AND_IMM_29:
      cpu->a &= cpu->operand;
      update_status(&cpu->status, cpu->a);
      return true;
    case ORA_IMM_09:
      cpu->a |= cpu->operand;
      update_status(&cpu->status, cpu->a);
      return true;
    case EOR_IMM_49:
      cpu->a ^= cpu->operand;
      update_status(&cpu->status, cpu->a);
      return true;
    case ADC_IMM_69:
    {
      uint16_t result = cpu->a + cpu->operand + cpu->status.c;
      update_status(&cpu->status, result);
      cpu->status.c = (result & 0xff00) != 0;
      cpu->status.v = (~(cpu->a ^ cpu->operand) & (cpu->a ^ result) & 0x80) == 0x80;
      cpu->a = result;
    }
      return true;
    case SBC_IMM_e9:
    {
      cpu->operand = (uint8_t)~cpu->operand;
      uint16_t result = cpu->a + cpu->operand + cpu->status.c;
      update_status(&cpu->status, result);
      cpu->status.c = (result & 0xff00) != 0;
      cpu->status.v = (~(cpu->a ^ cpu->operand) & (cpu->a ^ result) & 0x80) == 0x80;
      cpu->a = result;
    }
      return true;
    case CMP_IMM_c9:
    {
      uint8_t cmp = cpu->a - cpu->operand;
      update_status(&cpu->status, cmp);
      cpu->status.c = cpu->a >= cpu->operand;
    }
      return true;
    case CPY_IMM_c0:
    {
      uint8_t cmp = cpu->y - cpu->operand;
      update_status(&cpu->status, cmp);
      cpu->status.c = cpu->y >= cpu->operand;
    }
      return true;
    case CPX_IMM_e0:
    {
      uint8_t cmp = cpu->x - cpu->operand;
      update_status(&cpu->status, cmp);
      cpu->status.c = cpu->x >= cpu->operand;
    }
      return true;
    case BNE_REL_d0:
      if (cpu->status.z)
      {
        return true;
      }
      break;
    case BEQ_REL_f0:
      if (!cpu->status.z)
      {
        return true;
      }
      break;
    case BCC_REL_90:
      if (cpu->status.c)
      {
        return true;
      }
      break;
    case BCS_REL_b0:
      if (!cpu->status.c)
      {
        return true;
      }
      break;
    case BVS_REL_70:
      if (!cpu->status.v)
      {
        return true;
      }
      break;
    case BVC_REL_50:
      if (cpu->status.v)
      {
        return true;
      }
      break;
    case BPL_REL_10:
      if (cpu->status.n)
      {
        return true;
      }
      break;
    case BMI_REL_30:
      if (!cpu->status.n)
      {
        return true;
      }
      break;
    default:
      break;
    }

    return false;

  case 2:
    cpu->cycle++;
    switch (cpu->instruction)
    {
    case STY_ZPG_84:
      bus->write(bus->context, cpu->operand, cpu->y);
      return true;
    case STA_ZPG_85:
      bus->write(bus->context, cpu->operand, cpu->a);
      return true;
    case STX_ZPG_86:
      bus->write(bus->context, cpu->operand, cpu->x);
      return true;

    case BNE_REL_d0:
    case BEQ_REL_f0:
    case BCC_REL_90:
    case BCS_REL_b0:
    case BVC_REL_50:
    case BVS_REL_70:
    case BPL_REL_10:
    case BMI_REL_30:
      cpu->pc += cpu->operand;
      return true;
    case BIT_ZPG_24:
      union ic_6502_status d = {
          .raw = bus->read(bus->context, cpu->operand)};
      cpu->status.z = (d.raw & cpu->a) == 0;
      cpu->status.n = d.n;
      cpu->status.v = d.v;
      return true;
    case PHA_IMP_48:
    case PHP_IMP_08:
      bus->write(bus->context, 0x100 + cpu->sp, cpu->operand & 0xff);
      cpu->sp--;
      return true;
    case PLA_IMP_68:
    case PLP_IMP_28:
      return false;
    default:
      break;
    }

    cpu->operand |= bus->read(bus->context, cpu->pc) << 8;
    cpu->pc++;

    if (cpu->instruction == JMP_ABS_4c)
    {
      cpu->pc = cpu->operand;
      return true;
    }
    return false;

  case 3:
    cpu->cycle++;
    switch (cpu->instruction)
    {
    case JSR_ABS_20:
      bus->write(bus->context, 0x100 + cpu->sp, cpu->pc >> 8);
      cpu->sp--;
      break;
    case RTS_IMP_60:
      cpu->sp++;
      cpu->operand = bus->read(bus->context, 0x100 + cpu->sp) & 0xff;
      break;
    case PLA_IMP_68:
      cpu->a = cpu->operand;
      update_status(&cpu->status, cpu->a);
      return true;
    case PLP_IMP_28:
      cpu->status.raw = cpu->operand;
      cpu->status.b = 0;
      cpu->status._ = 1;
      return true;
    case STX_ABS_8e:
      bus->write(bus->context, cpu->operand, cpu->x);
      return true;
    case LDX_ABS_ae:
      cpu->x = bus->read(bus->context, cpu->operand);
      update_status(&cpu->status, cpu->x);
      return true;
    case LDA_ABS_ad:
      cpu->a = bus->read(bus->context, cpu->operand);
      update_status(&cpu->status, cpu->a);
      return true;
    case LDY_ABS_ac:
      cpu->y = bus->read(bus->context, cpu->operand);
      update_status(&cpu->status, cpu->y);
      return true;

    default:
      break;
    }
    return false;

  case 4:
    cpu->cycle++;
    switch (cpu->instruction)
    {
    case JSR_ABS_20:
      bus->write(bus->context, 0x100 + cpu->sp, cpu->pc & 0xff);
      cpu->sp--;
      break;
    case RTS_IMP_60:
      cpu->sp++;
      cpu->operand |= bus->read(bus->context, 0x100 + cpu->sp) << 8;
      break;
    default:
      break;
    }
    return false;

  case 5:
    cpu->cycle++;
    switch (cpu->instruction)
    {
    case JSR_ABS_20:
    case RTS_IMP_60:
      cpu->pc = cpu->operand;
      return true;
    default:
      break;
    }
    return false;

  default:
    return true;
    break;
  }
}

void tick_cpu(ic_6502_registers *cpu, struct ic_6502_bus *bus, bool irq, bool reset)
{
  // RESET aborts the current instruction.
  if (reset)
  {
    cpu->state = IC_6502_RESET;
    cpu->cycle = 0;
  }

  bool done = true;
  switch (cpu->state)
  {
  case IC_6502_INSTRUCTION:
    done = run_instruction(cpu, bus);
    break;

  case IC_6502_IRQ:
  case IC_6502_NMI:
  case IC_6502_RESET:
    done = run_break(cpu, bus);
    break;

  default:
    break;
  }

  // Check if the current instruction is progress.
  if (!done)
  {
    return;
  }

  if (cpu->nmi_requested)
  {
    cpu->state = IC_6502_NMI;
    cpu->cycle = 0;
  }
  else if (irq && !cpu->status.i)
  {
    cpu->state = IC_6502_IRQ;
    cpu->cycle = 0;
  }
  else
  {
    cpu->state = IC_6502_INSTRUCTION;
    cpu->cycle = 0;
  }
}
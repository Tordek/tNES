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
  bool branch_condition;

  cpu->cycle++;

  // Read instruction.
  if (cpu->cycle == 1)
  {
    // Read instruction.
    cpu->instruction = bus->read(bus->context, cpu->pc);
    cpu->pc++;
    return false;
  }

  // Read operand.
  switch (cpu->instruction)
  {
  case ADC_ABY_79:
  case AHX_ABY_9f:
  case AND_ABY_39:
  case CMP_ABY_d9:
  case DCP_ABY_db:
  case EOR_ABY_59:
  case ISC_ABY_fb:
  case LAS_ABY_bb:
  case LAX_ABY_bf:
  case LDA_ABY_b9:
  case LDX_ABY_be:
  case ORA_ABY_19:
  case RLA_ABY_3b:
  case RRA_ABY_7b:
  case SBC_ABY_f9:
  case SHX_ABY_9e:
  case SLO_ABY_1b:
  case SRE_ABY_5b:
  case STA_ABY_99:
  case TAS_ABY_9b:

    // Implied operand.
  case ASL_IMP_0a:
  case BRK_IMP_00:
    break;
  case CLC_IMP_18:
    cpu->status.c = 0;
    return true;
  case CLD_IMP_d8:
    cpu->status.d = 0;
    return true;
  case CLI_IMP_58:
    cpu->status.i = 0;
    return true;
  case CLV_IMP_b8:
    cpu->status.v = 0;
    return true;

  case DEX_IMP_ca:
  case DEY_IMP_88:
  case INX_IMP_e8:
  case INY_IMP_c8:
  case KIL_IMP_02:
  case KIL_IMP_12:
  case KIL_IMP_22:
  case KIL_IMP_32:
  case KIL_IMP_42:
  case KIL_IMP_52:
  case KIL_IMP_62:
  case KIL_IMP_72:
  case KIL_IMP_92:
  case KIL_IMP_b2:
  case KIL_IMP_d2:
  case KIL_IMP_f2:
  case LSR_IMP_4a:
  case NOP_IMP_1a:
  case NOP_IMP_3a:
  case NOP_IMP_5a:
  case NOP_IMP_7a:
  case NOP_IMP_da:
  case NOP_IMP_ea:
  case NOP_IMP_fa:
  case PHA_IMP_48:
  case PHP_IMP_08:
  case PLA_IMP_68:
  case PLP_IMP_28:
  case ROL_IMP_2a:
  case ROR_IMP_6a:
  case RTI_IMP_40:
  case RTS_IMP_60:
  case SEC_IMP_38:
  case SED_IMP_f8:
  case SEI_IMP_78:
  case TAX_IMP_aa:
  case TAY_IMP_a8:
  case TSX_IMP_ba:
  case TXA_IMP_8a:
  case TXS_IMP_9a:
  case TYA_IMP_98:
    break;

  // Immediates.
  case ADC_IMM_69:
  case ALR_IMM_4b:
  case ANC_IMM_0b:
  case ANC_IMM_2b:
  case AND_IMM_29:
  case ARR_IMM_6b:
  case AXS_IMM_cb:
  case CMP_IMM_c9:
  case CPX_IMM_e0:
  case CPY_IMM_c0:
  case EOR_IMM_49:
  case LAX_IMM_ab:
  case LDA_IMM_a9:
  case LDX_IMM_a2:
  case LDY_IMM_a0:
  case NOP_IMM_80:
  case NOP_IMM_82:
  case NOP_IMM_89:
  case NOP_IMM_c2:
  case NOP_IMM_e2:
  case ORA_IMM_09:
  case SBC_IMM_e9:
  case SBC_IMM_eb:
  case XAA_IMM_8b:

  // Rel jumps
  case BCS_REL_b0:
  case BCC_REL_90:
  case BEQ_REL_f0:
  case BMI_REL_30:
  case BNE_REL_d0:
  case BPL_REL_10:
  case BVC_REL_50:
  case BVS_REL_70:
    if (cpu->cycle == 2)
    {
      cpu->operand = bus->read(bus->context, cpu->pc++);
    }
    break;

  // Zero page address
  case STA_ZPG_85:
  case STX_ZPG_86:
  case STY_ZPG_84:
    if (cpu->cycle == 2)
    {
      cpu->address = bus->read(bus->context, cpu->pc++);
      return false;
    }
    break;

  // Zero page data
  case ADC_ZPG_65:
  case AND_ZPG_25:
  case ASL_ZPG_06:
  case BIT_ZPG_24:
  case CMP_ZPG_c5:
  case CPX_ZPG_e4:
  case CPY_ZPG_c4:
  case DCP_ZPG_c7:
  case DEC_ZPG_c6:
  case EOR_ZPG_45:
  case INC_ZPG_e6:
  case ISC_ZPG_e7:
  case LAX_ZPG_a7:
  case LDA_ZPG_a5:
  case LDX_ZPG_a6:
  case LDY_ZPG_a4:
  case LSR_ZPG_46:
  case NOP_ZPG_04:
  case NOP_ZPG_44:
  case NOP_ZPG_64:
  case ORA_ZPG_05:
  case RLA_ZPG_27:
  case ROL_ZPG_26:
  case ROR_ZPG_66:
  case RRA_ZPG_67:
  case SAX_ZPG_87:
  case SBC_ZPG_e5:
  case SLO_ZPG_07:
  case SRE_ZPG_47:
    switch (cpu->cycle)
    {
    case 2:
      cpu->address = bus->read(bus->context, cpu->pc++);
      return false;
    case 3:
      cpu->operand = bus->read(bus->context, cpu->address);
      break;
    }
    break;

  // Absolute address:
  case JMP_ABS_4c:
  case JSR_ABS_20:
    switch (cpu->cycle)
    {
    case 2:
      cpu->address = bus->read(bus->context, cpu->pc++);
      return false;
    case 3:
      cpu->address |= bus->read(bus->context, cpu->pc++) << 8;
      break;
    }
    break;

  // Absolute
  case ADC_ABS_6d:
  case AND_ABS_2d:
  case ASL_ABS_0e:
  case BIT_ABS_2c:
  case CMP_ABS_cd:
  case CPX_ABS_ec:
  case CPY_ABS_cc:
  case DCP_ABS_cf:
  case DEC_ABS_ce:
  case EOR_ABS_4d:
  case INC_ABS_ee:
  case ISC_ABS_ef:
  case LAX_ABS_af:
  case LDA_ABS_ad:
  case LDX_ABS_ae:
  case LDY_ABS_ac:
  case LSR_ABS_4e:
  case NOP_ABS_0c:
  case ORA_ABS_0d:
  case RLA_ABS_2f:
  case ROL_ABS_2e:
  case ROR_ABS_6e:
  case RRA_ABS_6f:
  case SAX_ABS_8f:
  case SBC_ABS_ed:
  case SLO_ABS_0f:
  case SRE_ABS_4f:
  case STA_ABS_8d:
  case STX_ABS_8e:
  case STY_ABS_8c:
    switch (cpu->cycle)
    {
    case 2:
      cpu->address = bus->read(bus->context, cpu->pc);
      cpu->pc++;
      return false;
    case 3:
      cpu->address |= bus->read(bus->context, cpu->pc) << 8;
      cpu->pc++;
      return false;
    case 4:
      cpu->operand = bus->read(bus->context, cpu->address);
      break;
    }
    break;
    // Abs+x
  case ADC_ABX_7d:
  case AND_ABX_3d:
  case ASL_ABX_1e:
  case CMP_ABX_dd:
  case DCP_ABX_df:
  case DEC_ABX_de:
  case EOR_ABX_5d:
  case INC_ABX_fe:
  case ISC_ABX_ff:
  case LDA_ABX_bd:
  case LDY_ABX_bc:
  case LSR_ABX_5e:
  case NOP_ABX_1c:
  case NOP_ABX_3c:
  case NOP_ABX_5c:
  case NOP_ABX_7c:
  case NOP_ABX_dc:
  case NOP_ABX_fc:
  case ORA_ABX_1d:
  case RLA_ABX_3f:
  case ROL_ABX_3e:
  case ROR_ABX_7e:
  case RRA_ABX_7f:
  case SBC_ABX_fd:
  case SHY_ABX_9c:
  case SLO_ABX_1f:
  case SRE_ABX_5f:
  case STA_ABX_9d:
    break;

    // Zero-page+Y
  case LAX_ZPY_b7:
  case LDX_ZPY_b6:
  case SAX_ZPY_97:
  case STX_ZPY_96:
    break;
  // Zero-page + X
  case ADC_ZPX_75:
  case AND_ZPX_35:
  case ASL_ZPX_16:
  case CMP_ZPX_d5:
  case DCP_ZPX_d7:
  case DEC_ZPX_d6:
  case EOR_ZPX_55:
  case INC_ZPX_f6:
  case ISC_ZPX_f7:
  case LDA_ZPX_b5:
  case LDY_ZPX_b4:
  case LSR_ZPX_56:
  case NOP_ZPX_14:
  case NOP_ZPX_34:
  case NOP_ZPX_54:
  case NOP_ZPX_74:
  case NOP_ZPX_d4:
  case NOP_ZPX_f4:
  case ORA_ZPX_15:
  case RLA_ZPX_37:
  case ROL_ZPX_36:
  case ROR_ZPX_76:
  case RRA_ZPX_77:
  case SBC_ZPX_f5:
  case SLO_ZPX_17:
  case SRE_ZPX_57:
  case STA_ZPX_95:
  case STY_ZPX_94:
    break;

  // Indexed
  case JMP_IND_6c:
    break;
  // Indexed X
  case ADC_IZX_61:
  case AND_IZX_21:
  case CMP_IZX_c1:
  case DCP_IZX_c3:
  case EOR_IZX_41:
  case ISC_IZX_e3:
  case LAX_IZX_a3:
  case LDA_IZX_a1:
  case ORA_IZX_01:
  case RLA_IZX_23:
  case RRA_IZX_63:
  case SAX_IZX_83:
  case SBC_IZX_e1:
  case SLO_IZX_03:
  case SRE_IZX_43:
  case STA_IZX_81:
    break;
  // Indexed on ZY:
  case ADC_IZY_71:
  case AHX_IZY_93:
  case AND_IZY_31:
  case CMP_IZY_d1:
  case DCP_IZY_d3:
  case EOR_IZY_51:
  case ISC_IZY_f3:
  case LAX_IZY_b3:
  case LDA_IZY_b1:
  case ORA_IZY_11:
  case RLA_IZY_33:
  case RRA_IZY_73:
  case SBC_IZY_f1:
  case SLO_IZY_13:
  case SRE_IZY_53:
  case STA_IZY_91:
    break;
  default:
    __builtin_unreachable();
  }

  // Operate.
  switch (cpu->instruction)
  {
  case ADC_ABS_6d:
  case ADC_ABX_7d:
  case ADC_ABY_79:
  case ADC_IMM_69:
  case ADC_IZX_61:
  case ADC_IZY_71:
  case ADC_ZPG_65:
  case ADC_ZPX_75:
  case AHX_ABY_9f:
  case AHX_IZY_93:
  case ALR_IMM_4b:
  case ANC_IMM_0b:
  case ANC_IMM_2b:
  case AND_ABS_2d:
  case AND_ABX_3d:
  case AND_ABY_39:
  case AND_IMM_29:
  case AND_IZX_21:
  case AND_IZY_31:
  case AND_ZPG_25:
  case AND_ZPX_35:
  case ARR_IMM_6b:
  case ASL_ABS_0e:
  case ASL_ABX_1e:
  case ASL_IMP_0a:
  case ASL_ZPG_06:
  case ASL_ZPX_16:
  case AXS_IMM_cb:
    break;
  case BCC_REL_90:
    branch_condition = !cpu->status.c;
    break;
  case BCS_REL_b0:
    branch_condition = cpu->status.c;
    break;
  case BEQ_REL_f0:
    branch_condition = cpu->status.z;
    break;
  case BIT_ABS_2c:
  case BIT_ZPG_24:
  {
    union ic_6502_status d = {
        .raw = cpu->operand};
    cpu->status.z = (d.raw & cpu->a) == 0;
    cpu->status.n = d.n;
    cpu->status.v = d.v;
    return true;
  }
  case BMI_REL_30:
    branch_condition = cpu->status.n;
    break;
  case BNE_REL_d0:
    branch_condition = !cpu->status.z;
    break;
  case BPL_REL_10:
    branch_condition = !cpu->status.n;
    break;
  case BRK_IMP_00:
  case BVC_REL_50:
    branch_condition = !cpu->status.v;
    break;
  case BVS_REL_70:
    branch_condition = cpu->status.v;
    break;
  case CLC_IMP_18:
  case CLD_IMP_d8:
  case CLI_IMP_58:
  case CLV_IMP_b8:
  case CMP_ABS_cd:
  case CMP_ABX_dd:
  case CMP_ABY_d9:
  case CMP_IMM_c9:
  case CMP_IZX_c1:
  case CMP_IZY_d1:
  case CMP_ZPG_c5:
  case CMP_ZPX_d5:
  case CPX_ABS_ec:
  case CPX_IMM_e0:
  case CPX_ZPG_e4:
  case CPY_ABS_cc:
  case CPY_IMM_c0:
  case CPY_ZPG_c4:
  case DCP_ABS_cf:
  case DCP_ABX_df:
  case DCP_ABY_db:
  case DCP_IZX_c3:
  case DCP_IZY_d3:
  case DCP_ZPG_c7:
  case DCP_ZPX_d7:
  case DEC_ABS_ce:
  case DEC_ABX_de:
  case DEC_ZPG_c6:
  case DEC_ZPX_d6:
  case DEX_IMP_ca:
  case DEY_IMP_88:
  case EOR_ABS_4d:
  case EOR_ABX_5d:
  case EOR_ABY_59:
  case EOR_IMM_49:
  case EOR_IZX_41:
  case EOR_IZY_51:
  case EOR_ZPG_45:
  case EOR_ZPX_55:
  case INC_ABS_ee:
  case INC_ABX_fe:
  case INC_ZPG_e6:
  case INC_ZPX_f6:
  case INX_IMP_e8:
  case INY_IMP_c8:
  case ISC_ABS_ef:
  case ISC_ABX_ff:
  case ISC_ABY_fb:
  case ISC_IZX_e3:
  case ISC_IZY_f3:
  case ISC_ZPG_e7:
  case ISC_ZPX_f7:
  case JMP_ABS_4c:
  case JMP_IND_6c:
  case JSR_ABS_20:
  case KIL_IMP_02:
  case KIL_IMP_12:
  case KIL_IMP_22:
  case KIL_IMP_32:
  case KIL_IMP_42:
  case KIL_IMP_52:
  case KIL_IMP_62:
  case KIL_IMP_72:
  case KIL_IMP_92:
  case KIL_IMP_b2:
  case KIL_IMP_d2:
  case KIL_IMP_f2:
  case LAS_ABY_bb:
  case LAX_ABS_af:
  case LAX_ABY_bf:
  case LAX_IMM_ab:
  case LAX_IZX_a3:
  case LAX_IZY_b3:
  case LAX_ZPG_a7:
  case LAX_ZPY_b7:
  case LDA_ABS_ad:
  case LDA_ABX_bd:
  case LDA_ABY_b9:
  case LDA_IMM_a9:
  case LDA_IZX_a1:
  case LDA_IZY_b1:
  case LDA_ZPG_a5:
  case LDA_ZPX_b5:
  case LDX_ABS_ae:
  case LDX_ABY_be:
  case LDX_IMM_a2:
  case LDX_ZPG_a6:
  case LDX_ZPY_b6:
  case LDY_ABS_ac:
  case LDY_ABX_bc:
  case LDY_IMM_a0:
  case LDY_ZPG_a4:
  case LDY_ZPX_b4:
  case LSR_ABS_4e:
  case LSR_ABX_5e:
  case LSR_IMP_4a:
  case LSR_ZPG_46:
  case LSR_ZPX_56:
  case NOP_ABS_0c:
  case NOP_ABX_1c:
  case NOP_ABX_3c:
  case NOP_ABX_5c:
  case NOP_ABX_7c:
  case NOP_ABX_dc:
  case NOP_ABX_fc:
  case NOP_IMM_80:
  case NOP_IMM_82:
  case NOP_IMM_89:
  case NOP_IMM_c2:
  case NOP_IMM_e2:
  case NOP_IMP_1a:
  case NOP_IMP_3a:
  case NOP_IMP_5a:
  case NOP_IMP_7a:
  case NOP_IMP_da:
  case NOP_IMP_ea:
  case NOP_IMP_fa:
  case NOP_ZPG_04:
  case NOP_ZPG_44:
  case NOP_ZPG_64:
  case NOP_ZPX_14:
  case NOP_ZPX_34:
  case NOP_ZPX_54:
  case NOP_ZPX_74:
  case NOP_ZPX_d4:
  case NOP_ZPX_f4:
  case ORA_ABS_0d:
  case ORA_ABX_1d:
  case ORA_ABY_19:
  case ORA_IMM_09:
  case ORA_IZX_01:
  case ORA_IZY_11:
  case ORA_ZPG_05:
  case ORA_ZPX_15:
    break;
  case PHA_IMP_48:
    cpu->operand = cpu->a;
    break;
  case PHP_IMP_08:
  {
    union ic_6502_status s = cpu->status;
    s.b = 1;
    s._ = 1;
    cpu->operand = s.raw;
  }
  break;
  case PLA_IMP_68:
    if (cpu->cycle == 2)
    {
      return false;
    }
    else if (cpu->cycle == 3)
    {
      cpu->operand = bus->read(bus->context, 0x100 + ++cpu->sp);
      return false;
    }
    break;
  case PLP_IMP_28:
  case RLA_ABS_2f:
  case RLA_ABX_3f:
  case RLA_ABY_3b:
  case RLA_IZX_23:
  case RLA_IZY_33:
  case RLA_ZPG_27:
  case RLA_ZPX_37:
  case ROL_ABS_2e:
  case ROL_ABX_3e:
  case ROL_IMP_2a:
  case ROL_ZPG_26:
  case ROL_ZPX_36:
  case ROR_ABS_6e:
  case ROR_ABX_7e:
  case ROR_IMP_6a:
  case ROR_ZPG_66:
  case ROR_ZPX_76:
  case RRA_ABS_6f:
  case RRA_ABX_7f:
  case RRA_ABY_7b:
  case RRA_IZX_63:
  case RRA_IZY_73:
  case RRA_ZPG_67:
  case RRA_ZPX_77:
    break;

  case RTI_IMP_40:
  case RTS_IMP_60:
    switch (cpu->cycle)
    {
    case 2:
      cpu->address = bus->read(bus->context, 0x100 + ++cpu->sp);
      return false;
    case 3:
      cpu->address |= bus->read(bus->context, 0x100 + ++cpu->sp) << 8;
      return false;
    case 4:
    case 5:
      return false;
    }
    break;
  case SAX_ABS_8f:
  case SAX_IZX_83:
  case SAX_ZPG_87:
  case SAX_ZPY_97:
  case SBC_ABS_ed:
  case SBC_ABX_fd:
  case SBC_ABY_f9:
  case SBC_IMM_e9:
  case SBC_IMM_eb:
  case SBC_IZX_e1:
  case SBC_IZY_f1:
  case SBC_ZPG_e5:
  case SBC_ZPX_f5:
    break;
  case SEC_IMP_38:
    cpu->status.c = 1;
    return true;
  case SED_IMP_f8:
    cpu->status.d = 1;
    return true;
  case SEI_IMP_78:
    cpu->status.i = 1;
    return true;
  case SHX_ABY_9e:
  case SHY_ABX_9c:
  case SLO_ABS_0f:
  case SLO_ABX_1f:
  case SLO_ABY_1b:
  case SLO_IZX_03:
  case SLO_IZY_13:
  case SLO_ZPG_07:
  case SLO_ZPX_17:
  case SRE_ABS_4f:
  case SRE_ABX_5f:
  case SRE_ABY_5b:
  case SRE_IZX_43:
  case SRE_IZY_53:
  case SRE_ZPG_47:
  case SRE_ZPX_57:
  case STA_ABS_8d:
  case STA_ABX_9d:
  case STA_ABY_99:
  case STA_IZX_81:
  case STA_IZY_91:
  case STA_ZPG_85:
  case STA_ZPX_95:
  case STX_ABS_8e:
  case STX_ZPG_86:
  case STX_ZPY_96:
  case STY_ABS_8c:
  case STY_ZPG_84:
  case STY_ZPX_94:
  case TAS_ABY_9b:
  case TAX_IMP_aa:
  case TAY_IMP_a8:
  case TSX_IMP_ba:
  case TXA_IMP_8a:
  case TXS_IMP_9a:
  case TYA_IMP_98:
  case XAA_IMM_8b:
    break;
  }

  // Handle relative branches:
  switch (cpu->instruction)
  {
  case BCS_REL_b0:
  case BCC_REL_90:
  case BEQ_REL_f0:
  case BMI_REL_30:
  case BNE_REL_d0:
  case BPL_REL_10:
  case BVC_REL_50:
  case BVS_REL_70:
    switch (cpu->cycle)
    {
    case 2:
      cpu->address = cpu->pc;
      if (branch_condition)
      {
        cpu->address += cpu->operand;
        return false;
      }
    case 3:
      // If overflow:
      // return false;
    }
    break;
  default:
    break;
  }

  // Store result.
  switch (cpu->instruction)
  {
  case JMP_ABS_4c:
  case JMP_IND_6c:
  case BCS_REL_b0:
  case BCC_REL_90:
  case BEQ_REL_f0:
  case BMI_REL_30:
  case BNE_REL_d0:
  case BPL_REL_10:
  case BVC_REL_50:
  case BVS_REL_70:
  case RTS_IMP_60:
  case RTI_IMP_40:
    cpu->pc = cpu->address;
    return true;

  case LDA_ABS_ad:
  case LDA_ABX_bd:
  case LDA_ABY_b9:
  case LDA_IMM_a9:
  case LDA_IZX_a1:
  case LDA_IZY_b1:
  case LDA_ZPG_a5:
  case LDA_ZPX_b5:
  case PLA_IMP_68:
    cpu->a = cpu->operand;
    update_status(&cpu->status, cpu->a);
    return true;

  case LDX_ABS_ae:
  case LDX_ABY_be:
  case LDX_IMM_a2:
  case LDX_ZPG_a6:
  case LDX_ZPY_b6:
    cpu->x = cpu->operand;
    update_status(&cpu->status, cpu->x);
    return true;

  case LDY_ABS_ac:
  case LDY_ABX_bc:
  case LDY_IMM_a0:
  case LDY_ZPG_a4:
  case LDY_ZPX_b4:
    cpu->y = cpu->operand;
    update_status(&cpu->status, cpu->y);
    return true;

  case STA_ABS_8d:
  case STA_ABX_9d:
  case STA_ABY_99:
  case STA_IZX_81:
  case STA_IZY_91:
  case STA_ZPG_85:
  case STA_ZPX_95:
  case STX_ABS_8e:
  case STX_ZPG_86:
  case STX_ZPY_96:
  case STY_ABS_8c:
  case STY_ZPG_84:
  case STY_ZPX_94:
    bus->write(bus->context, cpu->address, cpu->operand);
    return true;

  case PHP_IMP_08:
  case PHA_IMP_48:
    if (cpu->cycle == 2)
    {
      bus->write(bus->context, 0x100 + cpu->sp--, cpu->operand);
      return false;
    }
    else
    {
      return true;
    }

  case JSR_ABS_20:
    switch (cpu->cycle)
    {
    case 3:
      return false;
    case 4:
      bus->write(bus->context, 0x100 + cpu->sp--, cpu->pc >> 8);
      return false;
    case 5:
      bus->write(bus->context, 0x100 + cpu->sp--, cpu->pc);
      return false;
    case 6:
      cpu->pc = cpu->address;
      return true;
    }
    break;

  default:
    break; // TODO: Unreachable.
  }

  return true; // This should be unreachable at the end, but for now...
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

  cpu->cycle = 0;
  if (cpu->nmi_requested)
  {
    cpu->state = IC_6502_NMI;
  }
  else if (irq && !cpu->status.i)
  {
    cpu->state = IC_6502_IRQ;
  }
  else
  {
    cpu->state = IC_6502_INSTRUCTION;
  }
}
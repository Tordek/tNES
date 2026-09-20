#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cpu/ic_6502.h"

#define MAGIC (0xee)

void ic_6502_nmi(ic_6502_registers *cpu)
{
    cpu->nmi_requested = true;
}

void update_status(union ic_6502_status *status, uint8_t value)
{
    status->z = (value & 0xff) == 0;
    status->n = (value & 0x80) == 0x80;
}

static bool run_break(ic_6502_registers *cpu, struct ic_6502_bus *bus)
{
    uint8_t value;
    cpu->cycle++;

    switch (cpu->cycle)
    {
    case 1:
        // Read and discard instr lo (Advancing PC is handled in normal Instruction
        // path)
        bus->read(&value, bus->context, cpu->pc);
        return false;
    case 2:
        // Read and discard instr hi
        bus->read(&value, bus->context, cpu->pc + 1);
        return false;
    case 3:
        if (cpu->state == IC_6502_RESET)
        {
            cpu->a = 0;
            cpu->x = 0;
            cpu->y = 0;
            cpu->sp = 0x00;
            cpu->status.raw = 0x20;
            // Fake-push PC >> 8
            bus->read(&value, bus->context, 0x100 + cpu->sp);
        }
        else
        {
            bus->write(bus->context, 0x100 + cpu->sp, cpu->pc >> 8);
        }
        cpu->sp--;
        return false;
    case 4:
        // Fake-push PC
        if (cpu->state == IC_6502_RESET)
        {
            // Fake-push PC >> 8
            bus->read(&value, bus->context, 0x100 + cpu->sp);
        }
        else
        {
            bus->write(bus->context, 0x100 + cpu->sp, cpu->pc & 0xff);
        }
        cpu->sp--;
        return false;
    case 5:
        if (cpu->state == IC_6502_RESET)
        {
            // Fake-push status
            bus->read(&value, bus->context, 0x100 + cpu->sp);
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
    case 6:
        switch (cpu->state)
        {
        case IC_6502_RESET:
            bus->read(&value, bus->context, RES_VECTOR_LO);
            cpu->pc = value;
            break;
        case IC_6502_NMI:
            bus->read(&value, bus->context, NMI_VECTOR_LO);
            cpu->pc = value;
            break;
        case IC_6502_IRQ:
            bus->read(&value, bus->context, IRQ_VECTOR_LO);
            cpu->pc = value;
            break;
        case IC_6502_INSTRUCTION: // BRK
            bus->read(&value, bus->context, IRQ_VECTOR_LO);
            cpu->pc = value;
            break;
        }
        return false;
    case 7:
        switch (cpu->state)
        {
        case IC_6502_RESET:
            bus->read(&value, bus->context, RES_VECTOR_HI);
            cpu->pc |= value << 8;
            break;
        case IC_6502_NMI:
            bus->read(&value, bus->context, NMI_VECTOR_HI);
            cpu->pc |= value << 8;
            break;
        case IC_6502_IRQ:
            bus->read(&value, bus->context, IRQ_VECTOR_HI);
            cpu->pc |= value << 8;
            break;
        case IC_6502_INSTRUCTION: // BRK
            bus->read(&value, bus->context, IRQ_VECTOR_HI);
            cpu->pc |= value << 8;
            break;
        }
        return true;
    default:
        __builtin_unreachable();
    }
}

static bool run_instruction(ic_6502_registers *cpu, struct ic_6502_bus *bus)
{
    struct micro_instruction i = uinstructions[cpu->instruction][cpu->cycle++];
    bool should_jump = false;

    // Memory op.
    uint16_t address;
    switch (i.address)
    {
    case UI_ADDR_PC:
        address = cpu->pc;
        break;
    case UI_ADDR_PC_INC:
        address = cpu->pc++;
        break;
    case UI_ADDR_SP:
        address = 0x100 + cpu->sp;
        break;
    case UI_ADDR_SP_INC:
        address = 0x100 + cpu->sp++;
        break;
    case UI_ADDR_SP_DEC:
        address = 0x100 + cpu->sp--;
        break;
    case UI_ADDR_LATCH:
        address = cpu->address;
        if (cpu->page_jump != 0)
        {
            cpu->address += cpu->page_jump;
            cpu->page_jump = 0;
            should_jump = true;

            uint8_t top = address >> 8;
            switch (cpu->instruction)
            {
            case AHX_IZY_93:
            case AHX_ABY_9f:
            case TAS_ABY_9b:
            {
                uint8_t badhd = (top + 1) & cpu->a & cpu->x;
                cpu->address = badhd << 8 | (address & 0xff);
            }
            break;
            case SHX_ABY_9e:
            {
                uint8_t badhd = (top + 1) & cpu->x;
                cpu->address = badhd << 8 | (address & 0xff);
            }
            break;
            case SHY_ABX_9c:
            {
                uint8_t badhd = (top + 1) & cpu->y;
                cpu->address = badhd << 8 | (address & 0xff);
            }
            break;
            default:
                break;
            }
        }
        break;
    case UI_ADDR_TEMP:
        address = cpu->address;
        break;
    case UI_ADDR_TEMP_INC:
    {
        address = cpu->address;
        uint16_t addition = cpu->address + 1;
        cpu->address = (cpu->address & 0xff00) | (addition & 0xff);
        cpu->page_jump = addition - cpu->address;
    }
    break;
    case UI_ADDR_ZP:
        address = cpu->zp;
        break;
    case UI_ADDR_ZP_INC:
        address = cpu->zp;
        cpu->zp++;
        break;
    case UI_ADDR_BRK_LO:
        address = 0xFFFE;
        break;
    case UI_ADDR_BRK_HI:
        address = 0xFFFF;
        break;
    }

    if (i.action == UI_BUS_READ)
    {
        uint8_t mem_val;
        bus->read(&mem_val, bus->context, address);
        switch (i.mem_dst)
        {
        case UI_REG_OP:
            cpu->op = mem_val;
            break;
        case UI_REG_ZP:
            cpu->zp = mem_val;
            break;
        case UI_REG_INSTRUCTION:
            cpu->instruction = mem_val;
            break;
        case UI_REG_TEMP_LO:
            cpu->address = (cpu->address & 0xff00) | mem_val;
            break;
        case UI_REG_TEMP_HI:
            cpu->address = (cpu->address & 0x00ff) | mem_val << 8;
            break;
        default:
            break;
        }
    }
    else
    {
        bus->write(bus->context, address, cpu->op);
    }

    // Work on data.
    uint8_t value;
    {
        switch (i.src)
        {
        case UI_REG_INSTRUCTION:
        case UI_REG_ZP:
        case UI_REG_PC_LATCH_LO:
        case UI_REG_NONE:
        case UI_REG_AXS:
            // Can't happen.
            __builtin_unreachable();
            printf("This can't happen\n");
            break;
        case UI_REG_P:
            value = cpu->status.raw;
            break;
        case UI_REG_A:
            value = cpu->a;
            break;
        case UI_REG_X:
            value = cpu->x;
            break;
        case UI_REG_Y:
            value = cpu->y;
            break;
        case UI_REG_PC_LO:
            value = cpu->pc;
            break;
        case UI_REG_PC_HI:
            value = cpu->pc >> 8;
            break;
        case UI_REG_SP:
            value = cpu->sp;
            break;
        case UI_REG_OP:
            value = cpu->op;
            break;
        case UI_REG_AX:
            value = cpu->a & cpu->x;
            break;
        case UI_REG_TEMP_HI:
            value = cpu->address >> 8;
            break;
        case UI_REG_TEMP_LO:
            value = cpu->address & 0xff;
            break;
        }
    }

    // Operate
    switch (i.alu_op)
    {
    case UI_ALU_NONE:
        break;
    case UI_ALU_SBC:
        value = ~value;
    case UI_ALU_ADC:
    {
        uint16_t result = (uint16_t)value + cpu->a + cpu->status.c;
        if (!should_jump)
        {
            cpu->status.c = (result & 0xff00) != 0;
            cpu->status.v = (~(value ^ cpu->a) & (result ^ cpu->a) & 0x80) != 0;
        }
        value = result;
        update_status(&cpu->status, value);
    }
    break;
    // case UI_ALU_A:
    //     value = cpu->a;
    //     update_status(&cpu->status, value);
    //     break;
    // case UI_ALU_X:
    //     value = cpu->x;
    //     update_status(&cpu->status, value);
    //     break;
    // case UI_ALU_Y:
    //     value = cpu->y;
    //     update_status(&cpu->status, value);
    //     break;
    case UI_ALU_SP:
        value = cpu->sp;
        update_status(&cpu->status, value);
        break;
    case UI_ALU_INX:
        value = cpu->x + 1;
        update_status(&cpu->status, value);
        break;
    case UI_ALU_INY:
        value = cpu->y + 1;
        update_status(&cpu->status, value);
        break;
    case UI_ALU_DEX:
        value = cpu->x - 1;
        update_status(&cpu->status, value);
        break;
    case UI_ALU_DEY:
        value = cpu->y - 1;
        update_status(&cpu->status, value);
        break;
    case UI_ALU_EOR:
        value ^= cpu->a;
        update_status(&cpu->status, value);
        break;
    case UI_ALU_ORA:
        value |= cpu->a;
        update_status(&cpu->status, value);
        break;
    case UI_ALU_AND:
        value &= cpu->a;
        update_status(&cpu->status, value);
        break;
    case UI_ALU_LSRA:
        value = cpu->a;
    case UI_ALU_LSR:
    {
        cpu->status.c = value & 1;
        value >>= 1;
        update_status(&cpu->status, value);
    }
    break;
    case UI_ALU_ASLA:
        value = cpu->a;
    case UI_ALU_ASL:
    {
        cpu->status.c = (value & 0b10000000) > 0;
        value <<= 1;
        update_status(&cpu->status, value);
    }
    break;
    case UI_ALU_ROLA:
        value = cpu->a;
    case UI_ALU_ROL:
    {
        bool c = cpu->status.c;
        cpu->status.c = (value & 0b10000000) > 0;
        value = (value << 1) | c;
        update_status(&cpu->status, value);
    }
    break;
    case UI_ALU_RORA:
        value = cpu->a;
    case UI_ALU_ROR:
    {
        bool c = cpu->status.c;
        cpu->status.c = (value & 1) > 0;
        value = (value >> 1) | (c << 7);
        update_status(&cpu->status, value);
    }
    break;
    case UI_ALU_ZP_ADDX:
        cpu->zp += cpu->x;
        break;
    case UI_ALU_ZP_ADDY:
        cpu->zp += cpu->y;
        break;
    case UI_ALU_ADDR_ADDX:
    {
        uint16_t addition = (cpu->address & 0xff) + cpu->x;
        cpu->address = (cpu->address & 0xff00) | (addition & 0xff);
        cpu->page_jump = addition & 0xff00;
        value += cpu->x;
    }
    break;
    case UI_ALU_ADDR_ADDY:
    {
        uint16_t addition = (cpu->address & 0xff) + cpu->y;
        cpu->address = (cpu->address & 0xff00) | (addition & 0xff);
        cpu->page_jump = addition & 0xff00;
        value += cpu->y;
        break;
    }
    break;

    case UI_ALU_SEC:
        cpu->status.c = 1;
        break;
    case UI_ALU_CLC:
        cpu->status.c = 0;
        break;
    case UI_ALU_SEI:
        cpu->status.i = 1;
        break;
    case UI_ALU_SED:
        cpu->status.d = 1;
        break;
    case UI_ALU_CLD:
        cpu->status.d = 0;
        break;
    case UI_ALU_CLV:
        cpu->status.v = 0;
        break;
    case UI_ALU_CLI:
        cpu->status.i = 0;
        break;
    case UI_ALU_BIT:
        cpu->status.n = (value & 0x80) > 0;
        cpu->status.v = (value & 0x40) > 0;
        cpu->status.z = (cpu->a & value) == 0;
        break;
    case UI_ALU_PHP:
        value |= 0b00010000;
        break;
    case UI_ALU_PLP:
        value &= 0b11101111;
        break;
    case UI_ALU_CPX:
        update_status(&cpu->status, cpu->x - value);
        cpu->status.c = cpu->x >= value;
        break;
    case UI_ALU_CPY:
        update_status(&cpu->status, cpu->y - value);
        cpu->status.c = cpu->y >= value;
        break;
    case UI_ALU_CMP:
        update_status(&cpu->status, cpu->a - value);
        cpu->status.c = cpu->a >= value;
        break;
    case UI_ALU_DCP:
        value--;
        update_status(&cpu->status, cpu->a - value);
        cpu->status.c = cpu->a >= value;
        break;
    case UI_ALU_INC:
        value++;
        update_status(&cpu->status, value);
        break;
    case UI_ALU_DEC:
        value--;
        update_status(&cpu->status, value);
        break;
    case UI_ALU_SHIFT:
        break;
    case UI_ALU_BRANCH_CHECK:
        switch (cpu->instruction)
        {
        case BVS_REL_70:
            if (cpu->status.v)
            {
                should_jump = true;
            }
            break;
        case BVC_REL_50:
            if (!cpu->status.v)
            {
                should_jump = true;
            }
            break;
        case BCS_REL_b0:
            if (cpu->status.c)
            {
                should_jump = true;
            }
            break;
        case BCC_REL_90:
            if (!cpu->status.c)
            {
                should_jump = true;
            }
            break;
        case BEQ_REL_f0:
            if (cpu->status.z)
            {
                should_jump = true;
            }
            break;
        case BNE_REL_d0:
            if (!cpu->status.z)
            {
                should_jump = true;
            }
            break;
        case BMI_REL_30:
            if (cpu->status.n)
            {
                should_jump = true;
            }
            break;
        case BPL_REL_10:
            if (!cpu->status.n)
            {
                should_jump = true;
            }
            break;

        default:
            printf("Unimplemented condition\n");
            __builtin_unreachable();
            break;
        }
        if (should_jump)
        {
            uint16_t addition = (cpu->pc & 0xff) + (int8_t)value;
            cpu->pc_latch = (cpu->pc & 0xff00) | (addition & 0xff);
            cpu->page_jump = addition & 0xff00;
        }
        break;
    case UI_ALU_BRANCH_TRY:
        cpu->pc = cpu->pc_latch;
        if (cpu->page_jump != 0)
        {
            cpu->pc_latch = cpu->pc_latch + cpu->page_jump;
            cpu->page_jump = 0;
            should_jump = true;
        }
        break;
    case UI_ALU_BRANCH_JUMP:
        cpu->pc = cpu->pc_latch;
        break;
    case UI_ALU_BRK:
        value |= 0x10;
        cpu->status.i = 1;
        break;
    case UI_ALU_RTI:
        cpu->status.raw &= ~0x10;
        break;
    case UI_ALU_SLO:
        cpu->status.c = (value & 0x80) > 0;
        value <<= 1;
        cpu->a = cpu->a | value;
        update_status(&cpu->status, cpu->a);
        break;
    case UI_ALU_ANC:
        value &= cpu->a;
        cpu->status.c = (value & 0x80) > 0;
        update_status(&cpu->status, value);
        break;
    case UI_ALU_RLA:
    {
        bool carry = cpu->status.c;
        cpu->status.c = (value & 0x80) > 0;
        value = (value << 1) | carry;
        cpu->a &= value;
        update_status(&cpu->status, cpu->a);
    }
    break;
    case UI_ALU_SRE:
        cpu->status.c = (value & 0x01) > 0;
        value >>= 1;
        cpu->a ^= value;
        update_status(&cpu->status, cpu->a);
        break;
    case UI_ALU_ALR:
        value &= cpu->a;
        cpu->status.c = (value & 0x01) > 0;
        value >>= 1;
        update_status(&cpu->status, value);
        break;
    case UI_ALU_ARR:
    {
        bool carry = cpu->status.c;
        value &= cpu->a;
        value = value >> 1 | carry << 7;
        cpu->status.v = ((value ^ (value >> 1)) & 0x20) > 0;
        cpu->status.c = (value & 0x40) != 0;
        update_status(&cpu->status, value);
    }
    break;
    case UI_ALU_RRA:
    {
        bool carry = cpu->status.c;
        cpu->status.c = (value & 0x01) > 0;
        value = (value >> 1) | (carry << 7);

        uint16_t result = (uint16_t)value + cpu->a + cpu->status.c;
        cpu->status.c = (result & 0xff00) != 0;
        cpu->status.v = (~(value ^ cpu->a) & (result ^ cpu->a) & 0x80) != 0;
        cpu->a = result;
        update_status(&cpu->status, cpu->a);
    }
    break;
    case UI_ALU_XAA:
        value &= (cpu->a | MAGIC) & cpu->x;
        update_status(&cpu->status, value);
        break;
    case UI_ALU_LAX:
        value &= (cpu->a | MAGIC);
        update_status(&cpu->status, value);
        break;
    case UI_ALU_ISC:
    {
        value++;
        uint8_t sub = ~value;

        uint16_t result = (uint16_t)sub + cpu->a + cpu->status.c;
        cpu->status.c = (result & 0xff00) != 0;
        cpu->status.v = (~(sub ^ cpu->a) & (result ^ cpu->a) & 0x80) != 0;
        cpu->a = result;
        update_status(&cpu->status, cpu->a);
    }
    break;
    case UI_ALU_TAS:
        cpu->sp = cpu->a & cpu->x;
        if (!should_jump)
        {
            value++;
        }
        value &= cpu->sp;
        break;
    case UI_ALU_SHY:
        if (!should_jump)
        {
            value++;
        }
        value &= cpu->y;
        break;
    case UI_ALU_SHX:
        if (!should_jump)
        {
            value++;
        }
        value &= cpu->x;
        break;
    case UI_ALU_AHX:
        if (!should_jump)
        {
            value++;
        }
        value &= cpu->a & cpu->x;
        break;
    case UI_ALU_LAS:
        value &= cpu->sp;
        update_status(&cpu->status, value);
        break;
    case UI_ALU_AXS:
    {
        uint8_t operand = cpu->a & cpu->x;

        uint16_t result = operand - value;
        cpu->status.c = (result & 0xff00) == 0;
        value = result;
    }
    case UI_ALU_NOP:
        update_status(&cpu->status, value);
        break;

    default:
        printf("Unimplemented ALU operation\n");
        __builtin_unreachable();
        break;
    }

    // Write
    switch (i.dst)
    {
    case UI_REG_INSTRUCTION:
        cpu->instruction = value;
        break;
    case UI_REG_P:
        cpu->status.raw = value | 0b00100000;
        break;
    case UI_REG_OP:
        cpu->op = value;
        break;
    case UI_REG_A:
        if (!should_jump)
        {
            cpu->a = value;
        }
        break;
    case UI_REG_X:
        cpu->x = value;
        break;
    case UI_REG_Y:
        cpu->y = value;
        break;
    case UI_REG_AXS:
        if (!should_jump)
        {
            cpu->a = value;
            cpu->x = value;
            cpu->sp = value;
        }
        break;
    case UI_REG_TEMP_LO:
        cpu->address = (cpu->address & 0xff00) | value;
        break;
    case UI_REG_TEMP_HI:
        cpu->address = (cpu->address & 0x00ff) | value << 8;
        break;
    case UI_REG_PC_LATCH_LO:
        cpu->pc_latch = (cpu->pc_latch & 0xff00) | value;
        break;
    case UI_REG_PC_HI:
        cpu->pc = (cpu->pc_latch & 0x00ff) | value << 8;
        break;
    case UI_REG_SP:
        cpu->sp = value;
        break;
    case UI_REG_ZP:
        cpu->zp = value;
        break;
    case UI_REG_AX:
        cpu->a = value;
        cpu->x = value;
        break;
    case UI_REG_NONE:
    case UI_REG_PC_LO:
        break;
    }

    return i.finished && !should_jump;
}

void ic_6502_tick(ic_6502_registers *cpu, struct ic_6502_bus *bus, bool irq, bool reset)
{
    cpu->cycles++;

    // RESET aborts the current instruction.
    if (reset)
    {
        cpu->state = IC_6502_RESET;
        cpu->cycle = 0;
        cpu->instruction = 0;
        cpu->page_jump = 0;
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
    cpu->page_jump = 0;
    if (cpu->nmi_requested)
    {
        cpu->state = IC_6502_NMI;
        cpu->nmi_requested = false;
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

struct micro_instruction uinstructions[256][20] =
    {
        [BRK_IMP_00] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_PC_HI, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_SP_DEC, .mem_dst = UI_REG_NONE, .src = UI_REG_PC_LO, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_SP_DEC, .mem_dst = UI_REG_NONE, .src = UI_REG_P, .alu_op = UI_ALU_BRK, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_SP_DEC, .mem_dst = UI_REG_NONE, .src = UI_REG_P, .alu_op = UI_ALU_BRK, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_BRK_LO, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_PC_LATCH_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_BRK_HI, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_PC_HI, .finished = true},
        },
        [ORA_IZX_01] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ORA, .dst = UI_REG_A, .finished = true},
        },
        [KIL_IMP_02] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_BRK_HI, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_BRK_LO, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_BRK_LO, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_BRK_HI, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_BRK_HI, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_BRK_HI, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_BRK_HI, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_BRK_HI, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_BRK_HI, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = true},
        },
        [SLO_IZX_03] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_SLO, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_ZPG_04] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [ORA_ZPG_05] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ORA, .dst = UI_REG_A, .finished = true},
        },
        [ASL_ZPG_06] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ASL, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SLO_ZPG_07] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_SLO, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [PHP_IMP_08] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_NONE, .src = UI_REG_P, .alu_op = UI_ALU_PHP, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_SP_DEC, .mem_dst = UI_REG_NONE, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [ORA_IMM_09] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ORA, .dst = UI_REG_A, .finished = true},
        },
        [ASL_IMP_0a] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ASLA, .dst = UI_REG_A, .finished = true},
        },
        [ANC_IMM_0b] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ANC, .dst = UI_REG_A, .finished = true},
        },
        [NOP_ABS_0c] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [ORA_ABS_0d] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ORA, .dst = UI_REG_A, .finished = true},
        },
        [ASL_ABS_0e] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ASL, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SLO_ABS_0f] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_SLO, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [BPL_REL_10] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_CHECK, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_TRY, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_JUMP, .dst = UI_REG_NONE, .finished = true},
        },
        [ORA_IZY_11] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ORA, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ORA, .dst = UI_REG_A, .finished = true},
        },
        [KIL_IMP_12] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_INSTRUCTION, .finished = true},
        },
        [SLO_IZY_13] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_SLO, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_ZPX_14] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [ORA_ZPX_15] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ORA, .dst = UI_REG_A, .finished = true},
        },
        [ASL_ZPX_16] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ASL, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SLO_ZPX_17] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_SLO, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [CLC_IMP_18] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CLC, .dst = UI_REG_NONE, .finished = true},
        },
        [ORA_ABY_19] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ORA, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ORA, .dst = UI_REG_A, .finished = true},
        },
        [NOP_IMP_1a] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SLO_ABY_1b] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_SLO, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_ABX_1c] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [ORA_ABX_1d] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ORA, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ORA, .dst = UI_REG_A, .finished = true},
        },
        [ASL_ABX_1e] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ASL, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SLO_ABX_1f] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_SLO, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [JSR_ABS_20] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_PC_LATCH_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_SP, .mem_dst = UI_REG_NONE, .src = UI_REG_PC_HI, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_SP_DEC, .mem_dst = UI_REG_NONE, .src = UI_REG_PC_LO, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_SP_DEC, .mem_dst = UI_REG_NONE, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_PC_HI, .finished = true},
        },
        [AND_IZX_21] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_AND, .dst = UI_REG_A, .finished = true},
        },
        [KIL_IMP_22] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_INSTRUCTION, .finished = true},
        },
        [RLA_IZX_23] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_RLA, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [BIT_ZPG_24] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BIT, .dst = UI_REG_NONE, .finished = true},
        },
        [AND_ZPG_25] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_AND, .dst = UI_REG_A, .finished = true},
        },
        [ROL_ZPG_26] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ROL, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [RLA_ZPG_27] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_RLA, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [PLP_IMP_28] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_SP_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_SP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_PLP, .dst = UI_REG_P, .finished = true},
        },
        [AND_IMM_29] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_AND, .dst = UI_REG_A, .finished = true},
        },
        [ROL_IMP_2a] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ROLA, .dst = UI_REG_A, .finished = true},
        },
        [ANC_IMM_2b] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ANC, .dst = UI_REG_A, .finished = true},
        },
        [BIT_ABS_2c] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BIT, .dst = UI_REG_NONE, .finished = true},
        },
        [AND_ABS_2d] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_AND, .dst = UI_REG_A, .finished = true},
        },
        [ROL_ABS_2e] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ROL, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [RLA_ABS_2f] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_RLA, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [BMI_REL_30] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_CHECK, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_TRY, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_JUMP, .dst = UI_REG_NONE, .finished = true},
        },
        [AND_IZY_31] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_AND, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_AND, .dst = UI_REG_A, .finished = true},
        },
        [KIL_IMP_32] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_INSTRUCTION, .finished = true},
        },
        [RLA_IZY_33] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_RLA, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_ZPX_34] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [AND_ZPX_35] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_AND, .dst = UI_REG_A, .finished = true},
        },
        [ROL_ZPX_36] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ROL, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [RLA_ZPX_37] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_RLA, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SEC_IMP_38] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_SEC, .dst = UI_REG_NONE, .finished = true},
        },
        [AND_ABY_39] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_AND, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_AND, .dst = UI_REG_A, .finished = true},
        },
        [NOP_IMP_3a] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [RLA_ABY_3b] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_RLA, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_ABX_3c] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [AND_ABX_3d] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_AND, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_AND, .dst = UI_REG_A, .finished = true},
        },
        [ROL_ABX_3e] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ROL, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [RLA_ABX_3f] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_RLA, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [RTI_IMP_40] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_SP_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_SP_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_P, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_SP_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_PC_LATCH_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_SP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_RTI, .dst = UI_REG_PC_HI, .finished = true},
        },
        [EOR_IZX_41] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_EOR, .dst = UI_REG_A, .finished = true},
        },
        [KIL_IMP_42] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_INSTRUCTION, .finished = true},
        },
        [SRE_IZX_43] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_SRE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_ZPG_44] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [EOR_ZPG_45] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_EOR, .dst = UI_REG_A, .finished = true},
        },
        [LSR_ZPG_46] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_LSR, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SRE_ZPG_47] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_SRE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [PHA_IMP_48] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_A, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_SP_DEC, .mem_dst = UI_REG_NONE, .src = UI_REG_A, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [EOR_IMM_49] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_EOR, .dst = UI_REG_A, .finished = true},
        },
        [LSR_IMP_4a] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_LSRA, .dst = UI_REG_A, .finished = true},
        },
        [ALR_IMM_4b] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ALR, .dst = UI_REG_A, .finished = true},
        },
        [JMP_ABS_4c] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_PC_LATCH_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_PC_HI, .finished = true},
        },
        [EOR_ABS_4d] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_EOR, .dst = UI_REG_A, .finished = true},
        },
        [LSR_ABS_4e] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_LSR, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SRE_ABS_4f] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_SRE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [BVC_REL_50] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_CHECK, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_TRY, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_JUMP, .dst = UI_REG_NONE, .finished = true},
        },
        [EOR_IZY_51] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_EOR, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_EOR, .dst = UI_REG_A, .finished = true},
        },
        [KIL_IMP_52] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_INSTRUCTION, .finished = true},
        },
        [SRE_IZY_53] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_SRE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_ZPX_54] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [EOR_ZPX_55] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_EOR, .dst = UI_REG_A, .finished = true},
        },
        [LSR_ZPX_56] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_LSR, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SRE_ZPX_57] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_SRE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [CLI_IMP_58] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CLI, .dst = UI_REG_NONE, .finished = true},
        },
        [EOR_ABY_59] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_EOR, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_EOR, .dst = UI_REG_A, .finished = true},
        },
        [NOP_IMP_5a] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SRE_ABY_5b] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_SRE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_ABX_5c] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [EOR_ABX_5d] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_EOR, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_EOR, .dst = UI_REG_A, .finished = true},
        },
        [LSR_ABX_5e] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_LSR, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SRE_ABX_5f] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_SRE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [RTS_IMP_60] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_SP_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_SP_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_PC_LATCH_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_SP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_PC_HI, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [ADC_IZX_61] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ADC, .dst = UI_REG_A, .finished = true},
        },
        [KIL_IMP_62] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_INSTRUCTION, .finished = true},
        },
        [RRA_IZX_63] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_RRA, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_ZPG_64] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [ADC_ZPG_65] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ADC, .dst = UI_REG_A, .finished = true},
        },
        [ROR_ZPG_66] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ROR, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [RRA_ZPG_67] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_RRA, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [PLA_IMP_68] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_NONE, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_SP_INC, .mem_dst = UI_REG_NONE, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_SP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_A, .finished = true},
        },
        [ADC_IMM_69] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ADC, .dst = UI_REG_A, .finished = true},
        },
        [ROR_IMP_6a] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_RORA, .dst = UI_REG_A, .finished = true},
        },
        [ARR_IMM_6b] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ARR, .dst = UI_REG_A, .finished = true},
        },
        [JMP_IND_6c] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_PC_LATCH_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_PC_HI, .finished = true},
        },
        [ADC_ABS_6d] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ADC, .dst = UI_REG_A, .finished = true},
        },
        [ROR_ABS_6e] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ROR, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [RRA_ABS_6f] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_RRA, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [BVS_REL_70] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_CHECK, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_TRY, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_JUMP, .dst = UI_REG_NONE, .finished = true},
        },
        [ADC_IZY_71] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ADC, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ADC, .dst = UI_REG_A, .finished = true},
        },
        [KIL_IMP_72] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_INSTRUCTION, .finished = true},
        },
        [RRA_IZY_73] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_RRA, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_ZPX_74] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [ADC_ZPX_75] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ADC, .dst = UI_REG_A, .finished = true},
        },
        [ROR_ZPX_76] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ROR, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [RRA_ZPX_77] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_RRA, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SEI_IMP_78] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_SEI, .dst = UI_REG_NONE, .finished = true},
        },
        [ADC_ABY_79] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ADC, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ADC, .dst = UI_REG_A, .finished = true},
        },
        [NOP_IMP_7a] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [RRA_ABY_7b] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_RRA, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_ABX_7c] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [ADC_ABX_7d] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ADC, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ADC, .dst = UI_REG_A, .finished = true},
        },
        [ROR_ABX_7e] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ROR, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [RRA_ABX_7f] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_RRA, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_IMM_80] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [STA_IZX_81] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_A, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_A, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_IMM_82] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SAX_IZX_83] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_AX, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_AX, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [STY_ZPG_84] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_Y, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_Y, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [STA_ZPG_85] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_A, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [STX_ZPG_86] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_X, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SAX_ZPG_87] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_AX, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_AX, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [DEY_IMP_88] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_DEY, .dst = UI_REG_Y, .finished = true},
        },
        [NOP_IMM_89] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [TXA_IMP_8a] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_X, .alu_op = UI_ALU_NOP, .dst = UI_REG_A, .finished = true},
        },
        [XAA_IMM_8b] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_XAA, .dst = UI_REG_A, .finished = true},
        },
        [STY_ABS_8c] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_Y, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_Y, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [STA_ABS_8d] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_A, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_A, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [STX_ABS_8e] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_X, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SAX_ABS_8f] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_AX, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_AX, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [BCC_REL_90] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_CHECK, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_TRY, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_JUMP, .dst = UI_REG_NONE, .finished = true},
        },
        [STA_IZY_91] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_A, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_A, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [KIL_IMP_92] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_INSTRUCTION, .finished = true},
        },
        [AHX_IZY_93] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_TEMP_HI, .alu_op = UI_ALU_AHX, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_A, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [STY_ZPX_94] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_Y, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_Y, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [STA_ZPX_95] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_A, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_A, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [STX_ZPY_96] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_X, .alu_op = UI_ALU_ZP_ADDY, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_X, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SAX_ZPY_97] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_AX, .alu_op = UI_ALU_ZP_ADDY, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_X, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [TYA_IMP_98] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_Y, .alu_op = UI_ALU_NOP, .dst = UI_REG_A, .finished = true},
        },
        [STA_ABY_99] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_A, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_A, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [TXS_IMP_9a] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_X, .alu_op = UI_ALU_NONE, .dst = UI_REG_SP, .finished = true},
        },
        [TAS_ABY_9b] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_TEMP_HI, .alu_op = UI_ALU_TAS, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SHY_ABX_9c] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_TEMP_HI, .alu_op = UI_ALU_SHY, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [STA_ABX_9d] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_A, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_A, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SHX_ABY_9e] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_TEMP_HI, .alu_op = UI_ALU_SHX, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [AHX_ABY_9f] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_TEMP_HI, .alu_op = UI_ALU_AHX, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [LDY_IMM_a0] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_Y, .finished = true},
        },
        [LDA_IZX_a1] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_A, .finished = true},
        },
        [LDX_IMM_a2] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_X, .finished = true},
        },
        [LAX_IZX_a3] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_AX, .finished = true},
        },
        [LDY_ZPG_a4] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_Y, .finished = true},
        },
        [LDA_ZPG_a5] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_A, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_A, .finished = true},
        },
        [LDX_ZPG_a6] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_X, .finished = true},
        },
        [LAX_ZPG_a7] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_AX, .finished = true},
        },
        [TAY_IMP_a8] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_A, .alu_op = UI_ALU_NOP, .dst = UI_REG_Y, .finished = true},
        },
        [LDA_IMM_a9] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_A, .finished = true},
        },
        [TAX_IMP_aa] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_A, .alu_op = UI_ALU_NOP, .dst = UI_REG_X, .finished = true},
        },
        [LAX_IMM_ab] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_LAX, .dst = UI_REG_AX, .finished = true},
        },
        [LDY_ABS_ac] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_Y, .finished = true},
        },
        [LDA_ABS_ad] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_A, .finished = true},
        },
        [LDX_ABS_ae] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_X, .finished = true},
        },
        [LAX_ABS_af] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_AX, .finished = true},
        },
        [BCS_REL_b0] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_CHECK, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_TRY, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_JUMP, .dst = UI_REG_NONE, .finished = true},
        },
        [LDA_IZY_b1] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_A, .finished = true},
        },
        [KIL_IMP_b2] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_INSTRUCTION, .finished = true},
        },
        [LAX_IZY_b3] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_AX, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_AX, .finished = true},
        },
        [LDY_ZPX_b4] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_Y, .finished = true},
        },
        [LDA_ZPX_b5] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_A, .finished = true},
        },
        [LDX_ZPY_b6] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDY, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_X, .finished = true},
        },
        [LAX_ZPY_b7] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDY, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_AX, .finished = true},
        },
        [CLV_IMP_b8] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CLV, .dst = UI_REG_NONE, .finished = true},
        },
        [LDA_ABY_b9] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_A, .finished = true},
        },
        [TSX_IMP_ba] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_SP, .dst = UI_REG_X, .finished = true},
        },
        [LAS_ABY_bb] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_LAS, .dst = UI_REG_AXS, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_LAS, .dst = UI_REG_AXS, .finished = true},
        },
        [LDY_ABX_bc] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_Y, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_Y, .finished = true},
        },
        [LDA_ABX_bd] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_A, .finished = true},
        },
        [LDX_ABY_be] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_X, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_X, .finished = true},
        },
        [LAX_ABY_bf] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_AX, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NOP, .dst = UI_REG_AX, .finished = true},
        },
        [CPY_IMM_c0] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CPY, .dst = UI_REG_NONE, .finished = true},
        },
        [CMP_IZX_c1] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CMP, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_IMM_c2] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [DCP_IZX_c3] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_DCP, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [CPY_ZPG_c4] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CPY, .dst = UI_REG_NONE, .finished = true},
        },
        [CMP_ZPG_c5] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CMP, .dst = UI_REG_NONE, .finished = true},
        },
        [DEC_ZPG_c6] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_DEC, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [DCP_ZPG_c7] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_DCP, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [INY_IMP_c8] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_INY, .dst = UI_REG_Y, .finished = true},
        },
        [CMP_IMM_c9] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CMP, .dst = UI_REG_NONE, .finished = true},
        },
        [DEX_IMP_ca] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_DEX, .dst = UI_REG_X, .finished = true},
        },
        [AXS_IMM_cb] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_AXS, .dst = UI_REG_X, .finished = true},
        },
        [CPY_ABS_cc] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CPY, .dst = UI_REG_NONE, .finished = true},
        },
        [CMP_ABS_cd] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CMP, .dst = UI_REG_NONE, .finished = true},
        },
        [DEC_ABS_ce] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_DEC, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [DCP_ABS_cf] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_DCP, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [BNE_REL_d0] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_CHECK, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_TRY, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_JUMP, .dst = UI_REG_NONE, .finished = true},
        },
        [CMP_IZY_d1] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CMP, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CMP, .dst = UI_REG_NONE, .finished = true},
        },
        [KIL_IMP_d2] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_INSTRUCTION, .finished = true},
        },
        [DCP_IZY_d3] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_DCP, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_ZPX_d4] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [CMP_ZPX_d5] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CMP, .dst = UI_REG_NONE, .finished = true},
        },
        [DEC_ZPX_d6] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_DEC, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [DCP_ZPX_d7] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_DCP, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [CLD_IMP_d8] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CLD, .dst = UI_REG_NONE, .finished = true},
        },
        [CMP_ABY_d9] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CMP, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CMP, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_IMP_da] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [DCP_ABY_db] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_DEC, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_DCP, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_ABX_dc] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [CMP_ABX_dd] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CMP, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CMP, .dst = UI_REG_NONE, .finished = true},
        },
        [DEC_ABX_de] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_DEC, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [DCP_ABX_df] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_DCP, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [CPX_IMM_e0] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CPX, .dst = UI_REG_NONE, .finished = true},
        },
        [SBC_IZX_e1] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_SBC, .dst = UI_REG_A, .finished = true},
        },
        [NOP_IMM_e2] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [ISC_IZX_e3] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ISC, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [CPX_ZPG_e4] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CPX, .dst = UI_REG_NONE, .finished = true},
        },
        [SBC_ZPG_e5] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_SBC, .dst = UI_REG_A, .finished = true},
        },
        [INC_ZPG_e6] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_INC, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [ISC_ZPG_e7] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ISC, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [INX_IMP_e8] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_INX, .dst = UI_REG_X, .finished = true},
        },
        [SBC_IMM_e9] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_SBC, .dst = UI_REG_A, .finished = true},
        },
        [NOP_IMP_ea] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SBC_IMM_eb] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_SBC, .dst = UI_REG_A, .finished = true},
        },
        [CPX_ABS_ec] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_CPX, .dst = UI_REG_NONE, .finished = true},
        },
        [SBC_ABS_ed] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_SBC, .dst = UI_REG_A, .finished = true},
        },
        [INC_ABS_ee] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_INC, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [ISC_ABS_ef] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ISC, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [BEQ_REL_f0] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_CHECK, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_TRY, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_BRANCH_JUMP, .dst = UI_REG_NONE, .finished = true},
        },
        [SBC_IZY_f1] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_SBC, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_SBC, .dst = UI_REG_A, .finished = true},
        },
        [KIL_IMP_f2] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_INSTRUCTION, .finished = true},
        },
        [ISC_IZY_f3] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ISC, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_ZPX_f4] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SBC_ZPX_f5] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_SBC, .dst = UI_REG_A, .finished = true},
        },
        [INC_ZPX_f6] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_INC, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [ISC_ZPX_f7] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_ZP, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_ZP_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_ZP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ISC, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_ZP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SED_IMP_f8] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_SED, .dst = UI_REG_NONE, .finished = true},
        },
        [SBC_ABY_f9] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_SBC, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_SBC, .dst = UI_REG_A, .finished = true},
        },
        [NOP_IMP_fa] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [ISC_ABY_fb] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_TEMP_LO, .alu_op = UI_ALU_ADDR_ADDY, .dst = UI_REG_TEMP_LO, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ISC, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [NOP_ABX_fc] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [SBC_ABX_fd] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_SBC, .dst = UI_REG_A, .finished = true},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_SBC, .dst = UI_REG_A, .finished = true},
        },
        [INC_ABX_fe] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_INC, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
        [ISC_ABX_ff] = {
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_INSTRUCTION, .src = UI_REG_NONE, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_LO, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_PC_INC, .mem_dst = UI_REG_TEMP_HI, .src = UI_REG_OP, .alu_op = UI_ALU_ADDR_ADDX, .dst = UI_REG_NONE, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_LATCH, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_READ, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_OP, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_ISC, .dst = UI_REG_OP, .finished = false},
            {.action = UI_BUS_WRITE, .address = UI_ADDR_TEMP, .mem_dst = UI_REG_NONE, .src = UI_REG_OP, .alu_op = UI_ALU_NONE, .dst = UI_REG_NONE, .finished = true},
        },
};

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cpu/ic_6502.h"
#include "cpu/ic_rp2a03.h"

#define FREQ 48000.0

static uint8_t const length_table[] = {
    10,
    254,
    20,
    2,
    40,
    4,
    80,
    6,
    160,
    8,
    60,
    10,
    14,
    12,
    26,
    14,
    12,
    16,
    24,
    18,
    48,
    20,
    96,
    22,
    192,
    24,
    72,
    26,
    16,
    28,
    32,
    30,
};

static uint8_t const triangle_sequencer[] = {
    15,
    14,
    13,
    12,
    11,
    10,
    9,
    8,
    7,
    6,
    5,
    4,
    3,
    2,
    1,
    0,
    0,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
};

static uint16_t const noise_period[] = {
    4,
    8,
    16,
    32,
    64,
    96,
    128,
    160,
    202,
    254,
    380,
    508,
    762,
    1016,
    2034,
    4068,
};

static uint8_t duty_pattern[] = {0x40, 0x60, 0x78, 0x9f};

void ic_rp2a03_init(struct ic_rp2a03_registers *cpu)
{
  *cpu = (struct ic_rp2a03_registers){
      .pulse = {
          (struct ic_rp2a03_pulse_channel){
              .duty = 0,
              .counter_halt_envelope_loop = 0,
              .constant_volume = 0,
              .volume = 0,
              .sweep_shift = 0,
              .sweep_negate = 0,
              .sweep_period = 0,
              .sweep_enable = 0,
              .sweep_reload = 0,
              .timer = 0,
              .length = 0,

              .sweep_counter = 0,
              .duty_counter = 0,
              .time = 0,
              .value = 0,
          },

          (struct ic_rp2a03_pulse_channel){
              .duty = 0,
              .counter_halt_envelope_loop = 0,
              .constant_volume = 0,
              .volume = 0,
              .sweep_shift = 0,
              .sweep_negate = 0,
              .sweep_period = 0,
              .sweep_enable = 0,
              .sweep_reload = 0,
              .timer = 0,
              .length = 0,

              .sweep_counter = 0,
              .duty_counter = 0,
              .time = 0,
              .value = 0,
          },
      },
      .triangle_sequence = 0,
      .noise_lfsr = 1,

      .divider = 0,
      .frame_divider = 0,
  };
}

void ic_rp2a03_tick(struct ic_rp2a03_registers *cpu, struct ic_6502_bus *bus, bool irq, bool reset)
{
  cpu->dma_write_phase = !cpu->dma_write_phase;

  if (cpu->dma_write_time > 0)
  {
    cpu->dma_write_time--;

    uint8_t byte = (513 - cpu->dma_write_time) >> 1;
    if (cpu->dma_write_time > 512 && cpu->dma_write_phase)
    {
      // Alignment cycle; dupe last read.
      bus->read(&cpu->dma_data, bus->context, cpu->ic_6502.address);
    }
    else if (cpu->dma_write_phase && cpu->dma_write_time <= 512)
    {
      bus->write(bus->context, 0x2004, cpu->dma_data);
    }
    else
    {
      cpu->ic_6502.address = cpu->dma_page | byte;
      bus->read(&cpu->dma_data, bus->context, cpu->dma_page | byte);
    }
  }
  else
  {
    ic_6502_tick(&cpu->ic_6502, bus, irq, reset);
  }

  if (cpu->divider == 0)
  {
    cpu->divider = 1;

    for (int i = 0; i < 2; i++)
    {
      if (cpu->pulse[i].time == 0)
      {
        cpu->pulse[i].time = cpu->pulse[i].timer;
        cpu->pulse[i].duty_counter = (cpu->pulse[i].duty_counter + 1) % 8;

        if ((cpu->pulse[i].duty & (1 << cpu->pulse[i].duty_counter)) == 0 || cpu->pulse[i].sweep_mute || cpu->pulse[i].length == 0 || cpu->pulse[i].timer < 8)
        {
          cpu->pulse[i].value = 0;
        }
        else
        {
          cpu->pulse[i].value = cpu->pulse[i].constant_volume ? cpu->pulse[i].volume : cpu->pulse[i].envelope_decay_counter;
        }
      }
      else
      {
        cpu->pulse[i].time--;
      }
    }

    uint16_t feedback_bit = cpu->noise_lfsr >> (cpu->noise_mode ? 6 : 1) & 0x0001;
    feedback_bit ^= cpu->noise_lfsr & 0x0001;
    cpu->noise_lfsr = feedback_bit << 14 | cpu->noise_lfsr >> 1;
    if (cpu->noise_length == 0 || (cpu->noise_lfsr & 0x0001))
    {
      cpu->noise_value = 0;
    }
    else
    {
      cpu->noise_value = cpu->noise_constant_volume ? cpu->noise_volume : cpu->noise_envelope_decay_counter;
    }
  }
  else
  {
    cpu->divider--;
  }

  if (cpu->triangle_time == 0)
  {
    cpu->triangle_time = cpu->triangle_timer;
    if (cpu->triangle_linear_counter > 0 && cpu->triangle_length > 0)
    {
      cpu->triangle_sequence = (cpu->triangle_sequence + 1) % 32;
      cpu->triangle_value = triangle_sequencer[cpu->triangle_sequence];
    }
  }
  else
  {
    cpu->triangle_time--;
  }

  if (cpu->frame_divider == 0)
  {
    cpu->frame_divider = 1789773 / 240;

    // Skip frame 5 in mode 0
    if (cpu->frame_counter_mode == 0 && cpu->frame_counter == 4)
    {
      cpu->frame_counter = 3;
    }

    // Every frame count
    if (cpu->triangle_counter_reload)
    {
      cpu->triangle_linear_counter = cpu->triangle_counter_load;
    }
    else if (cpu->triangle_linear_counter > 0)
    {
      cpu->triangle_linear_counter--;
    }

    for (int i = 0; i < 2; i++)
    {
      if (cpu->pulse[i].envelope_start)
      {
        cpu->pulse[i].envelope_start = 0;
        cpu->pulse[i].envelope_decay_counter = 15;
        cpu->pulse[i].envelope_divider = cpu->pulse[i].volume;
      }
      else
      {
        if (cpu->pulse[i].envelope_divider == 0)
        {
          cpu->pulse[i].envelope_divider = cpu->pulse[i].volume;
          if (cpu->pulse[i].envelope_decay_counter == 0)
          {
            if (cpu->pulse[i].counter_halt_envelope_loop)
            {
              cpu->pulse[i].envelope_decay_counter = 15;
            }
          }
          else
          {
            cpu->pulse[i].envelope_decay_counter--;
          }
        }
        else
        {
          cpu->pulse[i].envelope_divider--;
        }
      }
    }

    if (cpu->noise_envelope_start)
    {
      cpu->noise_envelope_start = 0;
      cpu->noise_envelope_decay_counter = 15;
      cpu->noise_envelope_divider = cpu->noise_volume;
    }
    else
    {
      if (cpu->noise_envelope_divider == 0)
      {
        cpu->noise_envelope_divider = cpu->noise_volume;
        if (cpu->noise_envelope_decay_counter == 0)
        {
          if (cpu->noise_counter_halt_envelope_loop)
          {
            cpu->noise_envelope_decay_counter = 15;
          }
        }
        else
        {
          cpu->noise_envelope_decay_counter--;
        }
      }
      else
      {
        cpu->noise_envelope_divider--;
      }
    }

    // Every other frame count
    if (cpu->frame_counter % 2 == 1)
    {
      for (int i = 0; i < 2; i++)
      {
        if (cpu->pulse[i].length != 0 && !cpu->pulse[i].counter_halt_envelope_loop)
        {
          cpu->pulse[i].length--;
        }

        uint16_t change_amount = cpu->pulse[i].timer >> cpu->pulse[i].sweep_shift;
        if (cpu->pulse[i].sweep_negate)
        {
          // Channel 1 and 2 differ in how they negate the shift
          change_amount = i == 0 ? ~change_amount : -change_amount;
        }

        uint16_t target_period = cpu->pulse[i].timer + change_amount;

        if (target_period > 0x07ff)
        {
          cpu->pulse[i].sweep_mute = 1;
        }

        if (cpu->pulse[i].sweep_counter == 0 && cpu->pulse[i].sweep_enable && !cpu->pulse[i].sweep_mute)
        {
          cpu->pulse[i].timer = target_period;
        }

        if (cpu->pulse[i].sweep_counter == 0 || cpu->pulse[i].sweep_reload)
        {
          cpu->pulse[i].sweep_reload = 0;
          cpu->pulse[i].sweep_counter = cpu->pulse[i].sweep_period;
        }
        else
        {
          cpu->pulse[i].sweep_counter--;
        }
      }

      if (!cpu->triangle_counter_halt)
      {
        cpu->triangle_counter_reload = 0;
      }

      if (cpu->triangle_length > 0 && !cpu->triangle_counter_halt)
      {
        cpu->triangle_length--;
      }

      if (cpu->noise_length > 0)
      {
        cpu->noise_length--;
      }
    }

    if (cpu->frame_counter == 0)
    {
      cpu->frame_counter = 4;
    }
    else
    {
      cpu->frame_counter--;
    }
  }
  else
  {
    cpu->frame_divider--;
  }

  uint8_t pulse_group = cpu->pulse[0].value + cpu->pulse[1].value;
  float pulse_out = pulse_group == 0 ? 0 : 95.88 / (8128.0 / pulse_group + 100);
  float tnd_group = cpu->triangle_value / 8227.0 + (cpu->noise_value / 12241.0) + 0;
  float tnd_out = tnd_group == 0 ? 0 : 159.79 / (1 / tnd_group + 100);
  // return pulse_out + tnd_out;
}

void ic_rp2a03_mmapped_read(uint8_t *restrict data, struct ic_rp2a03_registers *cpu, uint16_t address)
{
  switch (address)
  {
  case 0x0000:
  case 0x0001:
  case 0x0002:
  case 0x0003:
  case 0x0004:
  case 0x0005:
  case 0x0006:
  case 0x0007:
  case 0x0008:
  case 0x0009:
  case 0x000a:
  case 0x000b:
  case 0x000c:
  case 0x000d:
  case 0x000e:
  case 0x000f:
  case 0x0010:
  case 0x0011:
  case 0x0012:
  case 0x0013:
    // Open.
    break;
  case 0x0015:
  {
    uint8_t result = 0;
    if (cpu->pulse[0].length > 0)
    {
      *data |= 0x01;
    }

    if (cpu->pulse[1].length > 0)
    {
      *data |= 0x02;
    }

    if (cpu->triangle_length > 0)
    {
      *data |= 0x04;
    }

    if (cpu->noise_length > 0)
    {
      *data |= 0x08;
    }

    return; // result;
  }
  break;
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
    printf("Unmapped APU port read: %x\n", address);
    // Open bus
  }
}

void ic_rp2a03_mmapped_write(struct ic_rp2a03_registers *cpu, uint16_t address, uint8_t data)
{
  switch (address)
  {
  case 0x0000:
  case 0x0004:
    cpu->pulse[address >> 2].duty = duty_pattern[data >> 6];
    cpu->pulse[address >> 2].counter_halt_envelope_loop = (data & 0x20) >> 5;
    cpu->pulse[address >> 2].constant_volume = (data & 0x10) >> 4;
    cpu->pulse[address >> 2].volume = (data & 0x0f);
    break;
  case 0x0001:
  case 0x0005:
    cpu->pulse[address >> 2].sweep_enable = (data & 0x80) >> 7;
    cpu->pulse[address >> 2].sweep_period = (data & 0x70) >> 4;
    cpu->pulse[address >> 2].sweep_negate = (data & 0x08) >> 3;
    cpu->pulse[address >> 2].sweep_shift = (data & 0x07);
    cpu->pulse[address >> 2].sweep_reload = 1;
    break;
  case 0x0002:
  case 0x0006:
    cpu->pulse[address >> 2].timer &= 0xff00;
    cpu->pulse[address >> 2].timer |= data;
    break;
  case 0x0003:
  case 0x0007:
    cpu->pulse[address >> 2].timer &= 0x00ff;
    cpu->pulse[address >> 2].timer |= (data & 0x07) << 8;
    cpu->pulse[address >> 2].length = length_table[data >> 3];

    cpu->pulse[address >> 2].duty_counter = 0;
    cpu->pulse[address >> 2].envelope_start = 1;
    cpu->pulse[address >> 2].sweep_mute = 0;
    break;
  case 0x0008:
    cpu->triangle_counter_halt = data >> 7;
    cpu->triangle_counter_load = data & 0x7f;
    break;
  case 0x0009:
    // Unused
    break;
  case 0x000a:
    cpu->triangle_timer &= 0xff00;
    cpu->triangle_timer |= data;
    break;
  case 0x000b:
    cpu->triangle_timer &= 0x00ff;
    cpu->triangle_timer |= (data & 0x07) << 8;
    cpu->triangle_time = 0;
    cpu->triangle_length = length_table[data >> 3];
    cpu->triangle_counter_reload = 1;
    break;
  case 0x000c:
    cpu->noise_counter_halt_envelope_loop = (data & 0x20) >> 5;
    cpu->noise_constant_volume = (data & 0x10) >> 4;
    cpu->noise_volume = data & 0x0f;
    break;
  case 0x000d:
    // Unused
    break;
  case 0x000e:
    cpu->noise_mode = (data & 0x80) >> 7;
    cpu->noise_timer = noise_period[data & 0x0f];
    break;
  case 0x000f:
    cpu->noise_counter_load = data >> 3;
    cpu->noise_length = cpu->noise_counter_load;
    cpu->noise_envelope_start = 1;
    break;

  case 0x0010:
    cpu->dmc_irq_enable = (data & 0x80) >> 7;
    cpu->dmc_loop = (data & 0x40) >> 6;
    cpu->dmc_frequency = data & 0x0f;
    break;
  case 0x0011:
    cpu->dmc_load_counter = data & 0x7f;
    break;
  case 0x0012:
    cpu->dmc_sample_address = data;
    break;
  case 0x0013:
    cpu->dmc_sample_length = data;
    break;
  case 0x14:
    cpu->dma_page = data << 8;
    cpu->dma_write_time = 513 + (!cpu->dma_write_phase);
    break;
  case 0x0015:
    if ((data & 0x08) == 0)
    {
      cpu->noise_value = 0;
      cpu->noise_length = 0;
    }

    if ((data & 0x04) == 0)
    {
      cpu->triangle_value = 0;
      cpu->triangle_length = 0;
    }

    if ((data & 0x02) == 0)
    {
      cpu->pulse[1].value = 0;
      cpu->pulse[1].length = 0;
    }

    if ((data & 0x01) == 0)
    {
      cpu->pulse[0].value = 0;
      cpu->pulse[0].length = 0;
    }

    break;
  case 0x16:
    cpu->player1.strobe = data & 0x01;
    if (cpu->player1.strobe)
    {
      cpu->player1.latch = cpu->player1.buttons.raw;
    }
    break;
  case 0x0017:
    cpu->frame_counter_mode = data >> 7;

    cpu->player2.strobe = data & 0x01;
    if (cpu->player2.strobe)
    {
      cpu->player2.latch = cpu->player2.buttons.raw;
    }
    break;

  default:
    printf("Unmapped APU port write: %x\n", address);
    break;
  }
}

void ic_rp2a03_nmi(struct ic_rp2a03_registers *cpu)
{
  ic_6502_nmi(&cpu->ic_6502);
}

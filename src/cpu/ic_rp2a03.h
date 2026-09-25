struct controller
{
  bool strobe;
  uint8_t latch;
  union
  {
    uint8_t raw;
    struct
    {
      uint8_t a : 1;
      uint8_t b : 1;
      uint8_t select : 1;
      uint8_t start : 1;
      uint8_t up : 1;
      uint8_t down : 1;
      uint8_t left : 1;
      uint8_t right : 1;
    };
  } buttons;
};

struct ic_rp2a03_pulse_channel
{
  uint8_t duty;
  uint8_t counter_halt_envelope_loop;
  uint8_t constant_volume;
  uint8_t volume;
  uint8_t sweep_shift;
  uint8_t sweep_negate;
  uint8_t sweep_period;
  uint8_t sweep_enable;
  int sweep_mute;
  int sweep_reload;
  uint16_t timer;
  uint8_t length;

  int envelope_start;
  uint8_t envelope_decay_counter;
  uint8_t envelope_divider;
  uint8_t sweep_counter;
  uint8_t duty_counter;
  uint16_t time;
  uint8_t value;
};

struct ic_rp2a03_registers
{
  struct ic_6502_registers ic_6502;

  struct ic_rp2a03_pulse_channel pulse[2];

  uint8_t triangle_counter_halt;
  uint8_t triangle_counter_load;
  uint16_t triangle_timer;
  uint16_t triangle_time;
  uint8_t triangle_length;
  uint8_t triangle_value;
  uint8_t triangle_sequence;
  int triangle_linear_counter;
  int triangle_counter_reload;

  uint8_t noise_counter_halt_envelope_loop;
  uint8_t noise_constant_volume;
  uint8_t noise_volume;
  uint8_t noise_mode;
  uint8_t noise_timer;
  uint8_t noise_time;
  uint8_t noise_counter_load;
  uint8_t noise_length;
  int noise_envelope_start;
  uint16_t noise_lfsr;
  uint8_t noise_value;
  uint8_t noise_envelope_divider;
  uint8_t noise_envelope_decay_counter;

  uint8_t dmc_irq_enable;
  uint8_t dmc_loop;
  uint8_t dmc_frequency;
  uint8_t dmc_load_counter;
  uint8_t dmc_sample_address;
  uint8_t dmc_sample_length;

  int frame_counter_mode;

  bool irq_inhibit;
  bool irq;

  int divider;
  int frame_divider;
  int frame_counter;

  struct controller player1;
  struct controller player2;

  int cycles;

  uint16_t dma_page;
  int dma_write_time;
  uint8_t dma_data;
  bool dma_write_phase;
};

void ic_rp2a03_tick(struct ic_rp2a03_registers *cpu, struct ic_6502_bus *bus, bool irq, bool reset);

void ic_rp2a03_mmapped_read(uint8_t *restrict value, struct ic_rp2a03_registers *restrict cpu, uint16_t address);

void ic_rp2a03_mmapped_write(struct ic_rp2a03_registers *cpu, uint16_t address, uint8_t data);

void ic_rp2a03_nmi(struct ic_rp2a03_registers *cpu);

void ic_rp2a03_init(struct ic_rp2a03_registers *cpu);
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

struct ic_rp2a03_registers
{
  struct ic_6502_registers ic_6502;

  struct controller player1;
  struct controller player2;

  int cycles;

  uint16_t dma_page;
  int dma_write_time;
};

void ic_rp2a03_tick(struct ic_rp2a03_registers *cpu, struct ic_6502_bus *bus, bool irq, bool reset);

void ic_rp2a03_mmapped_read(uint8_t *restrict value, struct ic_rp2a03_registers *restrict cpu, uint16_t address);
void ic_rp2a03_mmapped_write(struct ic_rp2a03_registers *cpu, uint16_t address, uint8_t data);
void ic_rp2a03_nmi(struct ic_rp2a03_registers *cpu);
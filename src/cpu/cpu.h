#include <stdint.h>
#include "bus/bus.h"

#define NMI_VECTOR_LO 0xFFFA
#define NMI_VECTOR_HI 0xFFFB

#define RES_VECTOR_LO 0xFFFC
#define RES_VECTOR_HI 0xFFFD

#define IRQ_VECTOR_LO 0xFFFE
#define IRQ_VECTOR_HI 0xFFFF

struct StatusFlags
{
  /** Carry */
  uint8_t c : 1;
  uint8_t z : 1;
  uint8_t i : 1;
  uint8_t d : 1;
  uint8_t b : 1;
  uint8_t _ : 1;
  uint8_t v : 1;
  uint8_t n : 1;
};

typedef struct Cpu
{
  uint16_t pc;
  uint8_t ac;
  uint8_t x;
  uint8_t y;
  StatusFlags status;
  uint8_t sp;
} Cpu;

void reset(Cpu *cpu, Bus const *bus);
void irq(Cpu *cpu, Bus const *bus);
void nmi(Cpu *cpu, Bus const *bus);
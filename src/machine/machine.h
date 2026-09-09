/**
 *  @brief The core hardware: maps reads/writes to common devices, like RAM and
 * Controllers.
 */
struct tnes_machine
{
  char main_ram[2048];
  char palette_ram[32];

  struct ic_6502_registers *cpu;
  struct ppu *ppu;
  struct cartridge *cartridge; ///< The game currently plugged into the machine.

  bool reset;
  int sampling_count;
  int cycles;
};

/**
 * Ticks the clock one step; ticks the PPU 3 times for each CPU tick.
 *
 * @param machine the machine.
 * @return Returns 1 at hsync to indicate it's time to render.
 */
int tick_machine(struct tnes_machine *machine);

/**
 * Reads a byte from `address` in CPU space - exact behavior may depend on the
 * device at the stated address.
 *
 * @param bus The bus to act on.
 * @param address The address to read from.
 */
uint8_t cpu_bus_read(void *device, uint16_t address);

/**
 * Writes byte to `address` in CPU space - exact behavior may depend on the
 * device at the stated address.
 *
 * @param bus The bus to act on.
 * @param address The address to write to.
 * @param data The value to store
 */
void cpu_bus_write(void *device, uint16_t address, uint8_t data);

/**
 * Reads a byte from `address` in PPU space - exact behavior may depend on the
 * device at the stated address.
 *
 * @param bus The bus to act on.
 * @param address The address to read from.
 */
uint8_t ppu_bus_read(void *device, uint16_t address);

/**
 * Writes byte to `address` in PPU space - exact behavior may depend on the
 * device at the stated address.
 *
 * @param bus The bus to act on.
 * @param address The address to write to.
 * @param data The value to store
 */
void ppu_bus_write(void *device, uint16_t address, uint8_t data);
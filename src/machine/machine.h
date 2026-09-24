/**
 * The core hardware: Mediates timing, sampling audio, and communication
 * between memory devices.
 */

#define AUDIO_BUFFER_SIZE 4800

struct tnes_machine
{
  uint8_t main_ram[0x800];
  uint8_t ppu_ram[0x800];

  struct ic_rp2a03_registers cpu;
  struct ic_2c02_registers ppu;
  struct cartridge *cartridge; ///< The game currently plugged into the machine.

  bool reset;
  int sampling_count;
  int cycles;

  uint8_t cpu_bus_data;
  struct ic_6502_bus cpu_bus;
  struct ic_2c02_bus ppu_bus;

  float audio_samples[AUDIO_BUFFER_SIZE];
  int audio_start;
  int audio_end;
  int sample_count;
};

/**
 * Initializes all fields that aren't being emulated (helpers, buffers, etc.).
 *
 * @param machine the machine.
 */
void init_machine(struct tnes_machine *machine);

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
void cpu_bus_read(uint8_t *restrict data, void *device, uint16_t address);

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
void ppu_bus_read(uint8_t *restrict data, void *device, uint16_t address);

/**
 * Writes byte to `address` in PPU space - exact behavior may depend on the
 * device at the stated address.
 *
 * @param bus The bus to act on.
 * @param address The address to write to.
 * @param data The value to store
 */
void ppu_bus_write(void *device, uint16_t address, uint8_t data);
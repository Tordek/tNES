/*
 * The BUS: Is responsible for ticking the clock, which in turn ticks each
 * device at the appropriate time, and mediates communication between CPU
 * and each device.
 */
#include <stdint.h>

typedef struct Address
{
  uint16_t address;
} Address;

typedef struct Bus
{

} Bus;

/**
 * Reads a byte from `address` - exact behavior may depend on the device at the
 * stated address.
 *
 * @param bus The bus to act on.
 * @param address The address to read from.
 */
uint8_t tnes_read_bus(struct Bus const *bus, Address const address);

/**
 * Writes byte to `address` - exact behavior may depend on the device at the
 * stated address.
 *
 * @param bus The bus to act on.
 * @param address The address to write to.
 * @param data The value to store
 */
void tnes_write_bus(struct Bus *bus, Address const address, uint8_t data);

/**
 * Ticks the clock one step; ticks the PPU 3 times for each CPU tick.
 *
 * @param bus The bus to act on.
 */
void tick(struct Bus const *bus);
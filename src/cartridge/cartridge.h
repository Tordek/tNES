/*
 * The Mapper is the central architecture of the NES:
 * Since each cartridge could add additional hardware, it needs to take care of
 * intercepting requests on both the CPU and PPU buses.
 */

struct cartridge
{
  void *ctx;
  uint8_t (*cpu_read)(void *ctx, uint16_t addr);
  void (*cpu_write)(void *ctx, uint16_t addr, uint8_t data);

  uint8_t (*ppu_read)(void *ctx, uint16_t addr);
  void (*ppu_write)(void *ctx, uint16_t addr, uint8_t data);
};

struct cartridge *cartridge_builder(struct nes_rom *rom);

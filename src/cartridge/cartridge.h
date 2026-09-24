/**
 * Since each cartridge could add additional hardware, it needs to intercept
 * requests on both the CPU and PPU buses.
 */

struct cartridge
{
  void (*cpu_read)(uint8_t *value, void *ctx, uint16_t addr);
  void (*cpu_write)(void *ctx, uint16_t addr, uint8_t data);

  void (*ppu_read)(uint8_t *value, void *ctx, uint8_t *ppu_ram, uint16_t addr);
  void (*ppu_write)(void *ctx, uint8_t *ppu_ram, uint16_t addr, uint8_t data);
};

struct cartridge *cartridge_builder(struct nes_rom *rom);

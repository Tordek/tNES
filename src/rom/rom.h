#include <stdbool.h>
#include <stdio.h>

enum tnes_ines_version
{
  TNES_INES_NES_20,
  TNES_INES,
  TNES_INES_07,
  TNES_INES_ARCHAIC,
};

struct nes_rom
{
  enum tnes_ines_version version;

  uint16_t prg_rom_size;
  uint16_t chr_rom_size;

  uint8_t mirroring;
  bool non_volatile_memory;
  bool has_trainer;
  uint8_t four_screen_mode;

  uint16_t mapper_id;
  uint8_t submapper_id;

  uint8_t *prg_rom;
  uint8_t *chr_rom;
  uint8_t *prg_ram;
};

enum read_rom_rv
{
  READ_ROM_SUCCESS,
  READ_ROM_FREAD_ERROR,
  READ_ROM_INVALID_HEADER,
};

/**
 * Reads a ROM file.
 */
int read_rom(struct nes_rom *rom, FILE *file);

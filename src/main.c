#include <stdint.h>
#include "rom/rom.h"

int main(int argc, char *argv[])
{
  if (argc != 2)
  {
    printf("Usage: %s rom\n", argv[0]);
    return -1;
  }

  struct nes_rom rom;
  FILE *rom_file = fopen(argv[1], "r");
  int rc = read_rom(&rom, rom_file);

  if (rc)
  {
    fprintf(stderr, "Not a iNES/NES2.0 file\n");
    return rc;
  }

  printf("ROM info: %s\n", argv[1]);
  printf("Mapper: %d\n", rom.mapper_id);
  printf("PRG_ROM size: %d bytes\n", rom.prg_rom_size);
  printf("CHR_ROM size: %d bytes\n", rom.chr_rom_size);

  return 0;
}
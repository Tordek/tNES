#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "cJSON.h"
#include "cpu/ic_6502.h"
#include "cpu/ic_rp2a03.h"
#include "rom/rom.h"
#include "cartridge/cartridge.h"
#include "ppu/ppu.h"
#include "machine/machine.h"

uint8_t ram[65536];
uint16_t last_add;
uint8_t last_val;
bool last_action;

void tracking_cpu_bus_read(uint8_t *result, void *device, uint16_t address)
{
  // cpu_bus_read(result, device, address);
  *result = ram[address];
  last_action = 0;
  last_add = address;
  last_val = *result;
}
void tracking_cpu_bus_write(void *device, uint16_t address, uint8_t data)
{

  // cpu_bus_write(device, address, data);
  ram[address] = data;
  last_action = 1;
  last_add = address;
  last_val = data;
}

bool failed;

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    fprintf(stderr, "Specify filename.\n");
    return 1;
  }

  FILE *file = fopen(argv[1], "r");
  if (!file)
  {
    fprintf(stderr, "Failed to open file: %s\n", argv[1]);
    return 1;
  }
  fseek(file, 0, SEEK_END);
  long pos = ftell(file);
  char *buffer = malloc(pos);
  fseek(file, 0, SEEK_SET);

  fread(buffer, 1, pos, file);
  cJSON *json = cJSON_ParseWithLength(buffer, pos);

  cJSON *test;
  cJSON_ArrayForEach(test, json)
  {
    cJSON *name = cJSON_GetObjectItemCaseSensitive(test, "name");

    struct ic_6502_registers cpu;
    ic_6502_init(&cpu);

    struct ic_6502_bus cpu_bus = {
        .context = NULL,
        .read = &tracking_cpu_bus_read,
        .write = &tracking_cpu_bus_write,
    };

    cJSON *initial = cJSON_GetObjectItemCaseSensitive(test, "initial");
    cJSON *initial_a = cJSON_GetObjectItemCaseSensitive(initial, "a");
    cJSON *initial_s = cJSON_GetObjectItemCaseSensitive(initial, "s");
    cJSON *initial_pc = cJSON_GetObjectItemCaseSensitive(initial, "pc");
    cJSON *initial_x = cJSON_GetObjectItemCaseSensitive(initial, "x");
    cJSON *initial_y = cJSON_GetObjectItemCaseSensitive(initial, "y");
    cJSON *initial_p = cJSON_GetObjectItemCaseSensitive(initial, "p");
    cpu.a = initial_a->valueint;
    cpu.pc = initial_pc->valueint;
    cpu.sp = initial_s->valueint;
    cpu.x = initial_x->valueint;
    cpu.y = initial_y->valueint;
    cpu.status.raw = initial_p->valueint;

    cJSON *raminit = cJSON_GetObjectItemCaseSensitive(initial, "ram");
    cJSON *ramstep;

    cJSON_ArrayForEach(ramstep, raminit)
    {
      cJSON *addr = cJSON_GetArrayItem(ramstep, 0);
      cJSON *value = cJSON_GetArrayItem(ramstep, 1);
      ram[addr->valueint] = value->valueint;
      cJSON_free(addr);
    }

    bool fail = false;
    cJSON *cycles = cJSON_GetObjectItemCaseSensitive(test, "cycles");
    cJSON *cycle;
    int i = 0;
    cJSON_ArrayForEach(cycle, cycles)
    {
      ic_6502_tick(&cpu, &cpu_bus, false, false);

      cJSON *addr = cJSON_GetArrayItem(cycle, 0);
      cJSON *value = cJSON_GetArrayItem(cycle, 1);
      cJSON *act = cJSON_GetArrayItem(cycle, 2);

      if (addr->valueint != last_add || value->valueint != last_val)
      {
        printf("Test %s failed: Cycle %d: %x at %x expected %x at %x\n", name->valuestring, i, last_val, last_add, value->valueint, addr->valueint);
        fail = true;
      }
      if (!strcmp(act->valuestring, "read") && last_action)
      {
        printf("Test %s failed: Cycle %d wrote when it should have read.\n", name->valuestring, i);
        fail = true;
      }
      if (!strcmp(act->valuestring, "write") && !last_action)
      {
        printf("Test %s failed: Cycle %d read when it should have written.\n", name->valuestring, i);
        fail = true;
      }
      i++;
    }

    cJSON *final = cJSON_GetObjectItemCaseSensitive(test, "final");

    cJSON *final_a = cJSON_GetObjectItemCaseSensitive(final, "a");
    cJSON *final_s = cJSON_GetObjectItemCaseSensitive(final, "s");
    cJSON *final_pc = cJSON_GetObjectItemCaseSensitive(final, "pc");
    cJSON *final_x = cJSON_GetObjectItemCaseSensitive(final, "x");
    cJSON *final_y = cJSON_GetObjectItemCaseSensitive(final, "y");
    cJSON *final_p = cJSON_GetObjectItemCaseSensitive(final, "p");
    if (cpu.a != final_a->valueint)
    {
      printf("Test %s failed: A (%x) doesn't match expected (%x)\n", name->valuestring, cpu.a, final_a->valueint);
      fail = true;
    }
    if (cpu.pc != final_pc->valueint)
    {
      printf("Test %s failed: PC (%x) doesn't match expected (%x)\n", name->valuestring, cpu.pc, final_pc->valueint);
      fail = true;
    }
    if (cpu.sp != final_s->valueint)
    {
      printf("Test %s failed: SP (%x) doesn't match expected (%x)\n", name->valuestring, cpu.sp, final_s->valueint);
      fail = true;
    }
    if (cpu.x != final_x->valueint)
    {
      printf("Test %s failed: X (%x) doesn't match expected (%x)\n", name->valuestring, cpu.x, final_x->valueint);
      fail = true;
    }
    if (cpu.y != final_y->valueint)
    {
      printf("Test %s failed: Y (%x) doesn't match expected (%x)\n", name->valuestring, cpu.y, final_y->valueint);
      fail = true;
    }
    if (cpu.status.raw != final_p->valueint)
    {
      printf("Test %s failed: P (%x) doesn't match expected (%x)\n", name->valuestring, cpu.status.raw, final_p->valueint);
      fail = true;
    }

    raminit = cJSON_GetObjectItemCaseSensitive(final, "ram");

    cJSON_ArrayForEach(ramstep, raminit)
    {
      cJSON *addr = cJSON_GetArrayItem(ramstep, 0);
      cJSON *value = cJSON_GetArrayItem(ramstep, 1);
      if (ram[addr->valueint] != value->valueint)
      {
        printf("Test %s failed: address %x should contain %x contains %x\n", name->valuestring, addr->valueint, value->valueint, ram[addr->valueint]);
      }
    }
    failed = failed || fail;

    if (!fail)
    {
      // printf(".");
    }

    // printf("Name: %s\n", string);
  }

  return failed;
}
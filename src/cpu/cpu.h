#define NMI_VECTOR_LO 0xffFA
#define NMI_VECTOR_HI 0xffFB

#define RES_VECTOR_LO 0xffFC
#define RES_VECTOR_HI 0xffFD

#define IRQ_VECTOR_LO 0xffFE
#define IRQ_VECTOR_HI 0xffFF

enum ic_6503_instruction_enum
{
  BRK_IMP_00 = 0x00,
  ORA_IZX_01 = 0x01,
  KIL_IMP_02 = 0x02,
  SLO_IZX_03 = 0x03,
  NOP_ZPG_04 = 0x04,
  ORA_ZPG_05 = 0x05,
  ASL_ZPG_06 = 0x06,
  SLO_ZPG_07 = 0x07,
  PHP_IMP_08 = 0x08,
  ORA_IMM_09 = 0x09,
  ASL_IMP_0a = 0x0a,
  ANC_IMM_0b = 0x0b,
  NOP_ABS_0c = 0x0c,
  ORA_ABS_0d = 0x0d,
  ASL_ABS_0e = 0x0e,
  SLO_ABS_0f = 0x0f,
  BPL_REL_10 = 0x10,
  ORA_IZY_11 = 0x11,
  KIL_IMP_12 = 0x12,
  SLO_IZY_13 = 0x13,
  NOP_ZPX_14 = 0x14,
  ORA_ZPX_15 = 0x15,
  ASL_ZPX_16 = 0x16,
  SLO_ZPX_17 = 0x17,
  CLC_IMP_18 = 0x18,
  ORA_ABY_19 = 0x19,
  NOP_IMP_1a = 0x1a,
  SLO_ABY_1b = 0x1b,
  NOP_ABX_1c = 0x1c,
  ORA_ABX_1d = 0x1d,
  ASL_ABX_1e = 0x1e,
  SLO_ABX_1f = 0x1f,
  JSR_ABS_20 = 0x20,
  AND_IZX_21 = 0x21,
  KIL_IMP_22 = 0x22,
  RLA_IZX_23 = 0x23,
  BIT_ZPG_24 = 0x24,
  AND_ZPG_25 = 0x25,
  ROL_ZPG_26 = 0x26,
  RLA_ZPG_27 = 0x27,
  PLP_IMP_28 = 0x28,
  AND_IMM_29 = 0x29,
  ROL_IMP_2a = 0x2a,
  ANC_IMM_2b = 0x2b,
  BIT_ABS_2c = 0x2c,
  AND_ABS_2d = 0x2d,
  ROL_ABS_2e = 0x2e,
  RLA_ABS_2f = 0x2f,
  BMI_REL_30 = 0x30,
  AND_IZY_31 = 0x31,
  KIL_IMP_32 = 0x32,
  RLA_IZY_33 = 0x33,
  NOP_ZPX_34 = 0x34,
  AND_ZPX_35 = 0x35,
  ROL_ZPX_36 = 0x36,
  RLA_ZPX_37 = 0x37,
  SEC_IMP_38 = 0x38,
  AND_ABY_39 = 0x39,
  NOP_IMP_3a = 0x3a,
  RLA_ABY_3b = 0x3b,
  NOP_ABX_3c = 0x3c,
  AND_ABX_3d = 0x3d,
  ROL_ABX_3e = 0x3e,
  RLA_ABX_3f = 0x3f,
  RTI_IMP_40 = 0x40,
  EOR_IZX_41 = 0x41,
  KIL_IMP_42 = 0x42,
  SRE_IZX_43 = 0x43,
  NOP_ZPG_44 = 0x44,
  EOR_ZPG_45 = 0x45,
  LSR_ZPG_46 = 0x46,
  SRE_ZPG_47 = 0x47,
  PHA_IMP_48 = 0x48,
  EOR_IMM_49 = 0x49,
  LSR_IMP_4a = 0x4a,
  ALR_IMM_4b = 0x4b,
  JMP_ABS_4c = 0x4c,
  EOR_ABS_4d = 0x4d,
  LSR_ABS_4e = 0x4e,
  SRE_ABS_4f = 0x4f,
  BVC_REL_50 = 0x50,
  EOR_IZY_51 = 0x51,
  KIL_IMP_52 = 0x52,
  SRE_IZY_53 = 0x53,
  NOP_ZPX_54 = 0x54,
  EOR_ZPX_55 = 0x55,
  LSR_ZPX_56 = 0x56,
  SRE_ZPX_57 = 0x57,
  CLI_IMP_58 = 0x58,
  EOR_ABY_59 = 0x59,
  NOP_IMP_5a = 0x5a,
  SRE_ABY_5b = 0x5b,
  NOP_ABX_5c = 0x5c,
  EOR_ABX_5d = 0x5d,
  LSR_ABX_5e = 0x5e,
  SRE_ABX_5f = 0x5f,
  RTS_IMP_60 = 0x60,
  ADC_IZX_61 = 0x61,
  KIL_IMP_62 = 0x62,
  RRA_IZX_63 = 0x63,
  NOP_ZPG_64 = 0x64,
  ADC_ZPG_65 = 0x65,
  ROR_ZPG_66 = 0x66,
  RRA_ZPG_67 = 0x67,
  PLA_IMP_68 = 0x68,
  ADC_IMM_69 = 0x69,
  ROR_IMP_6a = 0x6a,
  ARR_IMM_6b = 0x6b,
  JMP_IND_6c = 0x6c,
  ADC_ABS_6d = 0x6d,
  ROR_ABS_6e = 0x6e,
  RRA_ABS_6f = 0x6f,
  BVS_REL_70 = 0x70,
  ADC_IZY_71 = 0x71,
  KIL_IMP_72 = 0x72,
  RRA_IZY_73 = 0x73,
  NOP_ZPX_74 = 0x74,
  ADC_ZPX_75 = 0x75,
  ROR_ZPX_76 = 0x76,
  RRA_ZPX_77 = 0x77,
  SEI_IMP_78 = 0x78,
  ADC_ABY_79 = 0x79,
  NOP_IMP_7a = 0x7a,
  RRA_ABY_7b = 0x7b,
  NOP_ABX_7c = 0x7c,
  ADC_ABX_7d = 0x7d,
  ROR_ABX_7e = 0x7e,
  RRA_ABX_7f = 0x7f,
  NOP_IMM_80 = 0x80,
  STA_IZX_81 = 0x81,
  NOP_IMM_82 = 0x82,
  SAX_IZX_83 = 0x83,
  STY_ZPG_84 = 0x84,
  STA_ZPG_85 = 0x85,
  STX_ZPG_86 = 0x86,
  SAX_ZPG_87 = 0x87,
  DEY_IMP_88 = 0x88,
  NOP_IMM_89 = 0x89,
  TXA_IMP_8a = 0x8a,
  XAA_IMM_8b = 0x8b,
  STY_ABS_8c = 0x8c,
  STA_ABS_8d = 0x8d,
  STX_ABS_8e = 0x8e,
  SAX_ABS_8f = 0x8f,
  BCC_REL_90 = 0x90,
  STA_IZY_91 = 0x91,
  KIL_IMP_92 = 0x92,
  AHX_IZY_93 = 0x93,
  STY_ZPX_94 = 0x94,
  STA_ZPX_95 = 0x95,
  STX_ZPY_96 = 0x96,
  SAX_ZPY_97 = 0x97,
  TYA_IMP_98 = 0x98,
  STA_ABY_99 = 0x99,
  TXS_IMP_9a = 0x9a,
  TAS_ABY_9b = 0x9b,
  SHY_ABX_9c = 0x9c,
  STA_ABX_9d = 0x9d,
  SHX_ABY_9e = 0x9e,
  AHX_ABY_9f = 0x9f,
  LDY_IMM_a0 = 0xa0,
  LDA_IZX_a1 = 0xa1,
  LDX_IMM_a2 = 0xa2,
  LAX_IZX_a3 = 0xa3,
  LDY_ZPG_a4 = 0xa4,
  LDA_ZPG_a5 = 0xa5,
  LDX_ZPG_a6 = 0xa6,
  LAX_ZPG_a7 = 0xa7,
  TAY_IMP_a8 = 0xa8,
  LDA_IMM_a9 = 0xa9,
  TAX_IMP_aa = 0xaa,
  LAX_IMM_ab = 0xab,
  LDY_ABS_ac = 0xac,
  LDA_ABS_ad = 0xad,
  LDX_ABS_ae = 0xae,
  LAX_ABS_af = 0xaf,
  BCS_REL_b0 = 0xb0,
  LDA_IZY_b1 = 0xb1,
  KIL_IMP_b2 = 0xb2,
  LAX_IZY_b3 = 0xb3,
  LDY_ZPX_b4 = 0xb4,
  LDA_ZPX_b5 = 0xb5,
  LDX_ZPY_b6 = 0xb6,
  LAX_ZPY_b7 = 0xb7,
  CLV_IMP_b8 = 0xb8,
  LDA_ABY_b9 = 0xb9,
  TSX_IMP_ba = 0xba,
  LAS_ABY_bb = 0xbb,
  LDY_ABX_bc = 0xbc,
  LDA_ABX_bd = 0xbd,
  LDX_ABY_be = 0xbe,
  LAX_ABY_bf = 0xbf,
  CPY_IMM_c0 = 0xc0,
  CMP_IZX_c1 = 0xc1,
  NOP_IMM_c2 = 0xc2,
  DCP_IZX_c3 = 0xc3,
  CPY_ZPG_c4 = 0xc4,
  CMP_ZPG_c5 = 0xc5,
  DEC_ZPG_c6 = 0xc6,
  DCP_ZPG_c7 = 0xc7,
  INY_IMP_c8 = 0xc8,
  CMP_IMM_c9 = 0xc9,
  DEX_IMP_ca = 0xca,
  AXS_IMM_cb = 0xcb,
  CPY_ABS_cc = 0xcc,
  CMP_ABS_cd = 0xcd,
  DEC_ABS_ce = 0xce,
  DCP_ABS_cf = 0xcf,
  BNE_REL_d0 = 0xd0,
  CMP_IZY_d1 = 0xd1,
  KIL_IMP_d2 = 0xd2,
  DCP_IZY_d3 = 0xd3,
  NOP_ZPX_d4 = 0xd4,
  CMP_ZPX_d5 = 0xd5,
  DEC_ZPX_d6 = 0xd6,
  DCP_ZPX_d7 = 0xd7,
  CLD_IMP_d8 = 0xd8,
  CMP_ABY_d9 = 0xd9,
  NOP_IMP_da = 0xda,
  DCP_ABY_db = 0xdb,
  NOP_ABX_dc = 0xdc,
  CMP_ABX_dd = 0xdd,
  DEC_ABX_de = 0xde,
  DCP_ABX_df = 0xdf,
  CPX_IMM_e0 = 0xe0,
  SBC_IZX_e1 = 0xe1,
  NOP_IMM_e2 = 0xe2,
  ISC_IZX_e3 = 0xe3,
  CPX_ZPG_e4 = 0xe4,
  SBC_ZPG_e5 = 0xe5,
  INC_ZPG_e6 = 0xe6,
  ISC_ZPG_e7 = 0xe7,
  INX_IMP_e8 = 0xe8,
  SBC_IMM_e9 = 0xe9,
  NOP_IMP_ea = 0xea,
  SBC_IMM_eb = 0xeb,
  CPX_ABS_ec = 0xec,
  SBC_ABS_ed = 0xed,
  INC_ABS_ee = 0xee,
  ISC_ABS_ef = 0xef,
  BEQ_REL_f0 = 0xf0,
  SBC_IZY_f1 = 0xf1,
  KIL_IMP_f2 = 0xf2,
  ISC_IZY_f3 = 0xf3,
  NOP_ZPX_f4 = 0xf4,
  SBC_ZPX_f5 = 0xf5,
  INC_ZPX_f6 = 0xf6,
  ISC_ZPX_f7 = 0xf7,
  SED_IMP_f8 = 0xf8,
  SBC_ABY_f9 = 0xf9,
  NOP_IMP_fa = 0xfa,
  ISC_ABY_fb = 0xfb,
  NOP_ABX_fc = 0xfc,
  SBC_ABX_fd = 0xfd,
  INC_ABX_fe = 0xfe,
  ISC_ABX_ff = 0xff,
};

enum ic_6502_state
{
  IC_6502_INSTRUCTION,
  IC_6502_RESET,
  IC_6502_NMI,
  IC_6502_IRQ,
};

enum ic_6503_status_enum
{
  IC_6502_STATUS_B = 0x10
};

struct ic_6502_bus
{
  void *context;
  uint8_t (*read)(void *bus, uint16_t address);
  void (*write)(void *bus, uint16_t address, uint8_t data);
};

union ic_6502_status
{
  uint8_t raw;
  struct
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
};

typedef struct ic_6502_registers
{
  uint16_t pc;
  uint8_t a;
  uint8_t x;
  uint8_t y;
  union ic_6502_status status;
  uint8_t sp;

  bool nmi_requested;
  enum ic_6502_state state;
  int cycle;
  enum ic_6503_instruction_enum instruction;
  uint16_t operand;
} ic_6502_registers;

/**
 * Latches the NMI Interrupt Request.
 */
void nmi(ic_6502_registers *cpu);

/**
 * Runs one step of the CPU.
 * Since IRQ and RESET are level-sensitive, it must be checked at tick time,
 * unlike NMI.
 */
void tick_cpu(ic_6502_registers *cpu, struct ic_6502_bus *bus_ops, bool irq, bool reset);

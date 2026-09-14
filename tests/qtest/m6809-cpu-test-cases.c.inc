/* 6809 ISA programs: .disas is the source of truth for what the bytes mean. */
    {
        .name = "adda_half_carry_overflow",
        .disas = "lda #$7f; adda #$01",
        .code = (const uint8_t[]){ 0x86, 0x7f, 0x8b, 0x01 },
        .code_len = 4,
        .regs = (const char *const[]){ "A=80", "CC=7a", NULL }
    },
    {
        .name = "adda_carry_zero",
        .disas = "lda #$ff; adda #$01",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x8b, 0x01 },
        .code_len = 4,
        .regs = (const char *const[]){ "A=00", "CC=75", NULL }
    },
    {
        .name = "adda_signed_overflow",
        .disas = "lda #$80; adda #$80",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x8b, 0x80 },
        .code_len = 4,
        .regs = (const char *const[]){ "A=00", "CC=57", NULL }
    },
    {
        .name = "adda_indexed",
        .disas = "ldd #$80f0; tfr d,x; lda #$05; adda ,x",
        .code = (const uint8_t[]){ 0xcc, 0x80, 0xf0, 0x1f, 0x01, 0x86, 0x05, 0xab, 0x84 },
        .code_len = 9,
        .regs = (const char *const[]){ "A=08", "X=80f0", "CC=50", NULL },
        .operand = (const uint8_t[]){ 0x03 },
        .operand_len = 1
    },
    {
        .name = "addb_immediate",
        .disas = "clra; ldb #$05; addb #$03",
        .code = (const uint8_t[]){ 0x4f, 0xc6, 0x05, 0xcb, 0x03 },
        .code_len = 5,
        .regs = (const char *const[]){ "B=08", "D=0008", "CC=50", NULL }
    },
    {
        .name = "addd_immediate",
        .disas = "ldd #$0005; addd #$0003",
        .code = (const uint8_t[]){ 0xcc, 0x00, 0x05, 0xc3, 0x00, 0x03 },
        .code_len = 6,
        .regs = (const char *const[]){ "D=0008", "CC=50", NULL }
    },
    {
        .name = "daa_no_adjust",
        .disas = "lda #$12; adda #$34; daa",
        .code = (const uint8_t[]){ 0x86, 0x12, 0x8b, 0x34, 0x19 },
        .code_len = 5,
        .regs = (const char *const[]){ "A=46", "CC=50", NULL }
    },
    {
        .name = "daa_low_nibble",
        .disas = "lda #$09; adda #$01; daa",
        .code = (const uint8_t[]){ 0x86, 0x09, 0x8b, 0x01, 0x19 },
        .code_len = 5,
        .regs = (const char *const[]){ "A=10", "CC=50", NULL }
    },
    {
        .name = "daa_half_carry",
        .disas = "lda #$08; adda #$08; daa",
        .code = (const uint8_t[]){ 0x86, 0x08, 0x8b, 0x08, 0x19 },
        .code_len = 5,
        .regs = (const char *const[]){ "A=16", "CC=70", NULL }
    },
    {
        .name = "daa_both_digits",
        .disas = "lda #$99; adda #$01; daa",
        .code = (const uint8_t[]){ 0x86, 0x99, 0x8b, 0x01, 0x19 },
        .code_len = 5,
        .regs = (const char *const[]){ "A=00", "CC=55", NULL }
    },
    {
        .name = "daa_high_digit",
        .disas = "lda #$50; adda #$50; daa",
        .code = (const uint8_t[]){ 0x86, 0x50, 0x8b, 0x50, 0x19 },
        .code_len = 5,
        .regs = (const char *const[]){ "A=00", NULL }
    },
    {
        .name = "daa_input_carry",
        .disas = "lda #$90; adda #$90; daa",
        .code = (const uint8_t[]){ 0x86, 0x90, 0x8b, 0x90, 0x19 },
        .code_len = 5,
        .regs = (const char *const[]){ "A=80", NULL }
    },
    {
        .name = "idx_zero_x",
        .disas = "ldd #$2000; tfr d,x; lda ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xa6, 0x84 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=5a", "X=2000", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x5a } },
        .n_load = 1
    },
    {
        .name = "idx_offset5_positive",
        .disas = "ldd #$2000; tfr d,x; lda 15,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xa6, 0x0f },
        .code_len = 7,
        .regs = (const char *const[]){ "A=5a", "X=2000", NULL },
        .load = (const IsaLoad[]){ { 0x200f, 0x5a } },
        .n_load = 1
    },
    {
        .name = "idx_offset5_negative",
        .disas = "ldd #$2000; tfr d,x; lda -16,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xa6, 0x10 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=5a", "X=2000", NULL },
        .load = (const IsaLoad[]){ { 0x1ff0, 0x5a } },
        .n_load = 1
    },
    {
        .name = "idx_postinc1",
        .disas = "ldd #$2000; tfr d,x; lda ,x+",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xa6, 0x80 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=5a", "X=2001", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x5a } },
        .n_load = 1
    },
    {
        .name = "idx_postinc2",
        .disas = "ldd #$2000; tfr d,x; lda ,x++",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xa6, 0x81 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=5a", "X=2002", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x5a } },
        .n_load = 1
    },
    {
        .name = "idx_predec1",
        .disas = "ldd #$2001; tfr d,x; lda ,-x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x01, 0x1f, 0x01, 0xa6, 0x82 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=5a", "X=2000", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x5a } },
        .n_load = 1
    },
    {
        .name = "idx_predec2",
        .disas = "ldd #$2002; tfr d,x; lda ,--x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x02, 0x1f, 0x01, 0xa6, 0x83 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=5a", "X=2000", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x5a } },
        .n_load = 1
    },
    {
        .name = "idx_postinc_wrap",
        .disas = "ldd #$ffff; tfr d,x; lda ,x++",
        .code = (const uint8_t[]){ 0xcc, 0xff, 0xff, 0x1f, 0x01, 0xa6, 0x81 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=00", "X=0001", NULL }
    },
    {
        .name = "idx_predec_wrap",
        .disas = "ldd #$0000; tfr d,x; lda ,-x",
        .code = (const uint8_t[]){ 0xcc, 0x00, 0x00, 0x1f, 0x01, 0xa6, 0x82 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=00", "X=ffff", NULL }
    },
    {
        .name = "idx_b_offset",
        .disas = "ldd #$2000; tfr d,x; ldb #$80; lda b,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xc6, 0x80, 0xa6, 0x85 },
        .code_len = 9,
        .regs = (const char *const[]){ "A=5a", "B=80", NULL },
        .load = (const IsaLoad[]){ { 0x1f80, 0x5a } },
        .n_load = 1
    },
    {
        .name = "idx_a_offset",
        .disas = "ldd #$2000; tfr d,x; lda #$ff; lda a,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x86, 0xff, 0xa6, 0x86 },
        .code_len = 9,
        .regs = (const char *const[]){ "A=5a", NULL },
        .load = (const IsaLoad[]){ { 0x1fff, 0x5a } },
        .n_load = 1
    },
    {
        .name = "idx_offset8",
        .disas = "ldd #$2000; tfr d,x; lda -128,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xa6, 0x88, 0x80 },
        .code_len = 8,
        .regs = (const char *const[]){ "A=5a", "X=2000", NULL },
        .load = (const IsaLoad[]){ { 0x1f80, 0x5a } },
        .n_load = 1
    },
    {
        .name = "idx_offset16_wrap",
        .disas = "ldd #$fff0; tfr d,x; lda 32,x",
        .code = (const uint8_t[]){ 0xcc, 0xff, 0xf0, 0x1f, 0x01, 0xa6, 0x89, 0x00, 0x20 },
        .code_len = 9,
        .regs = (const char *const[]){ "A=5a", "X=fff0", NULL },
        .load = (const IsaLoad[]){ { 0x0010, 0x5a } },
        .n_load = 1
    },
    {
        .name = "idx_d_offset",
        .disas = "ldd #$2000; tfr d,x; ldd #$f000; lda d,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xcc, 0xf0, 0x00, 0xa6, 0x8b },
        .code_len = 10,
        .regs = (const char *const[]){ "A=5a", "X=2000", NULL },
        .load = (const IsaLoad[]){ { 0x1000, 0x5a } },
        .n_load = 1
    },
    {
        .name = "idx_pcr8",
        .disas = "lda -19,pcr",
        .code = (const uint8_t[]){ 0xa6, 0x8c, 0xed },
        .code_len = 3,
        .regs = (const char *const[]){ "A=5a", NULL },
        .load = (const IsaLoad[]){ { 0x7ff0, 0x5a } },
        .n_load = 1
    },
    {
        .name = "idx_pcr16",
        .disas = "lda -28676,pcr",
        .code = (const uint8_t[]){ 0xa6, 0x8d, 0x8f, 0xfc },
        .code_len = 4,
        .regs = (const char *const[]){ "A=5a", NULL },
        .load = (const IsaLoad[]){ { 0x1000, 0x5a } },
        .n_load = 1
    },
    {
        .name = "idx_ldd_boundary",
        .disas = "ldd #$7fff; tfr d,x; ldd ,x",
        .code = (const uint8_t[]){ 0xcc, 0x7f, 0xff, 0x1f, 0x01, 0xec, 0x84 },
        .code_len = 7,
        .regs = (const char *const[]){ "D=5acc", "X=7fff", NULL },
        .load = (const IsaLoad[]){ { 0x7fff, 0x5a } },
        .n_load = 1
    },
    {
        .name = "idx_ldd_wrap",
        .disas = "ldd #$ffff; tfr d,x; ldd ,x",
        .code = (const uint8_t[]){ 0xcc, 0xff, 0xff, 0x1f, 0x01, 0xec, 0x84 },
        .code_len = 7,
        .regs = (const char *const[]){ "D=0034", "X=ffff", NULL },
        .load = (const IsaLoad[]){ { 0x0000, 0x34 } },
        .n_load = 1
    },
    {
        .name = "idx_indirect_x",
        .disas = "ldd #$2000; tfr d,x; lda [,x]",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xa6, 0x94 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=5a", "X=2000", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x30 }, { 0x2001, 0x00 }, { 0x3000, 0x5a } },
        .n_load = 3
    },
    {
        .name = "idx_indirect_pointer_wrap",
        .disas = "ldd #$ffff; tfr d,x; lda [,x]",
        .code = (const uint8_t[]){ 0xcc, 0xff, 0xff, 0x1f, 0x01, 0xa6, 0x94 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=5a", "X=ffff", NULL },
        .load = (const IsaLoad[]){ { 0x0000, 0x30 }, { 0x0030, 0x5a } },
        .n_load = 2
    },
    {
        .name = "idx_indirect_postinc2",
        .disas = "ldd #$2000; tfr d,x; lda [,x++]",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xa6, 0x91 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=5a", "X=2002", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x30 }, { 0x2001, 0x00 }, { 0x3000, 0x5a } },
        .n_load = 3
    },
    {
        .name = "idx_indirect_predec2",
        .disas = "ldd #$2002; tfr d,x; lda [,--x]",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x02, 0x1f, 0x01, 0xa6, 0x93 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=5a", "X=2000", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x30 }, { 0x2001, 0x00 }, { 0x3000, 0x5a } },
        .n_load = 3
    },
    {
        .name = "idx_indirect_offset8",
        .disas = "ldd #$2000; tfr d,x; lda [-128,x]",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xa6, 0x98, 0x80 },
        .code_len = 8,
        .regs = (const char *const[]){ "A=5a", NULL },
        .load = (const IsaLoad[]){ { 0x1f80, 0x30 }, { 0x1f81, 0x00 }, { 0x3000, 0x5a } },
        .n_load = 3
    },
    {
        .name = "idx_extended_indirect",
        .disas = "lda [$2000]",
        .code = (const uint8_t[]){ 0xa6, 0x9f, 0x20, 0x00 },
        .code_len = 4,
        .regs = (const char *const[]){ "A=5a", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x30 }, { 0x2001, 0x00 }, { 0x3000, 0x5a } },
        .n_load = 3
    },
    {
        .name = "lda_direct",
        .disas = "lda #$80; tfr a,dp; lda <$f0",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x1f, 0x8b, 0x96, 0xf0 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=5a", "DP=80", "CC=50", NULL },
        .operand = (const uint8_t[]){ 0x5a },
        .operand_len = 1
    },
    {
        .name = "ldb_extended",
        .disas = "ldb $80f0",
        .code = (const uint8_t[]){ 0xf6, 0x80, 0xf0 },
        .code_len = 3,
        .regs = (const char *const[]){ "B=5a", "CC=50", NULL },
        .operand = (const uint8_t[]){ 0x5a },
        .operand_len = 1
    },
    {
        .name = "ldd_direct",
        .disas = "lda #$80; tfr a,dp; ldd <$f0",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x1f, 0x8b, 0xdc, 0xf0 },
        .code_len = 6,
        .regs = (const char *const[]){ "D=1234", "DP=80", "CC=50", NULL },
        .operand = (const uint8_t[]){ 0x12, 0x34 },
        .operand_len = 2
    },
    {
        .name = "ldd_extended_wrap",
        .disas = "ldd $ffff",
        .code = (const uint8_t[]){ 0xfc, 0xff, 0xff },
        .code_len = 3,
        .regs = (const char *const[]){ "D=0034", "CC=50", NULL },
        .load = (const IsaLoad[]){ { 0xffff, 0x00 }, { 0x0000, 0x34 } },
        .n_load = 2
    },
    {
        .name = "sta_direct",
        .disas = "lda #$20; tfr a,dp; lda #$5a; sta <$00",
        .code = (const uint8_t[]){ 0x86, 0x20, 0x1f, 0x8b, 0x86, 0x5a, 0x97, 0x00 },
        .code_len = 8,
        .regs = (const char *const[]){ "A=5a", "DP=20", "CC=50", NULL },
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x5a }, 1 } },
        .n_mem = 1
    },
    {
        .name = "sta_indexed",
        .disas = "ldd #$2000; tfr d,x; lda #$5a; sta ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x86, 0x5a, 0xa7, 0x84 },
        .code_len = 9,
        .regs = (const char *const[]){ "A=5a", "X=2000", "CC=50", NULL },
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x5a }, 1 } },
        .n_mem = 1
    },
    {
        .name = "sta_indirect",
        .disas = "ldd #$2000; tfr d,x; lda #$5a; sta [,x]",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x86, 0x5a, 0xa7, 0x94 },
        .code_len = 9,
        .regs = (const char *const[]){ "A=5a", "X=2000", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x30 }, { 0x2001, 0x00 } },
        .n_load = 2,
        .mem = (const IsaMem[]){ { 0x3000, (const uint8_t[]){ 0x5a }, 1 } },
        .n_mem = 1
    },
    {
        .name = "stb_indexed",
        .disas = "ldd #$2000; tfr d,x; ldb #$a5; stb ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xc6, 0xa5, 0xe7, 0x84 },
        .code_len = 9,
        .regs = (const char *const[]){ "B=a5", "X=2000", "CC=58", NULL },
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0xa5 }, 1 } },
        .n_mem = 1
    },
    {
        .name = "std_indexed",
        .disas = "ldd #$2000; tfr d,x; ldd #$1234; std ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xcc, 0x12, 0x34, 0xed, 0x84 },
        .code_len = 10,
        .regs = (const char *const[]){ "D=1234", "X=2000", "CC=50", NULL },
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x12, 0x34 }, 2 } },
        .n_mem = 1
    },
    {
        .name = "std_wrap",
        .disas = "ldd #$1234; std $ffff",
        .code = (const uint8_t[]){ 0xcc, 0x12, 0x34, 0xfd, 0xff, 0xff },
        .code_len = 6,
        .regs = (const char *const[]){ "D=1234", "CC=50", NULL },
        .mem = (const IsaMem[]){ { 0x0000, (const uint8_t[]){ 0x34 }, 1 } },
        .n_mem = 1
    },
    {
        .name = "std_boundary",
        .disas = "ldd #$1234; std $7fff",
        .code = (const uint8_t[]){ 0xcc, 0x12, 0x34, 0xfd, 0x7f, 0xff },
        .code_len = 6,
        .regs = (const char *const[]){ "D=1234", "CC=50", NULL },
        .mem = (const IsaMem[]){ { 0x7fff, (const uint8_t[]){ 0x12 }, 1 } },
        .n_mem = 1
    },
    {
        .name = "ldx_immediate",
        .disas = "ldx #$1234",
        .code = (const uint8_t[]){ 0x8e, 0x12, 0x34 },
        .code_len = 3,
        .regs = (const char *const[]){ "X=1234", "CC=50", NULL }
    },
    {
        .name = "ldx_indexed",
        .disas = "ldd #$2000; tfr d,y; ldx ,y",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x02, 0xae, 0xa4 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=1234", "Y=2000", "CC=50", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x12 }, { 0x2001, 0x34 } },
        .n_load = 2
    },
    {
        .name = "ldx_extended_wrap",
        .disas = "ldx $ffff",
        .code = (const uint8_t[]){ 0xbe, 0xff, 0xff },
        .code_len = 3,
        .regs = (const char *const[]){ "X=0034", "CC=50", NULL },
        .load = (const IsaLoad[]){ { 0xffff, 0x00 }, { 0x0000, 0x34 } },
        .n_load = 2
    },
    {
        .name = "ldy_immediate",
        .disas = "ldy #$1234",
        .code = (const uint8_t[]){ 0x10, 0x8e, 0x12, 0x34 },
        .code_len = 4,
        .regs = (const char *const[]){ "Y=1234", "CC=50", NULL }
    },
    {
        .name = "ldu_immediate",
        .disas = "ldu #$1234",
        .code = (const uint8_t[]){ 0xce, 0x12, 0x34 },
        .code_len = 3,
        .regs = (const char *const[]){ "U=1234", "CC=50", NULL }
    },
    {
        .name = "lds_immediate",
        .disas = "lds #$1234",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x12, 0x34 },
        .code_len = 4,
        .regs = (const char *const[]){ "S=1234", "CC=50", NULL }
    },
    {
        .name = "stx_indexed",
        .disas = "ldd #$2000; tfr d,y; ldx #$1234; stx ,y",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x02, 0x8e, 0x12, 0x34, 0xaf, 0xa4 },
        .code_len = 10,
        .regs = (const char *const[]){ "X=1234", "Y=2000", "CC=50", NULL },
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x12, 0x34 }, 2 } },
        .n_mem = 1
    },
    {
        .name = "stx_postinc",
        .disas = "ldd #$2000; tfr d,x; stx ,x++",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xaf, 0x81 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2002", "CC=50", NULL },
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x20, 0x02 }, 2 } },
        .n_mem = 1
    },
    {
        .name = "stx_predec",
        .disas = "ldd #$2002; tfr d,x; stx ,--x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x02, 0x1f, 0x01, 0xaf, 0x83 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2000", "CC=50", NULL },
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x20, 0x00 }, 2 } },
        .n_mem = 1
    },
    {
        .name = "sty_indexed",
        .disas = "ldd #$2000; tfr d,x; ldy #$1234; sty ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x10, 0x8e, 0x12, 0x34, 0x10, 0xaf, 0x84 },
        .code_len = 12,
        .regs = (const char *const[]){ "Y=1234", "X=2000", "CC=50", NULL },
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x12, 0x34 }, 2 } },
        .n_mem = 1
    },
    {
        .name = "stu_indexed",
        .disas = "ldd #$2000; tfr d,x; ldu #$1234; stu ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xce, 0x12, 0x34, 0xef, 0x84 },
        .code_len = 10,
        .regs = (const char *const[]){ "U=1234", "X=2000", "CC=50", NULL },
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x12, 0x34 }, 2 } },
        .n_mem = 1
    },
    {
        .name = "sts_indexed",
        .disas = "ldd #$2000; tfr d,x; lds #$1234; sts ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x10, 0xce, 0x12, 0x34, 0x10, 0xef, 0x84 },
        .code_len = 12,
        .regs = (const char *const[]){ "S=1234", "X=2000", "CC=50", NULL },
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x12, 0x34 }, 2 } },
        .n_mem = 1
    },
    {
        .name = "sta_preserves_carry",
        .disas = "lda #$ff; adda #$01; sta $2000",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x8b, 0x01, 0xb7, 0x20, 0x00 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=00", "CC=75", NULL },
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x00 }, 1 } },
        .n_mem = 1
    },
    {
        .name = "tfr_a_b",
        .disas = "lda #$12; ldb #$00; tfr a,b",
        .code = (const uint8_t[]){ 0x86, 0x12, 0xc6, 0x00, 0x1f, 0x89 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=12", "B=12", NULL }
    },
    {
        .name = "tfr_a_x",
        .disas = "lda #$5a; tfr a,x",
        .code = (const uint8_t[]){ 0x86, 0x5a, 0x1f, 0x81 },
        .code_len = 4,
        .regs = (const char *const[]){ "A=5a", "X=ff5a", NULL }
    },
    {
        .name = "tfr_x_y",
        .disas = "ldx #$1111; ldy #$2222; tfr x,y",
        .code = (const uint8_t[]){ 0x8e, 0x11, 0x11, 0x10, 0x8e, 0x22, 0x22, 0x1f, 0x12 },
        .code_len = 9,
        .regs = (const char *const[]){ "X=1111", "Y=1111", NULL }
    },
    {
        .name = "exg_a_b",
        .disas = "lda #$12; ldb #$34; exg a,b",
        .code = (const uint8_t[]){ 0x86, 0x12, 0xc6, 0x34, 0x1e, 0x89 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=34", "B=12", NULL }
    },
    {
        .name = "exg_d_x",
        .disas = "ldd #$1234; ldx #$abcd; exg d,x",
        .code = (const uint8_t[]){ 0xcc, 0x12, 0x34, 0x8e, 0xab, 0xcd, 0x1e, 0x01 },
        .code_len = 8,
        .regs = (const char *const[]){ "D=abcd", "X=1234", NULL }
    },
    {
        .name = "exg_u_s",
        .disas = "ldu #$aaaa; lds #$bbbb; exg u,s",
        .code = (const uint8_t[]){ 0xce, 0xaa, 0xaa, 0x10, 0xce, 0xbb, 0xbb, 0x1e, 0x34 },
        .code_len = 9,
        .regs = (const char *const[]){ "U=bbbb", "S=aaaa", NULL }
    },
    {
        .name = "sex_positive",
        .disas = "ldb #$7f; sex",
        .code = (const uint8_t[]){ 0xc6, 0x7f, 0x1d },
        .code_len = 3,
        .regs = (const char *const[]){ "D=007f", "A=00", "B=7f", "CC=50", NULL }
    },
    {
        .name = "sex_negative",
        .disas = "ldb #$80; sex",
        .code = (const uint8_t[]){ 0xc6, 0x80, 0x1d },
        .code_len = 3,
        .regs = (const char *const[]){ "D=ff80", "A=ff", "B=80", "CC=58", NULL }
    },
    {
        .name = "anda_value",
        .disas = "lda #$ff; anda #$0f",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x84, 0x0f },
        .code_len = 4,
        .regs = (const char *const[]){ "A=0f", "CC=50", NULL }
    },
    {
        .name = "anda_indexed",
        .disas = "ldd #$80f0; tfr d,x; lda #$ff; anda ,x",
        .code = (const uint8_t[]){ 0xcc, 0x80, 0xf0, 0x1f, 0x01, 0x86, 0xff, 0xa4, 0x84 },
        .code_len = 9,
        .regs = (const char *const[]){ "A=0f", "X=80f0", "CC=50", NULL },
        .operand = (const uint8_t[]){ 0x0f },
        .operand_len = 1
    },
    {
        .name = "andb_immediate",
        .disas = "ldb #$ff; andb #$0f",
        .code = (const uint8_t[]){ 0xc6, 0xff, 0xc4, 0x0f },
        .code_len = 4,
        .regs = (const char *const[]){ "B=0f", "CC=50", NULL }
    },
    {
        .name = "ora_negative",
        .disas = "lda #$00; ora #$80",
        .code = (const uint8_t[]){ 0x86, 0x00, 0x8a, 0x80 },
        .code_len = 4,
        .regs = (const char *const[]){ "A=80", "CC=58", NULL }
    },
    {
        .name = "ora_indexed",
        .disas = "ldd #$80f0; tfr d,x; lda #$00; ora ,x",
        .code = (const uint8_t[]){ 0xcc, 0x80, 0xf0, 0x1f, 0x01, 0x86, 0x00, 0xaa, 0x84 },
        .code_len = 9,
        .regs = (const char *const[]){ "A=0f", "X=80f0", NULL },
        .operand = (const uint8_t[]){ 0x0f },
        .operand_len = 1
    },
    {
        .name = "eora_zero",
        .disas = "lda #$ff; eora #$ff",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x88, 0xff },
        .code_len = 4,
        .regs = (const char *const[]){ "A=00", "CC=54", NULL }
    },
    {
        .name = "eora_indexed",
        .disas = "ldd #$80f0; tfr d,x; lda #$ff; eora ,x",
        .code = (const uint8_t[]){ 0xcc, 0x80, 0xf0, 0x1f, 0x01, 0x86, 0xff, 0xa8, 0x84 },
        .code_len = 9,
        .regs = (const char *const[]){ "A=f0", "X=80f0", NULL },
        .operand = (const uint8_t[]){ 0x0f },
        .operand_len = 1
    },
    {
        .name = "eorb_immediate",
        .disas = "ldb #$ff; eorb #$0f",
        .code = (const uint8_t[]){ 0xc6, 0xff, 0xc8, 0x0f },
        .code_len = 4,
        .regs = (const char *const[]){ "B=f0", "CC=58", NULL }
    },
    {
        .name = "bita_negative",
        .disas = "lda #$ff; bita #$80",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x85, 0x80 },
        .code_len = 4,
        .regs = (const char *const[]){ "A=ff", "CC=58", NULL }
    },
    {
        .name = "bita_indexed",
        .disas = "ldd #$80f0; tfr d,x; lda #$ff; bita ,x",
        .code = (const uint8_t[]){ 0xcc, 0x80, 0xf0, 0x1f, 0x01, 0x86, 0xff, 0xa5, 0x84 },
        .code_len = 9,
        .regs = (const char *const[]){ "A=ff", "X=80f0", "CC=50", NULL },
        .operand = (const uint8_t[]){ 0x0f },
        .operand_len = 1
    },
    {
        .name = "bitb_immediate",
        .disas = "ldb #$ff; bitb #$0f",
        .code = (const uint8_t[]){ 0xc6, 0xff, 0xc5, 0x0f },
        .code_len = 4,
        .regs = (const char *const[]){ "B=ff", "CC=50", NULL }
    },
    {
        .name = "anda_preserves_carry",
        .disas = "lda #$ff; adda #$01; anda #$00",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x8b, 0x01, 0x84, 0x00 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=00", "CC=75", NULL }
    },
    {
        .name = "suba_borrow",
        .disas = "lda #$00; suba #$01",
        .code = (const uint8_t[]){ 0x86, 0x00, 0x80, 0x01 },
        .code_len = 4,
        .regs = (const char *const[]){ "A=ff", "CC=59", NULL }
    },
    {
        .name = "suba_overflow",
        .disas = "lda #$80; suba #$01",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x80, 0x01 },
        .code_len = 4,
        .regs = (const char *const[]){ "A=7f", "CC=52", NULL }
    },
    {
        .name = "suba_indexed",
        .disas = "ldd #$80f0; tfr d,x; lda #$08; suba ,x",
        .code = (const uint8_t[]){ 0xcc, 0x80, 0xf0, 0x1f, 0x01, 0x86, 0x08, 0xa0, 0x84 },
        .code_len = 9,
        .regs = (const char *const[]){ "A=05", "X=80f0", "CC=50", NULL },
        .operand = (const uint8_t[]){ 0x03 },
        .operand_len = 1
    },
    {
        .name = "subb_immediate",
        .disas = "ldb #$08; subb #$03",
        .code = (const uint8_t[]){ 0xc6, 0x08, 0xc0, 0x03 },
        .code_len = 4,
        .regs = (const char *const[]){ "B=05", "CC=50", NULL }
    },
    {
        .name = "sbca_with_carry",
        .disas = "lda #$00; suba #$01; lda #$01; sbca #$00",
        .code = (const uint8_t[]){ 0x86, 0x00, 0x80, 0x01, 0x86, 0x01, 0x82, 0x00 },
        .code_len = 8,
        .regs = (const char *const[]){ "A=00", "CC=54", NULL }
    },
    {
        .name = "sbca_indexed",
        .disas = "ldd #$80f0; tfr d,x; lda #$08; suba #$00; sbca ,x",
        .code = (const uint8_t[]){ 0xcc, 0x80, 0xf0, 0x1f, 0x01, 0x86, 0x08, 0x80, 0x00, 0xa2, 0x84 },
        .code_len = 11,
        .regs = (const char *const[]){ "A=05", NULL },
        .operand = (const uint8_t[]){ 0x03 },
        .operand_len = 1
    },
    {
        .name = "adca_from_zero",
        .disas = "lda #$ff; adda #$01; adca #$00",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x8b, 0x01, 0x89, 0x00 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=01", "CC=50", NULL }
    },
    {
        .name = "adca_carry_out",
        .disas = "lda #$ff; adda #$01; adca #$ff",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x8b, 0x01, 0x89, 0xff },
        .code_len = 6,
        .regs = (const char *const[]){ "A=00", "CC=75", NULL }
    },
    {
        .name = "adca_overflow",
        .disas = "lda #$00; suba #$01; lda #$7f; adca #$00",
        .code = (const uint8_t[]){ 0x86, 0x00, 0x80, 0x01, 0x86, 0x7f, 0x89, 0x00 },
        .code_len = 8,
        .regs = (const char *const[]){ "A=80", "CC=7a", NULL }
    },
    {
        .name = "adcb_immediate",
        .disas = "ldb #$05; subb #$00; adcb #$03",
        .code = (const uint8_t[]){ 0xc6, 0x05, 0xc0, 0x00, 0xc9, 0x03 },
        .code_len = 6,
        .regs = (const char *const[]){ "B=08", "CC=50", NULL }
    },
    {
        .name = "subd_borrow",
        .disas = "ldd #$0000; subd #$0001",
        .code = (const uint8_t[]){ 0xcc, 0x00, 0x00, 0x83, 0x00, 0x01 },
        .code_len = 6,
        .regs = (const char *const[]){ "D=ffff", "CC=59", NULL }
    },
    {
        .name = "subd_overflow",
        .disas = "ldd #$8000; subd #$0001",
        .code = (const uint8_t[]){ 0xcc, 0x80, 0x00, 0x83, 0x00, 0x01 },
        .code_len = 6,
        .regs = (const char *const[]){ "D=7fff", "CC=52", NULL }
    },
    {
        .name = "subd_indexed",
        .disas = "ldd #$80f0; tfr d,x; ldd #$0008; .byte $a3; anda #$00",
        .code = (const uint8_t[]){ 0xcc, 0x80, 0xf0, 0x1f, 0x01, 0xcc, 0x00, 0x08, 0xa3, 0x84 },
        .code_len = 10,
        .regs = (const char *const[]){ "D=0005", "X=80f0", NULL },
        .operand = (const uint8_t[]){ 0x00, 0x03 },
        .operand_len = 2
    },
    {
        .name = "subd_extended_wrap",
        .disas = "ldd #$0034; .byte $b3; stu $0000",
        .code = (const uint8_t[]){ 0xcc, 0x00, 0x34, 0xb3, 0xff, 0xff },
        .code_len = 6,
        .regs = (const char *const[]){ "D=0000", "CC=54", NULL },
        .load = (const IsaLoad[]){ { 0xffff, 0x00 }, { 0x0000, 0x34 } },
        .n_load = 2
    },
    {
        .name = "suba_preserves_h",
        .disas = "lda #$0f; adda #$01; suba #$01",
        .code = (const uint8_t[]){ 0x86, 0x0f, 0x8b, 0x01, 0x80, 0x01 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=0f", "CC=70", NULL }
    },
    {
        .name = "cmpa_equal",
        .disas = "lda #$05; cmpa #$05",
        .code = (const uint8_t[]){ 0x86, 0x05, 0x81, 0x05 },
        .code_len = 4,
        .regs = (const char *const[]){ "A=05", "CC=54", NULL }
    },
    {
        .name = "cmpa_overflow",
        .disas = "lda #$80; cmpa #$01",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x81, 0x01 },
        .code_len = 4,
        .regs = (const char *const[]){ "A=80", "CC=52", NULL }
    },
    {
        .name = "cmpa_indexed",
        .disas = "ldd #$80f0; tfr d,x; lda #$08; cmpa ,x",
        .code = (const uint8_t[]){ 0xcc, 0x80, 0xf0, 0x1f, 0x01, 0x86, 0x08, 0xa1, 0x84 },
        .code_len = 9,
        .regs = (const char *const[]){ "A=08", "X=80f0", "CC=50", NULL },
        .operand = (const uint8_t[]){ 0x03 },
        .operand_len = 1
    },
    {
        .name = "cmpb_immediate",
        .disas = "ldb #$05; cmpb #$05",
        .code = (const uint8_t[]){ 0xc6, 0x05, 0xc1, 0x05 },
        .code_len = 4,
        .regs = (const char *const[]){ "B=05", "CC=54", NULL }
    },
    {
        .name = "cmpx_overflow",
        .disas = "ldx #$8000; cmpx #$0001",
        .code = (const uint8_t[]){ 0x8e, 0x80, 0x00, 0x8c, 0x00, 0x01 },
        .code_len = 6,
        .regs = (const char *const[]){ "X=8000", "CC=52", NULL }
    },
    {
        .name = "cmpx_indexed",
        .disas = "ldd #$80f0; tfr d,y; ldx #$1234; cmpx ,y",
        .code = (const uint8_t[]){ 0xcc, 0x80, 0xf0, 0x1f, 0x02, 0x8e, 0x12, 0x34, 0xac, 0xa4 },
        .code_len = 10,
        .regs = (const char *const[]){ "X=1234", "Y=80f0", "CC=54", NULL },
        .operand = (const uint8_t[]){ 0x12, 0x34 },
        .operand_len = 2
    },
    {
        .name = "cmpx_wrap",
        .disas = "ldx #$0034; cmpx $ffff",
        .code = (const uint8_t[]){ 0x8e, 0x00, 0x34, 0xbc, 0xff, 0xff },
        .code_len = 6,
        .regs = (const char *const[]){ "X=0034", "CC=54", NULL },
        .load = (const IsaLoad[]){ { 0x0000, 0x34 } },
        .n_load = 1
    },
    {
        .name = "cmpd_equal",
        .disas = "ldd #$1234; cmpd #$1234",
        .code = (const uint8_t[]){ 0xcc, 0x12, 0x34, 0x10, 0x83, 0x12, 0x34 },
        .code_len = 7,
        .regs = (const char *const[]){ "D=1234", "CC=54", NULL }
    },
    {
        .name = "cmpd_indexed",
        .disas = "ldd #$80f0; tfr d,x; ldd #$1234; cmpd ,x",
        .code = (const uint8_t[]){ 0xcc, 0x80, 0xf0, 0x1f, 0x01, 0xcc, 0x12, 0x34, 0x10, 0xa3, 0x84 },
        .code_len = 11,
        .regs = (const char *const[]){ "D=1234", "X=80f0", "CC=54", NULL },
        .operand = (const uint8_t[]){ 0x12, 0x34 },
        .operand_len = 2
    },
    {
        .name = "cmpy_immediate",
        .disas = "ldy #$1234; cmpy #$1234",
        .code = (const uint8_t[]){ 0x10, 0x8e, 0x12, 0x34, 0x10, 0x8c, 0x12, 0x34 },
        .code_len = 8,
        .regs = (const char *const[]){ "Y=1234", "CC=54", NULL }
    },
    {
        .name = "cmpu_immediate",
        .disas = "ldu #$1234; cmpu #$1234",
        .code = (const uint8_t[]){ 0xce, 0x12, 0x34, 0x11, 0x83, 0x12, 0x34 },
        .code_len = 7,
        .regs = (const char *const[]){ "U=1234", "CC=54", NULL }
    },
    {
        .name = "nega_one",
        .disas = "lda #$01; nega",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x40 },
        .code_len = 3,
        .regs = (const char *const[]){ "A=ff", "CC=59", NULL }
    },
    {
        .name = "nega_overflow",
        .disas = "lda #$80; nega",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x40 },
        .code_len = 3,
        .regs = (const char *const[]){ "A=80", "CC=5b", NULL }
    },
    {
        .name = "negb",
        .disas = "ldb #$01; negb",
        .code = (const uint8_t[]){ 0xc6, 0x01, 0x50 },
        .code_len = 3,
        .regs = (const char *const[]){ "B=ff", "CC=59", NULL }
    },
    {
        .name = "neg_indexed",
        .disas = "ldd #$2000; tfr d,x; neg ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x60, 0x84 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2000", "CC=59", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x5a } },
        .n_load = 1,
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0xa6 }, 1 } },
        .n_mem = 1
    },
    {
        .name = "coma_zero",
        .disas = "lda #$00; coma",
        .code = (const uint8_t[]){ 0x86, 0x00, 0x43 },
        .code_len = 3,
        .regs = (const char *const[]){ "A=ff", "CC=59", NULL }
    },
    {
        .name = "coma_ff",
        .disas = "lda #$ff; coma",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x43 },
        .code_len = 3,
        .regs = (const char *const[]){ "A=00", "CC=55", NULL }
    },
    {
        .name = "com_indexed",
        .disas = "ldd #$2000; tfr d,x; com ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x63, 0x84 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2000", "CC=59", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x5a } },
        .n_load = 1,
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0xa5 }, 1 } },
        .n_mem = 1
    },
    {
        .name = "lsra_one",
        .disas = "lda #$01; lsra",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x44 },
        .code_len = 3,
        .regs = (const char *const[]){ "A=00", "CC=55", NULL }
    },
    {
        .name = "lsra_80",
        .disas = "lda #$80; lsra",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x44 },
        .code_len = 3,
        .regs = (const char *const[]){ "A=40", "CC=50", NULL }
    },
    {
        .name = "lsra_preserves_v",
        .disas = "lda #$7f; adda #$01; lsra",
        .code = (const uint8_t[]){ 0x86, 0x7f, 0x8b, 0x01, 0x44 },
        .code_len = 5,
        .regs = (const char *const[]){ "A=40", "CC=72", NULL }
    },
    {
        .name = "lsr_indexed",
        .disas = "ldd #$2000; tfr d,x; lsr ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x64, 0x84 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2000", "CC=50", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x5a } },
        .n_load = 1,
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x2d }, 1 } },
        .n_mem = 1
    },
    {
        .name = "asra_80",
        .disas = "lda #$80; asra",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x47 },
        .code_len = 3,
        .regs = (const char *const[]){ "A=c0", "CC=58", NULL }
    },
    {
        .name = "asra_81",
        .disas = "lda #$81; asra",
        .code = (const uint8_t[]){ 0x86, 0x81, 0x47 },
        .code_len = 3,
        .regs = (const char *const[]){ "A=c0", "CC=59", NULL }
    },
    {
        .name = "asr_indexed",
        .disas = "ldd #$2000; tfr d,x; asr ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x67, 0x84 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2000", "CC=58", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x80 } },
        .n_load = 1,
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0xc0 }, 1 } },
        .n_mem = 1
    },
    {
        .name = "asla_40",
        .disas = "lda #$40; asla",
        .code = (const uint8_t[]){ 0x86, 0x40, 0x48 },
        .code_len = 3,
        .regs = (const char *const[]){ "A=80", "CC=5a", NULL }
    },
    {
        .name = "asla_80",
        .disas = "lda #$80; asla",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x48 },
        .code_len = 3,
        .regs = (const char *const[]){ "A=00", "CC=57", NULL }
    },
    {
        .name = "asl_indexed",
        .disas = "ldd #$2000; tfr d,x; asl ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x68, 0x84 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2000", "CC=5a", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x5a } },
        .n_load = 1,
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0xb4 }, 1 } },
        .n_mem = 1
    },
    {
        .name = "rola_carry_in",
        .disas = "lda #$ff; adda #$01; lda #$00; rola",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x8b, 0x01, 0x86, 0x00, 0x49 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=01", "CC=70", NULL }
    },
    {
        .name = "rola_80",
        .disas = "lda #$80; rola",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x49 },
        .code_len = 3,
        .regs = (const char *const[]){ "A=00", "CC=57", NULL }
    },
    {
        .name = "rol_indexed",
        .disas = "ldd #$2000; tfr d,x; rol ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x69, 0x84 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2000", "CC=5a", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x5a } },
        .n_load = 1,
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0xb4 }, 1 } },
        .n_mem = 1
    },
    {
        .name = "rora_carry_in",
        .disas = "lda #$ff; adda #$01; lda #$00; rora",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x8b, 0x01, 0x86, 0x00, 0x46 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=80", "CC=78", NULL }
    },
    {
        .name = "rora_one",
        .disas = "lda #$01; rora",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x46 },
        .code_len = 3,
        .regs = (const char *const[]){ "A=00", "CC=55", NULL }
    },
    {
        .name = "ror_indexed",
        .disas = "ldd #$2000; tfr d,x; ror ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x66, 0x84 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2000", "CC=50", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x5a } },
        .n_load = 1,
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x2d }, 1 } },
        .n_mem = 1
    },
    {
        .name = "deca_one",
        .disas = "lda #$01; deca",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x4a },
        .code_len = 3,
        .regs = (const char *const[]){ "A=00", "CC=54", NULL }
    },
    {
        .name = "deca_80",
        .disas = "lda #$80; deca",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x4a },
        .code_len = 3,
        .regs = (const char *const[]){ "A=7f", "CC=52", NULL }
    },
    {
        .name = "deca_preserves_c",
        .disas = "lda #$ff; adda #$01; deca",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x8b, 0x01, 0x4a },
        .code_len = 5,
        .regs = (const char *const[]){ "A=ff", "CC=79", NULL }
    },
    {
        .name = "dec_indexed",
        .disas = "ldd #$2000; tfr d,x; dec ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x6a, 0x84 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2000", "CC=50", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x5a } },
        .n_load = 1,
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x59 }, 1 } },
        .n_mem = 1
    },
    {
        .name = "inca_7f",
        .disas = "lda #$7f; inca",
        .code = (const uint8_t[]){ 0x86, 0x7f, 0x4c },
        .code_len = 3,
        .regs = (const char *const[]){ "A=80", "CC=5a", NULL }
    },
    {
        .name = "inca_ff",
        .disas = "lda #$ff; inca",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x4c },
        .code_len = 3,
        .regs = (const char *const[]){ "A=00", "CC=54", NULL }
    },
    {
        .name = "inca_preserves_c",
        .disas = "lda #$ff; adda #$01; inca",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x8b, 0x01, 0x4c },
        .code_len = 5,
        .regs = (const char *const[]){ "A=01", "CC=71", NULL }
    },
    {
        .name = "inc_indexed",
        .disas = "ldd #$2000; tfr d,x; inc ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x6c, 0x84 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2000", "CC=50", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x5a } },
        .n_load = 1,
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x5b }, 1 } },
        .n_mem = 1
    },
    {
        .name = "inc_idx_postinc",
        .disas = "ldd #$2000; tfr d,x; inc ,x++",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x6c, 0x81 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2002", "CC=50", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x05 } },
        .n_load = 1,
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x06 }, 1 } },
        .n_mem = 1
    },
    {
        .name = "clra",
        .disas = "lda #$5a; clra",
        .code = (const uint8_t[]){ 0x86, 0x5a, 0x4f },
        .code_len = 3,
        .regs = (const char *const[]){ "A=00", "CC=54", NULL }
    },
    {
        .name = "clra_clears_c",
        .disas = "lda #$ff; adda #$01; clra",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x8b, 0x01, 0x4f },
        .code_len = 5,
        .regs = (const char *const[]){ "A=00", "CC=74", NULL }
    },
    {
        .name = "clr_indexed",
        .disas = "ldd #$2000; tfr d,x; clr ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x6f, 0x84 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2000", "CC=54", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x5a } },
        .n_load = 1,
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x00 }, 1 } },
        .n_mem = 1
    },
    {
        .name = "tsta_zero",
        .disas = "lda #$00; tsta",
        .code = (const uint8_t[]){ 0x86, 0x00, 0x4d },
        .code_len = 3,
        .regs = (const char *const[]){ "A=00", "CC=54", NULL }
    },
    {
        .name = "tsta_negative",
        .disas = "lda #$80; tsta",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x4d },
        .code_len = 3,
        .regs = (const char *const[]){ "A=80", "CC=58", NULL }
    },
    {
        .name = "tsta_clears_v",
        .disas = "lda #$7f; adda #$01; tsta",
        .code = (const uint8_t[]){ 0x86, 0x7f, 0x8b, 0x01, 0x4d },
        .code_len = 5,
        .regs = (const char *const[]){ "A=80", "CC=78", NULL }
    },
    {
        .name = "tsta_preserves_c",
        .disas = "lda #$ff; adda #$01; lda #$01; tsta",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x8b, 0x01, 0x86, 0x01, 0x4d },
        .code_len = 7,
        .regs = (const char *const[]){ "A=01", "CC=71", NULL }
    },
    {
        .name = "tstb",
        .disas = "ldb #$80; tstb",
        .code = (const uint8_t[]){ 0xc6, 0x80, 0x5d },
        .code_len = 3,
        .regs = (const char *const[]){ "B=80", "CC=58", NULL }
    },
    {
        .name = "tst_indexed",
        .disas = "ldd #$2000; tfr d,x; tst ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x6d, 0x84 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2000", "CC=50", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x5a } },
        .n_load = 1,
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x5a }, 1 } },
        .n_mem = 1
    },
    {
        .name = "pshs_ab",
        .disas = "lds #$4000; lda #$12; ldb #$34; pshs #$06",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x40, 0x00, 0x86, 0x12, 0xc6, 0x34, 0x34, 0x06 },
        .code_len = 10,
        .regs = (const char *const[]){ "A=12", "B=34", "S=3ffe", "CC=50", NULL },
        .mem = (const IsaMem[]){ { 0x3ffe, (const uint8_t[]){ 0x12, 0x34 }, 2 } },
        .n_mem = 1
    },
    {
        .name = "puls_ab",
        .disas = "lds #$4000; lda #$12; ldb #$34; pshs #$06; lda #$00; ldb #$00; puls #$06",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x40, 0x00, 0x86, 0x12, 0xc6, 0x34, 0x34, 0x06, 0x86, 0x00, 0xc6, 0x00, 0x35, 0x06 },
        .code_len = 16,
        .regs = (const char *const[]){ "A=12", "B=34", "S=4000", NULL }
    },
    {
        .name = "pshs_pc",
        .disas = "lds #$4000; pshs #$80",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x40, 0x00, 0x34, 0x80 },
        .code_len = 6,
        .regs = (const char *const[]){ "S=3ffe", NULL },
        .mem = (const IsaMem[]){ { 0x3ffe, (const uint8_t[]){ 0x80, 0x06 }, 2 } },
        .n_mem = 1
    },
    {
        .name = "pshs_all",
        .disas = "lds #$4000; pshs #$ff",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x40, 0x00, 0x34, 0xff },
        .code_len = 6,
        .regs = (const char *const[]){ "S=3ff4", NULL },
        .mem = (const IsaMem[]){ { 0x3ff4, (const uint8_t[]){ 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x06 }, 12 } },
        .n_mem = 1
    },
    {
        .name = "pshs_empty",
        .disas = "lds #$4000; pshs #$00",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x40, 0x00, 0x34, 0x00 },
        .code_len = 6,
        .regs = (const char *const[]){ "S=4000", NULL }
    },
    {
        .name = "puls_pc",
        .disas = "lds #$2000; puls #$80",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x20, 0x00, 0x35, 0x80 },
        .code_len = 6,
        .regs = (const char *const[]){ "S=2002", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x80 }, { 0x2001, 0x06 } },
        .n_load = 2
    },
    {
        .name = "pshs_wrap_byte",
        .disas = "lds #$0001; lda #$5a; pshs #$02",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x00, 0x01, 0x86, 0x5a, 0x34, 0x02 },
        .code_len = 8,
        .regs = (const char *const[]){ "A=5a", "S=0000", NULL },
        .mem = (const IsaMem[]){ { 0x0000, (const uint8_t[]){ 0x5a }, 1 } },
        .n_mem = 1
    },
    {
        .name = "puls_wrap_byte",
        .disas = "lds #$0000; puls #$02",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x00, 0x00, 0x35, 0x02 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=5a", "S=0001", NULL },
        .load = (const IsaLoad[]){ { 0x0000, 0x5a } },
        .n_load = 1
    },
    {
        .name = "pshs_wrap_word",
        .disas = "lds #$0001; ldx #$1234; pshs #$10",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x00, 0x01, 0x8e, 0x12, 0x34, 0x34, 0x10 },
        .code_len = 9,
        .regs = (const char *const[]){ "X=1234", "S=ffff", NULL },
        .mem = (const IsaMem[]){ { 0x0000, (const uint8_t[]){ 0x34 }, 1 } },
        .n_mem = 1
    },
    {
        .name = "pshu_a",
        .disas = "ldu #$4000; lda #$5a; pshu #$02",
        .code = (const uint8_t[]){ 0xce, 0x40, 0x00, 0x86, 0x5a, 0x36, 0x02 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=5a", "U=3fff", NULL },
        .mem = (const IsaMem[]){ { 0x3fff, (const uint8_t[]){ 0x5a }, 1 } },
        .n_mem = 1
    },
    {
        .name = "pulu_a",
        .disas = "ldu #$2000; pulu #$02",
        .code = (const uint8_t[]){ 0xce, 0x20, 0x00, 0x37, 0x02 },
        .code_len = 5,
        .regs = (const char *const[]){ "A=5a", "U=2001", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x5a } },
        .n_load = 1
    },
    {
        .name = "pshu_s",
        .disas = "lds #$1234; ldu #$4000; pshu #$40",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x12, 0x34, 0xce, 0x40, 0x00, 0x36, 0x40 },
        .code_len = 9,
        .regs = (const char *const[]){ "S=1234", "U=3ffe", NULL },
        .mem = (const IsaMem[]){ { 0x3ffe, (const uint8_t[]){ 0x12, 0x34 }, 2 } },
        .n_mem = 1
    },
    {
        .name = "bsr_rts",
        .disas = "lds #$4000; lda #$00; bsr $800a; bra $800c; inca; rts",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x40, 0x00, 0x86, 0x00, 0x8d, 0x02, 0x20, 0x02, 0x4c, 0x39 },
        .code_len = 12,
        .regs = (const char *const[]){ "A=01", "S=4000", NULL }
    },
    {
        .name = "lbsr_rts",
        .disas = "lds #$4000; lda #$00; lbsr $2000",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x40, 0x00, 0x86, 0x00, 0x17, 0x9f, 0xf7 },
        .code_len = 9,
        .regs = (const char *const[]){ "A=01", "S=4000", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x4c }, { 0x2001, 0x39 } },
        .n_load = 2
    },
    {
        .name = "jsr_extended",
        .disas = "lds #$4000; lda #$00; jsr $2000",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x40, 0x00, 0x86, 0x00, 0xbd, 0x20, 0x00 },
        .code_len = 9,
        .regs = (const char *const[]){ "A=01", "S=4000", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x4c }, { 0x2001, 0x39 } },
        .n_load = 2
    },
    {
        .name = "jsr_indexed",
        .disas = "lds #$4000; ldd #$2000; tfr d,x; lda #$00; jsr ,x",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x40, 0x00, 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x86, 0x00, 0xad, 0x84 },
        .code_len = 13,
        .regs = (const char *const[]){ "A=01", "X=2000", "S=4000", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x4c }, { 0x2001, 0x39 } },
        .n_load = 2
    },
    {
        .name = "jsr_idx_postinc",
        .disas = "lds #$4000; ldd #$2000; tfr d,x; lda #$00; jsr ,x++",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x40, 0x00, 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x86, 0x00, 0xad, 0x81 },
        .code_len = 13,
        .regs = (const char *const[]){ "A=01", "X=2002", "S=4000", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x4c }, { 0x2001, 0x39 } },
        .n_load = 2
    },
    {
        .name = "mul_zero",
        .disas = "lda #$00; ldb #$05; mul",
        .code = (const uint8_t[]){ 0x86, 0x00, 0xc6, 0x05, 0x3d },
        .code_len = 5,
        .regs = (const char *const[]){ "D=0000", "CC=54", NULL }
    },
    {
        .name = "mul_simple",
        .disas = "lda #$03; ldb #$05; mul",
        .code = (const uint8_t[]){ 0x86, 0x03, 0xc6, 0x05, 0x3d },
        .code_len = 5,
        .regs = (const char *const[]){ "D=000f", "A=00", "B=0f", "CC=50", NULL }
    },
    {
        .name = "mul_carry",
        .disas = "lda #$02; ldb #$40; mul",
        .code = (const uint8_t[]){ 0x86, 0x02, 0xc6, 0x40, 0x3d },
        .code_len = 5,
        .regs = (const char *const[]){ "D=0080", "CC=51", NULL }
    },
    {
        .name = "mul_max",
        .disas = "lda #$ff; ldb #$ff; mul",
        .code = (const uint8_t[]){ 0x86, 0xff, 0xc6, 0xff, 0x3d },
        .code_len = 5,
        .regs = (const char *const[]){ "D=fe01", "CC=58", NULL }
    },
    {
        .name = "mul_preserves_n",
        .disas = "lda #$02; ldb #$03; orcc #$08; mul",
        .code = (const uint8_t[]){ 0x86, 0x02, 0xc6, 0x03, 0x1a, 0x08, 0x3d },
        .code_len = 7,
        .regs = (const char *const[]){ "D=0006", "CC=58", NULL }
    },
    {
        .name = "orcc_c",
        .disas = "orcc #$01",
        .code = (const uint8_t[]){ 0x1a, 0x01 },
        .code_len = 2,
        .regs = (const char *const[]){ "CC=51", NULL }
    },
    {
        .name = "andcc_clear_c",
        .disas = "lda #$ff; adda #$01; andcc #$fe",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x8b, 0x01, 0x1c, 0xfe },
        .code_len = 6,
        .regs = (const char *const[]){ "A=00", "CC=74", NULL }
    },
    {
        .name = "andcc_all",
        .disas = "andcc #$00",
        .code = (const uint8_t[]){ 0x1c, 0x00 },
        .code_len = 2,
        .regs = (const char *const[]){ "CC=00", NULL }
    },
    {
        .name = "andcc_keep_masks",
        .disas = "orcc #$0f; andcc #$50",
        .code = (const uint8_t[]){ 0x1a, 0x0f, 0x1c, 0x50 },
        .code_len = 4,
        .regs = (const char *const[]){ "CC=50", NULL }
    },
    {
        .name = "bra",
        .disas = "lda #$01; bra $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x20, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=01", NULL }
    },
    {
        .name = "brn",
        .disas = "lda #$01; brn $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x21, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "bhi_taken",
        .disas = "lda #$01; bhi $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x22, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=01", NULL }
    },
    {
        .name = "bhi_not_taken",
        .disas = "lda #$00; bhi $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x00, 0x22, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "bls_taken",
        .disas = "lda #$00; bls $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x00, 0x23, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=00", NULL }
    },
    {
        .name = "bls_not_taken",
        .disas = "lda #$01; bls $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x23, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "bcc_taken",
        .disas = "lda #$01; bcc $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x24, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=01", NULL }
    },
    {
        .name = "bcc_not_taken",
        .disas = "lda #$ff; adda #$01; bcc $8008; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x8b, 0x01, 0x24, 0x02, 0x86, 0x99 },
        .code_len = 8,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "bcs_taken",
        .disas = "lda #$ff; adda #$01; bcs $8008; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0xff, 0x8b, 0x01, 0x25, 0x02, 0x86, 0x99 },
        .code_len = 8,
        .regs = (const char *const[]){ "A=00", NULL }
    },
    {
        .name = "bcs_not_taken",
        .disas = "lda #$01; bcs $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x25, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "bne_taken",
        .disas = "lda #$01; bne $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x26, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=01", NULL }
    },
    {
        .name = "bne_not_taken",
        .disas = "lda #$00; bne $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x00, 0x26, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "beq_taken",
        .disas = "lda #$00; beq $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x00, 0x27, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=00", NULL }
    },
    {
        .name = "beq_not_taken",
        .disas = "lda #$01; beq $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x27, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "bvc_taken",
        .disas = "lda #$01; bvc $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x28, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=01", NULL }
    },
    {
        .name = "bvc_not_taken",
        .disas = "lda #$7f; adda #$01; bvc $8008; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x7f, 0x8b, 0x01, 0x28, 0x02, 0x86, 0x99 },
        .code_len = 8,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "bvs_taken",
        .disas = "lda #$7f; adda #$01; bvs $8008; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x7f, 0x8b, 0x01, 0x29, 0x02, 0x86, 0x99 },
        .code_len = 8,
        .regs = (const char *const[]){ "A=80", NULL }
    },
    {
        .name = "bvs_not_taken",
        .disas = "lda #$01; bvs $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x29, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "bpl_taken",
        .disas = "lda #$01; bpl $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x2a, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=01", NULL }
    },
    {
        .name = "bpl_not_taken",
        .disas = "lda #$80; bpl $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x2a, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "bmi_taken",
        .disas = "lda #$80; bmi $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x2b, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=80", NULL }
    },
    {
        .name = "bmi_not_taken",
        .disas = "lda #$01; bmi $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x2b, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "bge_taken",
        .disas = "lda #$01; bge $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x2c, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=01", NULL }
    },
    {
        .name = "bge_not_taken",
        .disas = "lda #$80; bge $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x2c, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "bge_taken_n_and_v",
        .disas = "lda #$7f; adda #$01; bge $8008; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x7f, 0x8b, 0x01, 0x2c, 0x02, 0x86, 0x99 },
        .code_len = 8,
        .regs = (const char *const[]){ "A=80", NULL }
    },
    {
        .name = "blt_taken",
        .disas = "lda #$80; blt $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x2d, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=80", NULL }
    },
    {
        .name = "blt_not_taken",
        .disas = "lda #$01; blt $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x2d, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "blt_not_taken_n_and_v",
        .disas = "lda #$7f; adda #$01; blt $8008; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x7f, 0x8b, 0x01, 0x2d, 0x02, 0x86, 0x99 },
        .code_len = 8,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "bgt_taken",
        .disas = "lda #$01; bgt $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x2e, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=01", NULL }
    },
    {
        .name = "bgt_not_taken",
        .disas = "lda #$00; bgt $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x00, 0x2e, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "bgt_taken_n_and_v",
        .disas = "lda #$7f; adda #$01; bgt $8008; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x7f, 0x8b, 0x01, 0x2e, 0x02, 0x86, 0x99 },
        .code_len = 8,
        .regs = (const char *const[]){ "A=80", NULL }
    },
    {
        .name = "ble_taken",
        .disas = "lda #$00; ble $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x00, 0x2f, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=00", NULL }
    },
    {
        .name = "ble_not_taken",
        .disas = "lda #$01; ble $8006; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x2f, 0x02, 0x86, 0x99 },
        .code_len = 6,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "bne_loop",
        .disas = "ldb #$03; decb; bne $8002",
        .code = (const uint8_t[]){ 0xc6, 0x03, 0x5a, 0x26, 0xfd },
        .code_len = 5,
        .regs = (const char *const[]){ "B=00", "CC=54", NULL }
    },
    {
        .name = "bra_max_positive",
        .disas = "lda #$5a; bra $8083; inca *127",
        .code = (const uint8_t[]){ 0x86, 0x5a, 0x20, 0x7f, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c, 0x4c },
        .code_len = 131,
        .regs = (const char *const[]){ "A=5a", NULL }
    },
    {
        .name = "bra_max_negative",
        .disas = "ldb #$00; incb; cmpb #$02; beq $8082; nop *121; bra $8002",
        .code = (const uint8_t[]){ 0xc6, 0x00, 0x5c, 0xc1, 0x02, 0x27, 0x7b, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x20, 0x80 },
        .code_len = 130,
        .regs = (const char *const[]){ "B=02", NULL }
    },
    {
        .name = "nop",
        .disas = "lda #$5a; orcc #$01; nop *3",
        .code = (const uint8_t[]){ 0x86, 0x5a, 0x1a, 0x01, 0x12, 0x12, 0x12 },
        .code_len = 7,
        .regs = (const char *const[]){ "A=5a", "CC=51", NULL }
    },
    {
        .name = "lbra",
        .disas = "lbra $8005; lda #$99",
        .code = (const uint8_t[]){ 0x16, 0x00, 0x02, 0x86, 0x99 },
        .code_len = 5,
        .regs = (const char *const[]){ "A=00", NULL }
    },
    {
        .name = "lbra_word_disp",
        .disas = "lbra $8083; lda #$99; nop *126",
        .code = (const uint8_t[]){ 0x16, 0x00, 0x80, 0x86, 0x99, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12 },
        .code_len = 131,
        .regs = (const char *const[]){ "A=00", NULL }
    },
    {
        .name = "lbrn",
        .disas = "lbrn $8006; lda #$5a",
        .code = (const uint8_t[]){ 0x10, 0x21, 0x00, 0x02, 0x86, 0x5a },
        .code_len = 6,
        .regs = (const char *const[]){ "A=5a", NULL }
    },
    {
        .name = "lbne_taken",
        .disas = "lda #$01; lbne $8008; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x10, 0x26, 0x00, 0x02, 0x86, 0x99 },
        .code_len = 8,
        .regs = (const char *const[]){ "A=01", NULL }
    },
    {
        .name = "lbeq_not_taken",
        .disas = "lda #$01; lbeq $8008; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x01, 0x10, 0x27, 0x00, 0x02, 0x86, 0x99 },
        .code_len = 8,
        .regs = (const char *const[]){ "A=99", NULL }
    },
    {
        .name = "jmp_extended",
        .disas = "jmp $8005; lda #$99",
        .code = (const uint8_t[]){ 0x7e, 0x80, 0x05, 0x86, 0x99 },
        .code_len = 5,
        .regs = (const char *const[]){ "A=00", NULL }
    },
    {
        .name = "jmp_direct",
        .disas = "lda #$80; tfr a,dp; jmp <$08; lda #$99",
        .code = (const uint8_t[]){ 0x86, 0x80, 0x1f, 0x8b, 0x0e, 0x08, 0x86, 0x99 },
        .code_len = 8,
        .regs = (const char *const[]){ "A=80", "DP=80", NULL }
    },
    {
        .name = "jmp_indexed",
        .disas = "ldd #$8009; tfr d,x; jmp ,x; lda #$99",
        .code = (const uint8_t[]){ 0xcc, 0x80, 0x09, 0x1f, 0x01, 0x6e, 0x84, 0x86, 0x99 },
        .code_len = 9,
        .regs = (const char *const[]){ "A=80", "X=8009", NULL }
    },
    {
        .name = "jmp_idx_postinc",
        .disas = "ldd #$8009; tfr d,x; jmp ,x++; lda #$99",
        .code = (const uint8_t[]){ 0xcc, 0x80, 0x09, 0x1f, 0x01, 0x6e, 0x81, 0x86, 0x99 },
        .code_len = 9,
        .regs = (const char *const[]){ "A=80", "X=800b", NULL }
    },
    {
        .name = "abx_simple",
        .disas = "ldd #$2000; tfr d,x; ldb #$05; abx",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xc6, 0x05, 0x3a },
        .code_len = 8,
        .regs = (const char *const[]){ "X=2005", "B=05", "CC=50", NULL }
    },
    {
        .name = "abx_unsigned",
        .disas = "ldd #$2000; tfr d,x; ldb #$80; abx",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xc6, 0x80, 0x3a },
        .code_len = 8,
        .regs = (const char *const[]){ "X=2080", "B=80", "CC=58", NULL }
    },
    {
        .name = "abx_wrap",
        .disas = "ldd #$ffff; tfr d,x; ldb #$01; abx",
        .code = (const uint8_t[]){ 0xcc, 0xff, 0xff, 0x1f, 0x01, 0xc6, 0x01, 0x3a },
        .code_len = 8,
        .regs = (const char *const[]){ "X=0000", "B=01", "CC=50", NULL }
    },
    {
        .name = "abx_preserves_z",
        .disas = "ldd #$ffff; tfr d,x; ldb #$01; lda #$00; abx",
        .code = (const uint8_t[]){ 0xcc, 0xff, 0xff, 0x1f, 0x01, 0xc6, 0x01, 0x86, 0x00, 0x3a },
        .code_len = 10,
        .regs = (const char *const[]){ "X=0000", "A=00", "B=01", "CC=54", NULL }
    },
    {
        .name = "leax_zero_x",
        .disas = "ldd #$2000; tfr d,x; lda #$01; leax ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x86, 0x01, 0x30, 0x84 },
        .code_len = 9,
        .regs = (const char *const[]){ "X=2000", "A=01", "CC=50", NULL }
    },
    {
        .name = "leax_offset5",
        .disas = "ldd #$2000; tfr d,x; leax 15,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x30, 0x0f },
        .code_len = 7,
        .regs = (const char *const[]){ "X=200f", "CC=50", NULL }
    },
    {
        .name = "leax_sets_z",
        .disas = "ldd #$0001; tfr d,x; lda #$01; leax -1,x",
        .code = (const uint8_t[]){ 0xcc, 0x00, 0x01, 0x1f, 0x01, 0x86, 0x01, 0x30, 0x1f },
        .code_len = 9,
        .regs = (const char *const[]){ "X=0000", "A=01", "CC=54", NULL }
    },
    {
        .name = "leax_preserves_n",
        .disas = "ldd #$2000; tfr d,x; lda #$00; orcc #$08; leax ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x86, 0x00, 0x1a, 0x08, 0x30, 0x84 },
        .code_len = 11,
        .regs = (const char *const[]){ "X=2000", "A=00", "CC=58", NULL }
    },
    {
        .name = "leax_wrap",
        .disas = "ldd #$ffff; tfr d,x; lda #$01; leax 1,x",
        .code = (const uint8_t[]){ 0xcc, 0xff, 0xff, 0x1f, 0x01, 0x86, 0x01, 0x30, 0x01 },
        .code_len = 9,
        .regs = (const char *const[]){ "X=0000", "A=01", "CC=54", NULL }
    },
    {
        .name = "leax_postinc_self",
        .disas = "ldd #$2000; tfr d,x; leax ,x++",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x30, 0x81 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2000", "CC=50", NULL }
    },
    {
        .name = "leax_predec_self",
        .disas = "ldd #$2002; tfr d,x; leax ,--x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x02, 0x1f, 0x01, 0x30, 0x83 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2000", "CC=50", NULL }
    },
    {
        .name = "leax_from_y_postinc",
        .disas = "ldd #$2000; tfr d,y; leax ,y++",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x02, 0x30, 0xa1 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=2000", "Y=2002", "CC=50", NULL }
    },
    {
        .name = "leax_offset8",
        .disas = "ldd #$2000; tfr d,x; leax -128,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x30, 0x88, 0x80 },
        .code_len = 8,
        .regs = (const char *const[]){ "X=1f80", "CC=50", NULL }
    },
    {
        .name = "leax_offset16_wrap",
        .disas = "ldd #$fff0; tfr d,x; leax 32,x",
        .code = (const uint8_t[]){ 0xcc, 0xff, 0xf0, 0x1f, 0x01, 0x30, 0x89, 0x00, 0x20 },
        .code_len = 9,
        .regs = (const char *const[]){ "X=0010", "CC=58", NULL }
    },
    {
        .name = "leax_a_offset",
        .disas = "ldd #$2000; tfr d,x; lda #$ff; leax a,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x86, 0xff, 0x30, 0x86 },
        .code_len = 9,
        .regs = (const char *const[]){ "X=1fff", "A=ff", "CC=58", NULL }
    },
    {
        .name = "leax_b_signed",
        .disas = "ldd #$2000; tfr d,x; ldb #$80; leax b,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xc6, 0x80, 0x30, 0x85 },
        .code_len = 9,
        .regs = (const char *const[]){ "X=1f80", "B=80", "CC=58", NULL }
    },
    {
        .name = "leax_d_offset",
        .disas = "ldd #$2000; tfr d,x; ldd #$f000; leax d,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0xcc, 0xf0, 0x00, 0x30, 0x8b },
        .code_len = 10,
        .regs = (const char *const[]){ "X=1000", "D=f000", "CC=58", NULL }
    },
    {
        .name = "leax_pcr8",
        .disas = "leax 0,pcr",
        .code = (const uint8_t[]){ 0x30, 0x8c, 0x00 },
        .code_len = 3,
        .regs = (const char *const[]){ "X=8003", "CC=50", NULL }
    },
    {
        .name = "leax_pcr16",
        .disas = "leax 16,pcr",
        .code = (const uint8_t[]){ 0x30, 0x8d, 0x00, 0x10 },
        .code_len = 4,
        .regs = (const char *const[]){ "X=8014", "CC=50", NULL }
    },
    {
        .name = "leax_indirect",
        .disas = "ldd #$2000; tfr d,x; leax [,x]",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x30, 0x94 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=1234", "CC=50", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x12 }, { 0x2001, 0x34 } },
        .n_load = 2
    },
    {
        .name = "leax_indirect_wrap",
        .disas = "ldd #$ffff; tfr d,x; leax [,x]",
        .code = (const uint8_t[]){ 0xcc, 0xff, 0xff, 0x1f, 0x01, 0x30, 0x94 },
        .code_len = 7,
        .regs = (const char *const[]){ "X=0034", "CC=58", NULL },
        .load = (const IsaLoad[]){ { 0x0000, 0x34 } },
        .n_load = 1
    },
    {
        .name = "leax_extended_indirect",
        .disas = "leax [$2000]",
        .code = (const uint8_t[]){ 0x30, 0x9f, 0x20, 0x00 },
        .code_len = 4,
        .regs = (const char *const[]){ "X=1234", "CC=50", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x12 }, { 0x2001, 0x34 } },
        .n_load = 2
    },
    {
        .name = "leay_from_x",
        .disas = "ldd #$2000; tfr d,x; lda #$01; leay ,x",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x01, 0x86, 0x01, 0x31, 0x84 },
        .code_len = 9,
        .regs = (const char *const[]){ "Y=2000", "X=2000", "A=01", "CC=50", NULL }
    },
    {
        .name = "leay_sets_z",
        .disas = "ldd #$0001; tfr d,y; lda #$01; leay -1,y",
        .code = (const uint8_t[]){ 0xcc, 0x00, 0x01, 0x1f, 0x02, 0x86, 0x01, 0x31, 0x3f },
        .code_len = 9,
        .regs = (const char *const[]){ "Y=0000", "A=01", "CC=54", NULL }
    },
    {
        .name = "leas_zero_result_keeps_z_clear",
        .disas = "ldd #$0001; tfr d,s; lda #$01; leas -1,s",
        .code = (const uint8_t[]){ 0xcc, 0x00, 0x01, 0x1f, 0x04, 0x86, 0x01, 0x32, 0x7f },
        .code_len = 9,
        .regs = (const char *const[]){ "S=0000", "A=01", "CC=50", NULL }
    },
    {
        .name = "leas_alloc",
        .disas = "ldd #$2000; tfr d,s; lda #$00; leas -2,s",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x04, 0x86, 0x00, 0x32, 0x7e },
        .code_len = 9,
        .regs = (const char *const[]){ "S=1ffe", "A=00", "CC=54", NULL }
    },
    {
        .name = "leas_wrap",
        .disas = "ldd #$0001; tfr d,s; lda #$00; leas -2,s",
        .code = (const uint8_t[]){ 0xcc, 0x00, 0x01, 0x1f, 0x04, 0x86, 0x00, 0x32, 0x7e },
        .code_len = 9,
        .regs = (const char *const[]){ "S=ffff", "A=00", "CC=54", NULL }
    },
    {
        .name = "leau_from_s",
        .disas = "ldd #$2000; tfr d,s; lda #$00; leau ,s",
        .code = (const uint8_t[]){ 0xcc, 0x20, 0x00, 0x1f, 0x04, 0x86, 0x00, 0x33, 0xe4 },
        .code_len = 9,
        .regs = (const char *const[]){ "U=2000", "S=2000", "A=00", "CC=54", NULL }
    },
    {
        .name = "swi_stacks",
        .disas = "lds #$4000; lda #$12; tfr a,dp; ldb #$34; ldx #$1111; ldy #$2222; ldu #$3333; swi",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x40, 0x00, 0x86, 0x12, 0x1f, 0x8b, 0xc6, 0x34, 0x8e, 0x11, 0x11, 0x10, 0x8e, 0x22, 0x22, 0xce, 0x33, 0x33, 0x3f },
        .code_len = 21,
        .regs = (const char *const[]){ "A=12", "B=34", "DP=12", "X=1111", "Y=2222", "U=3333", "S=3ff4", "CC=d0", NULL },
        .operand = (const uint8_t[]){ 0x86, 0x99, 0x20, 0xfe },
        .operand_len = 4,
        .mem = (const IsaMem[]){ { 0x3ff4, (const uint8_t[]){ 0xd0, 0x12, 0x34, 0x12, 0x11, 0x11, 0x22, 0x22, 0x33, 0x33, 0x80, 0x15 }, 12 } },
        .n_mem = 1,
        .vec = (const IsaVec[]){ { 0xfffa, 0x8015 }, { 0xfff4, 0x80f0 }, { 0xfff2, 0x80f0 } },
        .n_vec = 3
    },
    {
        .name = "swi_masks_if",
        .disas = "lds #$4000; andcc #$af; swi",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x40, 0x00, 0x1c, 0xaf, 0x3f },
        .code_len = 7,
        .regs = (const char *const[]){ "S=3ff4", "CC=d0", NULL },
        .operand = (const uint8_t[]){ 0x86, 0x99, 0x20, 0xfe },
        .operand_len = 4,
        .mem = (const IsaMem[]){ { 0x3ff4, (const uint8_t[]){ 0x80 }, 1 } },
        .n_mem = 1,
        .vec = (const IsaVec[]){ { 0xfffa, 0x8007 }, { 0xfff4, 0x80f0 }, { 0xfff2, 0x80f0 } },
        .n_vec = 3
    },
    {
        .name = "swi2_no_mask",
        .disas = "lds #$4000; andcc #$af; swi2",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x40, 0x00, 0x1c, 0xaf, 0x10, 0x3f },
        .code_len = 8,
        .regs = (const char *const[]){ "S=3ff4", "CC=80", NULL },
        .operand = (const uint8_t[]){ 0x86, 0x99, 0x20, 0xfe },
        .operand_len = 4,
        .mem = (const IsaMem[]){ { 0x3ff4, (const uint8_t[]){ 0x80 }, 1 }, { 0x3ffe, (const uint8_t[]){ 0x80, 0x08 }, 2 } },
        .n_mem = 2,
        .vec = (const IsaVec[]){ { 0xfffa, 0x80f0 }, { 0xfff4, 0x8008 }, { 0xfff2, 0x80f0 } },
        .n_vec = 3
    },
    {
        .name = "swi3_no_mask",
        .disas = "lds #$4000; andcc #$af; swi3",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x40, 0x00, 0x1c, 0xaf, 0x11, 0x3f },
        .code_len = 8,
        .regs = (const char *const[]){ "S=3ff4", "CC=80", NULL },
        .operand = (const uint8_t[]){ 0x86, 0x99, 0x20, 0xfe },
        .operand_len = 4,
        .mem = (const IsaMem[]){ { 0x3ff4, (const uint8_t[]){ 0x80 }, 1 }, { 0x3ffe, (const uint8_t[]){ 0x80, 0x08 }, 2 } },
        .n_mem = 2,
        .vec = (const IsaVec[]){ { 0xfffa, 0x80f0 }, { 0xfff4, 0x80f0 }, { 0xfff2, 0x8008 } },
        .n_vec = 3
    },
    {
        .name = "swi_rti",
        .disas = "lds #$4000; lda #$5a; ldx #$1111; swi",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x40, 0x00, 0x86, 0x5a, 0x8e, 0x11, 0x11, 0x3f },
        .code_len = 10,
        .regs = (const char *const[]){ "A=5a", "X=1111", "S=4000", "CC=d0", NULL },
        .operand = (const uint8_t[]){ 0x4c, 0xb7, 0x20, 0x00, 0x3b },
        .operand_len = 5,
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x5b }, 1 } },
        .n_mem = 1,
        .vec = (const IsaVec[]){ { 0xfffa, 0x80f0 } },
        .n_vec = 1
    },
    {
        .name = "swi2_rti",
        .disas = "lds #$4000; lda #$5a; swi2",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x40, 0x00, 0x86, 0x5a, 0x10, 0x3f },
        .code_len = 8,
        .regs = (const char *const[]){ "A=5a", "S=4000", "CC=d0", NULL },
        .operand = (const uint8_t[]){ 0x4c, 0xb7, 0x20, 0x00, 0x3b },
        .operand_len = 5,
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x5b }, 1 } },
        .n_mem = 1,
        .vec = (const IsaVec[]){ { 0xfff4, 0x80f0 } },
        .n_vec = 1
    },
    {
        .name = "rti_partial",
        .disas = "lds #$2000; lda #$99; rti",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x20, 0x00, 0x86, 0x99, 0x3b },
        .code_len = 7,
        .regs = (const char *const[]){ "A=99", "X=0000", "S=2003", "CC=50", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x50 }, { 0x2001, 0x80 }, { 0x2002, 0x07 } },
        .n_load = 3
    },
    {
        .name = "rti_entire",
        .disas = "lds #$2000; rti",
        .code = (const uint8_t[]){ 0x10, 0xce, 0x20, 0x00, 0x3b },
        .code_len = 5,
        .regs = (const char *const[]){ "A=12", "B=34", "DP=56", "X=1111", "Y=2222", "U=3333", "S=200c", "CC=80", NULL },
        .load = (const IsaLoad[]){ { 0x2000, 0x80 }, { 0x2001, 0x12 }, { 0x2002, 0x34 }, { 0x2003, 0x56 }, { 0x2004, 0x11 }, { 0x2005, 0x11 }, { 0x2006, 0x22 }, { 0x2007, 0x22 }, { 0x2008, 0x33 }, { 0x2009, 0x33 }, { 0x200a, 0x80 }, { 0x200b, 0x05 } },
        .n_load = 12
    },
    {
        .name = "rti_partial_wrap",
        .disas = "lds #$ffff; lda #$99; rti",
        .code = (const uint8_t[]){ 0x10, 0xce, 0xff, 0xff, 0x86, 0x99, 0x3b },
        .code_len = 7,
        .regs = (const char *const[]){ "A=99", "S=0002", "CC=00", NULL },
        .load = (const IsaLoad[]){ { 0x0000, 0x80 }, { 0x0001, 0x07 } },
        .n_load = 2
    },

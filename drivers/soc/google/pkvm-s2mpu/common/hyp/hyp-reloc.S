.data
.pushsection .hyp.reloc, "a"
.global __hyp_section_.hyp.text
.global __hyp_section_.hyp.rodata
.word 0
.reloc 0, R_AARCH64_PREL32, __hyp_section_.hyp.rodata + 0x50
.word 0
.reloc 4, R_AARCH64_PREL32, __hyp_section_.hyp.rodata + 0x98
.word 0
.reloc 8, R_AARCH64_PREL32, __hyp_section_.hyp.rodata + 0xa0
.word 0
.reloc 12, R_AARCH64_PREL32, __hyp_section_.hyp.rodata + 0xa8
.word 0
.reloc 16, R_AARCH64_PREL32, __hyp_section_.hyp.rodata + 0xb0
.word 0
.reloc 20, R_AARCH64_PREL32, __hyp_section_.hyp.rodata + 0xb8
.word 0
.reloc 24, R_AARCH64_PREL32, __hyp_section_.hyp.rodata + 0xc0
.word 0
.reloc 28, R_AARCH64_PREL32, __hyp_section_.hyp.rodata + 0xc8
.word 0
.reloc 32, R_AARCH64_PREL32, __hyp_section_.hyp.rodata + 0xd0
.word 0
.reloc 36, R_AARCH64_PREL32, __hyp_section_.hyp.rodata + 0xd8
.word 0
.reloc 40, R_AARCH64_PREL32, __hyp_section_.hyp.rodata + 0x108
.word 0
.reloc 44, R_AARCH64_PREL32, __hyp_section_.hyp.rodata + 0x110
.word 0
.reloc 48, R_AARCH64_PREL32, __hyp_section_.hyp.rodata + 0x118
.word 0
.reloc 52, R_AARCH64_PREL32, __hyp_section_.hyp.rodata + 0x120
.word 0
.reloc 56, R_AARCH64_PREL32, __hyp_section_.hyp.rodata + 0x128
.word 0
.reloc 60, R_AARCH64_PREL32, __hyp_section_.hyp.rodata + 0x130
.global __hyp_section_.hyp.data
.word 0
.reloc 64, R_AARCH64_PREL32, __hyp_section_.hyp.data + 0x0
.word 0
.reloc 68, R_AARCH64_PREL32, __hyp_section_.hyp.data + 0x20
.popsection

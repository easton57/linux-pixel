	.arch armv8-a
	.file	"hyp-constants.c"
// GNU C11 (GCC) version 15.1.1 20250425 (Red Hat Cross 15.1.1-1) (aarch64-linux-gnu)
//	compiled by GNU C version 15.1.1 20250425 (Red Hat 15.1.1-1), GMP version 6.3.0, MPFR version 4.2.2, MPC version 1.3.1, isl version none
// GGC heuristics: --param ggc-min-expand=100 --param ggc-min-heapsize=131072
// options passed: -mlittle-endian -mgeneral-regs-only -mabi=lp64 -mbranch-protection=pac-ret+leaf -mstack-protector-guard=sysreg -mstack-protector-guard-reg=sp_el0 -mstack-protector-guard-offset=1464 -O2 -std=gnu11 -fno-strict-aliasing -fno-common -fshort-wchar -fno-PIE -fno-asynchronous-unwind-tables -fno-unwind-tables -ffixed-x18 -fno-delete-null-pointer-checks -fno-allow-store-data-races -fstack-protector-strong -fno-omit-frame-pointer -fno-optimize-sibling-calls -ftrivial-auto-var-init=zero -fno-stack-clash-protection -fsanitize=shadow-call-stack -fno-strict-overflow -fstack-check=no -fconserve-stack -fsanitize=bounds -fsanitize-trap
	.text
	.section	.text.startup,"ax",@progbits
	.align	2
	.p2align 5,,15
	.global	main
	.type	main, %function
main:
	hint	25 // paciasp
// arch/arm64/kvm/hyp/hyp-constants.c:8: 	DEFINE(STRUCT_HYP_PAGE_SIZE,	sizeof(struct hyp_page));
#APP
// 8 "arch/arm64/kvm/hyp/hyp-constants.c" 1
	
.ascii "->STRUCT_HYP_PAGE_SIZE 4 sizeof(struct hyp_page)"	//
// 0 "" 2
// arch/arm64/kvm/hyp/hyp-constants.c:10: }
#NO_APP
	mov	w0, 0	//,
	hint	29 // autiasp
	ret	
	.size	main, .-main
	.ident	"GCC: (GNU) 15.1.1 20250425 (Red Hat Cross 15.1.1-1)"
	.section	.note.GNU-stack,"",@progbits
	.section	.note.gnu.property,"a"
	.align	3
	.word	4
	.word	16
	.word	5
	.string	"GNU"
	.word	3221225472
	.word	4
	.word	2
	.align	3

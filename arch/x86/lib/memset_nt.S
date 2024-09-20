#include <linux/linkage.h>
#include <asm/export.h>

/* Different version of memeset_orig which used a 
non tempral store to write the data in the Lloop_64 loop */
SYM_FUNC_START(memset_nt)
	movq %rdi,%r10

	/* expand byte value  */
	movzbl %sil,%ecx
	movabs $0x0101010101010101,%rax
	imulq  %rcx,%rax

	/* align dst */
	movl  %edi,%r9d
	andl  $7,%r9d
	jnz  .Lbad_alignment
.Lafter_bad_alignment:

	movq  %rdx,%rcx
	shrq  $6,%rcx
	jz	 .Lhandle_tail

	.p2align 4
.Lloop_64:
	decq  %rcx
	movnti  %rax,(%rdi)
	movnti  %rax,8(%rdi)
	movnti  %rax,16(%rdi)
	movnti  %rax,24(%rdi)
	movnti  %rax,32(%rdi)
	movnti  %rax,40(%rdi)
	movnti  %rax,48(%rdi)
	movnti  %rax,56(%rdi)
	leaq  64(%rdi),%rdi
	jnz    .Lloop_64
	sfence

	/* Handle tail in loops. The loops should be faster than hard
	   to predict jump tables. */
	.p2align 4
.Lhandle_tail:
	movl	%edx,%ecx
	andl    $63&(~7),%ecx
	jz 		.Lhandle_7
	shrl	$3,%ecx
	.p2align 4
.Lloop_8:
	decl   %ecx
	movnti  %rax,(%rdi)
	leaq  8(%rdi),%rdi
	jnz    .Lloop_8

.Lhandle_7:
	andl	$7,%edx
	jz      .Lende
	.p2align 4
.Lloop_1:
	decl    %edx
	movb 	%al,(%rdi)
	leaq	1(%rdi),%rdi
	jnz     .Lloop_1

.Lende:
	movq	%r10,%rax
	RET

.Lbad_alignment:
	cmpq $7,%rdx
	jbe	.Lhandle_7
	movnti %rax,(%rdi)	/* unaligned store */
	movq $8,%r8
	subq %r9,%r8
	addq %r8,%rdi
	subq %r8,%rdx
	jmp .Lafter_bad_alignment
.Lfinal:
SYM_FUNC_END(memset_nt)
EXPORT_SYMBOL(memset_nt)
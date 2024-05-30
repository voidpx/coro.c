.global atomic_cas
.global atomic_add
.global atomic_xaddl
.global atomic_casl
.text

# int atomic_cas(int *p, int val, int new)
atomic_casl:
	movq %rsi, %rax;
	lock cmpxchg %rdx, (%rdi);
	jz 1f;
	movl $0, %eax;
	jmp 2f;
1:
	movl $1, %eax;
2:
	retq;

atomic_cas:
	movl %esi, %eax;
	lock cmpxchg %edx, (%rdi);
	jz 1f;
	movl $0, %eax;
	jmp 2f;
1:
	movl $1, %eax;
2:
	retq;

# int atomic_xadd(int *p, int val)
atomic_xadd:
	movl %esi, %eax;
	lock xadd %eax, (%rdi);
	retq;

# long atomic_xadd(long *p, long val)
atomic_xaddl:
	movq %rsi, %rax;
	lock xadd %rax, (%rdi);
	retq;

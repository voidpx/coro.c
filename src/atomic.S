.global atomic_cas
.global atomic_casl
.global atomic_xadd
.global atomic_xaddl
.text

# int atomic_casl(long *p, long val, long new)
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
# int atomic_cas(int *p, int val, int new)
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

# long atomic_xaddl(long *p, long val)
atomic_xaddl:
	movq %rsi, %rax;
	lock xadd %rax, (%rdi);
	retq;

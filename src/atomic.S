.global atomic_cas
.global atomic_add
.text

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
atomic_add:
	movl %esi, %eax;
	lock xadd %edx, (%rdi);
	retq;

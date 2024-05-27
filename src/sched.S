.global __schedule
.global __schedule_end
.global _switch_to
.global coro_start
.global _task_cleanup

.text

# prepare for calling into c, make sure stack is properly aligned
__call_c:
	pushq %rax;
	pushq %rcx;
	movq %rsp, %rax;
	testq $0xf, %rax;
	jz 1f; # good stack
	andq $0xfffffffffffffff0, %rax;
	movq %rsp, %rcx;
	movq %rax, %rsp; # aligned sp
	pushq %rcx; # unaigned sp
	pushq $1; # flag indicating stack aligment
	jmp 2f;
1:
	pushq %rsp;
	pushq $0;
2:
	call *%r11; # function to call
	popq %rax; # don't care
	popq %rsp;
	popq %rcx;
	popq %rax;
	retq;

__sched_exit:
	pushq %r11;
	lea sched_exit(%rip), %r11;
	call __call_c;
	popq %r11;
	retq;

# main scheduler entry:
# called from the timer signal handler,in which case the stack is not necessarily aligned properly for calls
__schedule:
	movl $1, __scheduling(%rip); # first thing in scheduler

	# switch to scheduler sp, which is always properly aligned
	movq sched(%rip), %rax;
	# simulate call
	movq 8(%rax), %rsp;
    push (%rax); # sched pc, not really matter, it never comes back
	jmp _schedule; # never return


# _switch_to(task *)
_switch_to:

	#movq 8(%rdi), %rsp; # new stack
	movq %rdi, current(%rip); # set current
	cmpq sched(%rip), %rdi;
	jnz 1f;
	movq 8(%rdi), %rsp;
	jmp 4f; # runtime shutdown
1:

	movl 0x3c(%rdi), %eax; # state
	testl %eax, %eax;
	jnz 2f; # not new

	movq 8(%rdi), %rsp; # new stack
	pushq $0; # no return address, stack align
	lea task_entry(%rip), %rax;
	pushq %rax;

2:
	pushq %rdi;
	call __sched_exit;
	popq %rdi; # rdi is current task
	movl 0x3c(%rdi), %eax; # state
	testl %eax, %eax;
	jnz 3f; # not new

	movl $1, 0x3c(%rdi); # change to RUNNABLE
	jmp 4f;

3:

	movq 0x98(%rdi), %r8;
	movq 0xa0(%rdi), %r9;
	movq 0xa8(%rdi), %r10;
	movq 0xb0(%rdi), %r11;
	movq 0xb8(%rdi), %r12;
	movq 0xc0(%rdi), %r13;
	movq 0xc8(%rdi), %r14;
	movq 0xd0(%rdi), %r15;
	#movq 0xd8(%rdi), %rdi;
	movq 0xe0(%rdi), %rsi;
	movq 0xe8(%rdi), %rbp;
	movq 0xf0(%rdi), %rbx;
	movq 0xf8(%rdi), %rdx;
	#movq 0x100(%rdi),%rax;
	movq 0x108(%rdi),%rcx;
	#movq 0x118(%rdi),%rip;
	#movq 0x120(%rdi)  ,%efl;
	#movq 0x128(%rdi),%cs, %gs, %fs;
	#movq 0x130(%rdi),err;
	#movq 0x138(%rdi),trapno;
	#movq 0x140(%rdi),oldmask;
	#movq 0x148(%rdi),%cr2;

#	unsigned long r8, r9, r10, r11, r12, r13, r14, r15;
#	unsigned long rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp, rip, eflags;
#	unsigned short cs, gs, fs, __pad0;
#	unsigned long err, trapno, oldmask, cr2;
#	struct _fpstate *fpstate;

	fxrstor 0x150(%rdi);

	subq $8, %rsp;
	movq 0x120(%rdi), %rax;
	movq %rax, (%rsp);
	popfq;

	movq 0x110(%rdi),%rsp;
	movq %rdi, current(%rip); # set current

	movq 0x118(%rdi), %rax;
	movq %rax, __scratch_ip(%rip);
	movq 0x100(%rdi),%rax;

	# finally restore rdi
	movq 0xd8(%rdi), %rdi;

	movl $0, __scheduling(%rip); # last thing in scheduler
	jmp *__scratch_ip(%rip);

4:
	movl $0, __scheduling(%rip); # last thing in scheduler
	retq;
__schedule_end:

_task_cleanup:
	pushq %rbp;
	movq %rsp, %rbp;

	# switch to scheduler sp, which is always properly aligned
	movq sched(%rip), %rax;
	movq (%rax), %rsp;

    call task_cleanup;

	movq %rbp, %rsp;
	popq %rbp;
	retq;

# coroutne runtime entry
coro_start:
	pushq %rbp;
	movq %rsp, %rbp;
	pushq $0; # align
	lea Lcoro_exit(%rip), %rdx;
	pushq %rdx;
	movq %rsp, %rcx;
	call _coro_start;

Lcoro_exit:
	popq %rax; # align
	movq sched(%rip), %rdi;
	movq 0x18(%rdi), %rax; # return value

	call __sched_cleanup;
	popq %rbp;
	retq;

.data
__scratch_ip:
.quad 0


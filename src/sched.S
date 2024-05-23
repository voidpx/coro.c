.global schedule
.global _switch_to
.global coro_start
.global _task_cleanup
.text

# prepare for calling into c, mainly make sure stack is properly aligned
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

__sched_enter:
	pushq %r11;
	lea sched_enter(%rip), %r11;
	call __call_c;
	popq %r11;
	retq;

__sched_exit:
	pushq %r11;
	lea sched_exit(%rip), %r11;
	call __call_c;
	popq %r11;
	retq;

#__save_context:
#	pushq %r8;
#	pushq %r9;
#	pushq %r10;
#	pushq %r11;
#	pushq %r12;
#	pushq %r13;
#	pushq %r14;
#	pushq %r15;
#	pushq %rdi;
#	pushq %rsi;
#	pushq %rbp;
#	pushq %rbx;
#	pushq %rdx;
#	pushq %rax;
#	pushq %rcx;
#	pushfq;
#	subq $0x200, %rsp;
#	fxsave (%rsp);
#	pushq %rdi;
#	movq current(%rip), %rdi;
#	movq %r8, 0xa0(%rdi);
#	movq %r9, 0xa8(%rdi);
#	movq %r10, 0xb0(%rdi);
#	movq %r11, 0xb8(%rdi);
#	movq %r12, 0xc0(%rdi);
#	movq %r13, 0xc8(%rdi);
#	movq %r14, 0xd0(%rdi);
#	movq %r15, 0xd8(%rdi);
#	#movq %rdi, 0xe0(%rdi);
#	movq %rsi, 0xe8(%rdi);
#	movq %rbp, 0xf0(%rdi);
#	movq %rbx, 0xf8(%rdi);
#	movq %rdx, 0x100(%rdi);
#	movq %rax, 0x108(%rdi);
#	movq %rcx, 0x110(%rdi);
#	pushfq;
#	movq (%rsp), %rdx;
#	movq %rdx, 0x118(%rdi)
#	popfq;
#
#	fxsave 0x120(%rdi);
#	popq %rax; #original rdi
#	movq %rax, 0xe0(%rdi); #finally
#	pushq 0x108(%rdi)
#	movq %rax, %rdi; #restore
#	popq %rax;
#
#	retq;

#__restore_context:
#	popfq;
#	popq %rcx;
#	popq %rax;
#	popq %rdx;
#	popq %rbx;
#	popq %rbp;
#	popq %rsi;
#	popq %rdi;
#	popq %r15;
#	popq %r14;
#	popq %r13;
#	popq %r12;
#	popq %r11;
#	popq %r10;
#	popq %r9;
#	popq %r8;
#	movq %rbp, %rsp;
#	popq %rbp;

#	movq current(%rip), %rdi;
#	fxrstor 0x120(%rdi);
#	movq   0xa0(%rdi), %r8;
#	movq   0xa8(%rdi), %r9;
#	movq   0xb0(%rdi), %r10;
#	movq   0xb8(%rdi), %r11;
#	movq   0xc0(%rdi), %r12;
#	movq   0xc8(%rdi), %r13;
#	movq   0xd0(%rdi), %r14;
#	movq   0xd8(%rdi), %r15;
#	#movq   0xe0(%rdi), %rdi;
#	movq   0xe8(%rdi), %rsi;
#	movq   0xf0(%rdi), %rbp;
#	movq   0xf8(%rdi), %rbx;
#	movq  0x100(%rdi), %rdx;
#	movq  0x108(%rdi), %rax;
#	movq  0x110(%rdi), %rcx;
#	pushq %rax;
#	subq $8, %rsp;
#	movq  0x118(%rdi), %rax;
#	movq %rax, (%rsp);
#	popfq;
#	popq %rax;
#
#	# finally
#	movq   0xe0(%rdi), %rdi;
#
#	retq;

# main scheduler entry:
# 1. called from the timer signal handler,in which case the stack is not necessarily aligned properly for calls
# 2. user code
schedule:
	movl $1, __scheduling(%rip); # first thing in scheduler
	# save context, must be on stack
	pushfq;
	pushq %r8;
	pushq %r9;
	pushq %r10;
	pushq %r11;
	pushq %r12;
	pushq %r13;
	pushq %r14;
	pushq %r15;
	pushq %rdi;
	pushq %rsi;
	pushq %rbp;
	pushq %rbx;
	pushq %rdx;
	pushq %rax;
	pushq %rcx;

	# align stack
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

	subq $0x200, %rsp;
	fxsave (%rsp);

    movq current(%rip), %rdi;
    cmpq sched(%rip), %rdi;
	# sched has fixed pc & sp, don't touch it
    je 3f;


	call __sched_enter;

	movq current(%rip), %rdi;
	movq %rsp, (%rdi);
	#movq (%rsp), %r10; # return address of call schedule
	#movq %r10, 8(%rdi); # resume pc


3:
#===========debug================
#	movq %rax, %rdi;
#	lea debug_dump_sched_out(%rip), %r11;
#	call __call_c;
#===========debug end================

	# switch to scheduler sp, which is always properly aligned
	movq sched(%rip), %rax;
	# simulate call
	movq (%rax), %rsp;
    push 8(%rax);
	jmp _schedule; # never return


# _switch_to(task *)
_switch_to:
	movq (%rdi), %rsp; # new stack
	movq %rdi, current(%rip); # set current

	cmpq sched(%rip), %rdi;
	jz 3f;

	movl 0x3c(%rdi), %eax; # state
	testl %eax, %eax;
	jnz 1f; # not new

	pushq $0; # no return address, stack align
	lea task_entry(%rip), %rax;
	pushq %rax;

1:
	pushq %rdi;
	call __sched_exit;
	popq %rdi; # rdi is current task
	movl 0x3c(%rdi), %eax; # state
	testl %eax, %eax;
	jnz 2f; # not new

	movl $1, 0x3c(%rdi); # change to RUNNABLE
	jmp 3f;

2:
	# restore context
	fxrstor (%rsp);
	addq $0x200, %rsp;

	popq %rcx; # flag, don't care
	popq %rsp;

	popq %rcx;
	popq %rax;
	popq %rdx;
	popq %rbx;
	popq %rbp;
	popq %rsi;
	popq %rdi;
	popq %r15;
	popq %r14;
	popq %r13;
	popq %r12;
	popq %r11;
	popq %r10;
	popq %r9;
	popq %r8;
	popfq;

3:

	movl $0, __scheduling(%rip); # last thing in scheduler
	retq;


#===========debug================
#	pushq %rdi;
#	lea debug_dump_sched_in(%rip), %r11;
#	call __call_c;;
#	popq %rdi;
#===========debug end================

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

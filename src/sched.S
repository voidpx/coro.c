.global schedule
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
	#only the sp needs to be saved, pc is saved on the stack already

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

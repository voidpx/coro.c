.global schedule
.global _switch_to
.global coro_start
.global _task_cleanup
.text

# prepare for calling into c, mainly make sure stack is properly aligned
__call_c:
	movq %rsp, %rax;
	testq $0xf, %rax;
	jz 1f; # good stack
	andq $0xfffffffffffffff0, %rax;
	movq %rsp, %rbx;
	movq %rax, %rsp; # aligned sp
	pushq %rbx; # unaigned sp
	pushq $1; # flag indicating stack aligment
	jmp 2f;
1:
	pushq %rsp;
	pushq $0;
2:
	call *%rdi; # function to call
	popq %rax; # don't care
	popq %rsp;
	retq;

__sched_enter:
	pushq %rdi;
	lea sched_enter(%rip), %rdi;
	call __call_c;
	popq %rdi;
	retq;

__sched_exit:
	pushq %rdi;
	lea sched_exit(%rip), %rdi;
	call __call_c;
	popq %rdi;
	retq;

# main scheduler entry:
# 1. called from the timer signal handler,in which case the stack is not necessarily aligned properly for calls
# 2. user code
schedule:
	pushq %rbp;
	movq %rsp, %rbp;
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
	pushfq;

	call __sched_enter;
	#
    movq current(%rip), %rax;
    cmpq sched(%rip), %rax;

	# sched has fixed pc & sp, don't touch it
    je 1f;

	movq %rsp, (%rax);
	lea Lafter(%rip), %r10;
	movq %r10, 8(%rax); # resume pc
1:
	# switch to scheduler sp, which is always properly aligned
	movq sched(%rip), %rax;
	# simulate call
	movq (%rax), %rsp;
    push 8(%rax);
	jmp _schedule; # never return

# resume after the scheduler picks this again
Lafter:
	popfq;
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
	movq %rbp, %rsp;
	popq %rbp;

	retq;

# _switch_to(task *)
_switch_to:
	movq (%rdi), %rsp;
	movl 0x3c(%rdi), %eax; # state
	testl %eax, %eax;
	jnz 1f;
	movl $1, 0x3c(%rdi); # RUNNABLE
	pushq $0; # no return address, stack align
	lea task_entry(%rip), %rax;
	pushq %rax;
	jmp 2f;
1:
    pushq 8(%rdi); # pc
2:
	movq %rdi, current(%rip); # set current

	cmpq sched(%rip), %rdi;
	jz 3f; # coroutine runtime exit
	call __sched_exit;
3:
	retq;

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
	lea Lcoro_exit(%rip), %rdx;
	movq %rsp, %rcx;
	call _coro_start;

Lcoro_exit:
	lea sched(%rip), %rdi;
	movq 0x18(%rdi), %rax; # return value

	popq %rbp;
	retq;

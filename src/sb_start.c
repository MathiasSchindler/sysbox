// Minimal freestanding entrypoint.
// Parses initial stack: argc, argv[], NULL, envp[], NULL, auxv...
// Calls main(argc, argv, envp) and exits via exit_group.

__attribute__((naked, noreturn)) void _start(void) {
	__asm__ volatile(
		"xor %rbp, %rbp\n"
		"mov %rsp, %r8\n"          // save initial stack pointer
		"mov (%r8), %rdi\n"        // argc
		"lea 8(%r8), %rsi\n"       // argv
		"lea 16(%r8,%rdi,8), %rdx\n" // envp = &argv[argc + 1]
		"andq $-16, %rsp\n"        // align stack for call
		"call main\n"              // int main(int,char**,char**)
		"mov %eax, %edi\n"         // exit code
		"mov $231, %eax\n"         // SYS_exit_group
		"syscall\n"
		"hlt\n"
	);
}

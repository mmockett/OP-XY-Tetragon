// Entry for golden.cpp's counted build in the GDB Blackfin simulator (bfin-elf-run,
// its default virtual environment): turn the cycle counter on, call main, exit.
// System calls use the simulator's newlib convention: EXCPT 0 with the call's
// number in P0 and R0 pointing at its arguments.
	.text
	.align 4
	.global start
start:
	R0 = SYSCFG;
	BITSET (R0, 1);
	SYSCFG = R0;
	CSYNC;
	SP += -12;
	call _main;
	[--SP] = R0;
	R0 = SP;
	P0 = 1;
	EXCPT 0;
.Lhang:
	jump .Lhang;

// int golden_syscall(int number, int* args)
	.global _golden_syscall
	.type _golden_syscall, STT_FUNC
_golden_syscall:
	P0 = R0;
	R0 = R1;
	EXCPT 0;
	RTS;
	.size _golden_syscall, .-_golden_syscall

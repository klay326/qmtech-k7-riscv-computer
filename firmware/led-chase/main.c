// LED chase demo -- pure software, loaded over serialboot, no bitstream rebuild.

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <irq.h>
#include <libbase/uart.h>
#include <libbase/console.h>
#include <generated/csr.h>

// Pi digit spigot algorithm (Rabinowitz-Wagon), integer-only math -- no FPU
// needed. Verified against known digits of pi before porting here.
#define PI_DIGITS   150
#define PI_ARR_LEN  (PI_DIGITS * 10 / 3 + 2)
static int pi_arr[PI_ARR_LEN];

static uint32_t stopwatch_start(void)
{
	timer0_en_write(0);
	timer0_reload_write(0);
	timer0_load_write(0xffffffff);
	timer0_en_write(1);
	timer0_update_value_write(1);
	return timer0_value_read();
}

static uint32_t stopwatch_elapsed_ms(uint32_t start)
{
	timer0_update_value_write(1);
	uint32_t remaining = timer0_value_read();
	uint32_t elapsed_cycles = start - remaining;
	return elapsed_cycles / (CONFIG_CLOCK_FREQUENCY / 1000);
}

// Fixed-point (Q16.16) Mandelbrot set escape-time renderer -- much heavier
// per unit time than the pi loop (several multiplies per inner iteration,
// vs one division), and needs no arrays, so it isn't RAM-limited: intensity
// scales purely with MANDEL_MAX_ITER. Verified visually against a native
// build before porting here.
#define MANDEL_WIDTH     60
#define MANDEL_HEIGHT    24
#define MANDEL_MAX_ITER  400
#define FP_SHIFT         16
typedef int32_t fixed;
#define FLT2FP(f) ((fixed)((f) * (1 << FP_SHIFT)))

static fixed fp_mul(fixed a, fixed b)
{
	return (fixed)(((int64_t)a * (int64_t)b) >> FP_SHIFT);
}

// Returns escape iteration count; also adds the number of inner-loop
// iterations actually performed to *total_iters, for throughput reporting.
static int mandel_escape(fixed cx, fixed cy, int max_iter, uint32_t *total_iters)
{
	fixed x = 0, y = 0;
	int i;
	for (i = 0; i < max_iter; i++) {
		fixed x2 = fp_mul(x, x);
		fixed y2 = fp_mul(y, y);
		if (x2 + y2 > FLT2FP(4.0)) break;
		fixed xy = fp_mul(x, y);
		fixed new_x = x2 - y2 + cx;
		fixed new_y = (xy << 1) + cy;
		x = new_x;
		y = new_y;
	}
	*total_iters += (uint32_t)i;
	return i;
}

// One full frame render. Only prints the ASCII art if 'verbose' is set, same
// reasoning as pi_compute_once -- keep UART time out of the measurement.
static void mandel_render_once(int verbose, uint32_t *total_iters)
{
	static const char ramp[] = " .:-=+*#%@";
	for (int row = 0; row < MANDEL_HEIGHT; row++) {
		fixed cy = FLT2FP(-1.25) + (FLT2FP(2.5) * row) / MANDEL_HEIGHT;
		for (int col = 0; col < MANDEL_WIDTH; col++) {
			fixed cx = FLT2FP(-2.0) + (FLT2FP(2.5) * col) / MANDEL_WIDTH;
			int esc = mandel_escape(cx, cy, MANDEL_MAX_ITER, total_iters);
			if (verbose) {
				int idx = (esc == MANDEL_MAX_ITER) ? 9 : (esc * 9 / MANDEL_MAX_ITER);
				putchar(ramp[idx]);
			}
		}
		if (verbose) putchar('\n');
	}
}

static void mandel_cmd(void)
{
	uint32_t dummy_iters = 0;

	printf("Rendering (max depth %d):\n", MANDEL_MAX_ITER);
	mandel_render_once(1, &dummy_iters);

	printf("Stress-testing continuously, press any key to stop...\n");
	uint32_t t0 = stopwatch_start();
	uint32_t frames = 0;
	uint32_t total_iters = 0;

	while (!readchar_nonblock()) {
		mandel_render_once(0, &total_iters);
		frames++;
		if ((frames % 10) == 0) {
			uint32_t ms = stopwatch_elapsed_ms(t0);
			printf("%lu frames, %lu ms, %lu Kiter/sec\n",
				(unsigned long)frames, (unsigned long)ms,
				(unsigned long)(ms ? ((uint64_t)total_iters / (uint64_t)ms) : 0));
		}
	}
	getchar();

	uint32_t ms = stopwatch_elapsed_ms(t0);
	printf("Stopped: %lu frames, %lu total iterations, %lu ms, %lu Kiter/sec average\n",
		(unsigned long)frames, (unsigned long)total_iters, (unsigned long)ms,
		(unsigned long)(ms ? ((uint64_t)total_iters / (uint64_t)ms) : 0));
}

// One pass: recompute PI_DIGITS digits of pi. Only prints them if 'verbose'
// is set -- during the stress loop we skip printing so UART time doesn't
// dominate the measurement.
static void pi_compute_once(int verbose)
{
	int nines = 0, predigit = 0, out_count = 0;

	for (int i = 0; i < PI_ARR_LEN; i++) pi_arr[i] = 2;

	for (int j = 0; j < PI_DIGITS; j++) {
		int q = 0;
		for (int i = PI_ARR_LEN - 1; i >= 0; i--) {
			int x = 10 * pi_arr[i] + q * (i + 1);
			pi_arr[i] = x % (2 * i + 1);
			q = x / (2 * i + 1);
		}
		pi_arr[0] = q % 10;
		q /= 10;
		if (q == 9) {
			nines++;
		} else if (q == 10) {
			out_count++;
			if (verbose) {
				if (out_count > 1) putchar(predigit + 1 + '0');
				if (out_count == 2) putchar('.');
				for (int k = nines; k > 0; k--) putchar('0');
			}
			nines = 0;
			predigit = 0;
		} else {
			out_count++;
			if (verbose) {
				if (out_count > 1) putchar(predigit + '0');
				if (out_count == 2) putchar('.');
				for (int k = nines; k > 0; k--) putchar('9');
			}
			nines = 0;
			predigit = q;
		}
	}
	if (verbose) {
		putchar(predigit + '0');
		putchar('\n');
	}
}

// Continuous stress test: recompute pi over and over until a key is pressed,
// reporting running throughput. This is the actual "heat it up" workload --
// a single pass is too fast to be interesting (RAM caps how many digits fit
// in the 8KB SRAM region, so we can't just crank PI_DIGITS instead).
static void pi_cmd(void)
{
	printf("Verifying (%d digits): ", PI_DIGITS);
	pi_compute_once(1);

	printf("Stress-testing continuously, press any key to stop...\n");
	uint32_t t0 = stopwatch_start();
	uint32_t iterations = 0;

	while (!readchar_nonblock()) {
		pi_compute_once(0);
		iterations++;
		if ((iterations % 500) == 0) {
			uint32_t ms = stopwatch_elapsed_ms(t0);
			uint64_t total_digits = (uint64_t)iterations * PI_DIGITS;
			printf("%lu iterations, %lu ms, %lu digits/sec\n",
				(unsigned long)iterations, (unsigned long)ms,
				(unsigned long)(ms ? (total_digits * 1000ULL / ms) : 0));
		}
	}
	getchar();

	uint32_t ms = stopwatch_elapsed_ms(t0);
	uint64_t total_digits = (uint64_t)iterations * PI_DIGITS;
	printf("Stopped: %lu iterations, %lu total digits, %lu ms, %lu digits/sec average\n",
		(unsigned long)iterations, (unsigned long)total_digits, (unsigned long)ms,
		(unsigned long)(ms ? (total_digits * 1000ULL / ms) : 0));
}

static char *readstr(void)
{
	char c[2];
	static char s[64];
	static int ptr = 0;

	if (readchar_nonblock()) {
		c[0] = getchar();
		c[1] = 0;
		switch (c[0]) {
			case 0x7f:
			case 0x08:
				if (ptr > 0) {
					ptr--;
					fputs("\x08 \x08", stdout);
				}
				break;
			case '\r':
			case '\n':
				s[ptr] = 0x00;
				fputs("\n", stdout);
				ptr = 0;
				return s;
			default:
				if (ptr >= (sizeof(s) - 1))
					break;
				fputs(c, stdout);
				s[ptr] = c[0];
				ptr++;
				break;
		}
	}
	return NULL;
}

static char *get_token(char **str)
{
	char *c, *d;
	c = (char *)strchr(*str, ' ');
	if (c == NULL) {
		d = *str;
		*str = *str + strlen(*str);
		return d;
	}
	*c = 0;
	d = *str;
	*str = c + 1;
	return d;
}

static void prompt(void)
{
	printf("\e[92;1mled-chase\e[0m> ");
}

static void help(void)
{
	puts("\nLED chase demo app built " __DATE__ " " __TIME__ "\n");
	puts("Available commands:");
	puts("help               - Show this command");
	puts("reboot             - Reboot CPU");
	puts("chase              - Run the LED chase pattern (Ctrl-C to stop)");
	puts("donut              - Spinning ASCII donut");
	puts("pi                 - Crunch pi digits (memory-bound stress test)");
	puts("mandel             - Render + stress-test Mandelbrot set (compute-bound)");
	puts("x86                - Run a tiny real 8086 program on an interpreter");
}

extern void donut(void);

static void donut_cmd(void)
{
	printf("Donut demo...\n");
	donut();
}

static void reboot_cmd(void)
{
	ctrl_reset_write(1);
}

// LEDs are active-low at the CSR level is abstracted away by LedChaser,
// so writing a 1 bit here lights the corresponding LED (bit0=R26, bit1=P26, bit2=N26).
static void chase_cmd(void)
{
	int pos = 0;
	int dir = 1;

	printf("Running LED chase, press any key to stop...\n");
	while (!readchar_nonblock()) {
		leds_out_write(1 << pos);
		busy_wait(150);
		pos += dir;
		if (pos == 2) dir = -1;
		if (pos == 0) dir = 1;
	}
	getchar(); // consume the keypress that stopped us
	leds_out_write(0);
	printf("Stopped.\n");
}

// Minimal 8086 instruction interpreter -- decodes and executes exactly the
// opcodes emitted by a small hand-written test program (verified via `nasm
// -f bin` and a native run before porting here), not the full ISA. INT 0x21
// AH=02h/4Ch are implemented as tiny syscalls (print char in DL / halt with
// code in AL), matching real DOS convention even though this isn't DOS --
// just enough surface for a recognizable "real x86 machine code, actually
// executing" demo. No real segmentation: this only ever runs within one
// flat memory window, so effective addresses are just the 16-bit offset.
#define X86_MEM_SIZE 512
static uint8_t x86_mem[X86_MEM_SIZE];

typedef struct {
	uint16_t ax, bx, cx, dx, si, di, bp, sp;
	uint16_t ip;
	uint8_t zf, sf, cf;
	int halted;
	int exit_code;
} x86_cpu_t;

#define X86_GET_LO(r) ((uint8_t)((r) & 0xFF))
#define X86_GET_HI(r) ((uint8_t)(((r) >> 8) & 0xFF))
#define X86_SET_LO(r, v) ((r) = (uint16_t)(((r) & 0xFF00) | (uint8_t)(v)))
#define X86_SET_HI(r, v) ((r) = (uint16_t)(((r) & 0x00FF) | ((uint16_t)(uint8_t)(v) << 8)))

static uint8_t x86_get_reg8(x86_cpu_t *c, int code)
{
	switch (code) {
		case 0: return X86_GET_LO(c->ax);
		case 1: return X86_GET_LO(c->cx);
		case 2: return X86_GET_LO(c->dx);
		case 3: return X86_GET_LO(c->bx);
		case 4: return X86_GET_HI(c->ax);
		case 5: return X86_GET_HI(c->cx);
		case 6: return X86_GET_HI(c->dx);
		case 7: return X86_GET_HI(c->bx);
	}
	return 0;
}

static void x86_set_reg8(x86_cpu_t *c, int code, uint8_t val)
{
	switch (code) {
		case 0: X86_SET_LO(c->ax, val); break;
		case 1: X86_SET_LO(c->cx, val); break;
		case 2: X86_SET_LO(c->dx, val); break;
		case 3: X86_SET_LO(c->bx, val); break;
		case 4: X86_SET_HI(c->ax, val); break;
		case 5: X86_SET_HI(c->cx, val); break;
		case 6: X86_SET_HI(c->dx, val); break;
		case 7: X86_SET_HI(c->bx, val); break;
	}
}

static uint16_t *x86_reg16_ptr(x86_cpu_t *c, int code)
{
	switch (code) {
		case 0: return &c->ax;
		case 1: return &c->cx;
		case 2: return &c->dx;
		case 3: return &c->bx;
		case 4: return &c->sp;
		case 5: return &c->bp;
		case 6: return &c->si;
		case 7: return &c->di;
	}
	return &c->ax;
}

static uint8_t x86_fetch8(x86_cpu_t *c)
{
	return x86_mem[c->ip++ % X86_MEM_SIZE];
}

static int8_t x86_fetch_rel8(x86_cpu_t *c)
{
	return (int8_t)x86_fetch8(c);
}

static uint16_t x86_fetch16(x86_cpu_t *c)
{
	uint16_t lo = x86_fetch8(c);
	uint16_t hi = x86_fetch8(c);
	return (uint16_t)(lo | (hi << 8));
}

static void x86_step(x86_cpu_t *c)
{
	uint8_t op = x86_fetch8(c);

	if (op >= 0xB8 && op <= 0xBF) {          // MOV reg16, imm16
		int reg = op - 0xB8;
		*x86_reg16_ptr(c, reg) = x86_fetch16(c);
		return;
	}
	if (op >= 0xB0 && op <= 0xB7) {          // MOV reg8, imm8
		int reg = op - 0xB0;
		x86_set_reg8(c, reg, x86_fetch8(c));
		return;
	}
	switch (op) {
		case 0xAC: {                          // LODSB
			uint8_t val = x86_mem[c->si % X86_MEM_SIZE];
			X86_SET_LO(c->ax, val);
			c->si++;
			break;
		}
		case 0x3C: {                          // CMP AL, imm8
			uint8_t imm = x86_fetch8(c);
			uint8_t al = X86_GET_LO(c->ax);
			int result = al - imm;
			c->zf = (result == 0);
			c->sf = (result & 0x80) != 0;
			c->cf = (al < imm);
			break;
		}
		case 0x74: {                          // JE/JZ rel8
			int8_t rel = x86_fetch_rel8(c);
			if (c->zf) c->ip = (uint16_t)(c->ip + rel);
			break;
		}
		case 0x88: {                          // MOV r/m8, r8 (mod=11 only)
			uint8_t modrm = x86_fetch8(c);
			int mod = (modrm >> 6) & 3;
			int reg = (modrm >> 3) & 7;
			int rm  = modrm & 7;
			if (mod == 3) {
				uint8_t val = x86_get_reg8(c, reg);
				x86_set_reg8(c, rm, val);
			} else {
				printf("[x86] unsupported ModRM mod=%d at ip=%04x\n", mod, c->ip);
				c->halted = 1;
			}
			break;
		}
		case 0xCD: {                          // INT imm8
			uint8_t vec = x86_fetch8(c);
			if (vec == 0x21) {
				uint8_t ah = X86_GET_HI(c->ax);
				if (ah == 0x02) {
					putchar(X86_GET_LO(c->dx));
				} else if (ah == 0x4C) {
					c->halted = 1;
					c->exit_code = X86_GET_LO(c->ax);
				} else {
					printf("[x86] unsupported INT 0x21 AH=%02x\n", ah);
					c->halted = 1;
				}
			} else {
				printf("[x86] unsupported interrupt vector %02x\n", vec);
				c->halted = 1;
			}
			break;
		}
		case 0xEB: {                          // JMP rel8
			int8_t rel = x86_fetch_rel8(c);
			c->ip = (uint16_t)(c->ip + rel);
			break;
		}
		default:
			printf("[x86] unimplemented opcode %02x at ip=%04x\n", op, c->ip - 1);
			c->halted = 1;
			break;
	}
}

// Assembled from a tiny NASM source (`mov si,msg` / loop with `lodsb`+`cmp`+
// `je` / `int 0x21` print+exit) -- verified byte-for-byte before embedding.
static const uint8_t x86_hello_program[] = {
	0xbe, 0x14, 0x00, 0xac, 0x3c, 0x00, 0x74, 0x08, 0x88, 0xc2, 0xb4, 0x02,
	0xcd, 0x21, 0xeb, 0xf3, 0xb4, 0x4c, 0xcd, 0x21, 0x48, 0x65, 0x6c, 0x6c,
	0x6f, 0x20, 0x66, 0x72, 0x6f, 0x6d, 0x20, 0x38, 0x30, 0x38, 0x36, 0x20,
	0x6c, 0x61, 0x6e, 0x64, 0x21, 0x0d, 0x0a, 0x00
};

static void x86_cmd(void)
{
	x86_cpu_t cpu;
	memset(&cpu, 0, sizeof(cpu));
	memset(x86_mem, 0, sizeof(x86_mem));
	memcpy(x86_mem, x86_hello_program, sizeof(x86_hello_program));

	printf("Running real 8086 machine code:\n");
	uint32_t t0 = stopwatch_start();

	int steps = 0;
	while (!cpu.halted && steps < 100000) {
		x86_step(&cpu);
		steps++;
	}

	uint32_t ms = stopwatch_elapsed_ms(t0);
	printf("\n[x86] halted, exit_code=%d, %d instructions in %lu us\n",
		cpu.exit_code, steps, (unsigned long)(ms * 1000));
}

static void console_service(void)
{
	char *str;
	char *token;

	str = readstr();
	if (str == NULL) return;
	token = get_token(&str);
	if (strcmp(token, "help") == 0)
		help();
	else if (strcmp(token, "reboot") == 0)
		reboot_cmd();
	else if (strcmp(token, "chase") == 0)
		chase_cmd();
	else if (strcmp(token, "donut") == 0)
		donut_cmd();
	else if (strcmp(token, "pi") == 0)
		pi_cmd();
	else if (strcmp(token, "mandel") == 0)
		mandel_cmd();
	else if (strcmp(token, "x86") == 0)
		x86_cmd();
	prompt();
}

int main(void)
{
#ifdef CONFIG_CPU_HAS_INTERRUPT
	irq_setmask(0);
	irq_setie(1);
#endif
	uart_init();

	help();
	prompt();

	while (1) {
		console_service();
	}

	return 0;
}

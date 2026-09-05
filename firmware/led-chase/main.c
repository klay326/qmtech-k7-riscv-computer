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

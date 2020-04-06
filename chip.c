#include <SDL2/SDL.h>
#include <assert.h>
#include <stdbool.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#define min(a,b) (a<b?a:b)

struct chip8 {
	uint8_t *memory;
	uint8_t V[16];
	uint16_t I;
	uint8_t dt, st;
	uint16_t stack[16];
	uint8_t sp;
	uint8_t *display;
	bool keys[16];
	uint16_t pc;

	bool beep, draw;
	uint8_t halt_reg; // used in Fx0A
};

static char chip8_font[] = { 
	0xF0, 0x90, 0x90, 0x90, 0xF0,
	0x20, 0x60, 0x20, 0x20, 0x70,
	0xF0, 0x10, 0xF0, 0x80, 0xF0,
	0xF0, 0x10, 0xF0, 0x10, 0xF0,
	0x90, 0x90, 0xF0, 0x10, 0x10,
	0xF0, 0x80, 0xF0, 0x10, 0xF0,
	0xF0, 0x80, 0xF0, 0x90, 0xF0,
	0xF0, 0x10, 0x20, 0x40, 0x40,
	0xF0, 0x90, 0xF0, 0x90, 0xF0,
	0xF0, 0x90, 0xF0, 0x10, 0xF0,
	0xF0, 0x90, 0xF0, 0x90, 0x90,
	0xE0, 0x90, 0xE0, 0x90, 0xE0,
	0xF0, 0x80, 0x80, 0x80, 0xF0,
	0xE0, 0x90, 0x90, 0x90, 0xE0,
	0xF0, 0x80, 0xF0, 0x80, 0xF0,
	0xF0, 0x80, 0xF0, 0x80, 0x80
};

void chip8_init(struct chip8 *chip, const char *rom_data, size_t size_of_rom, bool eti660) {
	chip->memory = malloc(0x1000);
	chip->display = malloc(64 * 32);
	chip->pc = eti660 ? 0x660 : 0x200;
	chip->sp = 0xF;
	chip->I = 0;
	chip->dt = 0;
	chip->st = 0;

	memcpy(&chip->memory[chip->pc], rom_data, size_of_rom);
	memcpy(chip->memory, chip8_font, sizeof(chip8_font));
}

void chip8_update_timers(struct chip8 *chip) {
	if (chip->dt) {
		chip->dt--;
	}

	if (chip->st) {
		chip->st--;
		if (chip->st == 0) {
			chip->beep = true;
		}
	}
}

void chip8_execute_next_instruction(struct chip8 *chip) {
	if (chip->halt_reg) return;

	uint16_t op = (chip->memory[chip->pc++] << 8) | (chip->memory[chip->pc++]);
	uint16_t nnn =  op & 0x0FFF;
	uint8_t  nn  =  op & 0x00FF;
	uint8_t  n   =  op & 0x000F;
	uint8_t  x   = (op & 0x0F00) >> 8;
	uint8_t  y   = (op & 0x00F0) >> 4;

	#define Vx chip->V[x]
	#define Vy chip->V[y]

	switch (op >> 12) {
		case 0x0:
			switch (nnn) {
				case 0x0E0:
					memset(chip->display, 0, 64 * 32);
					chip->draw = true;
					break;

				case 0x0EE:
					chip->pc = chip->stack[++chip->sp];
					break;

				default:
					break;
			}
			break;

		case 0x1:
			chip->pc = nnn;
			break;

		case 0x2:
			chip->stack[chip->sp--] = chip->pc;
			chip->pc = nnn;
			break;

		case 0x3:
			if (Vx == nn) chip->pc += 2;
			break;

		case 0x4:
			if (Vx != nn) chip->pc += 2;
			break;

		case 0x5:
			if (Vx == Vy && n == 0) chip->pc += 2;
			break;

		case 0x6:
			Vx = nn;
			break;

		case 0x7:
			Vx += nn;
			break;

		case 0x8:
			switch (n) {
				case 0x0:
					Vx = Vy;
					break;

				case 0x1:
					Vx |= Vy;
					break;

				case 0x2:
					Vx &= Vy;
					break;

				case 0x3:
					Vx ^= Vy;
					break;

				case 0x4:
					chip->V[0xF] = (((uint16_t)Vx) + Vy) >> 8;
					Vx += Vy;
					break;

				case 0x5:
					chip->V[0xF] = Vx > Vy;
					Vx -= Vy;
					break;

				case 0x6:
					chip->V[0xF] = Vx & 1;
					Vx >>= 1;
					break;

				case 0x7:
					chip->V[0xF] = Vy > Vx;
					Vx = Vy - Vx;
					break;

				case 0xE:
					chip->V[0xF] = Vx >> 7;
					Vx <<= 1;
					break;
			} // switch (n)
			break;

		case 0x9:
			if (Vx != Vy && n == 0) chip->pc += 2;
			break;

		case 0xA:
			chip->I = nnn;
			break;

		case 0xB:
			chip->pc = chip->V[0] + nnn;
			break;

		case 0xC:
			Vx = rand() & nn;
			break;

		case 0xD:
			chip->V[0xF] = 0;
			for (unsigned i = 0; i < n; ++i) {
				uint8_t byte = chip->memory[chip->I + i];

				for (unsigned j = 0; j < 8; ++j) {
					int pos = ((Vx + j) % 64) + (((Vy + i) % 32) * 64);
					int bit = (byte >> (7 - j)) & 1;
					if (bit && chip->display[pos]) chip->V[0xF] = 1;
					chip->display[pos] ^= bit;
				}
			}
			chip->draw = true;
			break;

		case 0xE:
			switch (nn) {
				case 0x9E:
					if ( chip->keys[Vx]) chip->pc += 2;
					break;

				case 0xA1:
					if (!chip->keys[Vx]) chip->pc += 2;
					break;

				default:
					break;
			}
			break;

		case 0xF:
			switch (nn) {
				case 0x07:
					Vx = chip->dt;
					break;

				case 0x0A:
					chip->halt_reg = x;
					break;

				case 0x15:
					chip->dt = Vx;
					break;

				case 0x18:
					chip->st = Vx;
					break;

				case 0x1E:
					chip->I += Vx;
					break;

				case 0x29:
					chip->I = Vx * 5;
					break;

				case 0x33:
					chip->memory[chip->I + 0] = (Vx / 100);
					chip->memory[chip->I + 1] = (Vx / 10 ) % 10;
					chip->memory[chip->I + 2] = (Vx      ) % 10;
					break;

				case 0x55:
					for (unsigned i = 0; i <= x; ++i) chip->memory[chip->I + i] = chip->V[i];
					break;

				case 0x65:
					for (unsigned i = 0; i <= x; ++i) chip->V[i] = chip->memory[chip->I + i];
					break;
			} // switch (nn)
			break;
	} // switch (op >> 12)

	#undef Vx
	#undef Vy
}

uint32_t colour = 0xFFFFFFFF;
char *filename;
bool eti660;

void display_help(char *filename) {
	printf("Usage: %s [-h] [-c COLOUR] [-e] FILE              \n", filename);
	printf("Interpret Chip-8 code written in FILE.            \n");
	printf("                                                  \n");
	printf("-h: display this help and exit                    \n");
	printf("-c: set the colour of the display to COLOUR (RGBA)\n");
	printf("-e: run in ETI 660 mode                           \n");
	printf("                                                  \n");
	printf("example: %s -c F7A8B8FF -e pong.ch8               \n", filename);
	exit(0);
}

void process_args(int argc, char **argv) {
	char c;

	while ((c = getopt(argc, argv, "hc:e")) != -1) {
		switch (c) {
			case 'c':
				sscanf(optarg, "%x", &colour);
				break;
			
			case 'h':
				display_help(argv[0]);
				break;

			case 'e':
				eti660 = true;
				break;

			default:
				break;
		}
	}

	if (optind < argc) {
		filename = argv[optind];
	} else {
		display_help(argv[0]);
	}
}

static SDL_Keycode keypad_layout[16] = {
	SDLK_1, SDLK_2, SDLK_3, SDLK_q,
	SDLK_w, SDLK_e, SDLK_a, SDLK_s,
	SDLK_d, SDLK_x, SDLK_z, SDLK_c,
	SDLK_4, SDLK_r, SDLK_f, SDLK_v
};

int main(int argc, char **argv) {
	SDL_Window *window;
	SDL_Renderer *renderer;
	int width = 640, height = 320;
	SDL_Texture *framebuffer;
	SDL_Event event;
	bool closed = false;
	uint32_t *pixels;
	char *rom;
	size_t size_of_file;
	FILE *file;
	struct chip8 *chip;

	process_args(argc, argv);
	srand(time(0));

	chip = malloc(sizeof(struct chip8));

	file = fopen(filename, "rb");
	assert(file != NULL);

	fseek(file, 0, SEEK_END);
	size_of_file = min(ftell(file), 0x1000);
	fseek(file, 0, SEEK_SET);

	rom = malloc(size_of_file);
	fread(rom, 1, size_of_file, file);
	fclose(file);

	chip8_init(chip, rom, size_of_file, eti660);

	assert(SDL_Init(SDL_INIT_EVERYTHING) == 0);

	window = SDL_CreateWindow("Chip8", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 320, SDL_WINDOW_RESIZABLE);
	assert(window != NULL);

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	assert(renderer != NULL);

	framebuffer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, width, height);
	assert(framebuffer != NULL);

	pixels = malloc(width * height * sizeof(uint32_t));
	assert(pixels != NULL);

	while (!closed) {
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_QUIT:
					closed = true;
					break;
				
				case SDL_KEYDOWN:
				case SDL_KEYUP:
					for (unsigned i = 0; i < 16; ++i) {
						if (keypad_layout[i] == event.key.keysym.sym) {
							chip->keys[i] = (event.type == SDL_KEYDOWN);
							if (event.type == SDL_KEYDOWN && chip->halt_reg != 0) {
								chip->V[chip->halt_reg] = i;
								chip->halt_reg = 0;
							}
							break;
						}
					}
					break;

				case SDL_WINDOWEVENT:
					switch (event.window.type) {
						case SDL_WINDOWEVENT_RESIZED:
							SDL_GetWindowSize(window, &width, &height);
							SDL_DestroyTexture(framebuffer);
							framebuffer = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
							free(pixels);
							pixels = malloc(width * height * sizeof(uint32_t));
							break;

						default:
							break;
					}
					break;

				default:
					break;
			} // switch (event.type)
		} // while (SDL_PollEvent(&event))

		for (unsigned i = 0; i < 8; ++i) chip8_execute_next_instruction(chip);
		chip8_update_timers(chip);

		for (int p = 0; p < width * height; ++p) {
			unsigned chip_pos = ((p % width) / (width / 64)) + ((p / width) / (height / 32)) * 64;
			pixels[p] = chip->display[chip_pos] * colour;
		}

		SDL_UpdateTexture(framebuffer, NULL, pixels, width * sizeof (uint32_t));

		SDL_RenderClear(renderer);
		SDL_RenderCopy(renderer, framebuffer, NULL, NULL);
		SDL_RenderPresent(renderer);
	}
}
// le funy number has arrived

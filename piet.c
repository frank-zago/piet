/* A PIET interpreter - http://www.dangermouse.net/esoteric/piet.html
 *
 * Copyright (C) 2011 Frank Zago - www.zago.net
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * This is a crude implementation of the latest spec as of october
 * 2011. There is plenty of room for optimization, but at the cost of
 * enlarging the interpreter. The goal was to keep the code short but
 * readable.
 */

#include <SDL/SDL_image.h>
#include <SDL/SDL_endian.h>

#include "piet.h"

/* Program storage */
struct {
	unsigned char *image;
	unsigned int w;				/* width in codels */
	unsigned int h;				/* height in codels */
} program;

/* Interpreter data */
static struct {
	int dp;						/* 0=right, 1=bottom, 2=left, 3=top */
	int cc;						/* -1=left, 1=right */
	struct codel current;
	struct codel edge;
	int block_size;				/* number of codel in color block */
} interp;

static struct {
	unsigned int sp;			/* stack pointer */
	int array[1000];			/* big enough but should be resizable nevertheless */
} stack;

/* Get the color value of a pixel. Probably wrong on big endian
 * hosts.*/
static unsigned int get_pixel(SDL_Surface *image, int x, int y)
{
	unsigned int val;
	unsigned int bpp = image->format->BytesPerPixel;
	unsigned char *pixel = image->pixels + y * image->pitch + x * bpp;

	switch(bpp)
	{
	case 1:
		val = *pixel;
		/* Fixme: Not pretty and possibly wrong on big endian. */
		val = SDL_SwapBE32(*(uint32_t *)&(image->format->palette->colors[val]));
		val >>= 8;
		break;
	case 3:
	case 4:
		val = pixel[0] << 16 | pixel[1] << 8 | pixel[2];
		break;
	default:
		abort();
	}

	return val;
}

/* Convert a color into the internal representation. */
static unsigned char convert_to_code(const int color)
{
	switch(color) {
	case 0xFFC0C0 /* light_red */:     return (0 << HUE_SHIFT) | (0 << LIGHT_SHIFT);
	case 0xFFFFC0 /* light_yellow */:  return (1 << HUE_SHIFT) | (0 << LIGHT_SHIFT);
	case 0xC0FFC0 /* light_green */:   return (2 << HUE_SHIFT) | (0 << LIGHT_SHIFT);
	case 0xC0FFFF /* light_cyan */:    return (3 << HUE_SHIFT) | (0 << LIGHT_SHIFT);
	case 0xC0C0FF /* light_blue */:    return (4 << HUE_SHIFT) | (0 << LIGHT_SHIFT);
	case 0xFFC0FF /* light_magenta */: return (5 << HUE_SHIFT) | (0 << LIGHT_SHIFT);
	case 0xFF0000 /* red */:           return (0 << HUE_SHIFT) | (1 << LIGHT_SHIFT);
	case 0xFFFF00 /* yellow */:        return (1 << HUE_SHIFT) | (1 << LIGHT_SHIFT);
	case 0x00FF00 /* green */:         return (2 << HUE_SHIFT) | (1 << LIGHT_SHIFT);
	case 0x00FFFF /* cyan */:          return (3 << HUE_SHIFT) | (1 << LIGHT_SHIFT);
	case 0x0000FF /* blue */:          return (4 << HUE_SHIFT) | (1 << LIGHT_SHIFT);
	case 0xFF00FF /* magenta */:       return (5 << HUE_SHIFT) | (1 << LIGHT_SHIFT);
	case 0xC00000 /* dark_red */:      return (0 << HUE_SHIFT) | (2 << LIGHT_SHIFT);
	case 0xC0C000 /* dark_yellow */:   return (1 << HUE_SHIFT) | (2 << LIGHT_SHIFT);
	case 0x00C000 /* dark_green */:    return (2 << HUE_SHIFT) | (2 << LIGHT_SHIFT);
	case 0x00C0C0 /* dark_cyan */:     return (3 << HUE_SHIFT) | (2 << LIGHT_SHIFT);
	case 0x0000C0 /* dark_blue */:     return (4 << HUE_SHIFT) | (2 << LIGHT_SHIFT);
	case 0xC000C0 /* dark_magenta */:  return (5 << HUE_SHIFT) | (2 << LIGHT_SHIFT);
	case 0xFFFFFF /* white */:         return WHITE;
	case 0x000000 /* black */:         return BLACK;
	default:
		/* Could return WHITE or BLACK instead. Alas, the spec is open. */
		return INVALID_MASK;
	}
}

static void load_program(const char *filename, unsigned int codel)
{
	unsigned char *p;
	int x, y;
	SDL_Surface *image;

	image = IMG_Load(filename);

	if (!image) 
		goto bad;

	/* Ensure the program.image is sane. */
	if (image->h == 0 || image->w == 0)
		goto bad;

	if (image->h % codel || image->w % codel)
		goto bad;

	if (image->format->BytesPerPixel != 1 &&
		image->format->BytesPerPixel != 3 &&
		image->format->BytesPerPixel != 4)
		goto bad;

	program.w = image->w / codel;
	program.h = image->h / codel;

	/* Transcribe the program in something more palatable. */
	program.image = malloc(program.w * program.h);
	if (!program.image)
		goto bad;

	p = program.image;
	for(y = 0; y < program.h; y++) {
		for(x = 0; x < program.w; x++) {
			int color = get_pixel(image, x*codel, y*codel);
			*p = convert_to_code(color);
			p ++;
		}
	}
		
bad:
	if (image) {
		SDL_FreeSurface(image);
	}
}

/* Check whether the codel is in the program. This is overkill for
 * many callers, which usually change only a single value . */
static int in_bounds(struct codel *dot)
{
	return (dot->x >= 0 && dot->x < program.w &&
			dot->y >= 0 && dot->y < program.h);
}

/* Find the next codel following the DP. next contains the current
 * codel and will be changed. Return true if the codel is in the
 * program.. */
static int get_next_codel_dp(struct codel *next)
{
	switch(interp.dp) {
	case 0: next->x++; break;
	case 1: next->y++; break;
	case 2: next->x--; break;
	case 3: next->y--; break;
	default: abort();
	}

	return in_bounds(next);
}

/* Given a codel, return its address in the program, so that the color
 * value can be read/written. */
static unsigned char *get_codel_addr(struct codel *dot)
{
	return program.image + dot->y * program.w + dot->x;
}

/* Read the color value of a codel. */
static void get_codel_color(struct codel *dot)
{
	dot->color = *get_codel_addr(dot);
}

/* Get color block informations: value (ie number of codels in that
 * block) and the edge codel. For every codel in the color block, mark
 * the codel as read with the fill bit, and recurse with its four
 * neighbors. */
static void get_block_infos(int x, int y)
{
	struct codel codel;

	codel.x = x;
	codel.y = y;

	if (in_bounds(&codel)) {
		unsigned char *p = get_codel_addr(&codel);

		/* Note: if the fill bit has been set, then color value will
		 * be different, so this pixel will not be counted. */
		if (*p == interp.current.color) {

			/* Is it a new edge ? */
			switch(interp.dp) {
			case 0:
				if (x > interp.edge.x)
					interp.edge = codel;
				else if (x == interp.edge.x)
					if ((interp.cc == -1 && y<interp.edge.y) ||
						(interp.cc == 1 && y>interp.edge.y))
						interp.edge = codel;
				break;
			case 1:
				if (y > interp.edge.y)
					interp.edge = codel;
				else if (y == interp.edge.y)
					if ((interp.cc == -1 && x>interp.edge.x) ||
						(interp.cc == 1 && x<interp.edge.x))
						interp.edge = codel;
				break;
			case 2:
				if (x < interp.edge.x)
					interp.edge = codel;
				else if (x == interp.edge.x)
					if ((interp.cc == -1 && y>interp.edge.y) ||
						(interp.cc == 1 && y<interp.edge.y))
						interp.edge = codel;
				break;
			case 3:
				if (y < interp.edge.y)
					interp.edge = codel;
				else if (y <= interp.edge.y)
					if ((interp.cc == -1 && x<interp.edge.x) ||
						(interp.cc == 1 && x>interp.edge.x))
						interp.edge = codel;
				break;
			}

			interp.block_size ++;

			/* Mark this codel as processed, then recurse to the
			 * neighbors. */
			*p |= FILL_MASK;

			get_block_infos(x-1, y);
			get_block_infos(x, y-1);
			get_block_infos(x, y+1);
			get_block_infos(x+1, y);
		}
	}
}

/* Reset the fill bit on the whole program. */
static void reset_fill_bits(void)
{
	int size = program.w * program.h;
	unsigned char *p = program.image;
	while(size--)
		*p++ &= ~FILL_MASK;
}

/* Find the next color block. */
static int get_next_block(struct codel *next_codel)
{
	int found;
	struct codel next;
	int attempt;

	/* Get next intruction. */
	found = 0;
	for (attempt = 0; attempt < 9; attempt ++) {

		interp.edge = interp.current;
		interp.block_size = 0;

		get_block_infos(interp.current.x, interp.current.y);
		reset_fill_bits();

		next = interp.edge;
		if (get_next_codel_dp(&next)) {
			get_codel_color(&next);

			if (next.color != BLACK && next.color != interp.current.color) {
				*next_codel = next;
				found = 1;
				break;
			}
		}

		/* Invalid color or border reached. Alternatively rotate
		 * cc or dp. */
		if (attempt % 2)
			interp.dp = (interp.dp + 1) % 4;
		else
			interp.cc = -interp.cc;
	}

	return found;
}

/* Slide in a white block. There is no loop detection. */
static int slide_white(struct codel *next_codel)
{
	struct codel next;
	int attempt;
	int found;

	do {
		/* Find the edge in straight line. */
		next = interp.current;
		do {
			found = 0;

			if (get_next_codel_dp(&next)) {
				get_codel_color(&next);
				if (next.color == WHITE) {
					interp.current = next;
					found = 1;
				}
			}
		} while (found);
	
		/* Get next color block. */
		found = 0;
		for (attempt = 0; attempt < 9; attempt ++) {
			next = interp.current;
			if (get_next_codel_dp(&next)) {
				get_codel_color(&next);

				if (next.color != BLACK) {
					*next_codel = next;
					found = 1;
					break;
				}
			}

			/* Invalid color or border reached. Rotate
			 * cc or dp. */
			interp.cc = -interp.cc;
			interp.dp = (interp.dp + 1) % 4;
		}
	} while (found && next.color == WHITE);

	return found;
}

#define STACK_PUSH(val)	do { stack.array[stack.sp ++] = (val); } while(0)
#define STACK_POP(val) do { if (stack.sp < 1) return; (val) = stack.array[--stack.sp]; } while(0)

#define OP_2_VALUES(name, op)					\
	static void op_##name(void)					\
	{											\
		int val1;								\
		int val2;								\
												\
		if (stack.sp < 2)						\
			return;								\
												\
		STACK_POP(val1);						\
		STACK_POP(val2);						\
												\
		STACK_PUSH(val2 op val1);				\
	}

OP_2_VALUES(add, +);
OP_2_VALUES(substract, -);
OP_2_VALUES(multiply, *);
OP_2_VALUES(divide, /);
OP_2_VALUES(mod, %);

static void op_switch(void)
{
	unsigned int val;

	if (stack.sp < 1)
		return;

	STACK_POP(val);

	if (val % 2)
		interp.cc = -interp.cc;
}

static void op_push(void)
{
	STACK_PUSH(interp.block_size);
}

static void op_pop(void)
{
	if (stack.sp < 1)
		return;

	stack.sp --;
}

static void op_duplicate(void)
{
	if (stack.sp < 1)
		return;

	STACK_PUSH(stack.array[stack.sp-1]);
}

static void op_not(void)
{
	int val;

	if (stack.sp < 1)
		return;
	
	STACK_POP(val);

	if (val)
		STACK_PUSH(0);
	else
		STACK_PUSH(1);
}
	
static void op_greater(void)
{
	int val1;
	int val2;

	if (stack.sp < 2)
		return;
	
	STACK_POP(val1);
	STACK_POP(val2);

	if (val2 > val1)
		STACK_PUSH(1);
	else
		STACK_PUSH(0);
}

static void op_pointer(void)
{
	int val;

	STACK_POP(val);

	/* Bring inter.dp into the [-3, +3] range then in [0, 3]. */
	interp.dp = (interp.dp + val) % 4;

	if (interp.dp < 0)
		interp.dp += 4;
}

static void op_roll(void)
{
	int depth;
	int rolls;
	int i;
	int j;

	STACK_POP(rolls);
	STACK_POP(depth);

	if (depth < 0 || depth > stack.sp)
		return;


	if (rolls > 0) {
		for (i=0; i<rolls; i++) {
			int last = stack.array[stack.sp-1];
			for(j=stack.sp-1; j>stack.sp-depth; j--) {
				stack.array[j] = stack.array[j-1];
			}
			stack.array[stack.sp-depth] = last;
		}
	} 
	else if (rolls < 0) {
		rolls = -rolls;
		for (i=0; i<rolls; i++) {
			int last = stack.array[stack.sp-depth];
			for(j=stack.sp-depth; j<stack.sp-1; j++) {
				stack.array[j] = stack.array[j+1];
			}
			stack.array[stack.sp-1] = last;
		}
	}
}

static void op_out_number(void)
{
	int val;

	STACK_POP(val);
	printf("%d", val);
	fflush(stdout);
}

static void op_out_char(void)
{
	int val;

	STACK_POP(val);
	printf("%c", val);
	fflush(stdout);
}

static void op_in_number(void)
{
	int val;

	fscanf(stdin, "%d", &val);
	STACK_PUSH(val);
}

static void op_in_char(void)
{
	char val;

	fscanf(stdin, "%c", &val);
	STACK_PUSH(val);
}

static const struct operation ops[18] = {
	{ .name = "none" },			/* impossible by design */
	{ .name = "push",        .op=op_push },
	{ .name = "pop",         .op=op_pop },
	{ .name = "add",         .op=op_add },
	{ .name = "subtract",    .op=op_substract },
	{ .name = "multiply",    .op=op_multiply },
	{ .name = "divide",      .op=op_divide },
	{ .name = "mod",         .op=op_mod },
	{ .name = "not",         .op=op_not },
	{ .name = "greater",     .op=op_greater },
	{ .name = "pointer",     .op=op_pointer },
	{ .name = "switch",      .op=op_switch },
	{ .name = "duplicate",   .op=op_duplicate },
	{ .name = "roll",        .op=op_roll },
	{ .name = "in(number)",  .op=op_in_number },
	{ .name = "in(char)",    .op=op_in_char },
	{ .name = "out(number)", .op=op_out_number },
	{ .name = "out(char)",   .op=op_out_char }
};

/* Compare the current codel with the one given. Returns the Hue
 * and Lightness cycles. */
static const struct operation *get_operation(struct codel *next)
{
	int val1, val2;
	int hue;
	int lightness;

	/* Find current interpreter value in table. */
	val1 = (interp.current.color & HUE_MASK) >> HUE_SHIFT;
	val2 = (next->color & HUE_MASK) >> HUE_SHIFT;

	if (val1 > val2)
		hue = 6 - val1 + val2;
	else
		hue = val2 - val1;

	val1 = (interp.current.color & LIGHT_MASK) >> LIGHT_SHIFT;
	val2 = (next->color & LIGHT_MASK) >> LIGHT_SHIFT;

	if (val1 > val2)
		lightness = 3 - val1 + val2;
	else
		lightness = val2 - val1;

	return &ops[hue * 3 + lightness];
}

/* Arg1 is program file, arg2 is codel size in pixels. */
int main(int argc, char *argv[])
{
	if (argc != 3) {
		printf("Usage: piet <image> <codel size>\n");
		return 1;
	}

	load_program(argv[1], atoi(argv[2]));

	if (!program.image) {
		fprintf(stderr, "Program not found/not valid\n");
		return 1;
	}

	interp.dp = 0;
	interp.cc = -1;
	interp.current.x = 0;
	interp.current.y = 0;
	get_codel_color(&interp.current);

	int found;

	do {
		struct codel next;

		if (interp.current.color == WHITE) {
			if (slide_white(&next))
				interp.current = next;
			else
				break;
		}

		found = get_next_block(&next);
		if (found) {
			if (next.color == WHITE) {
				interp.current = next;
			} else {
				const struct operation *op = get_operation(&next);
				op->op();
				interp.current = next;
			}
		}
	} while(found);
	
	return 0;
}

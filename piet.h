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

struct operation {
	char *name;
	void (*op)(void);
} operation;

/* Defines a codel, with its position and color. */
struct codel {
	int x;
	int y;
	int color;
};

/* In the reconstruted program, a codel is represented as:
 *    bits 0-2: hue
 *    bits 3-4: lightness
 *    bit 5:    fill helper to get the block value
 *    bit 6:    indicate an invalid color
 *    bit 7:    indicate a special color (black or white)
 */
#define HUE_SHIFT    0
#define HUE_MASK     (0x07 << HUE_SHIFT)
#define LIGHT_SHIFT  3
#define LIGHT_MASK   (0x3 << LIGHT_SHIFT)
#define FILL_MASK    (1 << 5)
#define INVALID_MASK (1 << 6)
#define SPECIAL_MASK (1 << 7)

#define BLACK (SPECIAL_MASK | 0)
#define WHITE (SPECIAL_MASK | 1)

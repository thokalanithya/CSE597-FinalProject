/*
 * fb.c - a framebuffer console driver (Assignment 1, CSE 597)
 * Copyright 2025 Ruslan Nikolaev <rnikola@psu.edu>
 */

#include <fb.h>
#include <types.h>

extern unsigned char __ascii_font[2048]; /* ascii_font.c */

#define FONT_WIDTH 8
#define FONT_HEIGHT 16

#define FB_STATUS_BOX		56
#define FB_STATUS_MARGIN	10
#define FB_STATUS_WIDTH		18
#define FB_STATUS_HEIGHT	36

static unsigned int *Fb;
static unsigned int Width, PosX, PosY, MaxX, MaxY;

static unsigned int StatusCurr[2];
static unsigned int StatusStart[2];
static unsigned int StatusEnd[2];
static unsigned int StatusY;

#define HELLO_STATEMENT \
	"Framebuffer Console (CSE 597)\nCopyright (C) 2024 Ruslan Nikolaev\n\n"

void fb_init(unsigned int *fb, unsigned int width, unsigned int height)
{
	size_t i, num = (size_t) width * height;
	const char *__hello_statement = HELLO_STATEMENT;

	/* Clean up the screen */
	for (i = 0; i < num; i++) {
		fb[i] = 0x00000000U;
	}

	Fb = fb;
	Width = width;
	PosX = 0;
	PosY = 0;
	MaxX = width / FONT_WIDTH;
	MaxY = (height - FB_STATUS_BOX) / FONT_HEIGHT;

	/* Print a hello statement */
	for (i = 0; i < sizeof(HELLO_STATEMENT)-1; i++) {
		fb_output(__hello_statement[i]);
	}

	/* Draw the status box */
	for (i = 0; i < width; i++) {
		fb[(height - FB_STATUS_BOX + 1) * width + i] = 0xFFFFFFFFU;
		fb[(height - 2) * width + i] = 0xFFFFFFFFU;
	}
	for (i = height - FB_STATUS_BOX + 2; i < height - 2; i++) {
		fb[width * i] = 0xFFFFFFFFU;
		fb[width * i + (width / 2)] = 0xFFFFFFFFU;
		fb[width * i + width - 1] = 0xFFFFFFFFU;
	}

	StatusY = height - FB_STATUS_BOX + FB_STATUS_MARGIN;
	StatusStart[0] = StatusCurr[0] = FB_STATUS_MARGIN;
	StatusStart[1] = StatusCurr[1] = FB_STATUS_MARGIN + (width / 2);
	StatusEnd[0] = width / 2 - FB_STATUS_MARGIN;
	StatusEnd[1] = width - FB_STATUS_MARGIN;
}

void fb_status_update(unsigned int task_id)
{
	unsigned int curr, i, j;

	if (task_id >= 2)
		return;

	curr = StatusCurr[task_id];

	/* Clean up the previous box. */
	if (curr >= StatusStart[task_id] + FB_STATUS_WIDTH) {
		for (j = StatusY; j < StatusY + FB_STATUS_HEIGHT; j++) {
			for (i = curr - FB_STATUS_WIDTH; i < curr; i++) {
				Fb[j * Width + i] = 0x00000000U;
			}
		}
	}

	/* Draw a new box. */
	if (curr + FB_STATUS_WIDTH > StatusEnd[task_id])
		curr = StatusStart[task_id];

	for (j = StatusY; j < StatusY + FB_STATUS_HEIGHT; j++) {
		for (i = curr; i < curr + FB_STATUS_WIDTH; i++) {
			Fb[j * Width + i] = 0x88888888U;
		}
	}
	StatusCurr[task_id] = curr + FB_STATUS_WIDTH;
}

static void fb_scrollup(void)
{
	/* Move the text up one row */
	size_t cur = 0, count = Width * ((MaxY - 1) * FONT_HEIGHT);
	size_t row = Width * FONT_HEIGHT;
	do {
		Fb[cur] = Fb[cur+row];
		cur++;
	} while (--count != 0);

	/* Clean up the last row */
	do {
		Fb[cur] = 0x00000000U;
		cur++;
	} while (--row != 0);
}

void fb_output(char ch)
{
	size_t cur;
	unsigned char *ptr;
	if ((signed char) ch <= 0) { /* not in the ASCII subset */
		if (ch == 0) return;
		ch = '?'; /* an unknown character */
	}
	if (ch == '\n' || PosX == MaxX) {
		PosX = 0;
		PosY++;
	}
	if (PosY == MaxY) {
		PosY--;
		fb_scrollup();
	}
	if (ch == '\n')
		return;
	ptr = &__ascii_font[(unsigned char) ch * (FONT_WIDTH * FONT_HEIGHT / 8)];
	cur = (size_t) PosX * FONT_WIDTH + (PosY * FONT_HEIGHT) * Width;
	for (size_t j = 0; j < FONT_HEIGHT; j++) {
		/* for simplicity, assume that FONT_WIDTH=8, i.e., fits in one byte */
		signed char bitmap = ptr[j];
		for (size_t i = 0; i < FONT_WIDTH; i++) {
			signed char color = (bitmap >> 7); /* propagate the sign bit */
			Fb[cur + i] = (signed int) color; /* sign extend to 32 bits */
			bitmap <<= 1;
		}
		cur += Width;
	}
	PosX++;
}

/*
 * fbvis - a framebuffer image viewer
 *
 * Copyright (C) 2013 Ali Gholami Rudi
 *
 * This file is released under the modified BSD license.
 */
#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pty.h>
#include "draw.h"
#include "lodepng.h"

/* optimized version of fb_val() */
#define FB_VAL(r, g, b)	fb_val(r, g, b)

#define PAGESTEPS	8
#define CTRLKEY(x)	((x) - 96)
#define MIN(a, b)	((a) < (b) ? (a) : (b))
#define MAX(a, b)	((a) > (b) ? (a) : (b))
#define REGION(a, b, x)	(MIN(b, MAX(x, a)))

static int cols, rows;
static int ch;
static unsigned char *buf;
static int head, left;
static int count;
static struct termios termios;
static int fullscreen;
static char **files;
static int curfile = -1;

static void draw(void)
{
	char row[1 << 14];
	int bpp = FBM_BPP(fb_mode());
	int rs = head;
	int re = rs + MIN(rows - rs, fb_rows());
	int cs = left;
	int ce = cs + MIN(cols - cs, fb_cols());
	int fbr = (fb_rows() - (re - rs)) >> 1;
	int fbc = (fb_cols() - (ce - cs)) >> 1;
	int i, j;
	for (i = rs; i < re; i++) {
		for (j = cs; j < ce; j++) {
			unsigned char *src = buf + (i * cols + j) * ch;
			unsigned int *dst = (void *) (row + (j - cs) * bpp);
			*dst = FB_VAL(src[0], src[1], src[2]);
		}
		fb_set(fbr + i - rs, fbc, row, ce - cs);
	}
}

static void drawfs(void)
{
	char row[1 << 14];
	int fsrows = rows * fb_cols() / cols;
	int rs = head * MAX(0, fsrows - fb_rows()) / MAX(1, rows - fb_rows());
	int bpp = FBM_BPP(fb_mode());
	int i, j;
	for (i = 0; i < fb_rows(); i++) {
		int r = (rs + i) * rows / fsrows;
		if (r >= rows)
			memset(row, 0, fb_cols() * bpp);
		for (j = 0; j < fb_cols() && r < rows; j++) {
			int c = j * cols / fb_cols();
			unsigned char *src = buf + (r * cols + c) * ch;
			unsigned int *dst = (void *) (row + j * bpp);
			*dst = FB_VAL(src[0], src[1], src[2]);
		}
		fb_set(i, 0, row, fb_cols());
	}
}

unsigned char *stbi_load(char const *filename, int *x, int *y, int *comp, int req_comp);
char *ppm_load(char *path, int *h, int *w);

static int loadfile(char *path)
{
	ch = 4;
	lodepng_decode32_file(&buf, (void *) &cols, (void *) &rows, path);
	if (!buf)
		buf = stbi_load(path, &cols, &rows, &ch, 0);
	if (!buf) {
		ch = 3;
		buf = (void *) ppm_load(path, &rows, &cols);
	}
	return !buf;
}

static void printinfo(void)
{
	printf("\rFBVIS:     file:%s\x1b[K\r", files[curfile]);
	fflush(stdout);
}

static int nextfile(int dir)
{
	if (buf)
		free(buf);
	buf = NULL;
	head = 0;
	while (!buf) {
		curfile += dir;
		if (curfile < 0 || !files[curfile])
			return 1;
		if (loadfile(files[curfile]))
			printf("failed to load image <%s>\n", files[curfile]);
	}
	printinfo();
	return 0;
}

static int readkey(void)
{
	unsigned char b;
	if (read(0, &b, 1) <= 0)
		return -1;
	return b;
}

static int getcount(int def)
{
	int result = count ? count : def;
	count = 0;
	return result;
}

static void term_setup(void)
{
	struct termios newtermios;
	tcgetattr(0, &termios);
	newtermios = termios;
	newtermios.c_lflag &= ~ICANON;
	newtermios.c_lflag &= ~ECHO;
	tcsetattr(0, TCSAFLUSH, &newtermios);
}

static void term_cleanup(void)
{
	tcsetattr(0, 0, &termios);
}

static void mainloop(void)
{
	int step = fb_rows() / PAGESTEPS;
	int hstep = fb_cols() / PAGESTEPS;
	int c;
	term_setup();
	draw();
	while ((c = readkey()) != -1) {
		switch (c) {
		case 'j':
			head += step * getcount(1);
			break;
		case 'k':
			head -= step * getcount(1);
			break;
		case 'l':
			left += hstep * getcount(1);
			break;
		case 'h':
			left -= hstep * getcount(1);
			break;
		case 'H':
			head = 0;
			break;
		case 'L':
			head = MAX(0, rows - fb_rows());
			break;
		case 'M':
			head = MAX(0, (rows - fb_rows()) >> 1);
			break;
		case ' ':
		case CTRL('d'):
			head += fb_rows() * getcount(1) - step;
			break;
		case 127:
		case CTRL('u'):
			head -= fb_rows() * getcount(1) - step;
			break;
		case 'f':
			fullscreen = 1 - fullscreen;
			break;
		case 'r':
		case CTRLKEY('l'):
			break;
		case CTRLKEY('f'):
		case CTRLKEY('b'):
			if (!nextfile(c == CTRLKEY('f') ? getcount(1) : -getcount(1)))
				break;
		case 'q':
			term_cleanup();
			return;
		case 'i':
			printinfo();
		default:
			if (c == 'd')
				sleep(getcount(1));
			if (isdigit(c))
				count = count * 10 + c - '0';
			if (c == 27)
				count = 0;
			/* no need to redraw */
			continue;
		}
		head = REGION(0, MAX(0, rows - fb_rows()), head);
		left = REGION(0, MAX(0, cols - fb_cols()), left);
		if (fullscreen)
			drawfs();
		else
			draw();
	}
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("usage: %s file\n", argv[0]);
		return 0;
	}
	files = argv + 1;
	if (nextfile(1))
		return 1;
	if (fb_init())
		return 1;
	mainloop();
	fb_free();
	printf("\n");
	free(buf);
	return 0;
}

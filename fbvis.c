/*
 * fbvis - a framebuffer image viewer
 *
 * Copyright (C) 2013-2014 Ali Gholami Rudi
 *
 * This file is released under the Modified BSD license.
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

static unsigned char *obuf;	/* original image */
static int ocols, orows;	/* obuf dimensions */
static int ch;			/* bytes per pixel */
static unsigned char *buf;	/* zoomed image for framebuffer */
static int cols, rows;		/* buf dimensions */
static int czoom;		/* current zoom */
static int head, left;		/* current viewing position */
static int count;		/* command prefix */
static struct termios termios;
static char **files;
static int curfile = -1;

#define ZOOM_ORIG	0
#define ZOOM_FITHT	1
#define ZOOM_FITWID	2

static void zoom(int z)
{
	int bpp = FBM_BPP(fb_mode());
	int i, j;
	int c = 100;
	if (z == ZOOM_FITHT)
		c = 100 * fb_rows() / orows;
	if (z == ZOOM_FITWID)
		c = 100 * fb_cols() / ocols;
	czoom = z;
	cols = ocols * c / 100;
	rows = orows * c / 100;
	buf = malloc(rows * cols * bpp);
	for (i = 0; i < rows; i++) {
		for (j = 0; j < cols; j++) {
			unsigned char *src = obuf + (i * 100 / c * ocols +
							j * 100 / c) * ch;
			unsigned int *dst = (void *) buf + (i * cols + j) * bpp;
			*dst = FB_VAL(src[0], src[1], src[2]);
		}
	}
}

static void draw(void)
{
	int bpp = FBM_BPP(fb_mode());
	int rs = head;
	int re = rs + MIN(rows - rs, fb_rows());
	int cs = left;
	int ce = cs + MIN(cols - cs, fb_cols());
	int fbr = (fb_rows() - (re - rs)) >> 1;
	int fbc = (fb_cols() - (ce - cs)) >> 1;
	int i;
	for (i = rs; i < re; i++)
		fb_set(fbr + i - rs, fbc, buf + (i * cols + cs) * bpp, ce - cs);
}

unsigned char *stbi_load(char const *filename, int *x, int *y, int *comp, int req_comp);
char *ppm_load(char *path, int *h, int *w);

static int loadfile(char *path)
{
	FILE *fp = fopen(path, "r");
	obuf = NULL;
	if (!fp)
		return 1;
	fclose(fp);
	ch = 4;
	lodepng_decode32_file(&obuf, (void *) &ocols, (void *) &orows, path);
	if (!obuf)
		obuf = stbi_load(path, &ocols, &orows, &ch, 0);
	if (!obuf) {
		ch = 3;
		obuf = (void *) ppm_load(path, &orows, &ocols);
	}
	return !obuf;
}

static void printinfo(void)
{
	printf("\rFBVIS:     file:%s\x1b[K\r", files[curfile]);
	fflush(stdout);
}

static void freebufs(void)
{
	free(buf);
	free(obuf);
	buf = NULL;
	obuf = NULL;
}

static int nextfile(int dir)
{
	freebufs();
	head = 0;
	while (1) {
		curfile += dir;
		if (curfile < 0 || !files[curfile])
			return 1;
		if (!loadfile(files[curfile]))
			break;
		else
			printf("failed to load image <%s>\n", files[curfile]);
	}
	zoom(czoom);
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
		case CTRLKEY('d'):
			head += fb_rows() * getcount(1) - step;
			break;
		case 127:
		case CTRLKEY('u'):
			head -= fb_rows() * getcount(1) - step;
			break;
		case 'f':
			zoom(ZOOM_FITHT);
			break;
		case 'w':
			zoom(ZOOM_FITWID);
			break;
		case 'z':
			zoom(ZOOM_ORIG);
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
	if (fb_init())
		return 1;
	if (nextfile(1)) {
		fb_free();
		return 1;
	}
	mainloop();
	fb_free();
	freebufs();
	printf("\n");
	return 0;
}

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

static int nextchar(int fd)
{
	unsigned char b[4] = {0};
	read(fd, b, 1);
	return b[0];
}

static int cutint(int fd)
{
	int c;
	int n = 0;
	do {
		c = nextchar(fd);
	} while (isspace(c));
	while (isdigit(c)) {
		n = n * 10 + c - '0';
		c = nextchar(fd);
	}
	return n;
}

char *ppm_load(char *path, int *h, int *w)
{
	char *d;
	int fd = open(path, O_RDONLY);
	if (fd < 0 || nextchar(fd) != 'P' || nextchar(fd) != '6')
		return NULL;
	*w = cutint(fd);	/* image width */
	*h = cutint(fd);	/* image height */
	cutint(fd);		/* max color val */

	d = malloc(*h * *w * 3); 
	read(fd, d, *h * *w * 3);
	close(fd);
	return d;
}

void ppm_save(char *path, char *s, int h, int w)
{
	char sig[128];
	int fd;
	sprintf(sig, "P6\n%d %d\n255\n", w, h);
	fd = open(path, O_WRONLY | O_TRUNC | O_CREAT, 0600);
	write(fd, sig, strlen(sig));
	write(fd, s, h * w * 3);
	close(fd);
}

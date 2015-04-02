/*
 * Copyright (c) 2011-2013 Kevin Lange.  All rights reserved.
 *
 * Developed by:            Kevin Lange
 *                          http://github.com/klange/nyancat
 *                          http://nyancat.dakko.us
 *
 * 40-column support by:    Peter Hazenberg
 *                          http://github.com/Peetz0r/nyancat
 *                          http://peter.haas-en-berg.nl
 *
 * Build tools unified by:  Aaron Peschel
 *                          https://github.com/apeschel
 *
 * For a complete listing of contributers, please see the git commit history.
 *
 * This is a simple telnet server / standalone application which renders the
 * classic Nyan Cat (or "poptart cat") to your terminal.
 *
 * It makes use of various ANSI escape sequences to render color, or in the case
 * of a VT220, simply dumps text to the screen.
 *
 * For more information, please see:
 *
 *     http://nyancat.dakko.us
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal with the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimers.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimers in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the names of the Association for Computing Machinery, Kevin
 *      Lange, nor the names of its contributors may be used to endorse
 *      or promote products derived from this Software without specific prior
 *      written permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * WITH THE SOFTWARE.
 */

#define _XOPEN_SOURCE 500
#define _DARWIN_C_SOURCE 1
#define __BSD_VISIBLE 1
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <sys/ioctl.h>

/*
 * The animation frames are stored separately in
 * this header so they don't clutter the core source
 */
#include "animation.c"

/*
 * Color palette to use for final output
 * Specifically, this should be either control sequences
 * or raw characters (ie, for vt220 mode)
 */
const char * colors[256] = {NULL};

/*
 * For most modes, we output spaces, but for some
 * we will use block characters (or even nothing)
 */
const char * output = "  ";

/*
 * Are we currently in telnet mode?
 */
int telnet = 0;

/*
 * Whether or not to show the counter
 */
int show_counter = 1;

/*
 * Number of frames to show before quitting
 * or 0 to repeat forever (default)
 */
unsigned int frame_count = 0;

/*
 * Clear the screen between frames (as opposed to reseting
 * the cursor position)
 */
int clear_screen = 1;

/*
 * Force-set the terminal title.
 */
int set_title = 1;

/*
 * I refuse to include libm to keep this low
 * on external dependencies.
 *
 * Count the number of digits in a number for
 * use with string output.
 */
int digits(int val) {
	int d = 1, c;
	if (val >= 0) for (c = 10; c <= val; c *= 10) d++;
	else for (c = -10 ; c >= val; c *= 10) d++;
	return (c < 0) ? ++d : d;
}

/*
 * These values crop the animation, as we have a full 64x64 stored,
 * but we only want to display 40x24 (double width).
 */
int min_row = -1;
int max_row = -1;
int min_col = -1;
int max_col = -1;

/*
 * Actual width/height of terminal.
 */
int terminal_width = 66;
int terminal_height = 18;

/*
 * Flags to keep track of whether width/height were automatically set.
 */
char using_automatic_width = 0;
char using_automatic_height = 0;

/*
 * Print escape sequences to return cursor to visible mode
 * and exit the application.
 */
void finish() {
	if (clear_screen) {
		printf("\033[?25h\033[0m\033[H\033[2J");
	} else {
		printf("\033[0m\n");
	}
	exit(0);
}

/*
 * Telnet requires us to send a specific sequence
 * for a line break (\r\000\n), so let's make it happy.
 */
void newline(int n) {
	int i = 0;
	for (i = 0; i < n; ++i) {
		/* We will send `n` linefeeds to the client */
		if (telnet) {
			/* Send the telnet newline sequence */
			putc('\r', stdout);
			putc(0, stdout);
			putc('\n', stdout);
		} else {
			/* Send a regular line feed */
			putc('\n', stdout);
		}
	}
}

/*
 * Print the usage / help text describing options
 */
void usage(char * argv[]) {
	printf("Terminal Nyancat\n");
}

int main(int argc, char ** argv) {

	/* The default terminal is ANSI */
	char term[1024] = {'a','n','s','i', 0};
	unsigned int k;
	int ttype;
	uint32_t option = 0, done = 0, sb_mode = 0;
	/* Various pieces for the telnet communication */
	char  sb[1024] = {0};
	unsigned short sb_len   = 0;

	/* Whether or not to show the MOTD intro */
	char show_intro = 0;
	char skip_intro = 0;

	/* We are running standalone, retrieve the
	 * terminal type from the environment. */
	char * nterm = getenv("TERM");
	if (nterm) {
		strcpy(term, nterm);
	}

	/* Also get the number of columns */
//	struct winsize w;
//	ioctl(0, TIOCGWINSZ, &w);
//	terminal_width = w.ws_col;
//	terminal_height = w.ws_row;

	/* Convert the entire terminal string to lower case */
	for (k = 0; k < strlen(term); ++k) {
		term[k] = tolower(term[k]);
	}

	/* Do our terminal detection */
	ttype = 1; /* 256-color, spaces */

	int always_escape = 0; /* Used for text mode */

	switch (ttype) {
		case 1:
			colors[',']  = "\033[48;5;17m";  /* Blue background */
			colors['.']  = "\033[48;5;231m"; /* White stars */
			colors['\''] = "\033[48;5;16m";  /* Black border */
			colors['@']  = "\033[48;5;230m"; /* Tan poptart */
			colors['$']  = "\033[48;5;175m"; /* Pink poptart */
			colors['-']  = "\033[48;5;162m"; /* Red poptart */
			colors['>']  = "\033[48;5;196m"; /* Red rainbow */
			colors['&']  = "\033[48;5;214m"; /* Orange rainbow */
			colors['+']  = "\033[48;5;226m"; /* Yellow Rainbow */
			colors['#']  = "\033[48;5;118m"; /* Green rainbow */
			colors['=']  = "\033[48;5;33m";  /* Light blue rainbow */
			colors[';']  = "\033[48;5;19m";  /* Dark blue rainbow */
			colors['*']  = "\033[48;5;240m"; /* Gray cat face */
			colors['%']  = "\033[48;5;175m"; /* Pink cheeks */
			break;
		case 2:
			colors[',']  = "\033[104m";      /* Blue background */
			colors['.']  = "\033[107m";      /* White stars */
			colors['\''] = "\033[40m";       /* Black border */
			colors['@']  = "\033[47m";       /* Tan poptart */
			colors['$']  = "\033[105m";      /* Pink poptart */
			colors['-']  = "\033[101m";      /* Red poptart */
			colors['>']  = "\033[101m";      /* Red rainbow */
			colors['&']  = "\033[43m";       /* Orange rainbow */
			colors['+']  = "\033[103m";      /* Yellow Rainbow */
			colors['#']  = "\033[102m";      /* Green rainbow */
			colors['=']  = "\033[104m";      /* Light blue rainbow */
			colors[';']  = "\033[44m";       /* Dark blue rainbow */
			colors['*']  = "\033[100m";      /* Gray cat face */
			colors['%']  = "\033[105m";      /* Pink cheeks */
			break;
		case 3:
			colors[',']  = "\033[25;44m";    /* Blue background */
			colors['.']  = "\033[5;47m";     /* White stars */
			colors['\''] = "\033[25;40m";    /* Black border */
			colors['@']  = "\033[5;47m";     /* Tan poptart */
			colors['$']  = "\033[5;45m";     /* Pink poptart */
			colors['-']  = "\033[5;41m";     /* Red poptart */
			colors['>']  = "\033[5;41m";     /* Red rainbow */
			colors['&']  = "\033[25;43m";    /* Orange rainbow */
			colors['+']  = "\033[5;43m";     /* Yellow Rainbow */
			colors['#']  = "\033[5;42m";     /* Green rainbow */
			colors['=']  = "\033[25;44m";    /* Light blue rainbow */
			colors[';']  = "\033[5;44m";     /* Dark blue rainbow */
			colors['*']  = "\033[5;40m";     /* Gray cat face */
			colors['%']  = "\033[5;45m";     /* Pink cheeks */
			break;
		case 4:
			colors[',']  = "\033[0;34;44m";  /* Blue background */
			colors['.']  = "\033[1;37;47m";  /* White stars */
			colors['\''] = "\033[0;30;40m";  /* Black border */
			colors['@']  = "\033[1;37;47m";  /* Tan poptart */
			colors['$']  = "\033[1;35;45m";  /* Pink poptart */
			colors['-']  = "\033[1;31;41m";  /* Red poptart */
			colors['>']  = "\033[1;31;41m";  /* Red rainbow */
			colors['&']  = "\033[0;33;43m";  /* Orange rainbow */
			colors['+']  = "\033[1;33;43m";  /* Yellow Rainbow */
			colors['#']  = "\033[1;32;42m";  /* Green rainbow */
			colors['=']  = "\033[1;34;44m";  /* Light blue rainbow */
			colors[';']  = "\033[0;34;44m";  /* Dark blue rainbow */
			colors['*']  = "\033[1;30;40m";  /* Gray cat face */
			colors['%']  = "\033[1;35;45m";  /* Pink cheeks */
			output = "██";
			break;
		case 5:
			colors[',']  = "\033[0;34;44m";  /* Blue background */
			colors['.']  = "\033[1;37;47m";  /* White stars */
			colors['\''] = "\033[0;30;40m";  /* Black border */
			colors['@']  = "\033[1;37;47m";  /* Tan poptart */
			colors['$']  = "\033[1;35;45m";  /* Pink poptart */
			colors['-']  = "\033[1;31;41m";  /* Red poptart */
			colors['>']  = "\033[1;31;41m";  /* Red rainbow */
			colors['&']  = "\033[0;33;43m";  /* Orange rainbow */
			colors['+']  = "\033[1;33;43m";  /* Yellow Rainbow */
			colors['#']  = "\033[1;32;42m";  /* Green rainbow */
			colors['=']  = "\033[1;34;44m";  /* Light blue rainbow */
			colors[';']  = "\033[0;34;44m";  /* Dark blue rainbow */
			colors['*']  = "\033[1;30;40m";  /* Gray cat face */
			colors['%']  = "\033[1;35;45m";  /* Pink cheeks */
			output = "\333\333";
			break;
		case 6:
			colors[',']  = "::";             /* Blue background */
			colors['.']  = "@@";             /* White stars */
			colors['\''] = "  ";             /* Black border */
			colors['@']  = "##";             /* Tan poptart */
			colors['$']  = "??";             /* Pink poptart */
			colors['-']  = "<>";             /* Red poptart */
			colors['>']  = "##";             /* Red rainbow */
			colors['&']  = "==";             /* Orange rainbow */
			colors['+']  = "--";             /* Yellow Rainbow */
			colors['#']  = "++";             /* Green rainbow */
			colors['=']  = "~~";             /* Light blue rainbow */
			colors[';']  = "$$";             /* Dark blue rainbow */
			colors['*']  = ";;";             /* Gray cat face */
			colors['%']  = "()";             /* Pink cheeks */
			always_escape = 1;
			break;
		case 7:
			colors[',']  = ".";             /* Blue background */
			colors['.']  = "@";             /* White stars */
			colors['\''] = " ";             /* Black border */
			colors['@']  = "#";             /* Tan poptart */
			colors['$']  = "?";             /* Pink poptart */
			colors['-']  = "O";             /* Red poptart */
			colors['>']  = "#";             /* Red rainbow */
			colors['&']  = "=";             /* Orange rainbow */
			colors['+']  = "-";             /* Yellow Rainbow */
			colors['#']  = "+";             /* Green rainbow */
			colors['=']  = "~";             /* Light blue rainbow */
			colors[';']  = "$";             /* Dark blue rainbow */
			colors['*']  = ";";             /* Gray cat face */
			colors['%']  = "o";             /* Pink cheeks */
			always_escape = 1;
			terminal_width = 40;
			break;
		default:
			break;
	}

	if (min_col == max_col) {
		min_col = (FRAME_WIDTH - terminal_width/2) / 2;
		max_col = (FRAME_WIDTH + terminal_width/2) / 2;
		using_automatic_width = 1;
	}

	if (min_row == max_row) {
		min_row = (FRAME_HEIGHT - (terminal_height-1)) / 2;
		max_row = (FRAME_HEIGHT + (terminal_height-1)) / 2;
		using_automatic_height = 1;
	}

	/* Attempt to set terminal title */
	if (set_title) {
		printf("\033kNyanyanyanyanyanyanya...\033\134");
		printf("\033]1;Nyanyanyanyanyanyanya...\007");
		printf("\033]2;Nyanyanyanyanyanyanya...\007");
	}

	if (clear_screen) {
		/* Clear the screen */
		printf("\033[H\033[2J\033[?25l");
	} else {
		printf("\033[s");
	}

	/* Store the start time */
	time_t start, current;
	time(&start);

	int playing = 1;    /* Animation should continue [left here for modifications] */
	size_t i = 0;       /* Current frame # */
	unsigned int f = 0; /* Total frames passed */
	char last = 0;      /* Last color index rendered */
	int y, x;        /* x/y coordinates of what we're drawing */
	while (playing) {
		/* Reset cursor */
		if (clear_screen) {
			printf("\033[H");
		} else {
			printf("\033[u");
		}
		/* Render the frame */
		for (y = min_row; y < max_row; ++y) {
			for (x = min_col; x < max_col; ++x) {
				char color;
				if (y > 23 && y < 43 && x < 0) {
					/*
					 * Generate the rainbow tail.
					 *
					 * This is done with a pretty simplistic square wave.
					 */
					int mod_x = ((-x+2) % 16) / 8;
					if ((i / 2) % 2) {
						mod_x = 1 - mod_x;
					}
					/*
					 * Our rainbow, with some padding.
					 */
					const char *rainbow = ",,>>&&&+++###==;;;,,";
					color = rainbow[mod_x + y-23];
					if (color == 0) color = ',';
				} else if (x < 0 || y < 0 || y >= FRAME_HEIGHT || x >= FRAME_WIDTH) {
					/* Fill all other areas with background */
					color = ',';
				} else {
					/* Otherwise, get the color from the animation frame. */
					color = frames[i][y][x];
				}
				if (always_escape) {
					/* Text mode (or "Always Send Color Escapes") */
					printf("%s", colors[(int)color]);
				} else {
					if (color != last && colors[(int)color]) {
						/* Normal Mode, send escape (because the color changed) */
						last = color;
						printf("%s%s", colors[(int)color], output);
					} else {
						/* Same color, just send the output characters */
						printf("%s", output);
					}
				}
			}
			/* End of row, send newline */
			newline(1);
		}
		if (show_counter) {
			/* Get the current time for the "You have nyaned..." string */
			time(&current);
			double diff = difftime(current, start);
			/* Now count the length of the time difference so we can center */
			int nLen = digits((int)diff);
			/*
			 * 29 = the length of the rest of the string;
			 * XXX: Replace this was actually checking the written bytes from a
			 * call to sprintf or something
			 */
			int width = (terminal_width - 29 - nLen) / 2;
			/* Spit out some spaces so that we're actually centered */
			while (width > 0) {
				printf(" ");
				width--;
			}
			/* You have nyaned for [n] seconds!
			 * The \033[J ensures that the rest of the line has the dark blue
			 * background, and the \033[1;37m ensures that our text is bright white.
			 * The \033[0m prevents the Apple ][ from flipping everything, but
			 * makes the whole nyancat less bright on the vt220
			 */
			printf("\033[1;37mYou have nyaned for %0.0f seconds!\033[J\033[0m", diff);
		}
		/* Reset the last color so that the escape sequences rewrite */
		last = 0;
		/* Update frame count */
		++f;
		if (frame_count != 0 && f == frame_count) {
			finish();
		}
		++i;
		if (!frames[i]) {
			/* Loop animation */
			i = 0;
		}
		/* Wait */
		usleep(90000);
	}
	return 0;
}

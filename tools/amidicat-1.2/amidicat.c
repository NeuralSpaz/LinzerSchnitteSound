/*
 *	aMIDIcat
 *
 *	Copyright 2010 Joshua Lehan
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


/*
 *	After searching for a while, it seems there is no program
 *	to simply send MIDI data into the ALSA MIDI system,
 *	with a minimum of fuss.
 *
 *	The program aplaymidi(1) will play .MID files,
 *	but not "live" MIDI data from standard input.
 *
 *	The program amidi(1) can send live data, but only works for
 *	devices in the ALSA "RawMIDI" namespace, not the overall
 *	MIDI system, so it can't be used to drive software
 *	MIDI synthesizers such as TiMidity.
 *
 *	Creating stub .MID files on-the-fly and rapidly playing them
 *	is a common workaround, but this is undesirable for
 * 	all but the most trivial of uses.
 *
 *	This program attempts to rectify these limitations,
 *	similarly to the purpose of netcat(1).
 *
 *	What's more, this program also works in both directions
 *	simultaneously: MIDI data can also be read out from
 *	the ALSA MIDI system, and made available as standard
 *	output.
 *
 *	This program is an ALSA sequencer client.
 *
 *	Much code was borrowed from vkeybd(1).
 */


#include <pthread.h>
#include <stdio.h>
#include <getopt.h>
#include <alsa/asoundlib.h>
#include <errno.h>
#include <time.h>


/* Constants */
#define DEFAULT_SEQ_NAME	("default")
#define DEFAULT_CLI_NAME	("aMIDIcat")
#define ERR_OPEN		(10)
#define ERR_FINDPORT		(11)
#define ERR_CONNPORT		(12)
#define ERR_PARAM		(13)
#define ERR_THREAD		(14)
#define DEFAULT_BUFSIZE		(65536)
#define MAX_WAIT		(1000)
#define MAX_DELAY		(1000)
#define FILE_STDIN		("-")
#define MAX_HEX_DIGITS		(2)

/* This is here for convenience only, it should be in the Makefile */
#ifndef VERSION_STR
#define VERSION_STR		("1.0")
#endif


/* Globals */
pthread_mutex_t	g_mutex;
pthread_cond_t	g_cond;
int		g_is_endinput;


/* Structures */
struct in_args_t {
	char * *	args_ptr;
	snd_seq_t *	handle;
	int		is_hex;
	int		cli_ID;
	int		port_ID;
	int		target_cli;
	int		target_port;
	int		spacing_us;
	int		wait_sec;
	int		is_read;
};

struct out_args_t {
	snd_seq_t *	handle;
	int		is_hex;
};


/*
 * Better atoi() that returns -1, not 0, if error
 * and checks entire string, not just first part, for validity
 */
int
better_atoi(const char *nptr)
{
	char *	endptr;
	long	num;
	int	ret;
	
	num = strtol(nptr, &endptr, 0);
	if (NULL != nptr && '\0' == *endptr) {
		/* String is valid */
		ret = num;

		/* Guard against size mismatch by comparing int and long value */
		if (ret == num) {
			return ret;
		}
	}

	return -1;
}


/*
 * Better usleep() that does not use signals,
 * so is safe to use along with threads
 */
void
microsleep(long usec)
{
	struct timespec	tv;

	/* No need to sleep if time is not positive */
	if (usec > 0) {
		tv.tv_sec = usec / 1000000;
		tv.tv_nsec = (usec % 1000000) * 1000;

		/* Ignore return value, waking up early is not a problem */
		nanosleep(&tv, NULL);
	}
}


/*
 * Iterates through list of all active ALSA sequencer ports
 * handle = ALSA sequencer handle
 * str = String to search for a match for, or NULL to ignore
 * is_print = Set to 1 if you want to see output
 * outcli = Receives found client ID, if searching
 * outport = Receives found port ID, if searching
 * Returns 0 if a match was found, -1 if no matches were found
 * The search is by exact string match (no wildcards or
 * substrings yet).  The column of port names will be
 * searched first, in top-down order, then the column of
 * client names will be similarly searched.  First match wins.
 */
int
discover_ports(snd_seq_t *handle, char *str, int is_print, int *outcli, int *outport)
{
	snd_seq_client_info_t *	cli_info;
	snd_seq_port_info_t *	port_info;
	char *			cli_name;
	char *			port_name;
	unsigned int		port_capmask;
	unsigned int		port_typemask;
	int			r;
	int			cli_ID;
	int			cli_numPorts;
	int			port_ID;
	int			lowest_port;
	int			match_bycli_cli_ID = -1;
	int			match_bycli_port_ID = -1;
	int			match_byport_cli_ID = -1;
	int			match_byport_port_ID = -1;
	int			is_first = 1;

	snd_seq_client_info_malloc(&cli_info);
	snd_seq_port_info_malloc(&port_info);
	
	/* Iterate through all clients */
	snd_seq_client_info_set_client(cli_info, -1);
	for(;;) {
		r = snd_seq_query_next_client(handle, cli_info);
		if (r < 0) {
			break;
		}
		
		/* Got a client */
		cli_ID = snd_seq_client_info_get_client(cli_info);
		cli_name = strdup(snd_seq_client_info_get_name(cli_info));
		cli_numPorts = snd_seq_client_info_get_num_ports(cli_info);
		
		/* Iterate through all ports on this client */
		snd_seq_port_info_set_client(port_info, cli_ID);
		lowest_port = -1;
		snd_seq_port_info_set_port(port_info, lowest_port);
		for(;;) {
			r = snd_seq_query_next_port(handle, port_info);
			if (r < 0) {
				break;
			}
			
			/* Got a port */
			port_ID = snd_seq_port_info_get_port(port_info);
			port_name = strdup(snd_seq_port_info_get_name(port_info));
			port_typemask = snd_seq_port_info_get_type(port_info);
			port_capmask = snd_seq_port_info_get_capability(port_info);
			
			/* Arbitrarily use lowest port number on this client when matching by name */
			if (-1 == lowest_port) {
				lowest_port = port_ID;
			}
			
			/* Print header */
			if (is_first) {
				/* The field lengths of strings are taken from "aplaymidi -l" */
				if (is_print) {
					printf(" Port    %-32s %-32s RW\n", "Client name", "Port name");
				}
				is_first = 0;
			}
			
			/* Print line */
			/* The R/W letters indicate read/write capability */
			/* Letters are capitalized if also capable of subscription read/write */
			/* FUTURE: Perhaps also print type bits if relevant */
			if (is_print) {
				printf("%3d:%-3d  %-32s %-32s %s%s\n", cli_ID, port_ID,
					cli_name, port_name,
					((port_capmask & SND_SEQ_PORT_CAP_READ) ?
					((port_capmask & SND_SEQ_PORT_CAP_SUBS_READ) ? "R" : "r") : " "),
					((port_capmask & SND_SEQ_PORT_CAP_WRITE) ?
					((port_capmask & SND_SEQ_PORT_CAP_SUBS_WRITE) ? "W" : "w") : " "));
			}

			/* Test for match, if not already matched by port */
			if (NULL != str && -1 == match_byport_cli_ID) {
				if (0 == strcasecmp(str, port_name)) {
					match_byport_cli_ID = cli_ID;
					match_byport_port_ID = port_ID;
				}
			}
						
			free(port_name);
		}
		
		/* Test for match, if not already matched by client */
		if (NULL != str && -1 == match_bycli_cli_ID) {
			if (0 == strcasecmp(str, cli_name)) {
				if (-1 != lowest_port) {
					match_bycli_cli_ID = cli_ID;
					match_bycli_port_ID = lowest_port;
				}
			}
		}

		free(cli_name);
	}

	snd_seq_port_info_free(port_info);
	snd_seq_client_info_free(cli_info);
	
	/* If both matched, prefer port match (most specific) over client match */
	if (-1 != match_byport_cli_ID) {
		*outcli = match_byport_cli_ID;
		*outport = match_byport_port_ID;
		return 0;
	}
	if (-1 != match_bycli_cli_ID) {
		*outcli = match_bycli_cli_ID;
		*outport = match_bycli_port_ID;
		return 0;
	}
	
	/* No match found, or no string given to match by */
	return 1;
}

 
/*
 * Parses string into ALSA client:port numbers
 * String can be 2 numbers separated by ":" or "." or "-" delimiters,
 * or the name of a client or port can be given
 * and a lookup will be done in order to learn the numbers.
 * Results stored in "outcli" and "outport".
 * Returns 0 if successful, or -1 if unparseable or not found.
 */
int
str_to_cli_port(snd_seq_t *handle, char *str, int *outcli, int *outport)
{
	char *	delim;
	char *	nextstr;
	int	cli_ID = -1;
	int	port_ID = -1;
	int	r;
	char	savedch;
	
	/* Fairly generous in choice of delimiters */
	delim = strpbrk(str, ":.-");
	if (delim) {
		/* Look at string immediately following the delimiter */
		nextstr = delim + 1;
		
		/* Perform string fission */
		savedch = *delim;
		*delim = '\0';
		
		cli_ID = better_atoi(str);
		port_ID = better_atoi(nextstr);
		
		*delim = savedch;
	}
	
	/* If not parseable as numbers, try string match */
	if (cli_ID < 0 || port_ID < 0) {
		r = discover_ports(handle, str, 0, &cli_ID, &port_ID);
		if (r < 0) {
			/* Return error, regardless of ID, if discovery failed */
			return -1;
		}
	}
	
	if (cli_ID < 0 || port_ID < 0) {
		/* Not found or not parseable */
		return -1;
	}

	/* Both are good results */	
	*outcli = cli_ID;
	*outport = port_ID;
	return 0;
}


/*
 * Opens a new connection to the ALSA sequencer
 * Can be opened for input (read), output (write), or both
 * Returns handle or NULL if error, prints message if error
 */
snd_seq_t *
seq_open(int is_read, int is_write)
{
	snd_seq_t *	handle;
	int		err;
	int		mode;
	
	/* Default to duplex unless told otherwise */
	mode = SND_SEQ_OPEN_DUPLEX;
	if (is_read && !is_write) {
		/* Read only */
		mode = SND_SEQ_OPEN_INPUT;
	}
	if (is_write && !is_read) {
		/* Write only */
		mode = SND_SEQ_OPEN_OUTPUT;
	}
	
	/* Always use the default "sequencer name" here, this is not the client name */
	err = snd_seq_open(&handle, DEFAULT_SEQ_NAME, mode, 0);
	if (err < 0) {
		fprintf(stderr, "Unable to open ALSA sequencer: %s\n", strerror(errno));
		return NULL;
	}
	return handle;
}


/*
 * Closes the connection to the ALSA sequencer
 */
void
seq_close(snd_seq_t *handle)
{
	snd_seq_close(handle);
}


/*
 * Sets up the ALSA sequencer for use
 * Sets our client name
 * Allocates our port
 * Connects to remote client and port,
 * unless they are -1 then use ALSA subscription mechanism instead
 * Learns our own ID's and saves them in "outcli" and "outport"
 * Returns 0 if all went well, or -1 if error, prints message if error
 */
int
seq_setup(snd_seq_t *handle, char *cli_name, int target_cli, int target_port, int is_read, int is_write, int *outcli, int *outport)
{
	int		cli_ID;
	int		port_ID;
	int		r;
	int		caps;
	int		is_subs = 0;
	
	/* Get our client ID */
	cli_ID = snd_seq_client_id(handle);
	*outcli = cli_ID;
	
	/* Set our name */
	r = snd_seq_set_client_name(handle, cli_name);
	if (r < 0) {
		/* This early in the program, it's not threaded, errno is OK to use */
		fprintf(stderr, "Unable to set ALSA client name: %s\n", strerror(errno));
		return -1;
	}

	/* Enable reading and/or writing */
	caps = 0;
	if (is_read) {
		caps |= SND_SEQ_PORT_CAP_READ;
	}
	if (is_write) {
		caps |= SND_SEQ_PORT_CAP_WRITE;
	}
	if (is_read && is_write) {
		caps |= SND_SEQ_PORT_CAP_DUPLEX;
	}
	if (target_cli < 0 || target_port < 0) {
		/* Use ALSA subscription mechanism if target not given */
		/* FUTURE: Might want both subscription and target at once */
		if (is_read) {
			caps |= SND_SEQ_PORT_CAP_SUBS_READ;
		}
		if (is_write) {
			caps |= SND_SEQ_PORT_CAP_SUBS_WRITE;
		}
		/* There is no corresponding SUBS_DUPLEX flag */
		is_subs = 1;
	}
	
	/* Open origin port */
	/* FUTURE: Do we need any more type bits here? */
	port_ID = snd_seq_create_simple_port(handle, cli_name, caps,
		SND_SEQ_PORT_TYPE_MIDI_GENERIC);
	if (port_ID < 0) {
		fprintf(stderr, "Unable to open ALSA sequencer port: %s\n", strerror(errno));
		return -1;
	}		

	/* Connect both to and from target port, if not using subscription */
	if (!is_subs) {
		if (is_write) {
			r = snd_seq_connect_to(handle, port_ID, target_cli, target_port);
			if (r < 0) {
				fprintf(stderr, "Unable to connect to ALSA port %d:%d: %s\n", target_cli, target_port, strerror(errno));
				return -1;
			}
		}
		if (is_read) {
			r = snd_seq_connect_from(handle, port_ID, target_cli, target_port);
			if (r < 0) {
				fprintf(stderr, "Unable to connect from ALSA port %d:%d: %s\n", target_cli, target_port, strerror(errno));
				return -1;
			}
		}
	}
	
	/* Should be all good to go now */
	*outcli = cli_ID;
	*outport = port_ID;
	return 0;
}


/*
 * Writes an event into ALSA
 * Blocks/retries until ALSA has received and delivered the event (as best as we can verify)
 * Tries its best to avoid flooding the ALSA input queue
 * ev = The event to be sent into ALSA
 * port_ID = Our port ID
 * target_cli, target_port = The target's client and port ID,
 * or -1 to just send to "subscribers"
 * spacing_us = Spacing time, in microseconds,
 * to be used when busywaiting some loops
 * Returns negative error code if error,
 * or the number of retries required if successful
 */
int
write_event(snd_seq_t *handle, snd_seq_event_t *ev, int port_ID, int target_cli, int target_port, int spacing_us)
{
	int	r;
	int	ct_output = 0;
	int	ct_drain = 0;
	int	ct_sync = 0;
	int	is_draingood;
	int	is_syncgood;
	int	total;
	
	/* Fill in event data structure, these are macros and never fail */
	snd_seq_ev_set_source(ev, port_ID);
	if (target_cli < 0 || target_port < 0) {
		/* Send to all subscribers, possibly playing to an empty house */
		snd_seq_ev_set_subs(ev);
	}
	else {
		snd_seq_ev_set_dest(ev, target_cli, target_port);
	}
	snd_seq_ev_set_direct(ev);

	/* Fire event */
	for(;;) {
		/* FUTURE: Maybe have option to let user choose between output and output_direct? */
		r = snd_seq_event_output_direct(handle, ev);
		if (r < 0) {
			/* Return if a real error happened */
			if (-EAGAIN != r) {
				return r;
			}
			
			/* Error was "Try again", wait and do just that */
			microsleep(spacing_us);
			ct_output ++;
			continue;
		}

		/* Event sent into ALSA, do NOT try again */
		break;
	}
	
	/* Loop until output is fully pushed into ALSA */
	/* FUTURE: Even though this loop works, it's still too easy to flood the other end and overrun, perhaps a queuing bug internally within ALSA? */
	for(;;) {
		is_draingood = 0;
		is_syncgood = 0;
		
		r = snd_seq_drain_output(handle);
		if (r < 0) {
			/* Return if a real error happened */
			if (-EAGAIN != r) {
				return r;
			}
		}
		
		/* Stop retrying only when there no more events left to be drained */
		if (0 == r) {
			is_draingood = 1;
		}
		else {
			ct_drain ++;
		}
		
		r = snd_seq_sync_output_queue(handle);
		if (r < 0) {
			/* Return if a real error happened */
			if (-EAGAIN != r) {
				return r;
			}
		}

		/* FUTURE: Why does snd_seq_sync_output_queue() always return 1, not 0 as it should? */
		if (0 == r || 1 == r) {
			is_syncgood = 1;
		}
		else {
			ct_sync ++;
		}
		
		/* Only return if both drain and sync are clear */
		if (is_draingood && is_syncgood) {
			break;
		}
		
		/* Throttle CPU when busywaiting */
		microsleep(spacing_us);
	}

	total = ct_output + ct_drain + ct_sync;
	/* FUTURE: Suppress this text if verbose flag was given (it becomes redundant) */
	if (total > 0) {
		fprintf(stderr, "Incoming congestion");
		if (ct_output > 0) {
			fprintf(stderr, ", %d output retries", ct_output);
		}
		if (ct_drain > 0) {
			fprintf(stderr, ", %d drain retries", ct_drain);
		}
		if (ct_sync > 0) {
			fprintf(stderr, ", %d sync retries", ct_sync);
		}
		fprintf(stderr, "\n");
	}
	
	return total;
}
 

/*
 * Writes a buffer to stdout
 * Returns 0 if successful or nonzero if error
 * Writes either as binary bytes or hex digits
 */
int
write_stdout(unsigned char *buf, long bufsize, int is_hex)
{
	unsigned char *		bufptr;
	long			size_left;
	long			size_written;
	unsigned int		ui;
	int			r;
	unsigned char		uc;

	bufptr = buf;
	size_left = bufsize;
	if (is_hex) {
		/* Print hex bytes, e.g. 90 3C 7F */
		while(size_left > 0) {
			uc = *bufptr;
			ui = uc;
			
			/* Separate by spaces, unless it's the last one which gets newline */
			r = printf("%02X%s", ui, ((size_left > 1) ? " " : "\n"));
			if (r < 0) {
				return -1;
			}

			bufptr ++;
			size_left --;
		}
	}
	else {
		/* Full write to stdout */
		while(size_left > 0) {
			size_written = write(STDOUT_FILENO, bufptr, size_left);
			if (size_written < 0) {
				return -1;
			}
				
			bufptr += size_written;
			size_left -= size_written;
		}
	}
	
	return 0;
}


/*
 * Parses an incoming unsigned byte of ASCII text, representing a
 * hex digit, eventually building up and returning complete hex numbers
 * Uses static variables to keep state across calls
 * Hex digits are separated by whitespace, or if enough hex digits
 * have been read and maximum size has been reached, no separator
 * is necessary (so digits can all be ran together)
 * If not whitespace, each byte of text must be in range [0-9A-Fa-f]
 * Returns unsigned hex number if successful
 * Returns -1 if error (unrecognized byte of ASCII text)
 * Returns -2 if the complete hex number is not available yet (still good)
 * As a special case, pass in a value of -2 when at an input boundary,
 * this will reset the state and return the hex number that was in
 * progress (if any)
 */
int
parse_hex_byte(int ch)
{
	static int	hex_value = 0;
	static int	read_nybbles = 0;

	int		ret;
	int		nybble;
		
	/* Parse human-readable digit into nybble value */
	nybble = -1;
	if (ch >= '0' && ch <= '9') {
		nybble = ch - '0';
	}
	if (ch >= 'A' && ch <= 'F') {
		nybble = 10 + (ch - 'A');
	}
	if (ch >= 'a' && ch <= 'f') {
		nybble = 10 + (ch - 'a');
	}

	if (-1 == nybble) {
		switch(ch) {
			/* Special case accept -2 as a zero-length reset request */
			case -2:
				/* Fall through */
				
			/* Standard C whitespace */
			/* Avoid usage of isspace() because that would introduce locale variations */
			case ' ':
			case '\f':
			case '\n':
			case '\r':
			case '\t':
			case '\v':
				/* Clear state, and return -2 if there was nothing to begin with */
				ret = -2;
				if (read_nybbles > 0) {
					ret = hex_value;
				}
				hex_value = 0;
				read_nybbles = 0;
				return ret;

			/* No default */
		}
		
		/* Unrecognized character */
		return -1;
	}

	/* Digit is valid, build up a number with it */
	hex_value <<= 4;
	hex_value += nybble;
	read_nybbles ++;
	
	/* Force number to be finished, if maximum digit count reached */
	if (read_nybbles >= MAX_HEX_DIGITS) {
		ret = hex_value;
		hex_value = 0;
		read_nybbles = 0;
		return ret;
	}

	/* Successfully stored digit, but nothing to return yet */
	return -2;
}


/*
 * Reads a byte of input from various sources
 * Fills in bytePtr with the byte that was read
 * Uses static variables to keep state across calls
 * Pass in the argc array from the command line,
 * after all options have been removed.
 * Each string in the array should be a filename, and will
 * be opened and read from.
 * The filename "-" is special, and means standard input.
 * Passing in a pointer that is valid but points to NULL,
 * indicating an empty command line,
 * is also special, and means standard input.
 * If is_hex is true, will read human-readable hex digits,
 * assemble them into bytes, and return the bytes.
 * Returns 1 if successful, -1 if error,
 * or 0 if clean EOF after finishing all files.
 */
int
input_byte(char** args_ptr, int is_hex, char *bytePtr)
{
	/* Static variables for holding state */
	static char **	args_iter = NULL;
	static char *	file_name = NULL;
	static int	file_fd = -1;
	static int	need_open = 0;
	static int	is_inited = 0;
	static int	is_delayedEOF = 0;

	unsigned char	byteBuf;
	int		ret;
	int		val;
	int		r;

	/* Only do initialization once */
	if (!is_inited) {
		args_iter = args_ptr;
		need_open = 1;
		
		is_inited = 1;
	}

	/* Keep looping around, opening next files as necessary, until we have something to return */
	for(;;) {
		/* This gets set if previous file reached EOF, or during init */
		if (need_open) {
			/* Open next file pointed to */
			file_name = *args_iter;
			if (NULL != file_name) {
				/* Filename given */
				if (0 == strcmp(FILE_STDIN, file_name)) {
					/* Special case filename "-" is stdin */
					file_fd = STDIN_FILENO;
				}
				else {
					/* Open the given filename */
					if (-1 != file_fd) {
						fprintf(stderr, "Internal error, failed to close previous file\n");
						return -1;
					}
					file_fd = open(file_name, O_RDONLY);
					if (-1 == file_fd) {
						/* FUTURE: Should it auto-advance to next file, instead of erroring out? */
						fprintf(stderr, "Unable to open file %s: %s\n", file_name, strerror(errno));
						return file_fd;
					}
				}
			}
			else {
				/* No files at all on command line, special case read from stdin */
				file_name = "standard input";
				file_fd = STDIN_FILENO;
			}
			
			/* Successfully at beginning of next file */
			if (is_hex) {
				/* Init hex state, better already be empty from previous file */
				val = parse_hex_byte(-2);
				if (-2 != val) {
					fprintf(stderr, "Internal error, failed to clear hex state across files\n");
					return -1;
				}
			}
			need_open = 0;
		}

		/* Should have a valid file_fd at this point */
		if (is_delayedEOF) {
			/* No new reading of data during this pass, just held over EOF from last time */
			ret = 0;
			is_delayedEOF = 0;
		}
		else {
			/* Read a byte of raw input (might be a hex digit) */
			ret = read(file_fd, &byteBuf, 1);
		}
		
		if (-1 == ret) {
			/* Ignore harmless errors and retry */
			if (EINTR == errno || EAGAIN == errno) {
				continue;
			}
			
			fprintf(stderr, "Problem reading from file %s: %s\n", file_name, strerror(errno));
			return ret;
		}
		
		/* Advance to next file, if EOF detected in this file */
		if (0 == ret) {
			/* The file might have ended in the middle of a hex digit */
			if (is_hex) {
				val = parse_hex_byte(-2);
				if (-2 != val) {
					/* Poor man's coroutine: delay EOF by one pass, insert this final byte */
					is_delayedEOF = 1;

					/* Return final byte */					
					*bytePtr = (char)val;
					return 1;
				}
			}

			if (-1 != file_fd) {
				/* Don't close stdin, we may need it again if user gives "-" more than once on command line */
				if (file_fd != STDIN_FILENO) {
					r = close(file_fd);
					if (0 != r) {
						/* This error is nonfatal, don't return here */
						fprintf(stderr, "Problem closing file %s: %s\n", file_name, strerror(errno));
					}
				}
				file_fd = -1;
			}

			/* Take another look at command line */
			file_name = *args_iter;
			if (NULL != file_name) {
				/* Advance to next file in sequence */
				args_iter ++;
				
				file_name = *args_iter;
				if (NULL != file_name) {
					/* Next file will be opened after we loop around */
					need_open = 1;
					continue;
				}
				else {
					/* Finished with all files on command line */
					return 0;
				}
			}
			else {
				/* No files at all on command line, EOF means end of stdin */
				return 0;
			}
		}

		/* Successfully read a byte of data */
		if (1 == ret) {
			/* Piece hex number together */
			if (is_hex) {
				val = parse_hex_byte(byteBuf);
				if (-2 == val) {
					/* Successfully read a byte, but hex number not complete yet */
					continue;
				}
				if (-1 == val) {
					fprintf(stderr, "Unrecognizable hex digit text in file %s: %c (%d)\n", file_name, (int)byteBuf, (int)byteBuf);
					return -1;
				}
				
				/* Successfully assembled hex number */
				*bytePtr = (char)val;
				return ret;
			}
			
			/* Hex not used, return byte exactly as it was read */
			*bytePtr = (char)byteBuf;
			return ret;
		}

		/* Should never get here */
		break;
	}
	
	/* Should never get here */
	fprintf(stderr, "Internal error in file reading: %d\n", ret);
	return -1;
}


void *
stdin_loop(void *args)
{
	struct in_args_t *	in_args;
	char * *		args_ptr;
	snd_midi_event_t *	parser;
	snd_seq_t *		handle;
	snd_seq_event_t		ev;
	long			ct_bytes = 0;
	int			cli_ID;
	int			port_ID;
	int			target_cli;
	int			target_port;
	int			spacing_us;
	int			wait_sec;
	int			is_read;
	int			is_hex;
	int			r;
	int			i;
	int			is_active = 1;
	int			ct_events = 0;
	int			ct_congested = 0;
	char			bytein;

	/* Recover arguments */
	in_args = (struct in_args_t *)args;
	args_ptr    = in_args->args_ptr;
	is_hex      = in_args->is_hex;
	handle      = in_args->handle;
	cli_ID      = in_args->cli_ID;
	port_ID     = in_args->port_ID;
	target_cli  = in_args->target_cli;
	target_port = in_args->target_port;
	spacing_us  = in_args->spacing_us;
	wait_sec    = in_args->wait_sec;
	is_read     = in_args->is_read;
		
	snd_midi_event_new(DEFAULT_BUFSIZE, &parser);
	snd_midi_event_init(parser);
	snd_midi_event_reset_decode(parser);

	snd_midi_event_no_status(parser, 1);
	
	/* Reset event */
	snd_seq_ev_clear(&ev);

	/* FUTURE: Might need to maintain our own buffer, to overcome ALSA SysEx size limitation */
	while(is_active) {
		/* Read from input, either stdin or files */
		r = input_byte(args_ptr, is_hex, &bytein);
		switch(r) {
			case 0:		/* Clean EOF */
				is_active = 0;
				break;
			
			case 1:		/* Byte received */
				/* No action necessary */
				ct_bytes ++;
				break;
				
			default:
				/* Error messages already printed by input_byte() */
				is_active = 0;
				break;
		}
		
		/* Feed byte into parser, as int */
		i = bytein;
		r = snd_midi_event_encode_byte(parser, i, &ev);
		switch(r) {
			case 0:		/* More bytes needed for event */
				/* No action necessary */
				break;
				
			case 1:		/* Message complete */
				/* Send completed event into ALSA */
				r = write_event(handle, &ev, port_ID, target_cli, target_port, spacing_us);
				if (r < 0) {
					fprintf(stderr, "Event write error: %s\n", strerror(-r));
					is_active = 0;
					break;
				}
				if (r > 0) {
					/* The return value was the number of retries */
					ct_congested ++;
				}

				/* Reset event after write */
				snd_seq_ev_clear(&ev);
				ct_events ++;
				
				/* Wait for spacing between events, if desired */
				microsleep(spacing_us);
				break;
				
			default:	/* Error */
				fprintf(stderr, "Internal error, from ALSA event encode byte: %s\n", strerror(-r));
				is_active = 0;
				break;
		}
	}	

	/* Input finished */
	/* FUTURE: Perhaps an option to suppress this */
	fprintf(stderr, "Input total: %d MIDI messages, %ld bytes", ct_events, ct_bytes);
	if (ct_congested > 0) {
		fprintf(stderr, ", %d events congested", ct_congested);
	}
	fprintf(stderr, "\n");

	/* Give some time for output-only if the user desires */
	microsleep(1000000 * wait_sec);

	/* Set global variable, under mutex, so other thread sees it */
	pthread_mutex_lock(&g_mutex);
	g_is_endinput = 1;
	pthread_mutex_unlock(&g_mutex);
	
	/* Tell the reader thread that we are done */
	if (is_read) {
		/* Send a dummy message to ourself, so ALSA gets unblocked in other thread */
		snd_seq_ev_clear(&ev);
		r = write_event(handle, &ev, port_ID, cli_ID, port_ID, spacing_us);
		if (r < 0) {
			fprintf(stderr, "Final event write error: %s\n", strerror(-r));
			/* FUTURE: Indicate error result here */
		}
	}
	
	snd_midi_event_free(parser);
	
	/* FUTURE: Perhaps bubble up an error result */
	return NULL;
}


void *
stdout_loop(void *args)
{
	struct out_args_t *	out_args;
	unsigned char *		buffer;
	snd_seq_t *		handle;
	snd_midi_event_t *	parser;
	snd_seq_event_t *	evptr;
	long			size_ev;
	long			ct_bytes = 0;
	int			is_hex;
	int			r;
	int			is_active = 1;
	int			ct_overruns = 0;
	int			ct_events = 0;
	int			ct_nonevents = 0;
	int			is_endinput;

	/* Recover arguments */
	out_args = (struct out_args_t *)args;
	handle = out_args->handle;
	is_hex = out_args->is_hex;
	
	snd_midi_event_new(DEFAULT_BUFSIZE, &parser);
	snd_midi_event_init(parser);
	snd_midi_event_reset_decode(parser);

	snd_midi_event_no_status(parser, 1);

	/* Allocate buffer */
	buffer = malloc(DEFAULT_BUFSIZE);
			
	while(is_active)
	{
		/* ALSA will set this pointer to somewhere within itself, if successful */
		/* FUTURE: This is not threadsafe, but since this is the only thread that does ALSA event input, hopefully it's OK for now */
		evptr = NULL;

		/* BLOCK until event comes in from ALSA */
		r = snd_seq_event_input(handle, &evptr);
		if (r < 0) {
			/* ENOSPC indicates that ALSA's internal buffer overran and we lost some events */
			if (-ENOSPC == r) {
				/* FUTURE: Only show this if verbose is turned on */
				fprintf(stderr, "Reported overrun while reading from ALSA to output\n");
				ct_overruns ++;
				continue;
			}
			else {
				fprintf(stderr, "Error reading event from ALSA to output: %s\n", strerror(-r));
				is_active = 0;
				break;
			}
		}
		if (NULL == evptr) {
			/* FUTURE: Shouldn't happen, perhaps remove this */
			fprintf(stderr, "Internal error reading event from ALSA\n");
			is_active = 0;
			break;
		}

		/* Check global flag, under lock, see if other thread is telling us to exit */
		pthread_mutex_lock(&g_mutex);
		is_endinput = g_is_endinput;
		pthread_mutex_unlock(&g_mutex);
		if (is_endinput) {
			/* Clean exit */
			is_active = 0;
			break;
		}

		/* Unpack event into bytes */
		size_ev = snd_midi_event_decode(parser, buffer, DEFAULT_BUFSIZE, evptr);
		if (size_ev < 0) {
			/* ENOENT indicates an event that is not a MIDI message, silently skip it */
			if (-ENOENT == size_ev) {
				ct_nonevents ++;
				/* FUTURE: Suppress this with quiet option */
				fprintf(stderr, "Received non-MIDI message\n");
				continue;
			}
			else {
				fprintf(stderr, "Error decoding event from ALSA to output: %s\n", strerror(-size_ev));
				is_active = 0;
				break;
			}
		}

		/* FUTURE: Might need some code here to cat multiple SysEx events together (0xF0 ... 0xF7), to overcome ALSA internal size limit splitting them */
		
		/* Output to stdout */
		if (size_ev > 0) {
			r = write_stdout(buffer, size_ev, is_hex);
			if (r < 0) {
				fprintf(stderr, "Error writing output: %s\n", strerror(errno));
				is_active = 0;
				break;
			}

			ct_bytes += size_ev;
		}
		
		ct_events ++;
	}

	/* This block will only be reached once other thread tells us to exit */
	/* If blocked in ALSA above, a dummy event will need to be faked up, to get ALSA to return */
	/* FUTURE: Perhaps suppress this text with quiet option */
	fprintf(stderr, "Output total: %d MIDI messages, %ld bytes", ct_events, ct_bytes);
	if (ct_nonevents > 0) {
		fprintf(stderr, ", %d non-MIDI events", ct_nonevents);
	}
	if (ct_overruns > 0) {
		fprintf(stderr, ", %d ALSA read overruns", ct_overruns);
	}
	fprintf(stderr, "\n");
		
	free(buffer);
	snd_midi_event_free(parser);
	
	/* FUTURE: Perhaps bubble up an error result */
	return NULL;
}


/* Trivial function */
void
show_version(void)
{
	printf("aMIDIcat version %s\n", VERSION_STR);
	printf("Josh Lehan\n");
	printf("amidicat@krellan.com\n");
}


void
help_screen(char *exename)
{
	/* Put help screen on stdout, not stderr, because help screen replaces normal output */
	printf("Usage: %s\n", exename);
	printf("       [--help] [--version] [--list]\n");
	printf("       [--name STRING]\n");
	printf("       [--port CLIENT:PORT] [--addr CLIENT:PORT]\n");
	printf("       [--hex] [--verbose] [--nowrite] [--noread]\n");
	printf("       [--delay MILLISECONDS] [--wait SECONDS]\n");
	printf("       input files....\n");
	printf("aMIDIcat hooks up standard input and standard output to the ALSA sequencer.\n");
	printf("Like cat(1), this program will concatenate multiple input files together.\n");
	printf("--help    = Show this help screen and exit.\n");
	printf("--version = Show version line and exit.  This version: %s\n", VERSION_STR);
	printf("--list    = Show list of all ALSA sequencer devices and exit:\n");
	printf(" For each usable ALSA client and port, number and name are shown,\n");
	printf(" and flags: r,w = port can be read from or written to directly,\n");
	printf("            R,W = also can use ALSA \"subscription\" to read or write.\n");
	printf("--name    = Sets name of this program's ALSA connection, as shown\n");
	printf("            in --list, default is \"%s\".\n", DEFAULT_CLI_NAME);
	printf("--port    = Makes direct connection to ALSA port, for reading and\n");
	printf("            writing, instead of the default which is just to set up\n");
	printf("            a passive connection for use with ALSA \"subscription\".\n");
	printf(" Syntax is either numeric CLIENT:PORT (example: 128:0), or name of\n");
	printf(" another program's ALSA connection (use --list to see available).\n");
	printf("--addr    = For compatibility, an exact synonym of the --port option.\n");
	printf("--hex     = Change input and output to be human-readable hex\n");
	printf("            digits (example: 90 3C 7F) instead of binary MIDI bytes.\n");
	printf("--verbose = Provide additional, useful, output to standard error.\n");
	printf("--nowrite = Disable writing data to ALSA from standard input.\n");
	printf("            Intended for allowing connection to read-only devices.\n");
	printf("            Input files may not be given when this option is used.\n");
	printf("--noread  = Disable reading data from ALSA for standard output.\n");
	printf("            Intended for allowing connection to write-only devices.\n");
	printf("--delay   = Inserts a delay, in milliseconds, between each MIDI\n");
	printf("            event submitted to ALSA from standard input.\n");
	printf("            Intended for avoiding event loss due to queue congestion.\n");
	printf("--wait    = After all input is finished, continue running program for\n");
	printf("            this amount of time, in seconds.\n");
	printf("            Intended for allowing output to continue after input.\n");
}


int
main(int argc, char **argv)
{
	/* For getopt */
	struct option long_options[] = {
		{ "help",	0, NULL, 'h' },
		{ "list",	0, NULL, 'l' },
		{ "name",	1, NULL, 'n' },
		{ "port",	1, NULL, 'p' },
		{ "addr",	1, NULL, 'a' },
		{ "hex",	0, NULL, 'x' },
		{ "delay",	1, NULL, 'd' },
		{ "wait",	1, NULL, 'w' },
		{ "verbose",	0, NULL, 'v' },
		{ "version",	0, NULL, 'V' },
		{ "nowrite",	0, NULL, 'W' },
		{ "noread",	0, NULL, 'R' },
		{ NULL,   	0, NULL, 0 }
	};
	
	/* For threading */
	struct in_args_t	in_args;
	struct out_args_t	out_args;
	pthread_t		in_thread;
	pthread_t		out_thread;
	int			is_in_started = 0;
	int			is_out_started = 0;
	
	snd_seq_t *	handle = NULL;
	char *		cli_name;
	int		cli_ID;
	int		port_ID;
	int		target_cli = -1;
	int		target_port = -1;
	int		ret;
	int		c;
	int		r;
	int		is_write = 1;
	int		is_read = 1;
	int		is_done = 0;
	int		is_list = 0;
	int		is_hex = 0;
	int		is_verbose = 0;
	int		is_help = 0;
	int		is_version = 0;
	int		spacing_us = 0;
	int		wait_sec = 0;

	/* Initialize defaults */
	cli_name = strdup(DEFAULT_CLI_NAME);
		
	/* Parse options */
	while(!is_done) {
		c = getopt_long(argc, argv, "hln:p:a:xd:w:vVWR", long_options, NULL);
		switch(c) {
			case 'h': /* --help */
				is_help = 1;
				break;
			
			case 'l': /* --list */
				is_list = 1;
				break;
				
			case 'n': /* --name */
				free(cli_name);
				cli_name = strdup(optarg);
				break;
				
			case 'p': /* --port */
			case 'a': /* --addr, exactly the same */
				/* Open ALSA sequencer early, we need it for port lookup */
				if (NULL == handle) {
					handle = seq_open(is_read, is_write);
				}
				if (NULL == handle) {
					/* Abort program if unable to open */
					ret = ERR_OPEN;
					goto cleanup;
				}
				r = str_to_cli_port(handle, optarg, &target_cli, &target_port);
				if (r < 0) {
					/* Abort program if unable to locate target port */
					fprintf(stderr, "Unable to find ALSA sequencer port: %s\n", optarg);
					ret = ERR_FINDPORT;
					goto cleanup;
				}
				break;

			case 'x': /* --hex */
				is_hex = 1;
				break;
			
			case 'd': /* --delay */
				spacing_us = better_atoi(optarg);
				if (spacing_us < 0) {
					fprintf(stderr, "Parameter for --delay must be a positive integer: %s\n", optarg);
					ret = ERR_PARAM;
					goto cleanup;
				}
				if (spacing_us > MAX_DELAY) {
					fprintf(stderr, "Parameter for --delay must be %d or less: %s\n", MAX_DELAY, optarg);
					ret = ERR_PARAM;
					goto cleanup;
				}
				/* Argument is in milliseconds, but stored value is in microseconds */
				spacing_us *= 1000;
				break;
									
			case 'w': /* --wait */
				wait_sec = better_atoi(optarg);
				if (wait_sec < 0) {
					fprintf(stderr, "Parameter for --wait must be a positive integer: %s\n", optarg);
					ret = ERR_PARAM;
					goto cleanup;
				}
				if (wait_sec > MAX_WAIT) {
					fprintf(stderr, "Parameter for --wait must be %d or less: %s\n", MAX_WAIT, optarg);
					ret = ERR_PARAM;
					goto cleanup;
				}
				break;
			
			case 'v': /* --verbose */
				/* FUTURE: Perhaps multiple verbosity levels */
				is_verbose = 1;
				break;
			
			case 'V': /* --version */
				is_version = 1;
				break;
					
			case 'W': /* --nowrite */
				is_write = 0;
				break;
					
			case 'R': /* --noread */
				is_read = 0;
				break;
					
			case -1: /* Clean end of getopt processing */
				is_done = 1;
				break;

			default: /* Error */
				/* Error message already has been printed by getopt */
				ret = ERR_PARAM;
				goto cleanup;
				break;
		}
	}

	/* If both read and write are disabled, there's no point in running */
	if (!is_read && !is_write) {
		fprintf(stderr, "Parameters --noread and --nowrite cannot coexist\n");
		ret = ERR_PARAM;
		goto cleanup;
	}
	
	/* If write to ALSA is disabled, nothing to do with input files */
	if (!is_write) {
		/* Command line must be empty after the options */
		if (NULL != argv[optind]) {
			fprintf(stderr, "Parameter --nowrite must not be used with any input files\n");
			ret = ERR_PARAM;
			goto cleanup;
		}
	}
	
	/* If write to ALSA is disabled, no input, so no waiting after input */
	if (!is_write) {
		if (wait_sec > 0) {
			fprintf(stderr, "Parameters --nowrite and --wait cannot coexist\n");
			ret = ERR_PARAM;
			goto cleanup;
		}
	}

	/* For --help, show help screen and exit successfully */
	if (is_help) {
		help_screen(argv[0]);
		ret = 0;
		goto cleanup;
	}
	
	/* For --version, show version line and exit successfully */
	if (is_version) {
		show_version();
		ret = 0;
		goto cleanup;
	}
	
	/* Open ALSA sequencer, if it is not already open */
	if (NULL == handle) {
		handle = seq_open(is_read, is_write);
	}
	if (NULL == handle) {
		/* Abort program if unable to open */
		ret = ERR_OPEN;
		goto cleanup;
	}

	/* Set up connection */
	r = seq_setup(handle, cli_name, target_cli, target_port, is_read, is_write, &cli_ID, &port_ID);
	if (r < 0) {
		ret = ERR_CONNPORT;
		goto cleanup;
	}
	
	/* For --list, show list of ports and exit successfully */
	if (is_list) {
		discover_ports(handle, NULL, 1, NULL, NULL);
		ret = 0;
		goto cleanup;
	}

	if (is_verbose) {
		fprintf(stderr, "Connected to ALSA sequencer on port %d:%d\n", cli_ID, port_ID);
	}
	
	/* Initialize globals used for thread synchronization */
	pthread_mutex_init(&g_mutex, NULL);
	pthread_cond_init(&g_cond, NULL);
	g_is_endinput = 0;

	/* The output thread reads from ALSA and provides output */
	if (is_read) {
		/* Start output thread first, to avoid input backlog */
		out_args.handle = handle;
		out_args.is_hex = is_hex;
		r = pthread_create(&out_thread, NULL, stdout_loop, &out_args);
		if (r != 0) {
			fprintf(stderr, "Unable to start output thread: %s\n", strerror(errno));
			ret = ERR_THREAD;
			goto threadwait;
		}
		is_out_started = 1;
	}
	
	/* The input thread takes input and writes to ALSA */
	if (is_write) {
		/* Start input thread */
		/* Save pointer to rest of command line, after options */
		in_args.args_ptr    = &(argv[optind]);
		in_args.handle      = handle;
		in_args.is_hex      = is_hex;
		in_args.cli_ID      = cli_ID;
		in_args.port_ID     = port_ID;
		in_args.target_cli  = target_cli;
		in_args.target_port = target_port;
		in_args.spacing_us  = spacing_us;
		in_args.wait_sec    = wait_sec;
		in_args.is_read     = is_read;
		r = pthread_create(&in_thread, NULL, stdin_loop, &in_args);
		if (r != 0) {
			fprintf(stderr, "Unable to start input thread: %s\n", strerror(errno));
			ret = ERR_THREAD;
			goto threadwait;
		}
		is_in_started = 1;
	}
	
	/* Initialization successful, now just wait for threads to exit */
	ret = 0;

threadwait:
	/* Reap threads */
	if (is_in_started) {
		pthread_join(in_thread, NULL);
	}			
	if (is_out_started) {
		pthread_join(out_thread, NULL);
	}
	
cleanup:
	/* Cleanup */
	if (handle != NULL) {
		seq_close(handle);
	}
	free(cli_name);
	
	return ret;
}


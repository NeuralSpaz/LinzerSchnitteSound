/*
    LinzerSchnitte Midi Sound Server - Midi Interface for generating pure tones for controlling LinzerSchnitte
    Copyright (C) 2014  Josh Gardiner

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>
    Based on miniFMsynth by Matthias Nagorni. 	
    
    Complie with
	gcc -lm -lasound -lcurses -o LinzerSchnitteMidi0.7 LinzerSchnitteMidibeta0.7.c 
*/


#include <stdio.h>
#include <stdlib.h>
#include <alsa/asoundlib.h>
#include <math.h>
#include <curses.h>
#include <unistd.h>

snd_seq_t *seq_handle;
snd_pcm_t *playback_handle;
short *buf;
double phi[512], velocity[512], midichannel[512], attack, decay, sustain, release, env_time[512], env_level[512];
int note[512], gate[512], note_active[512];
int rate, poly, gain, buffer_size, freq_start, freq_channel_width;


/*int polyphony, buffersize, outputvolume, firstnotefreq, freqchannelwidth; */

/* TODO Make sample rate, buffer, gain and polyphony set from CL interface*/

snd_seq_t *open_seq() {

    snd_seq_t *seq_handle;

    if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
        fprintf(stderr, "Error opening ALSA sequencer.\n");
        exit(1);
    }
    snd_seq_set_client_name(seq_handle, "LinzerSchnitteMIDI");
    if (snd_seq_create_simple_port(seq_handle, "LinzerSchnitteMIDI",
        SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION) < 0) {
        fprintf(stderr, "Error creating sequencer port.\n");
        exit(1);
    }
    return(seq_handle);
}

snd_pcm_t *open_pcm(char *pcm_name) {

    snd_pcm_t *playback_handle;
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_sw_params_t *sw_params;

    if (snd_pcm_open (&playback_handle, pcm_name, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        fprintf (stderr, "cannot open audio device %s\n", pcm_name);
        exit (1);
    }
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(playback_handle, hw_params);
    snd_pcm_hw_params_set_access(playback_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(playback_handle, hw_params, SND_PCM_FORMAT_S16_LE);

    snd_pcm_hw_params_set_rate_near(playback_handle, hw_params, &rate, 0);

    snd_pcm_hw_params_set_channels(playback_handle, hw_params, 2);
    snd_pcm_hw_params_set_periods(playback_handle, hw_params, 2, 0);
    snd_pcm_hw_params_set_period_size(playback_handle, hw_params, buffer_size, 0);
    snd_pcm_hw_params(playback_handle, hw_params);
    snd_pcm_sw_params_alloca(&sw_params);
    snd_pcm_sw_params_current(playback_handle, sw_params);
    snd_pcm_sw_params_set_avail_min(playback_handle, sw_params, buffer_size);
    snd_pcm_sw_params(playback_handle, sw_params);
    return(playback_handle);
}

double envelope(int *note_active, int gate, double *env_level, double t, double attack, double decay, double sustain, double release) {

    if (gate)  {
        if (t > attack + decay) return(*env_level = sustain);
        if (t > attack) return(*env_level = 1.0 - (1.0 - sustain) * (t - attack) / decay);
        return(*env_level = t / attack);
    } else {
        if (t > release) {
            if (note_active) *note_active = 0;
            return(*env_level = 0);
        }
        return(*env_level * (1.0 - t / release));
    }
}
/* TODO: ADD MIDI PANIC/ALL NOTES OFF */
int midi_callback() {

    snd_seq_event_t *ev;
    int l1;

    do {
        snd_seq_event_input(seq_handle, &ev);
        switch (ev->type) {
            case SND_SEQ_EVENT_NOTEON:
                for (l1 = 0; l1 < poly; l1++) {
                    if (!note_active[l1]) {
                        note[l1] = ev->data.note.note;
			midichannel[l1] = ev->data.note.channel;
			velocity[l1] = ev->data.note.velocity;
			velocity[l1] = velocity[l1] / 127;
			attron(COLOR_PAIR(1));			
			printw("CH %2.0f ", midichannel[l1]+1);
			printw("Note %3d ON ", note[l1]);
			printw("Velocity %3.0f ", velocity[l1]*127);
			printw("Frequency %3.1f Hz \n", ((note[l1]*freq_channel_width)+((128*freq_channel_width*midichannel[l1])+freq_start)) );
			refresh();
			attroff(COLOR_PAIR(1));
                        env_time[l1] = 0;
                        gate[l1] = 1;
                        note_active[l1] = 1;
                        break;
                    }
                }
                break;
            case SND_SEQ_EVENT_NOTEOFF:
                for (l1 = 0; l1 < poly; l1++) {
                    if (gate[l1] && note_active[l1] && (note[l1] == ev->data.note.note)) {
			midichannel[l1] = ev->data.note.channel;
			velocity[l1] = ev->data.note.velocity;
			velocity[l1] = velocity[l1] / 127;			
			printw("CH %2.0f ", midichannel[l1]+1);
			printw("Note %3d OFF", note[l1]);
			printw("Velocity %3.0f ", velocity[l1]*127);
			printw("Frequency %3.1f Hz\n", ((note[l1]*freq_channel_width)+((128*freq_channel_width*midichannel[l1])+freq_start)) );			
			refresh();
                        env_time[l1] = 0;
                        gate[l1] = 0;
                    }
                }
                break;
        }
        snd_seq_free_event(ev);
    } while (snd_seq_event_input_pending(seq_handle, 0) > 0);
    return (0);
}

int playback_callback (snd_pcm_sframes_t nframes) {

    int l1, l2;
    double dphi, freq_note, sound;

    memset(buf, 0, nframes * 4);
    for (l2 = 0; l2 < poly; l2++) {
        if (note_active[l2]) {
            freq_note = (note[l2]*freq_channel_width)+((128*freq_channel_width*midichannel[l2])+freq_start);
            dphi = (M_PI * freq_note * 2) / (rate);
	    for (l1 = 0; l1 < nframes; l1++) {
                phi[l2] += dphi;
                if (phi[l2] > 2.0 * M_PI) {
			phi[l2] -= 2.0 * M_PI;
		}
                sound = gain * sin(phi[l2])* envelope(&note_active[l2], gate[l2], &env_level[l2], env_time[l2], attack, decay, sustain, release);
                env_time[l2] += 1.0 / rate;
                buf[2 * l1] += sound;
                buf[2 * l1 + 1] += sound;
            }
        }
    }
    return snd_pcm_writei (playback_handle, buf, nframes);
}

void do_endwin(void) {endwin();}

int main (int argc, char *argv[]) {

    int nfds, seq_nfds, l1;
    int key;
    key=0;
    char *hwdevice;
    char *Dvalue = NULL;
    char *pvalue = NULL;
    char *vvalue = NULL;
    char *hvalue = NULL;
    char *avalue = NULL;
    char *dvalue = NULL;
    char *gvalue = NULL;
    char *rvalue = NULL;
    char *bvalue = NULL;
    char *mvalue = NULL;
    char *svalue = NULL;
    char *ovalue = NULL;
    char *tvalue = NULL;
    char *wvalue = NULL;
    
    int index;
    int c;
     
    opterr = 0;

    struct pollfd *pfds;

    initscr();				  /* start the curses setup */
    atexit(do_endwin);
    keypad(stdscr, TRUE);
    noecho();
    scrollok(stdscr, TRUE);
    printw("Welcome to LinzerSchnitte\n");
    refresh();
    start_color();	
    init_pair(1, COLOR_RED, COLOR_BLACK); /* end the curses setup */

/* Set default */    
    hwdevice = "hw:0,0,1";    //case D
    attack = 0.001;           //case a
    decay = 0.001;            //case d
    sustain = 1;              //case s
    release = 0.001;          //case o
    poly = 3;                 //case p
    rate = 44100;             //case r
    gain = 1000;	      //case g
    buffer_size = 1024;	      //case b
    freq_start = 300;         //case t
    freq_channel_width = 100; //case w
	
while ((c = getopt (argc, argv, "D:p:v:ha:d:g:r:b:s:o:t:w:")) != -1)
	switch (c)
	{
	case 'D':
		Dvalue = optarg;
		hwdevice = Dvalue;
		break;
	case 'p':
		pvalue = optarg;
		poly = atoi(pvalue);
		break;
	case 'v':
		vvalue = optarg;
		break;
	case 'h':
		printf("Help menu\n");
		return (1);
		break;
	case 'a':
		avalue = optarg;
		attack = atof(avalue);
		break;
	case 'd':
		dvalue = optarg;
		decay = atof(dvalue);
		break;
	case 'g':
		gvalue = optarg;
		gain = atoi(gvalue);
		break;
	case 'r':
		rvalue = optarg;
		rate = atoi(rvalue);
		break;
	case 'b':
		bvalue = optarg;
		buffer_size = atoi(bvalue);
		break;
	case 's':
		svalue = optarg;
		sustain = atof(svalue); 
		break;
	case 'o':
		ovalue = optarg;
		release = atof(ovalue);
		break;
	case 't':
		tvalue = optarg;
		freq_start = atoi(tvalue);
		break;
	case 'w':
		wvalue = optarg;
		freq_channel_width = atoi(wvalue);
		break;
	case '?':
		if (optopt == 'c')
		    fprintf (stderr, "Option -%c requires an value.\n", optopt);
		else if (isprint (optopt))
		    fprintf (stderr, "Unknown option `-%c'.\n", optopt);
		else
		    fprintf (stderr,
		    "Unknown option character `\\x%x'.\n",
		    optopt);
	return 0;
	default:
		abort ();
	}    
    
    printw("Using snd device   = %s \n", hwdevice);
    printw("Using sample rate  = %d Hz\n", rate);
    printw("Using Buffer size  = %5d samples\n", buffer_size);
    printw("Using Attack of    = %3.3f seconds\n", attack);
    printw("Using Decay of     = %3.3f seconds\n", decay);
    printw("Using Sustain of   = %3.3f seconds\n", sustain);
    printw("Using Release of   = %3.3f seconds\n", release);
    printw("Using polyphony of = %5d tones\n", poly);
    printw("Using gain of      = %5d \n", gain);
    printw("Using base freq of = %5d Hz\n", freq_start);
    printw("Using freq step of = %5d Hz\n", freq_channel_width);
    refresh();




 
    buf = (short *) malloc (2 * sizeof (short) * buffer_size);
    playback_handle = open_pcm(hwdevice);
    seq_handle = open_seq();
    seq_nfds = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
    nfds = snd_pcm_poll_descriptors_count (playback_handle);
    pfds = (struct pollfd *)alloca(sizeof(struct pollfd) * (seq_nfds + nfds));
    snd_seq_poll_descriptors(seq_handle, pfds, seq_nfds, POLLIN);
    snd_pcm_poll_descriptors (playback_handle, pfds+seq_nfds, nfds);
    for (l1 = 0; l1 < poly; note_active[l1++] = 0);
    while (1) {
	if (poll (pfds, seq_nfds + nfds, 1000) > 0) {
            for (l1 = 0; l1 < seq_nfds; l1++) {
               if (pfds[l1].revents > 0) midi_callback();
            }
            for (l1 = seq_nfds; l1 < seq_nfds + nfds; l1++) {
                if (pfds[l1].revents > 0) {
                    if (playback_callback(buffer_size) < buffer_size) {
                        fprintf (stderr, "xrun ! increase buffer \n");
                        snd_pcm_prepare(playback_handle);
                    }
                }
            }
        }
    }
    snd_pcm_close (playback_handle);
    snd_seq_close (seq_handle);
    free(buf);
    return (0);
}

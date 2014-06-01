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

*/


#include <stdio.h>
#include <stdlib.h>
#include <alsa/asoundlib.h>
#include <math.h>
#include <curses.h>

#define POLY 127
#define GAIN 1000.0
#define BUFSIZE 1024
#define FREQ_START 300
#define FREQ_CHANNEL_WIDTH 25

snd_seq_t *seq_handle;
snd_pcm_t *playback_handle;
short *buf;
double phi[POLY], velocity[POLY], midichannel[POLY], attack, decay, sustain, release, env_time[POLY], env_level[POLY];
int note[POLY], gate[POLY], note_active[POLY];
unsigned int rate = 192000;
/*int polyphony, buffersize, outputvolume, firstnotefreq, freqchannelwidth; */

/* TODO Make sample rate, buffer, gain and polyphony set from CL interface*/
void connect2MidiThroughPort(snd_seq_t *seq_handle) {
        snd_seq_addr_t sender, dest;
        snd_seq_port_subscribe_t *subs;
        int myID;
        myID=snd_seq_client_id(seq_handle);
        fprintf(stderr,"MyID=%d",myID);  
        sender.client = 14;
        sender.port = 0;
        dest.client = myID;
        dest.port = 0;
        snd_seq_port_subscribe_alloca(&subs);
        snd_seq_port_subscribe_set_sender(subs, &sender);
        snd_seq_port_subscribe_set_dest(subs, &dest);
        snd_seq_port_subscribe_set_queue(subs, 1);
        snd_seq_port_subscribe_set_time_update(subs, 1);
        snd_seq_port_subscribe_set_time_real(subs, 1);
        snd_seq_subscribe_port(seq_handle, subs);
}

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
    snd_pcm_hw_params_set_period_size(playback_handle, hw_params, BUFSIZE, 0);
    snd_pcm_hw_params(playback_handle, hw_params);
    snd_pcm_sw_params_alloca(&sw_params);
    snd_pcm_sw_params_current(playback_handle, sw_params);
    snd_pcm_sw_params_set_avail_min(playback_handle, sw_params, BUFSIZE);
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
                for (l1 = 0; l1 < POLY; l1++) {
                    if (!note_active[l1]) {
                        note[l1] = ev->data.note.note;
			midichannel[l1] = ev->data.note.channel;
			velocity[l1] = ev->data.note.velocity / 127;
			attron(COLOR_PAIR(1));			
			printw("CH %2.0f ", midichannel[l1]+1);
			printw("Note %3d ", note[l1]);
			printw("Velocity %3.0f ", velocity[l1] * 127);
			printw("Frequency %3.1f \n", ((note[l1]*FREQ_CHANNEL_WIDTH)+((128*FREQ_CHANNEL_WIDTH*midichannel[l1])+FREQ_START)) );
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
                for (l1 = 0; l1 < POLY; l1++) {
                    if (gate[l1] && note_active[l1] && (note[l1] == ev->data.note.note)) {
			printw("Note OFF %3d \n", note[l1]);
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
    for (l2 = 0; l2 < POLY; l2++) {
        if (note_active[l2]) {
            freq_note = (note[l2]*FREQ_CHANNEL_WIDTH)+((128*FREQ_CHANNEL_WIDTH*midichannel[l2])+FREQ_START);
            dphi = (M_PI * freq_note * 2) / (rate);
	    for (l1 = 0; l1 < nframes; l1++) {
                phi[l2] += dphi;
                if (phi[l2] > 2.0 * M_PI) {
			phi[l2] -= 2.0 * M_PI;
		}
                sound = GAIN * sin(phi[l2])* envelope(&note_active[l2], gate[l2], &env_level[l2], env_time[l2], attack, decay, sustain, release);
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
    //int key;
    //key=0;
    char *hwdevice;
    struct pollfd *pfds;
/*
    if (argc < 2) {
        fprintf(stderr, "LinzerSchnitteMIDI <hw:0,0,1> <attack> <decay> <sustain> <release>\n"); 
        exit(1);
    }
*/
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
    hwdevice = "hw:0,0,1";
    attack = 0.001;
    decay = 0.001;
    sustain = 1;
    release = 0.001;	
    
    if (argc > 1) {
    	hwdevice = argv[1];
    }

/*    attack = atof(argv[2]);
    decay = atof(argv[3]);
    sustain = atof(argv[4]);
    release = atof(argv[5]);
*/  
    buf = (short *) malloc (2 * sizeof (short) * BUFSIZE);
    playback_handle = open_pcm(hwdevice);
    seq_handle = open_seq();
    seq_nfds = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
    nfds = snd_pcm_poll_descriptors_count (playback_handle);
    pfds = (struct pollfd *)alloca(sizeof(struct pollfd) * (seq_nfds + nfds));
    snd_seq_poll_descriptors(seq_handle, pfds, seq_nfds, POLLIN);
    snd_pcm_poll_descriptors (playback_handle, pfds+seq_nfds, nfds);
    connect2MidiThroughPort(seq_handle);
    for (l1 = 0; l1 < POLY; note_active[l1++] = 0);
    while (1) {
	if (poll (pfds, seq_nfds + nfds, 1000) > 0) {
            for (l1 = 0; l1 < seq_nfds; l1++) {
               if (pfds[l1].revents > 0) midi_callback();
            }
            for (l1 = seq_nfds; l1 < seq_nfds + nfds; l1++) {
                if (pfds[l1].revents > 0) {
                    if (playback_callback(BUFSIZE) < BUFSIZE) {
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


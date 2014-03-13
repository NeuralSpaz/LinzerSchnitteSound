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
	$ gcc -lm -lasound -lcurses -o LinzerSchnitteMidi0.7 LinzerSchnitteMidibeta0.7.c 
*/


#include <stdio.h>
#include <stdlib.h>
#include <alsa/asoundlib.h>
#include <math.h>
#include <ncurses.h>
#include <unistd.h>

#define NOTES 128
#define SAMPLES 4410

snd_seq_t *seq_handle;
snd_pcm_t *playback_handle;
short *buf;
double phi[512], velocity[512], midichannel[512], attack, decay, sustain, release, env_time[512], env_level[512];
int note[512], gate[512], note_active[512];
int rate, poly, gain, buffer_size, freq_start, freq_channel_width, row, col;
WINDOW *my_win, *my_other_win;

int sample[NOTES][SAMPLES];
int sample_offset[NOTES];

generate_samples()
{
    int note_frequency;
    int sample_rate;
    double sample_gain;
    double phase, sound, delta_phase;

    sample_rate = 44100;
    sample_gain = 1000;

    int i;
    int n;
    for (i=0; i<NOTES; i++){
      note_frequency = (i*10)+300;
      delta_phase = (M_PI * note_frequency * 2) / sample_rate ;
      phase = 0;
      for (n=0; n<SAMPLES; n++ ){
        if (phase > 2 * M_PI) {
            phase -= 2 * M_PI;
          }
        sound = sin(phase) * gain;
        sample[i][n]= sound;
        phase += delta_phase;
      }
    }
}

snd_seq_t *open_seq() {

    snd_seq_t *seq_handle;

    if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
	attron(COLOR_PAIR(1));
        printw("\n Error opening ALSA sequencer.\n");
        attroff(COLOR_PAIR(1));
        exit(1);
    }
    snd_seq_set_client_name(seq_handle, "LSMin");
    if (snd_seq_create_simple_port(seq_handle, "LSMin",
        SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION) < 0) {
	attron(COLOR_PAIR(1));
        printw("\n Error creating sequencer port.\n");
	attroff(COLOR_PAIR(1));
        exit(1);
    }
    return(seq_handle);
}

snd_pcm_t *open_pcm(char *pcm_name) {

    snd_pcm_t *playback_handle;
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_sw_params_t *sw_params;

    if (snd_pcm_open (&playback_handle, pcm_name, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
	attron(COLOR_PAIR(1));
	printw("\n Error: cannot open audio device %s\n\n", pcm_name);
        attroff(COLOR_PAIR(1));
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
//			wattrset(my_win, COLOR_PAIR(3));			
//			wprintw(my_win,"CH %2.0f ", midichannel[l1]+1);
//			wprintw(my_win,"Note %3d ON  ", note[l1]);
//			wprintw(my_win,"Velocity %3.0f ", velocity[l1]*127);
//			wprintw(my_win,"Frequency %6.0f Hz \n", ((note[l1]*freq_channel_width)+((128*freq_channel_width*midichannel[l1])+freq_start)) );
//			refresh();
//			wrefresh(my_win);
//			attroff(COLOR_PAIR(1));
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
//			wattrset(my_win, COLOR_PAIR(1));			
//			wprintw(my_win,"CH %2.0f ", midichannel[l1]+1);
//			wprintw(my_win,"Note %3d OFF ", note[l1]);
//			wprintw(my_win,"Velocity %3.0f ", velocity[l1]*127);
//			wprintw(my_win,"Frequency %6.0f Hz\n", ((note[l1]*freq_channel_width)+((128*freq_channel_width*midichannel[l1])+freq_start)) );			
//			refresh();
//			wrefresh(my_win);
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

    int l1, l2, b ,c;
    double dphi, freq_note, sound;

    memset(buf, 0, nframes * 4);
    for (l2 = 0; l2 < poly; l2++) {
        if (note_active[l2]) {
//            freq_note = (note[l2]*freq_channel_width)+((128*freq_channel_width*midichannel[l2])+freq_start);
//            dphi = (M_PI * freq_note * 2) / (rate);
	    b = note[l2];
	    for (l1 = 0; l1 < nframes; l1++) {
//                phi[l2] += dphi;
//                if (phi[l2] > 2.0 * M_PI) {
//			phi[l2] -= 2.0 * M_PI;
//		}
		c = sample_offset[b];
		
                sound = sample[b][c] * envelope(&note_active[l2], gate[l2], &env_level[l2], env_time[l2], attack, decay, sustain, release);
                if (sample_offset[b]<SAMPLES){
		   ++sample_offset[b];
                }
                else {
                   sample_offset[b] = 0;
                }
                env_time[l2] += 1.0 / rate;
                buf[2 * l1] += sound;
                buf[2 * l1 + 1] += sound;
            }
        }
    }
    return snd_pcm_writei (playback_handle, buf, nframes);
}

void do_endwin(void) 
{
    printw("Press any key to end the program...");
    refresh();
    getch();
    endwin();
}

int main (int argc, char *argv[]) {


//    int startx, starty, width, height;
//    starty = 1;	/* Calculating for a center placement */
//    startx = 1;	/* of the window		*/

//    height = 20;
//    width = 20;

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
		printf("Usage: LinzerSchnitteMidi  [-Dadsoprgbtw]\n");
		printf("-D hardware device eg hw:0,1  Default= %s \n", hwdevice);
		printf("-a Attack time in seconds     Default= %3.3f \n", attack);
		printf("-d Decay time in seconds      Default= %3.3f \n", decay);
		printf("-s Sustain level 0-1          Default= %3.3f \n", sustain);
		printf("-o Release time               Default= %3.3f \n", release);
		printf("-p Polyphony                  Default= %d \n", poly);
		printf("-r Sample rate in Hz          Default= %d \n", rate);
		printf("-g Gain level                 Default= %d \n", gain);
		printf("-b Buffer/period size         Default= %d \n", buffer_size);
		printf("-t base frequency             Default= %d \n", freq_start);
		printf("-w Frequency step             Default= %d \n", freq_channel_width);
		return(1);
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

    initscr();				  /* start the curses setup */
    atexit(do_endwin);
    keypad(stdscr, TRUE);
    noecho();
//    scrollok(stdscr, TRUE);
    refresh();
    start_color();
    getmaxyx(stdscr,col,row);	


    my_win = newwin(22, 60, 1, 0);
    my_other_win = newwin(22, 20, 1 , 60);
    refresh();

    init_pair(1, COLOR_RED, COLOR_BLACK); 
    init_pair(2, COLOR_YELLOW, COLOR_CYAN);
    init_pair(3, COLOR_GREEN, COLOR_BLACK);
    init_pair(4, COLOR_WHITE, COLOR_BLACK);
    attron(A_BOLD);

    attron(COLOR_PAIR(2));
    mvprintw(0,(row/2)-40,"                       LinzerSchnitte MIDI Sound Control                        ");
    attroff(COLOR_PAIR(2));
    attroff(A_BOLD);

    scrollok(my_win, TRUE);
    scrollok(my_other_win, FALSE);

    attron(COLOR_PAIR(2));
    wprintw(my_other_win,"Device:  %s \n", hwdevice);
    wprintw(my_other_win,"Rate:    %d Hz\n", rate);
    attron(COLOR_PAIR(2));
    wprintw(my_other_win,"Buffer:  %4d \n", buffer_size);
    wprintw(my_other_win,"Attack:  %3.3f sec\n", attack);
    wprintw(my_other_win,"Decay:   %3.3f sec\n", decay);
    wprintw(my_other_win,"Release: %3.3f sec\n", release);
    wprintw(my_other_win,"Sustain: %3.3f\n", sustain);
    wprintw(my_other_win,"Poly:  %4d tones\n", poly);
    wprintw(my_other_win,"Gain:   %5d \n", gain);
    wprintw(my_other_win,"Base freq:%4d Hz\n", freq_start);
    wprintw(my_other_win,"Freq step:%4d Hz\n", freq_channel_width);
    attroff(COLOR_PAIR(2));
    refresh();
    wrefresh(my_other_win);



    generate_samples();
 
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


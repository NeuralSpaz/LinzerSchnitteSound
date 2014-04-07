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
	$ gcc -lm -lasound -o LinzerSchnitteMidi0.8 LinzerSchnitteMidibeta0.8.c 
*/


#include <stdio.h>
#include <stdlib.h>
#include <alsa/asoundlib.h>
#include <math.h>
#include <unistd.h>

#define NOTES 128
#define SAMPLES 480

snd_seq_t *seq_handle;
snd_pcm_t *playback_handle;
short *buf;
double phi[512], velocity[512], midichannel[512], attack, decay, sustain, release, env_time[512], env_level[512];
int note[512], gate[512], note_active[512];
int rate, poly, gain, buffer_size, freq_start, freq_channel_width, row, col;
int *wavetable;
int debug;

int sample_offset[NOTES];

snd_seq_t *open_seq() {

    snd_seq_t *seq_handle;

    if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
        printf("\n Error opening ALSA sequencer.\n");
        exit(1);
    }
    snd_seq_set_client_name(seq_handle, "LSMidi");
    if (snd_seq_create_simple_port(seq_handle, "LSMidi",
        SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION) < 0) {
        printf("\n Error creating sequencer port.\n");
        exit(1);
    }
    return(seq_handle);
}

int generate_wave_table (int *ptr_wavetable,int sample_rate, int frequency_step){

    double phase, delta_phase;

    int num_samples;
    num_samples= sample_rate / frequency_step;

    delta_phase = (M_PI * frequency_step * 2) / sample_rate ;
    phase = 0;

    int i;
    for (i=0; i<num_samples; i++ ){
      if (phase > 2 * M_PI)
          phase -= 2 * M_PI;
      ptr_wavetable[i] = sin(phase) * gain;
      phase += delta_phase;
	  if (debug==1) {	  printf("Sample %d = %d \n", i, ptr_wavetable[i]);}
    }
    return(0);


}


snd_pcm_t *open_pcm(char *pcm_name) {

    snd_pcm_t *playback_handle;
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_sw_params_t *sw_params;

    if (snd_pcm_open (&playback_handle, pcm_name, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
	printf("\n Error: cannot open audio device %s\n\n", pcm_name);
        exit (1);
    }
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(playback_handle, hw_params);
    snd_pcm_hw_params_set_access(playback_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(playback_handle, hw_params, SND_PCM_FORMAT_S16_LE);

    snd_pcm_hw_params_set_rate_near(playback_handle, hw_params, &rate, 0);

    snd_pcm_hw_params_set_channels(playback_handle, hw_params, 1);
    snd_pcm_hw_params_set_periods(playback_handle, hw_params, 1, 0);
    snd_pcm_hw_params_set_period_size(playback_handle, hw_params, buffer_size, 0);
    snd_pcm_hw_params(playback_handle, hw_params);
    snd_pcm_sw_params_alloca(&sw_params);
    snd_pcm_sw_params_current(playback_handle, sw_params);
    snd_pcm_sw_params_set_avail_min(playback_handle, sw_params, buffer_size);
    snd_pcm_sw_params(playback_handle, sw_params);
    return(playback_handle);
}
// Make faster envelope function
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
			if (debug==1) {			
				printf("CH %2.0f ", midichannel[l1]+1);
				printf("Note %3d ON  ", note[l1]);
				printf("Vel %3.0f ", velocity[l1]*127);
				printf("Frequency %6.0f Hz\n", ((note[l1]*freq_channel_width)+((128*freq_channel_width*midichannel[l1])+freq_start)) );
			}
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
			if (debug==1) {	
				printf("CH %2.0f ", midichannel[l1]+1);
				printf("Note %3d OFF ", note[l1]);
				printf("Vel %3.0f ", velocity[l1]*127);
				printf("Frequency %6.0f Hz\n", ((note[l1]*freq_channel_width)+((128*freq_channel_width*midichannel[l1])+freq_start)) );
			}
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



int playback_callback2 (snd_pcm_sframes_t nframes) {

    int l1, l2, b ,c;
    double dphi, freq_note, sound;
	int delta_position = 0;
    memset(buf, 0, nframes * 4);
	
    for (l2 = 0; l2 < poly; l2++) {
        if (note_active[l2]) {
	    	b = note[l2];
			delta_position = ((b+3)*100);
	    	for (l1 = 0; l1 < nframes; l1++) {
				if (sample_offset[b] > 22049){
					sample_offset[b] -= 22050;
				}
//printf("frame = %d , Sample position = %d \n", l1, sample_position);
                sound = wavetable[sample_offset[b]] * envelope(&note_active[l2], gate[l2], &env_level[l2], env_time[l2], attack, decay, sustain, release);
                env_time[l2] += 1.0 / rate;
				sample_offset[b] += delta_position;
                buf[2 * l1] += sound;
//                buf[2 * l1 + 1] += sound;
            }
        }
    }
    return snd_pcm_writei (playback_handle, buf, nframes);
}













void capture_keyboard(snd_seq_t *seq_handle) {
        snd_seq_addr_t sender, dest;
        snd_seq_port_subscribe_t *subs;
        sender.client = 14;
        sender.port = 0;
        dest.client = 128;
        dest.port = 0;
        snd_seq_port_subscribe_alloca(&subs);
        snd_seq_port_subscribe_set_sender(subs, &sender);
        snd_seq_port_subscribe_set_dest(subs, &dest);
        snd_seq_port_subscribe_set_queue(subs, 1);
        snd_seq_port_subscribe_set_time_update(subs, 1);
        snd_seq_port_subscribe_set_time_real(subs, 1);
        snd_seq_subscribe_port(seq_handle, subs);
}


int main (int argc, char *argv[]) {


	debug = 1;

    int nfds, seq_nfds, l1;

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

// Set default  
// Move to function setdefaults()
    hwdevice = "hw:0";        //case D
    attack = 0.005;           //case a
    decay = 0.005;            //case d
    sustain = 1;              //case s
    release = 0.005;          //case o
    poly = 5;                 //case p
    rate = 22050;             //case r
    gain = 1000;	      //case g
    buffer_size = 256;	      //case b
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
// Move to function printhelp()
		printf("Usage: LinzerSchnitteMidi  [-Dadsoprgbtw]\n");
		printf("-D hardware device eg hw:0,0,1  Default= %s \n", hwdevice);
		printf("-a Attack time in seconds     Default= %3.3f \n", attack);
		printf("-d Decay time in seconds      Default= %3.3f \n", decay);
//		printf("-s Sustain level 0-1          Default= %3.3f \n", sustain);
		printf("-o Release time               Default= %3.3f \n", release);
		printf("-p Polyphony                  Default= %d \n", poly);
		printf("-r Sample rate in Hz          Default= %d \n", rate);
		printf("-g Gain level                 Default= %d \n", gain);
		printf("-b Buffer/period size         Default= %d \n", buffer_size);
//		printf("-t base frequency             Default= %d \n", freq_start);
//		printf("-w Frequency step             Default= %d \n", freq_channel_width);
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
    

    int sample_rate, bit_depth, frequency_step, wavetable_size;

    sample_rate = 22050;
    bit_depth = 16;
    frequency_step = 1;
    
	wavetable_size = sample_rate/frequency_step;
    wavetable = (int *) malloc((wavetable_size)*sizeof(int));
    generate_wave_table(&wavetable[0],sample_rate,frequency_step);

 
    buf = (short *) malloc (sizeof (short) * buffer_size);
//    buf = (short *) malloc (2 * sizeof (short) * buffer_size);
    playback_handle = open_pcm(hwdevice);
    seq_handle = open_seq();
    seq_nfds = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
    nfds = snd_pcm_poll_descriptors_count (playback_handle);
    pfds = (struct pollfd *)alloca(sizeof(struct pollfd) * (seq_nfds + nfds));
    snd_seq_poll_descriptors(seq_handle, pfds, seq_nfds, POLLIN);
    snd_pcm_poll_descriptors (playback_handle, pfds+seq_nfds, nfds);

    capture_keyboard(seq_handle);

    for (l1 = 0; l1 < poly; note_active[l1++] = 0);
    while (1) {
	if (poll (pfds, seq_nfds + nfds, 1000) > 0) {
            for (l1 = 0; l1 < seq_nfds; l1++) {
               if (pfds[l1].revents > 0) midi_callback();
            }
            for (l1 = seq_nfds; l1 < seq_nfds + nfds; l1++) {
                if (pfds[l1].revents > 0) {
                    if (playback_callback2(buffer_size) < buffer_size) {
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
    free(wavetable);
    return (0);
}

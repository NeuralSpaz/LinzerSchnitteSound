/*
    LinzerSchnitte Midi 2 RDS - Midi Interface for generating RDS for Linzer Schnitter
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

    Complie with
	$ gcc -lasound -o midi2rds midi2rds.c
*/


#include <stdio.h>
#include <stdlib.h>
#include <alsa/asoundlib.h>
#include <math.h>
#include <unistd.h>
#include "i2c_bitbang.h"

int cmd03_ontime1, cmd03_ontime2;	//CC102 CC103
int cmd04_delayoff1, cmd04_delayoff2;	//CC104 CC105
int cmd06_tonethreshhold; 		//CC106
int cmd07_hysteresis; 			//CC107
int cmd0b_attacktime, cmd0b_decaytime;	//CC108 CC109
int cmd0d_ontime, cmd0d_offtime; 	//CC110 CC111
int cmd0e_ontimeramp, cmd0e_offtime;	//CC112 CC113
int cmd0f_ontimeseed, cmd0f_offtimeseed;//CC114 CC115
int cmd10_ontimeramp, cmd10_offtime;	//CC116 CC117

int group;

snd_seq_t *seq_handle;


snd_seq_t *open_seq() {

    snd_seq_t *seq_handle;

    if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
        printf("\n Error opening ALSA sequencer.\n");
        exit(1);
    }
    snd_seq_set_client_name(seq_handle, "midi2rds");
    if (snd_seq_create_simple_port(seq_handle, "midi2rds",
        SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION) < 0) {
        printf("\n Error creating sequencer port.\n");
        exit(1);
    }
    return(seq_handle);
}
int note_on_action(int channel, int note, int velocity) {

//    printf("NOTE ON  CH %2d, NOTE %3d, VEL %3d \n",channel,note,velocity);
    return(0);
}

int note_off_action(int channel, int note, int velocity) {

//    printf("NOTE OFF CH %2d, NOTE %3d, VEL %3d \n",channel,note,velocity);
    return(0);
}

int program_change_action(int channel, int value) {

//    printf("PgC CH %2d,VALUE %3d \n",channel,value);
    switch (value){
    	case 3:
	  printf("RDSCMD: %2.2x %4.4x %4.4x\n",value,group,cmd03_ontime2*cmd03_ontime1); 
				LS_CMD( value, group, cmd03_ontime2*cmd03_ontime1);
	  break;  
        case 4:
	  printf("RDSCMD: %2.2x %4.4x %4.4x\n",value,group,cmd04_delayoff2*cmd04_delayoff1); 
			    LS_CMD( value, group, cmd04_delayoff2*cmd04_delayoff1);
	  break;
        case 6:
	  printf("RDSCMD: %2.2x %4.4x %4.4x\n",value,group,cmd06_tonethreshhold); 
				 LS_CMD( value, group, cmd06_tonethreshhold);
	  break;
        case 7:
	  printf("RDSCMD: %2.2x %4.4x %4.4x\n",value,group,cmd07_hysteresis); 
				LS_CMD( value, group, cmd07_hysteresis);
	  break; 
        case 9:
	  printf("RDSCMD: %2.2x %4.4x 0000\n",value,group); 
				LS_CMD( value, group, 0);
	  break; 
        case 10:
	  printf("RDSCMD: %2.2x %4.4x 0000\n",value,group); 
				LS_CMD( value, group, 0);
	  break;
	case 11:
	  printf("RDSCMD: %2.2x %4.4x %2.2x%2.2x\n",value,group,cmd0b_attacktime,cmd0b_decaytime); 
				LS_CMD( value, group, cmd0b_attacktime*0x100+cmd0b_decaytime);
	  break;
        case 12:
	  printf("RDSCMD: %2.2x %4.4x 0000\n",value,group); 
				LS_CMD( value, group, 0);
	  break;
	case 13:
	  printf("RDSCMD: %2.2x %4.4x %2.2x%2.2x\n",value,group,cmd0d_ontime,cmd0d_offtime); //0D 
				LS_CMD( value, group, cmd0d_ontime*0x100+cmd0d_offtime);
	  break; 
	case 14:
	  printf("RDSCMD: %2.2x %4.4x %2.2x%2.2x\n",value,group,cmd0e_ontimeramp,cmd0e_offtime); //0E
				LS_CMD( value, group, cmd0e_ontimeramp*0x100+cmd0e_offtime);
	  break;
	case 15:
	  printf("RDSCMD: %2.2x %4.4x %2.2x%2.2x\n",value,group,cmd0f_ontimeseed,cmd0f_offtimeseed); //0F
				LS_CMD( value, group, cmd0f_ontimeseed*0x100+cmd0f_offtimeseed);
	  break;
	case 16:
	  printf("RDSCMD: %2.2x %4.4x %2.2x%2.2x\n",value,group,cmd10_ontimeramp,cmd10_offtime); //10
				LS_CMD( value, group, cmd10_ontimeramp*0x100+cmd10_offtime);
	  break;
	case 17:
	  printf("RDSCMD: %2.2x %4.4x 0000\n",value,group); //11
				LS_CMD( value, group, 0);
	  break;
    }
    return(0);
}

int control_change_action(int channel, int param, int value) {

//    SET RDS PARAMETERS
    switch (param){
    	case 102: printf("!!RDS SET ON TIME1 FOR CMD 03 %3d           \n",(value));cmd03_ontime1=value; break; 
    	case 103: printf("!!RDS SET ON TIME2 FOR CMD 03 %3d           \n",(value));cmd03_ontime2=value; break; 
        case 104: printf("!!RDS SET DELAY OFF TIME FOR CMD 04 %3dms    \n",(value*10));cmd04_delayoff1=value; break;
        case 105: printf("!!RDS SET DELAY OFF TIME FOR CMD 04 %3dms    \n",(value*10));cmd04_delayoff2=value; break;
        case 106: printf("!!RDS SET TONE THRESHOLD LEVEL CMD 06 %3d    \n",value);cmd06_tonethreshhold=value; break;
        case 107: printf("!!RDS SET TONE HYSTERESIS LEVEL CMD 07 %3d   \n",value);cmd07_hysteresis=value; break;
	case 108: printf("!!RDS SET FADE ON RAMP TIME CMD 0B %3dms     \n",(value*10));cmd0b_attacktime=value; break;
        case 109: printf("!!RDS SET FADE OFF RAMP TIME CMD 0B %3dms    \n",(value*10));cmd0b_decaytime=value; break;
	case 110: printf("!!RDS SET BLINK ON TIME CMD 0D %3dms         \n",(value*10));cmd0d_ontime=value; break;
	case 111: printf("!!RDS SET BLINK OFF TIME CMD 0D %3dms        \n",(value*10));cmd0d_offtime=value; break;
	case 112: printf("!!RDS SET BREATH ON RAMP TIME CMD 0E %3dms   \n",(value*10));cmd0e_ontimeramp=value; break;
	case 113: printf("!!RDS SET BREATH OFF TIME CMD 0E %3dms       \n",(value*10));cmd0e_offtime=value; break;
	case 114: printf("!!RDS SET SPARKLE ON TIME SEED CMD 0F %3dms  \n",(value*10));cmd0f_ontimeseed=value; break;
	case 115: printf("!!RDS SET SPARKLE OFF TIME SEED CMD 0F %3dms \n",(value*10));cmd0f_offtimeseed=value; break;
	case 116: printf("!!RDS SET TWINKLE ON RAMP SEED CMD 10 %3dms  \n",(value*10));cmd10_ontimeramp=value; break;
	case 117: printf("!!RDS SET TWINKLE OFF TIME SEED CMD 10 %3dms \n",(value*10));cmd10_offtime=value; break;
    }
    return(0);
}

int midi_read() {

    snd_seq_event_t *ev;
    int channel,value,note,velocity,param;

    do {
        snd_seq_event_input(seq_handle, &ev);
        switch (ev->type) {
            case SND_SEQ_EVENT_NOTEON:
		channel = ev->data.note.channel;
		note = ev->data.note.note & 0x7f;
		velocity = ev->data.note.velocity;
		note_on_action(channel,note,velocity);
		break;
            case SND_SEQ_EVENT_NOTEOFF:
		channel = ev->data.note.channel;
		note = ev->data.note.note;
		velocity = ev->data.note.velocity;
		note_off_action(channel,note,velocity);
		break;
            case SND_SEQ_EVENT_PGMCHANGE:
		channel = ev->data.control.channel;
		value = ev->data.control.value;
		program_change_action(channel,value);	
		break;
            case SND_SEQ_EVENT_CONTROLLER:
		channel = ev->data.control.channel;
		param = ev->data.control.param;
		value = ev->data.control.value;
		control_change_action(channel,param,value);	
		break;	    
	 
        }
        snd_seq_free_event(ev);
    } while (snd_seq_event_input_pending(seq_handle, 0) > 0);
    return (0);
}

int init_cmd_defaults() {
    cmd03_ontime1=0;
    cmd03_ontime2=0;
    cmd04_delayoff1=0;
    cmd04_delayoff2=0;
    cmd06_tonethreshhold=100;
    cmd07_hysteresis=25;
    cmd0b_attacktime=10;
    cmd0b_decaytime=10;
    cmd0d_ontime=10;
    cmd0d_offtime=10;
    cmd0e_ontimeramp=10;
    cmd0e_offtime=10;
    cmd0f_ontimeseed=100;
    cmd0f_offtimeseed=100;
    cmd10_ontimeramp=100;
    cmd10_offtime=100;
    group=65535;
    return (0);
}

int main () {

    if (gpioSetup() != OK)
    {
        dbgPrint(DBG_INFO, "gpioSetup failed. Exiting\n");
        return 1;
    }


    seq_handle = open_seq();

    init_cmd_defaults();

    while (1) {
	midi_read();
    }

    snd_seq_close (seq_handle);
	gpioCleanup();
    return (0);
}


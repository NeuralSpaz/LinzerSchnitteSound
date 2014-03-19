# LinzerSchnitteMidi

LinzerSchnitteMidi generate pure tones using a MIDI interface for controlling LinzerSchnitte Receivers

For more information on the LinzerSchnitte Project:

[Please see the other LinzerSchnitte Receiver Repos] (https://github.com/LinzerSchnitte)

[Please see the LinzerSchnitte Wiki] (http://www.aec.at/linzerschnitte/wiki/index.php/Main_Page)


## Install from source code


```bash
git clone https://github.com/LinzerSchnitte/linzerschnitte-server.git

cd cd linzerschnitte-server/
```

### Compile / Install

The following packages are required to build LinzerSchnitteSound:

```bash
sudo apt-get install libasound2-dev libncurses5-dev
```

Compile LinzerSchnitteMidi as follows:

```bash
gcc LinzerSchnitteMidibeta0.7.c -o LSMidi -lm -lasound
```
or use 
```bash
make
```


## Run LinzerSchnitteMidi on raspPi

 * ``` $ ./LSMidi -D hw:0,0,1 ```

to run a test use the provided test.midi

 * ``` $ aplaymidi --port LSMidi test.midi ```

You may have to adjust your alsa setting. 

On RaspberryPi using the 3.5mm jack produces a pop between notes,
to fix update firmware to current version. If you are using Raspian
you can do this by running 

* ``` $ sudo rpi-update ```






# jumaudio
### Core dependencies (submoduled in repo):
* [pffft](https://bitbucket.org/jpommier/pffft/src/master/)
* [miniaudio](https://github.com/mackron/miniaudio)

### Dependencies to build example:
* [SDL2](https://www.libsdl.org/)

## Summary
C library that provides a basic interface to play audio files or capture audio from a recording device and produce spectral audio visualizations in realtime. Playback of all audio file formats supported by miniaudio is supported (mp3, flac, WAV, etc.).

# Usage
See simple_example.c for an example implementation. The provided example uses SDL2 to create the window and opengl context and draws some lines based on the visualizer output.

To initialize the library `jum_initAudio` is called, this allocates and sets up a new `jum_AudioSetup` struct, and gets the available playback and capture devices.

Once the audio setup is initialized, playback or capture can be started using `jum_startPlayback` or `jum_startCapture`.

To initialize the visualization capabilities, `jum_initFFT` must be called, this allocates and sets up a new `jum_FFTSetup` struct, using the provided user configuration.

Once initialized and audio is playing/being captured into a buffer, `jum_FFTSetup` and `jum_AudioSetup` structs can be passed to `jum_analyze`. `jum_analyze` also takes a value in milliseconds of time passed since `jum_analyze` was last called so that the visualization effects are independent of framerate. `jum_analyze` stores the histogram result is an array of floats between 0-1 in `jum_AudioSetup.result`.

## Demo
Demo of the audio library in use, integrated into another one of my projects:

https://github.com/HunterWhyte/jumaudio/assets/61810551/3ce1555a-019e-43c2-83a0-ab2930986d78



## Filtering Details
Here is an outline of the steps taken from raw DFT through to the final result, it's more interesting than I expected, and it's fun to watch the visual progression of the output.

### The raw DFT output
In order to create a spectral visualization we must first convert the signal from the time domain into a representation in the frequency domain. The DFT is the basis for most[*](https://github.com/demetri/libquincy) music visualization software. Like in the VLC media player as seen here:


https://user-images.githubusercontent.com/61810551/210050268-edc39e8f-9c56-4715-8eec-7fd7fd39351d.mp4


When computing the DFT for a set of samples the output is a set of values back that represent the frequency components in bins at equally distributed intervals from 0Hz to the Nyquist rate of our audio file.
As a starting point lets try displaying the raw DFT output.
For demonstration purposes we will be using an mp3 file of an exponential sine wave frequency sweep. The raw DFT output looks like this: (each bar is one bin output by the DFT)


https://user-images.githubusercontent.com/61810551/210051732-f05e66f0-c434-4f20-9388-ed1b58ee82b9.mp4


https://user-images.githubusercontent.com/61810551/210050301-275cd6cf-4d92-4ee6-bc7d-af8e68007d86.mp4

There are some clear improvements that can be made.

***
### Windowing
Visually there are some obvious flaws with the previous example, perhaps the most noticeable is the stuttering effect and jagged shape of the frequency peaks. 
This can be improved by modifying how we are inputting samples into the DFT. Currently our ["windowing"](https://en.wikipedia.org/wiki/Window_function) is just taking just a number of samples out of the audio buffer each frame and feeding feeding it to the DFT unmodified i.e. a "rectangular window"\
![](https://upload.wikimedia.org/wikipedia/commons/thumb/6/6a/Window_function_and_frequency_response_-_Rectangular.svg/480px-Window_function_and_frequency_response_-_Rectangular.svg.png)\
Using the rectangular window can create some unwanted effects for our purpose.
An alternative windowing funciton is a [Hamming window](https://en.wikipedia.org/wiki/Hann_function).\
![](https://upload.wikimedia.org/wikipedia/commons/thumb/f/f7/Window_function_and_its_Fourier_transform_%E2%80%93_Hann_%28n_%3D_0...N%29.svg/480px-Window_function_and_its_Fourier_transform_%E2%80%93_Hann_%28n_%3D_0...N%29.svg.png)\
If we multiply our segement of the audio buffer by the windowing function before feeding it in to the FFT, our output now looks like this:

https://user-images.githubusercontent.com/61810551/210051512-259dc34e-ccbf-44dd-b519-e08677e8851c.mp4

A noticeable improvement over the rectangular window.
***
### Averaging into bins
Another visual flaw with our output is that while the test audio may sound like the frequency is increasing at a constant rate, the peak on screen seems to move slowly at the beginning and then speeds up. This is because we are displaying a linear distribution of the frequencies bins, when our perception of pitches is [closer to a log scale](http://www.rctn.org/bruno/psc129/handouts/logs-and-music/logs-and-music.html).

You can see in the previous video, there are barely any bins for most of the lower pitches we can hear, and a lot of bins for higher frequencies that aren't very distinguishable to us.

It makes sense for our audio visualization to have more bins for the lower frequencies and less for higher frequencies. In order to make this configurable to whatever we think looks good, jumaudio takes a 2D array of points for each frequency, and creates a look up table on init for the frequency of each bin in the output. Using this lookup table we take the raw output from the DFT and average it into more favorably distributed bins.

For this example we are using a roughly logarithmic scale from 35Hz to 20kHz, as seen in simple_example.c. We remap the linearly distributed output of samples from the DFT onto whatever distribution we want, in order to better see what is really going on in the music/audio. The output with the frequency remapping looks like this:


https://user-images.githubusercontent.com/61810551/210051540-11308913-ad48-4a12-a41a-04ae187d2eec.mp4


The frequency distribution now matches our exponential sine sweep better. This is especially noticeable once we start playing music through our visualizer, we will be able to see individual pitches being played much clearer.
***
### Weighting
The next thing to tackle is that our perceived loudness is not linearly related to the output of the DFT, and varies across the frequency sepctrum. This is most obvious when playing music through the visualizer.


https://user-images.githubusercontent.com/61810551/210050456-d17d434b-2370-4046-9a37-cf9df620f0e2.mp4


To me, the low bass frequencies seem disproportionally high on the output for how loud they sound in the song. This part is subject to personal taste so jumaudio provides a means of applying arbitary weighting to the spectrum, for simple_example.c I used some values based roughly on [A-weighting](https://en.wikipedia.org/wiki/A-weighting) and tweaked until I was happy with them. The log of the signal is also taken. The output now looks like this:


https://user-images.githubusercontent.com/61810551/210050505-b1ec8927-248e-413b-9aaa-c2510093a3c9.mp4


***
### Time Smoothing
When playing the music in the previous video, you may have noticed how jumpy/noisy the output is, I would prefer a smoother effect. To accomplish this we average out the value of the sample over time with an IIR filter in the form of an exponential moving average ![](https://wikimedia.org/api/rest_v1/media/math/render/svg/bfa44faafef0c46a23b7327359a28bf9a30ffe42). One little trick we use for this is using a different alpha value for when the level is increasing vs. when it is decreasing, this enables us to have a quick attack but slow decay.

https://user-images.githubusercontent.com/61810551/210050559-5b8440b2-b93a-40ab-a216-67c1ff7fc06d.mp4

***
### Averaging across bins
The last thing to clean up is how jagged and sparse some of the bars are. With the DFT output remapped to a logarithmic scale there aren't very many samples for the lower frequencies and there are single output points being shared across multiple bins, this resuluts in the blocky output. The higher frequencies have multiple samples per bin and can be noisy with large single bin spires. To mitigate this we are going to take the average across multiple bins to attempt to smooth everything out. This way the lower frequencies will look less blocky, and the higher frequencies will have less spikes.

https://user-images.githubusercontent.com/61810551/210050584-da12cc1d-cdd7-42f6-aa5e-0d241d58680b.mp4

***
### Final demo


https://user-images.githubusercontent.com/61810551/210050613-d58f5696-904c-4f5a-8c42-6deb81737e93.mp4



https://user-images.githubusercontent.com/61810551/210050621-bac66787-3a8b-462c-9a77-5c2ffd33bb45.mp4

***
Acknowledgement to [this very helpful post using the Processing library minim](https://stackoverflow.com/a/20584591).

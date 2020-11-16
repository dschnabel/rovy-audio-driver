#include "audio.h"

#include <rubberband/rubberband-c.h>
#include <sndfile.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>

static double ratio = 1.0;
static double duration = 0.0;
static double pitchshift = 3.0;
static double frequencyshift = 1.0;
static int realtime = 0;
static int precise = 1;
static int threading = 0;
static int lamination = 0;
static int longwin = 0;
static int shortwin = 1;
static int smoothing = 0;
static int hqpitch = 0;
static int formant = 0;
static int together = 0;
static int crispness = 6;

static RubberBandOptions options;

static SNDFILE *sndfileOut;
static SF_INFO sfinfoOut;

static SF_VIRTUAL_IO vio;

typedef struct sf_user_data {
    float volume;
    size_t current_sample;
    viseme_timing_t *marks;
} sf_user_data_;

void ad_play_raw(char *data, size_t count);
void ad_timing_check(size_t current_sample, viseme_timing_t *t);

//************* virtual local functions ************************
static sf_count_t vfget_filelen (void *user_data) {return 0;}
static sf_count_t vfseek (sf_count_t offset, int whence, void *user_data) {return offset;}
static sf_count_t vftell (void *user_data) {return 0;}
static sf_count_t vfread (void *ptr, sf_count_t count, void *user_data) {return count;}

static sf_count_t vfwrite (const void *ptr, sf_count_t count, void *user_data) {
    const short *buffer = (const short *)ptr;
    size_t new_count = count * 4;
    sf_user_data_ *data = (sf_user_data_ *)user_data;
    short *processed = (short *)malloc(sizeof(short)*new_count);

    data->current_sample += count;
    ad_timing_check(data->current_sample, data->marks);

    const sf_count_t sample_count = count/2;
    int j = 0;
    for (int i = 0; i < sample_count; i++) {
        short sample = buffer[i];

        // volume
        if (data->volume != 1.0) {
            if (data->volume > 1.0) {
                sample += sample * (data->volume - 1.0);
            } else if (data->volume > 0.0) {
                sample -= sample * (1.0 - data->volume);
            }
        }

        // inflate sample rate (+2) to stereo mode (+2)
        for (int a = 0; a < 4; a++) {
            memcpy(&processed[j++], &sample, sizeof(short));
        }
    }

    ad_play_raw((char *)processed, new_count);
    free(processed);

    return count ;
}
//************ /virtual local functions ************************

void ad_init_rubberband() {
    enum {
        NoTransients,
        BandLimitedTransients,
        Transients
    } transients = Transients;

    enum {
        CompoundDetector,
        PercussiveDetector,
        SoftDetector
    } detector = CompoundDetector;

    switch (crispness) {
    case -1: crispness = 5; break;
    case 0: detector = CompoundDetector; transients = NoTransients; lamination = 0; longwin = 1; shortwin = 0; break;
    case 1: detector = SoftDetector; transients = Transients; lamination = 0; longwin = 1; shortwin = 0; break;
    case 2: detector = CompoundDetector; transients = NoTransients; lamination = 0; longwin = 0; shortwin = 0; break;
    case 3: detector = CompoundDetector; transients = NoTransients; lamination = 1; longwin = 0; shortwin = 0; break;
    case 4: detector = CompoundDetector; transients = BandLimitedTransients; lamination = 1; longwin = 0; shortwin = 0; break;
    case 5: detector = CompoundDetector; transients = Transients; lamination = 1; longwin = 0; shortwin = 0; break;
    case 6: detector = CompoundDetector; transients = Transients; lamination = 0; longwin = 0; shortwin = 1; break;
    };

    options = 0;
    if (realtime)    options |= RubberBandOptionProcessRealTime;
    if (precise)     options |= RubberBandOptionStretchPrecise;
    if (!lamination) options |= RubberBandOptionPhaseIndependent;
    if (longwin)     options |= RubberBandOptionWindowLong;
    if (shortwin)    options |= RubberBandOptionWindowShort;
    if (smoothing)   options |= RubberBandOptionSmoothingOn;
    if (formant)     options |= RubberBandOptionFormantPreserved;
    if (hqpitch)     options |= RubberBandOptionPitchHighQuality;
    if (together)    options |= RubberBandOptionChannelsTogether;

    switch (threading) {
    case 0:
        options |= RubberBandOptionThreadingAuto;
        break;
    case 1:
        options |= RubberBandOptionThreadingNever;
        break;
    case 2:
        options |= RubberBandOptionThreadingAlways;
        break;
    }

    switch (transients) {
    case NoTransients:
        options |= RubberBandOptionTransientsSmooth;
        break;
    case BandLimitedTransients:
        options |= RubberBandOptionTransientsMixed;
        break;
    case Transients:
        options |= RubberBandOptionTransientsCrisp;
        break;
    }

    switch (detector) {
    case CompoundDetector:
        options |= RubberBandOptionDetectorCompound;
        break;
    case PercussiveDetector:
        options |= RubberBandOptionDetectorPercussive;
        break;
    case SoftDetector:
        options |= RubberBandOptionDetectorSoft;
        break;
    }

    if (pitchshift != 0.0) {
        frequencyshift *= pow(2.0, pitchshift / 12);
    }

    /* Set up pointers to the locally defined functions. */
    vio.get_filelen = vfget_filelen;
    vio.seek = vfseek;
    vio.read = vfread;
    vio.write = vfwrite;
    vio.tell = vftell;

    memset(&sfinfoOut, 0, sizeof(SF_INFO));

    sfinfoOut.channels = 1;
    sfinfoOut.format = SF_FORMAT_RAW | SF_FORMAT_PCM_16;
    sfinfoOut.samplerate = SAMPLE_RATE;
}

void ad_play_ogg_file_pitched(const char *path, float volume, viseme_timing_t *t, int *stop) {

    SNDFILE *sndfile;
    SF_INFO sfinfo;
    memset(&sfinfo, 0, sizeof(SF_INFO));

    sndfile = sf_open(path, SFM_READ, &sfinfo);
    if (!sndfile) {
        printf("ERROR: Failed to open input file %s: %s\n", path, sf_strerror(sndfile));
        return;
    }

    if (duration != 0.0) {
        double induration = (double)(sfinfo.frames) / (double)(sfinfo.samplerate);
        if (induration != 0.0) ratio = duration / induration;
    }

    sfinfoOut.frames = (int)(sfinfo.frames * ratio + 0.1);
    sfinfoOut.sections = sfinfo.sections;
    sfinfoOut.seekable = sfinfo.seekable;

    sf_user_data_ data;
    data.volume = volume;
    data.current_sample = 0;
    data.marks = t;

    if ((sndfileOut = sf_open_virtual(&vio, SFM_WRITE, &sfinfoOut, &data)) == NULL) {
        printf("sf_open_virtual() error\n");
        return;
    }

    int ibs = 1024;
    size_t channels = sfinfo.channels;

    RubberBandState ts = rubberband_new(sfinfo.samplerate, channels, options, ratio, frequencyshift);
    rubberband_set_expected_input_duration(ts, sfinfo.frames);

    float *fbuf = (float *)malloc(sizeof(float)*channels*ibs);
    float **ibuf = (float **)malloc(sizeof(float*)*channels);
    for (size_t i = 0; i < channels; ++i) ibuf[i] = (float *)malloc(sizeof(float)*ibs);

    int frame = 0;
    int percent = 0;

    //
    // Studying
    //

    sf_seek(sndfile, 0, SEEK_SET);

    while (frame < sfinfo.frames && !*stop) {
        int count = -1;
        if ((count = sf_readf_float(sndfile, fbuf, ibs)) <= 0) break;

        for (size_t c = 0; c < channels; ++c) {
            for (int i = 0; i < count; ++i) {
                float value = fbuf[i * channels + c];
                ibuf[c][i] = value;
            }
        }

        int final = (frame + ibs >= sfinfo.frames);

        rubberband_study(ts, (const float *const *)ibuf, count, final);

        int p = (int)(((double)(frame) * 100.0) / sfinfo.frames);
        if (p > percent || frame == 0) percent = p;

        frame += ibs;
    }

    //
    // Calculating profile and Processing
    //

    sf_seek(sndfile, 0, SEEK_SET);

    frame = 0;
    percent = 0;

    size_t countIn = 0, countOut = 0;

    while (frame < sfinfo.frames && !*stop) {

        int count = -1;
        if ((count = sf_readf_float(sndfile, fbuf, ibs)) < 0) break;
        countIn += count;

        for (size_t c = 0; c < channels; ++c) {
            for (int i = 0; i < count; ++i) {
                float value = fbuf[i * channels + c];
                ibuf[c][i] = value;
            }
        }

        int final = (frame + ibs >= sfinfo.frames);

        rubberband_process(ts, (const float *const *)ibuf, count, final);

        int avail = rubberband_available(ts);
        if (avail > 0) {
            float **obf = (float **)malloc(sizeof(float*)*channels);
            for (size_t i = 0; i < channels; ++i) {
                obf[i] = (float *)malloc(sizeof(float)*avail);
            }
            rubberband_retrieve(ts, obf, avail);
            countOut += avail;
            float *fobf = (float *)malloc(sizeof(float)*channels*avail);
            for (size_t c = 0; c < channels; ++c) {
                for (int i = 0; i < avail; ++i) {
                    float value = obf[c][i];
                    if (value > 1.f) value = 1.f;
                    if (value < -1.f) value = -1.f;
                    fobf[i * channels + c] = value;
                }
            }

            sf_writef_float(sndfileOut, fobf, avail);
            free(fobf);
            for (size_t i = 0; i < channels; ++i) free(obf[i]);
            free(obf);
        }

        int p = (int)(((double)(frame) * 100.0) / sfinfo.frames);
        if (p > percent || frame == 0) {
            percent = p;
        }

        frame += ibs;
    }

    int avail;

    while ((avail = rubberband_available(ts)) >= 0 && !*stop) {
        if (avail > 0) {
            float **obf = (float **)malloc(sizeof(float*)*channels);
            for (size_t i = 0; i < channels; ++i) {
                obf[i] = (float *)malloc(sizeof(float)*avail);
            }
            rubberband_retrieve(ts, obf, avail);
            countOut += avail;
            float *fobf = (float *)malloc(sizeof(float)*channels*avail);
            for (size_t c = 0; c < channels; ++c) {
                for (int i = 0; i < avail; ++i) {
                    float value = obf[c][i];
                    if (value > 1.f) value = 1.f;
                    if (value < -1.f) value = -1.f;
                    fobf[i * channels + c] = value;
                }
            }

            sf_writef_float(sndfileOut, fobf, avail);
            free(fobf);
            for (size_t i = 0; i < channels; ++i) free(obf[i]);
            free(obf);
        } else {
            usleep(10000);
        }
    }

    for (size_t i = 0; i < channels; ++i) free(ibuf[i]);
    free(ibuf);
    free(fbuf);

    sf_close(sndfile);
    sf_close(sndfileOut);

    rubberband_delete(ts);
}

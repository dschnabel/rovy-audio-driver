#include "audio.h"

#include <alsa/asoundlib.h>
#include <mpg123.h>
#include <math.h>

static snd_pcm_t *pcm_handle;

static mpg123_handle *mpg_handle_feed, *mpg_handle_file;
static unsigned char *mpg_buffer;
static size_t mpg_buffer_size;

static pthread_t lipsync_thread;
static pthread_mutex_t access_lock, sync_lock;

static int play_id = 0;
static int stop = 0;

void ad_init_rubberband();
void ad_play_ogg_file_pitched(const char *path, float volume, viseme_timing_t *t, int *stop);

void ad_init() {
    /* asoundlib initializations */
    int err;
    snd_pcm_hw_params_t *params;

    char *device = "plughw:1,0";
    err = snd_pcm_open(&pcm_handle, device, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) printf("ERROR: Can't open \"%s\" PCM device. %s\n", device, snd_strerror(err));

    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm_handle, params);

    err = snd_pcm_hw_params_set_access(pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) printf("ERROR: Can't set interleaved mode. %s\n", snd_strerror(err));

    err = snd_pcm_hw_params_set_format(pcm_handle, params, SND_PCM_FORMAT_S16_LE);
    if (err < 0) printf("ERROR: Can't set format. %s\n", snd_strerror(err));

    err = snd_pcm_hw_params_set_channels(pcm_handle, params, 2);
    if (err < 0) printf("ERROR: Can't set channels number. %s\n", snd_strerror(err));

    int rate = SAMPLE_RATE;
    err = snd_pcm_hw_params_set_rate_near(pcm_handle, params, &rate, 0);
    if (err < 0) printf("ERROR: Can't set rate. %s\n", snd_strerror(err));

    err = snd_pcm_hw_params(pcm_handle, params);
    if (err < 0) printf("ERROR: Can't set harware parameters. %s\n", snd_strerror(err));

    /* mpg123 initializations */
    mpg123_init();

    mpg_handle_feed = mpg123_new(NULL, &err);
    mpg_handle_file = mpg123_new(NULL, &err);

    mpg123_param(mpg_handle_feed, MPG123_FLAGS, MPG123_QUIET, 0);
    mpg123_param(mpg_handle_file, MPG123_FORCE_RATE, SAMPLE_RATE, 0);
    mpg123_param(mpg_handle_file, MPG123_FLAGS, MPG123_FORCE_STEREO, 0);

    mpg_buffer_size = 100000;
    mpg_buffer = (unsigned char*) malloc(mpg_buffer_size * sizeof(unsigned char));

    mpg123_open_feed(mpg_handle_feed);

    if (pthread_mutex_init(&access_lock, NULL) != 0 || pthread_mutex_init(&sync_lock, NULL) != 0) {
        printf("ad_init mutex init failed\n");
    }

    ad_init_rubberband();
}

void ad_destroy() {
    free(mpg_buffer);
    mpg123_close(mpg_handle_feed);
    mpg123_close(mpg_handle_file);
    mpg123_delete(mpg_handle_feed);
    mpg123_delete(mpg_handle_file);
    mpg123_exit();

    pthread_mutex_destroy(&access_lock);
    pthread_mutex_destroy(&sync_lock);
}

void _ad_play_prepare(mpg123_handle *mh) {
    int channels, encoding;
    long rate;
    mpg123_getformat(mh, &rate, &channels, &encoding);

    //printf("rate: %ld, channels: %d, encoding: %d\n", rate, channels, encoding);
    if (rate != SAMPLE_RATE) {
        printf("_ad_play_prepare bad rate (%ld). Should be %d.\n", rate, SAMPLE_RATE);
    }
}

void _ad_timing_cancel(viseme_timing_t *t) {
    if (t && t->next_timing < t->timing_size) {
        pthread_mutex_lock(&t->lock);
        t->next_timing = t->timing_size+1;
        pthread_cond_signal(&t->cond);
        pthread_mutex_unlock(&t->lock);
    }
}

void *_ad_lipsync_thread(void *obj) {
    viseme_timing_t *t = (viseme_timing_t *)obj;
    size_t current_sample = 0;
    int sleepMS = 100;
    int fraction = round((sleepMS / 1000.0) * (double)SAMPLE_RATE);

    while (!stop && t && t->next_timing < t->timing_size) {
        int elapsedMS = ((double)current_sample / (double)SAMPLE_RATE) * 1000;

        if (elapsedMS >= t->timing[t->next_timing]) {
            pthread_mutex_lock(&t->lock);
            while (elapsedMS >= t->timing[t->next_timing]) t->next_timing++;
            pthread_cond_signal(&t->cond);
            pthread_mutex_unlock(&t->lock);
        }

        usleep(sleepMS * 1000);
        current_sample += fraction;
    }

    return NULL;
}

void ad_play_sync_prep(viseme_timing_t *t) {
    snd_pcm_drop(pcm_handle);
    snd_pcm_prepare(pcm_handle);

    pthread_mutex_lock(&sync_lock);
    if (lipsync_thread) {
        pthread_join(lipsync_thread, NULL);
    }
    pthread_create(&lipsync_thread, NULL, _ad_lipsync_thread, t);
    pthread_mutex_unlock(&sync_lock);
}

void ad_play_sync_cleanup() {
    pthread_mutex_lock(&sync_lock);
    if (lipsync_thread) {
        pthread_join(lipsync_thread, NULL);
    }
    lipsync_thread = 0;
    pthread_mutex_unlock(&sync_lock);
}

int ad_wait_ready() {
    stop = 1;
    pthread_mutex_lock(&access_lock);
    int id = ++play_id;
    pthread_mutex_unlock(&access_lock);
    return id;
}

void ad_play_mp3_file(int id, const char *path, float volume, viseme_timing_t *t) {
    static int first_time = 1;
    static float prev_volume = 1.0;

    if (id != play_id) return;
    stop = 1;
    pthread_mutex_lock(&access_lock);
    if (id != play_id) {
        pthread_mutex_unlock(&access_lock);
        return;
    }
    stop = 0;

    mpg123_open(mpg_handle_file, path);

    if (first_time) {
        first_time = 0;
        _ad_play_prepare(mpg_handle_file);
    }
    if (volume != prev_volume) {
        prev_volume = volume;
        mpg123_volume(mpg_handle_file, volume);
    }

    ad_play_sync_prep(t);

    size_t done;
    while (!stop) {
        int c = mpg123_read(mpg_handle_file, mpg_buffer, mpg_buffer_size, &done);
        if (c == MPG123_OK || c == MPG123_DONE) {
            int err = snd_pcm_writei(pcm_handle, mpg_buffer, done / 4);
            if (err < 0) printf("snd_pcm_writei error: %s\n", snd_strerror(err));
        } else {
            printf("ad_play_audio_file error %d\n", c);
            break;
        }

        if (c == MPG123_DONE) {
            break;
        }
    }

    ad_play_sync_cleanup();

    mpg123_close(mpg_handle_file);
    _ad_timing_cancel(t);

    pthread_mutex_unlock(&access_lock);
}

void ad_play_mp3_buffer(int id, const char *buffer, unsigned int size, float volume, viseme_timing_t *t) {
    static int first_time = 1;
    static float prev_volume = 1.0;

    if (id != play_id) return;
    stop = 1;
    pthread_mutex_lock(&access_lock);
    if (id != play_id) {
        pthread_mutex_unlock(&access_lock);
        return;
    }
    stop = 0;

    mpg123_feed(mpg_handle_feed, buffer, size);

    if (first_time) {
        first_time = 0;
        _ad_play_prepare(mpg_handle_feed);
    }
    if (volume != prev_volume) {
        prev_volume = volume;
        mpg123_volume(mpg_handle_feed, volume);
    }

    snd_pcm_drop(pcm_handle);
    snd_pcm_prepare(pcm_handle);

    size_t done;
    while (!stop) {
        int c = mpg123_read(mpg_handle_feed, mpg_buffer, mpg_buffer_size, &done);
        if (c == MPG123_OK || c == MPG123_NEED_MORE) {
            int err = snd_pcm_writei(pcm_handle, mpg_buffer, done / 4);
            if (err < 0) printf("snd_pcm_writei error: %s\n", snd_strerror(err));
        } else {
            printf("ad_play_audio_buffer error %d\n", c);
            break;
        }

        if (c == MPG123_NEED_MORE) {
            break;
        }
    }

    snd_pcm_drain(pcm_handle);

    pthread_mutex_unlock(&access_lock);
}

void ad_play_ogg_file(int id, const char *path, float volume, viseme_timing_t *t) {
    if (id != play_id) return;
    stop = 1;
    pthread_mutex_lock(&access_lock);
    if (id != play_id) {
        pthread_mutex_unlock(&access_lock);
        return;
    }
    stop = 0;

    ad_play_ogg_file_pitched(path, volume, t, &stop);

    _ad_timing_cancel(t);
    pthread_mutex_unlock(&access_lock);
}

void ad_play_raw(char *data, size_t count) {
    int err = snd_pcm_writei(pcm_handle, data, count / 4);
    if (err < 0) printf("snd_pcm_writei error: %s\n", snd_strerror(err));
}

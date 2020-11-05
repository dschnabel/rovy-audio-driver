#include "audio.h"

#include <ao/ao.h>
#include <mpg123.h>
#include <pthread.h>

#define BITS 8

static ao_device *ao_dev;

static mpg123_handle *mpg_handle_feed, *mpg_handle_file;
static unsigned char *mpg_buffer;
static size_t mpg_buffer_size;

static pthread_mutex_t lock;
static int play_id = 0;
static int stop = 0;

void ad_init() {
    /* ao initializations */
    ao_initialize();
    int driver = ao_default_driver_id();

    ao_sample_format format;
    format.bits = mpg123_encsize(208) * BITS;
    format.rate = 48000;
    format.channels = 2;
    format.byte_format = AO_FMT_NATIVE;
    format.matrix = 0;
    ao_dev = ao_open_live(driver, &format, NULL);

    /* mpg123 initializations */
    int err;
    mpg123_init();
    mpg_handle_feed = mpg123_new(NULL, &err);
    mpg_handle_file = mpg123_new(NULL, &err);
    mpg123_param(mpg_handle_feed, MPG123_FLAGS, MPG123_QUIET, 0);
    mpg_buffer_size = 30000; // choosing this value to keep one read/play loop to around 150ms
                             // which is good for faster stopping of audio
    mpg_buffer = (unsigned char*) malloc(mpg_buffer_size * sizeof(unsigned char));

    mpg123_open_feed(mpg_handle_feed);

    if (pthread_mutex_init(&lock, NULL) != 0) {
        printf("ad_init mutex init failed\n");
    }
}

void ad_destroy() {
    free(mpg_buffer);
    mpg123_close(mpg_handle_feed);
    mpg123_close(mpg_handle_file);
    mpg123_delete(mpg_handle_feed);
    mpg123_delete(mpg_handle_file);
    mpg123_exit();

    ao_close(ao_dev);
    ao_shutdown();

    pthread_mutex_destroy(&lock);
}

void _ad_play_prepare(mpg123_handle *mh) {
    int channels, encoding;
    long rate;
    mpg123_getformat(mh, &rate, &channels, &encoding);
    if (rate != 48000) {
        printf("_ad_play_prepare bad rate (%ld). Should be 48000.\n", rate);
    }
}

int ad_wait_ready() {
    stop = 1;
    pthread_mutex_lock(&lock);
    int id = ++play_id;
    pthread_mutex_unlock(&lock);
    return id;
}

void ad_play_audio_file(int id, const char *path, float volume) {
    static int first_time = 1;
    static float prev_volume = 1.0;

    if (id != play_id) return;
    stop = 1;
    pthread_mutex_lock(&lock);
    if (id != play_id) {
        pthread_mutex_unlock(&lock);
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

    size_t done;
    while (!stop) {
        int c = mpg123_read(mpg_handle_file, mpg_buffer, mpg_buffer_size, &done);
        if (c == MPG123_OK || c == MPG123_DONE) {
            ao_play(ao_dev, mpg_buffer, done);
        } else {
            printf("ad_play_audio_file error %d\n", c);
            break;
        }

        if (c == MPG123_DONE) {
            break;
        }
    }

    mpg123_close(mpg_handle_file);

    pthread_mutex_unlock(&lock);
}

void ad_play_audio_buffer(int id, const char *buffer, unsigned int size, float volume) {
    static int first_time = 1;
    static float prev_volume = 1.0;

    if (id != play_id) return;
    stop = 1;
    pthread_mutex_lock(&lock);
    if (id != play_id) {
        pthread_mutex_unlock(&lock);
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

    size_t done;
    while (!stop) {
        int c = mpg123_read(mpg_handle_feed, mpg_buffer, mpg_buffer_size, &done);
        if (c == MPG123_OK || c == MPG123_NEED_MORE) {
            ao_play(ao_dev, mpg_buffer, done);
        } else {
            printf("ad_play_audio_buffer error %d\n", c);
            break;
        }

        if (c == MPG123_NEED_MORE) {
            break;
        }
    }

    pthread_mutex_unlock(&lock);
}

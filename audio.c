#include "audio.h"

#include <ao/ao.h>
#include <mpg123.h>

#define BITS 8

static ao_device *ao_dev;

static mpg123_handle *mpg_handle_feed, *mpg_handle_file;
static unsigned char *mpg_buffer;
static size_t mpg_buffer_size;

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
    mpg_buffer_size = mpg123_outblock(mpg_handle_file);
    mpg_buffer = (unsigned char*) malloc(mpg_buffer_size * sizeof(unsigned char));

    mpg123_open_feed(mpg_handle_feed);
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
}

void _ad_play_prepare(mpg123_handle *mh) {
    int channels, encoding;
    long rate;
    mpg123_getformat(mh, &rate, &channels, &encoding);
}

int ad_play_audio_file(unsigned char *path, float volume) {
    static int first_time = 1;
    static float prev_volume = 1.0;

    mpg123_open(mpg_handle_file, path);

    if (first_time) {
        first_time = 0;
        _ad_play_prepare(mpg_handle_file);
    }
    if (volume != prev_volume) {
        prev_volume = volume;
        mpg123_volume(mpg_handle_file, volume);
    }

    int ret = 0;
    size_t done;
    while (1) {
        int c = mpg123_read(mpg_handle_file, mpg_buffer, mpg_buffer_size, &done);
        if (c == MPG123_OK || c == MPG123_DONE) {
            ao_play(ao_dev, mpg_buffer, done);
        } else {
            ret = -1;
            break;
        }

        if (c == MPG123_DONE) {
            break;
        }
    }

    mpg123_close(mpg_handle_file);
    return ret;
}

int ad_play_audio_buffer(unsigned char *buffer, unsigned int size, float volume) {
    static int first_time = 1;
    static float prev_volume = 1.0;

    mpg123_feed(mpg_handle_feed, buffer, size);

    if (first_time) {
        first_time = 0;
        _ad_play_prepare(mpg_handle_feed);
    }
    if (volume != prev_volume) {
        prev_volume = volume;
        mpg123_volume(mpg_handle_feed, volume);
    }

    int ret = 0;
    size_t done;
    int c = mpg123_read(mpg_handle_feed, mpg_buffer, mpg_buffer_size, &done);
    if (c == MPG123_NEED_MORE) {
        ao_play(ao_dev, mpg_buffer, done);
    } else {
        ret = -1;
    }

    return ret;
}

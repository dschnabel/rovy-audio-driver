#ifndef AUDIO_H_
#define AUDIO_H_

#include <pthread.h>

#ifdef __cplusplus
extern "C"{
#endif

typedef struct viseme_timing {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int next_timing;
    int timing_size;
    int *timing;
} viseme_timing_t;

void ad_init();
void ad_destroy();

int ad_wait_ready();
void ad_play_mp3_file(int id, const char *path, float volume, viseme_timing_t *t);
void ad_play_mp3_buffer(int id, const char *buffer, unsigned int size, float volume, viseme_timing_t *t);
void ad_play_ogg_file_pitched(int id, const char *path, float volume, viseme_timing_t *t);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H_ */

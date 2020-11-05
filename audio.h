#ifndef AUDIO_H_
#define AUDIO_H_

#ifdef __cplusplus
extern "C"{
#endif

void ad_init();
void ad_destroy();

int ad_wait_ready();
void ad_play_audio_file(int id, const char *path, float volume);
void ad_play_audio_buffer(int id, const char *buffer, unsigned int size, float volume);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H_ */

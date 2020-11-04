#ifndef AUDIO_H_
#define AUDIO_H_

#ifdef __cplusplus
extern "C"{
#endif

void ad_init();
void ad_destroy();

int ad_play_audio_file(unsigned char *path, float volume);
int ad_play_audio_buffer(unsigned char *buffer, unsigned int size, float volume);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H_ */

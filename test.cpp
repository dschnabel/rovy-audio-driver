#include "audio.h"

#include "stdio.h"
#include <unistd.h>

int main (int argc, char **argv) {
    ad_init();

    printf("Play file (vol 4.0)\n");
    ad_play_audio_file((unsigned char *)"audio/blink.mp3", 4.0);
    sleep(1);
    printf("Play file (vol 1.0)\n");
    ad_play_audio_file((unsigned char *)"audio/blink.mp3", 1.0);
    sleep(1);
    printf("Play file (vol 0.3)\n");
    ad_play_audio_file((unsigned char *)"audio/blink.mp3", 0.3);
    sleep(1);

    FILE *f;
    unsigned char buffer[4032];
    f = fopen("audio/blink.mp3", "rb");
    if (f) fread(buffer, 4032, 1, f);
    else { printf("error opening file\n"); return -1; }
    fclose(f);

    printf("Play buffer (vol 4.0)\n");
    ad_play_audio_buffer(buffer, 4032, 4.0);
    sleep(1);
    printf("Play buffer (vol 1.0)\n");
    ad_play_audio_buffer(buffer, 4032, 1.0);
    sleep(1);
    printf("Play buffer (vol 0.3)\n");
    ad_play_audio_buffer(buffer, 4032, 0.3);
    sleep(1);

    printf("Play file (vol 1.0)\n");
    ad_play_audio_file((unsigned char *)"audio/blink.mp3", 1.0);
    sleep(1);
    printf("Play buffer (vol 0.3)\n");
    ad_play_audio_buffer(buffer, 4032, 0.3);
    sleep(1);
    printf("Play file (vol 0.3)\n");
    ad_play_audio_file((unsigned char *)"audio/blink.mp3", 0.3);
    sleep(1);
    printf("Play buffer (vol 4.0)\n");
    ad_play_audio_buffer(buffer, 4032, 4.0);

    ad_destroy();
}

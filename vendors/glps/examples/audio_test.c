#include <GLPS/glps_audio_stream.h>
int main()
{
    glps_audio_stream *am = glps_audio_stream_init("default",
                                                   4096, 44100, 2, 16, 4096);
    glps_audio_stream_play(am, "./test.mp3", 44100, 2, 16, 4096);
    sleep(5);
    glps_audio_stream_destroy(am);
    return 0;
}
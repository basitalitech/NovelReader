#pragma once
enum SceAudioOutPortType { SCE_AUDIO_OUT_PORT_TYPE_MAIN };
enum SceAudioOutMode { SCE_AUDIO_OUT_MODE_MONO };
int sceAudioOutOpenPort(SceAudioOutPortType, int, int, SceAudioOutMode);
int sceAudioOutOutput(int, const void*);
int sceAudioOutReleasePort(int);

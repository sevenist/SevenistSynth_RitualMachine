#pragma once
#include "ESP32Synth.h"

#ifdef ARDUINO
int8_t ESP32Synth::setupStream(uint16_t voice, fs::FS &fs, const char* path, uint32_t rootFreqCentiHz, bool loop) {
#else
int8_t ESP32Synth::setupStream(uint16_t voice, const char* path, uint32_t rootFreqCentiHz, bool loop) {
#endif
    if (voice >= MAX_VOICES) return -1;

    // Start SD task on-demand
    if (streamTaskHandle == NULL) {
        xTaskCreatePinnedToCore(sdLoaderTask, "SynthSDTask", 4096, this, 1, &streamTaskHandle, SYNTH_SD_TASK_CORE);
    }

    // Clear existing stream on this voice
    if (voices[voice].streamTrackId >= 0) {
        stopStream(voice);
    }

    // Find free stream slot
    int8_t streamId = -1;
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (!streams[i].active) { streamId = i; break; }
    }
    if (streamId == -1) return -1; // No free stream tracks

    SYNTH_FILE file = SYNTH_STREAM_OPEN();
    if (!SYNTH_FILE_VALID(file)) return -1;

    uint32_t sRate, dPos, dSize;
    uint16_t channels, bits;

    if (!parseWavHeader(file, sRate, dPos, dSize, channels, bits)) {
        SYNTH_FILE_CLOSE(file);
        return -1;
    }
    SYNTH_FILE_SEEK(file, dPos);

    StreamTrack* trk = &streams[streamId];
    *trk = {}; // Clear stream track before use

    trk->file           = file;
    trk->sampleRate     = sRate;
    trk->dataStartPos   = dPos;
    trk->dataSize       = dSize;
    trk->numChannels    = channels;
    trk->bitsPerSample  = bits;
    trk->loop           = loop;
    trk->seekTarget     = -1;
    trk->loopStartBytes = dPos;
    trk->loopEndBytes   = dPos + dSize;
    trk->rootFreqCentiHz = rootFreqCentiHz;
    trk->active         = true;

    Voice* vo           = &voices[voice];
    vo->type            = WAVE_STREAM;
    vo->streamTrackId   = streamId;
    vo->active          = false;
    vo->envState        = ENV_IDLE;
    vo->currEnvVal      = 0;

    return streamId;
}

#ifdef ARDUINO
int8_t ESP32Synth::playStream(uint16_t voice, fs::FS &fs, const char* path, uint16_t volume, uint32_t rootFreqCentiHz, bool loop) {
#else
int8_t ESP32Synth::playStream(uint16_t voice, const char* path, uint16_t volume, uint32_t rootFreqCentiHz, bool loop) {
#endif
    if (voice >= MAX_VOICES) return -1;

#ifdef ARDUINO
    int8_t streamId = setupStream(voice, fs, path, rootFreqCentiHz, loop);
#else
    int8_t streamId = setupStream(voice, path, rootFreqCentiHz, loop);
#endif

    if (streamId < 0) return -1;

    StreamTrack* trk = &streams[streamId];
    trk->playing     = true;

    // Wait briefly for the buffer to pre-fill
    int timeout = 100;
    while (trk->head == trk->tail && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
        timeout--;
    }

    Voice* vo = &voices[voice];
    vo->vol   = volume << _volShift;

    if (vo->freqVal == 0) vo->freqVal = rootFreqCentiHz;
    uint64_t ratio1616 = ((uint64_t)vo->freqVal << 16) / rootFreqCentiHz;
    vo->sampleInc1616  = (uint32_t)((ratio1616 * trk->sampleRate) / _sampleRate);

    // Start ADSR envelope
    if (vo->rateAttack >= ENV_MAX) {
        vo->currEnvVal = ENV_MAX;
        vo->envState   = ENV_DECAY;
    } else {
        vo->currEnvVal = 0;
        vo->envState   = ENV_ATTACK;
    }
    vo->active = true;

    return streamId;
}

void ESP32Synth::pauseStream(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        streams[voices[voice].streamTrackId].playing = false;
    }
}

void ESP32Synth::resumeStream(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        streams[voices[voice].streamTrackId].playing = true;
    }
}

void ESP32Synth::stopStream(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        StreamTrack* trk = &streams[voices[voice].streamTrackId];

        trk->playing = false;
        trk->active  = false;

        SYNTH_FILE_CLOSE(trk->file);
        voices[voice].streamTrackId = -1;
    }
}

void ESP32Synth::seekStreamMs(uint16_t voice, uint32_t ms) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        StreamTrack* trk    = &streams[voices[voice].streamTrackId];
        uint32_t sampleTarget = (uint32_t)(((uint64_t)ms * trk->sampleRate) / 1000ULL);
        trk->seekTarget       = sampleTarget;
    }
}

void ESP32Synth::setStreamLoopPointsMs(uint16_t voice, uint32_t startMs, uint32_t endMs) {
    if (voice >= MAX_VOICES || voices[voice].streamTrackId < 0) return;
    StreamTrack* trk = &streams[voices[voice].streamTrackId];

    uint32_t bytesPerSample = (trk->bitsPerSample / 8) * trk->numChannels;
    if (bytesPerSample == 0) bytesPerSample = 2;

    uint32_t startBytes = trk->dataStartPos + (uint32_t)((((uint64_t)startMs * trk->sampleRate) / 1000ULL) * bytesPerSample);
    uint32_t endBytes   = trk->dataStartPos + (uint32_t)((((uint64_t)endMs   * trk->sampleRate) / 1000ULL) * bytesPerSample);

    // Force the target byte to fall exactly on an audio frame boundary,
    // so we never split a sample in the middle.
    startBytes -= (startBytes - trk->dataStartPos) % bytesPerSample;
    endBytes   -= (endBytes   - trk->dataStartPos) % bytesPerSample;

    if (startBytes < trk->dataStartPos)                                   startBytes = trk->dataStartPos;
    if (endBytes > trk->dataStartPos + trk->dataSize || endMs == 0)       endBytes   = trk->dataStartPos + trk->dataSize;

    trk->loopStartBytes = startBytes;
    trk->loopEndBytes   = endBytes;
    trk->loop           = true;
}

uint32_t ESP32Synth::getStreamPositionMs(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        StreamTrack* trk = &streams[voices[voice].streamTrackId];
        return (uint32_t)(((uint64_t)trk->samplesPlayed * 1000ULL) / trk->sampleRate);
    }
    return 0;
}

uint32_t ESP32Synth::getStreamDurationMs(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        StreamTrack* trk        = &streams[voices[voice].streamTrackId];
        uint32_t bytesPerSample = (trk->bitsPerSample / 8) * trk->numChannels;
        if (bytesPerSample == 0) bytesPerSample = 2;
        uint32_t totalSamples = trk->dataSize / bytesPerSample;
        return (uint32_t)(((uint64_t)totalSamples * 1000ULL) / trk->sampleRate);
    }
    return 0;
}

bool ESP32Synth::isStreamPlaying(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        return streams[voices[voice].streamTrackId].playing;
    }
    return false;
}

#ifdef ARDUINO
bool ESP32Synth::startRecording(fs::FS &fs, const char* path) {
#else
bool ESP32Synth::startRecording(const char* path) {
#endif
    if (_isRecording) return false;

    // Tenta alocar o Ring Buffer. Usa heap interna para acesso DMA/CPU mais rápido possível.
    if (!_recBuffer) {
        _recBuffer = (int16_t*)heap_caps_malloc(RECORD_BUF_SAMPLES * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!_recBuffer) return false; // Sem memória RAM suficiente
    }

    _recordFile = SYNTH_STREAM_OPEN(); // Note: we are re-using the open macro, assuming it uses "w" or "wb" internally.
    // **MUITO IMPORTANTE:** A macro SYNTH_STREAM_OPEN usa "r" e "rb" no topo do header.
    // Então usaremos o nativo aqui para garantir que escreve!
#ifdef ARDUINO
    _recordFile = fs.open(path, "w");
#else
    _recordFile = fopen(path, "wb");
#endif

    if (!SYNTH_FILE_VALID(_recordFile)) {
        heap_caps_free(_recBuffer);
        _recBuffer = nullptr;
        return false;
    }

    _recHead = 0;
    _recTail = 0;
    _recordedDataSize = 0;

    // Pula 44 bytes. Escreveremos o Header correto quando a gravação acabar!
    uint8_t dummyHeader[44] = {0};
    SYNTH_FILE_WRITE(_recordFile, dummyHeader, 44);

    _isRecording = true;

    if (xTaskCreatePinnedToCore(sdWriterTask, "SynthRec", 4096, this, 1, &recordTaskHandle, SYNTH_SD_TASK_CORE) != pdPASS) {
        _isRecording = false;
        SYNTH_FILE_CLOSE(_recordFile);
        heap_caps_free(_recBuffer);
        _recBuffer = nullptr;
        return false;
    }

    return true;
}

void ESP32Synth::stopRecording() {
    if (!_isRecording) return;
    _isRecording = false; // Sinaliza a task para terminar

    // Aguarda a Task salvar os resíduos do buffer e escrever o header do .wav
    while (recordTaskHandle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool ESP32Synth::isRecordingActive() {
    return _isRecording;
}

// Background Writer Task (Roda longe do áudio principal)
void ESP32Synth::sdWriterTask(void* param) {
    ESP32Synth* synth = (ESP32Synth*)param;
    const int CHUNK_SAMPLES = 1024;
    const int CHUNK_BYTES = CHUNK_SAMPLES * 2;
    uint8_t* writeBuf = (uint8_t*)heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (!writeBuf) {
        synth->_isRecording = false;
        vTaskDelete(NULL);
        return;
    }

    // Grava até mandarem parar OU até o buffer de memória ser totalmente esvaziado
    while (synth->_isRecording || synth->_recHead != synth->_recTail) {
        uint32_t h = synth->_recHead;
        uint32_t t = synth->_recTail;
        uint32_t avail = (RECORD_BUF_SAMPLES + h - t) & RECORD_BUF_MASK;

        // If recording stopped, flush whatever remaining samples exist.
        uint32_t samplesToWrite = 0;
        if (avail >= CHUNK_SAMPLES) {
            samplesToWrite = CHUNK_SAMPLES;
        } else if (!synth->_isRecording && avail > 0) {
            samplesToWrite = avail; // Flush the remaining tail
        }

        if (samplesToWrite > 0) {
            uint32_t tillEnd = RECORD_BUF_SAMPLES - t;
            uint32_t bytesToWrite = samplesToWrite * 2;

            // Lock-Free wrap-around read
            if (samplesToWrite <= tillEnd) {
                memcpy(writeBuf, (void*)&synth->_recBuffer[t], bytesToWrite);
            } else {
                memcpy(writeBuf, (void*)&synth->_recBuffer[t], tillEnd * 2);
                memcpy(writeBuf + (tillEnd * 2), (void*)&synth->_recBuffer[0], (samplesToWrite - tillEnd) * 2);
            }
            synth->_recTail = (t + samplesToWrite) & RECORD_BUF_MASK;

            SYNTH_FILE_WRITE(synth->_recordFile, writeBuf, bytesToWrite);
            synth->_recordedDataSize += bytesToWrite;
        } else {
            if (!synth->_isRecording) break; // Finished flushing, exit loop safely
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    // --- ESCRITA DO CABEÇALHO WAV OFICIAL ---
    uint32_t sRate = synth->_sampleRate;
    uint32_t dataSize = synth->_recordedDataSize;
    uint32_t fileSize = dataSize + 36;
    uint32_t byteRate = sRate * 2; // 1 channel (Mono), 16 bits (2 bytes)

    uint8_t head[44];
    head[0] = 'R'; head[1] = 'I'; head[2] = 'F'; head[3] = 'F';
    head[4] = (uint8_t)(fileSize); head[5] = (uint8_t)(fileSize >> 8); head[6] = (uint8_t)(fileSize >> 16); head[7] = (uint8_t)(fileSize >> 24);
    head[8] = 'W'; head[9] = 'A'; head[10] = 'V'; head[11] = 'E';
    head[12] = 'f'; head[13] = 'm'; head[14] = 't'; head[15] = ' ';
    head[16] = 16; head[17] = 0; head[18] = 0; head[19] = 0;
    head[20] = 1;  head[21] = 0; // PCM
    head[22] = 1;  head[23] = 0; // Mono
    head[24] = (uint8_t)(sRate); head[25] = (uint8_t)(sRate >> 8); head[26] = (uint8_t)(sRate >> 16); head[27] = (uint8_t)(sRate >> 24);
    head[28] = (uint8_t)(byteRate); head[29] = (uint8_t)(byteRate >> 8); head[30] = (uint8_t)(byteRate >> 16); head[31] = (uint8_t)(byteRate >> 24);
    head[32] = 2;  head[33] = 0; // BlockAlign
    head[34] = 16; head[35] = 0; // 16 Bits
    head[36] = 'd'; head[37] = 'a'; head[38] = 't'; head[39] = 'a';
    head[40] = (uint8_t)(dataSize); head[41] = (uint8_t)(dataSize >> 8); head[42] = (uint8_t)(dataSize >> 16); head[43] = (uint8_t)(dataSize >> 24);

    SYNTH_FILE_SEEK(synth->_recordFile, 0);
    SYNTH_FILE_WRITE(synth->_recordFile, head, 44);
    SYNTH_FILE_CLOSE(synth->_recordFile);

    // Limpeza pesada e segura de RAM
    heap_caps_free(writeBuf);
    if (synth->_recBuffer) {
        heap_caps_free(synth->_recBuffer);
        synth->_recBuffer = nullptr;
    }
    
    synth->recordTaskHandle = NULL;
    vTaskDelete(NULL);
}
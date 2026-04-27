#include "AudioClient.h"
#include <iostream>

#define MINIAUDIO_IMPLEMENTATION
#include "../../deps/miniaudio/miniaudio.h"
#include "../../deps/opus/include/opus.h"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <cmath>

// RNNoise public API — the implementation is compiled via rnnoise_amalgam.c
#include "../../deps/rnnoise/rnnoise.h"

static void AudioLog(const std::string& msg) {
    // Debug logging disabled
    return;
}

AudioClient::AudioClient()
    : m_context(nullptr), m_device(nullptr), m_encoder(nullptr), m_decoder(nullptr),
      m_isInitialized(false), m_isVoiceActive(false) {
}

AudioClient::~AudioClient() {
    Shutdown();
}

bool AudioClient::Initialize() {
    if (m_isInitialized) return true;

    int err = 0;
    
    // Initialize Opus Encoder
    m_encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP, &err);
    if (err < 0) {
        std::cerr << "Failed to create Opus encoder: " << opus_strerror(err) << std::endl;
        return false;
    }

    // Initialize Opus Decoder
    m_decoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &err);
    if (err < 0) {
        std::cerr << "Failed to create Opus decoder: " << opus_strerror(err) << std::endl;
        opus_encoder_destroy(m_encoder);
        m_encoder = nullptr;
        return false;
    }

    // Initialize RNNoise (two instances — one per 480-sample half of our 960-frame)
    m_rnnoise0 = rnnoise_create(nullptr);
    m_rnnoise1 = rnnoise_create(nullptr);
    if (!m_rnnoise0 || !m_rnnoise1) {
        std::cerr << "Failed to initialize RNNoise" << std::endl;
        // Non-fatal — we'll just disable it
        if (m_rnnoise0) { rnnoise_destroy(m_rnnoise0); m_rnnoise0 = nullptr; }
        if (m_rnnoise1) { rnnoise_destroy(m_rnnoise1); m_rnnoise1 = nullptr; }
    }

    m_context = new ma_context;
    ma_backend backends[] = { ma_backend_wasapi, ma_backend_dsound, ma_backend_winmm };
    AudioLog("Initializing context with backends: WASAPI, DSound, WinMM...");
    
    ma_result res = ma_context_init(backends, 3, NULL, m_context);
    if (res != MA_SUCCESS) {
        AudioLog("ma_context_init failed! Error: " + std::to_string((int)res));
        return false;
    }
    AudioLog("Context initialized successfully using backend: " + std::string(ma_get_backend_name(m_context->backend)));
    m_device = new ma_device;
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_duplex);
    deviceConfig.capture.pDeviceID  = NULL;
    deviceConfig.capture.format     = ma_format_s16;
    deviceConfig.capture.channels   = CHANNELS;
    deviceConfig.capture.shareMode  = ma_share_mode_shared;
    
    deviceConfig.playback.pDeviceID = NULL;
    deviceConfig.playback.format    = ma_format_s16;
    deviceConfig.playback.channels  = CHANNELS;
    
    deviceConfig.sampleRate         = SAMPLE_RATE;
    deviceConfig.dataCallback       = DataCallback;
    deviceConfig.pUserData          = this;
    deviceConfig.periodSizeInFrames = FRAME_SIZE;

    if (ma_device_init(m_context, &deviceConfig, m_device) != MA_SUCCESS) {
        std::cerr << "Failed to initialize miniaudio device." << std::endl;
        ma_context_uninit(m_context);
        opus_encoder_destroy(m_encoder);
        opus_decoder_destroy(m_decoder);
        delete m_context;
        delete m_device;
        return false;
    }

    m_isInitialized = true;
    return true;
}

void AudioClient::Shutdown() {
    if (!m_isInitialized) return;

    StopVoice();

    if (m_device) {
        ma_device_uninit(m_device);
        delete m_device;
        m_device = nullptr;
    }

    if (m_context) {
        ma_context_uninit(m_context);
        delete m_context;
        m_context = nullptr;
    }

    if (m_encoder) {
        opus_encoder_destroy(m_encoder);
        m_encoder = nullptr;
    }

    if (m_decoder) {
        opus_decoder_destroy(m_decoder);
        m_decoder = nullptr;
    }

    if (m_rnnoise0) { rnnoise_destroy(m_rnnoise0); m_rnnoise0 = nullptr; }
    if (m_rnnoise1) { rnnoise_destroy(m_rnnoise1); m_rnnoise1 = nullptr; }

    m_isInitialized = false;
}

void AudioClient::SetRNNoiseEnabled(bool enabled) {
    m_rnnoiseEnabled = enabled && (m_rnnoise0 != nullptr);
}

bool AudioClient::StartVoice() {
    if (!m_isInitialized || m_isVoiceActive) return false;

    // Clear queues
    {
        std::lock_guard<std::mutex> lock(m_outMutex);
        while (!m_outQueue.empty()) m_outQueue.pop();
    }
    {
        std::lock_guard<std::mutex> lock(m_inMutex);
        while (!m_inQueue.empty()) m_inQueue.pop();
    }

    if (ma_device_start(m_device) != MA_SUCCESS) {
        std::cerr << "Failed to start miniaudio device." << std::endl;
        return false;
    }

    m_isVoiceActive = true;
    return true;
}

void AudioClient::StopVoice() {
    if (!m_isVoiceActive) return;

    ma_device_stop(m_device);
    m_isVoiceActive = false;
}

std::vector<AudioDeviceInfo> AudioClient::GetInputDevices() {
    std::vector<AudioDeviceInfo> devices;
    if (!m_context) {
        AudioLog("GetInputDevices: m_context is NULL");
        return devices;
    }

    ma_device_info* pCaptureInfos;
    ma_uint32 captureCount;
    ma_result res = ma_context_get_devices(m_context, nullptr, nullptr, &pCaptureInfos, &captureCount);
    if (res == MA_SUCCESS) {
        AudioLog("Found " + std::to_string(captureCount) + " input devices.");
        for (ma_uint32 i = 0; i < captureCount; ++i) {
            AudioLog("  -> " + std::string(pCaptureInfos[i].name));
            devices.push_back({ pCaptureInfos[i].name, pCaptureInfos[i].name, (bool)pCaptureInfos[i].isDefault });
        }
    } else {
        AudioLog("ma_context_get_devices (capture) failed with error: " + std::to_string((int)res));
    }
    return devices;
}

std::vector<AudioDeviceInfo> AudioClient::GetOutputDevices() {
    std::vector<AudioDeviceInfo> devices;
    if (!m_context) {
        AudioLog("GetOutputDevices: m_context is NULL");
        return devices;
    }

    ma_device_info* pPlaybackInfos;
    ma_uint32 playbackCount;
    ma_result res = ma_context_get_devices(m_context, &pPlaybackInfos, &playbackCount, nullptr, nullptr);
    if (res == MA_SUCCESS) {
        AudioLog("Found " + std::to_string(playbackCount) + " output devices.");
        for (ma_uint32 i = 0; i < playbackCount; ++i) {
            AudioLog("  -> " + std::string(pPlaybackInfos[i].name));
            devices.push_back({ pPlaybackInfos[i].name, pPlaybackInfos[i].name, (bool)pPlaybackInfos[i].isDefault });
        }
    } else {
        AudioLog("ma_context_get_devices (playback) failed with error: " + std::to_string((int)res));
    }
    return devices;
}

bool AudioClient::SetDevices(const std::string& inputName, const std::string& outputName) {
    if (!m_isInitialized) return false;

    bool wasActive = m_isVoiceActive;
    if (wasActive) StopVoice();

    ma_device_uninit(m_device);

    ma_device_info* pPlaybackInfos;
    ma_uint32 playbackCount;
    ma_device_info* pCaptureInfos;
    ma_uint32 captureCount;
    ma_context_get_devices(m_context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount);

    ma_device_id* pCaptureID = nullptr;
    ma_device_id* pPlaybackID = nullptr;

    for (ma_uint32 i = 0; i < captureCount; ++i) {
        if (inputName == pCaptureInfos[i].name) {
            pCaptureID = &pCaptureInfos[i].id;
            break;
        }
    }
    for (ma_uint32 i = 0; i < playbackCount; ++i) {
        if (outputName == pPlaybackInfos[i].name) {
            pPlaybackID = &pPlaybackInfos[i].id;
            break;
        }
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_duplex);
    deviceConfig.capture.pDeviceID  = pCaptureID;
    deviceConfig.capture.format     = ma_format_s16;
    deviceConfig.capture.channels   = CHANNELS;
    deviceConfig.capture.shareMode  = ma_share_mode_shared;
    
    deviceConfig.playback.pDeviceID = pPlaybackID;
    deviceConfig.playback.format    = ma_format_s16;
    deviceConfig.playback.channels  = CHANNELS;
    
    deviceConfig.sampleRate         = SAMPLE_RATE;
    deviceConfig.dataCallback       = DataCallback;
    deviceConfig.pUserData          = this;
    deviceConfig.periodSizeInFrames = FRAME_SIZE;

    if (ma_device_init(m_context, &deviceConfig, m_device) != MA_SUCCESS) {
        return false;
    }

    m_currentInputName = inputName;
    m_currentOutputName = outputName;
    if (wasActive) StartVoice();
    return true;
}

void AudioClient::PushVoiceData(const std::vector<uint8_t>& data) {
    if (!m_isVoiceActive) return;
    std::lock_guard<std::mutex> lock(m_inMutex);
    m_inQueue.push(data);
}

bool AudioClient::GetEncodedVoiceData(std::vector<uint8_t>& outData) {
    std::lock_guard<std::mutex> lock(m_outMutex);
    if (m_outQueue.empty()) return false;
    
    outData = m_outQueue.front();
    m_outQueue.pop();
    return true;
}

void AudioClient::DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, unsigned int frameCount) {
    AudioClient* client = (AudioClient*)pDevice->pUserData;
    if (client) {
        client->ProcessAudio(pOutput, pInput, frameCount);
    }
}

void AudioClient::ProcessAudio(void* pOutput, const void* pInput, unsigned int frameCount) {
    // ---------------------------------------------------------------
    // 1. CAPTURE: microphone -> (optional RNNoise) -> VAD -> Opus encode
    // ---------------------------------------------------------------
    if (pInput != NULL && !m_isMuted && !m_isDeafened) {
        const opus_int16* pcmInput = (const opus_int16*)pInput;
        
        // --- Optional RNNoise AI noise cancellation ---
        // RNNoise works on float samples, in RNNOISE_FRAME (480) chunks.
        // Our frame is 960 samples, so we process it in two halves.
        static opus_int16 cleanBuf[FRAME_SIZE];
        const opus_int16* pcmToEncode = pcmInput;

        if (m_rnnoiseEnabled && m_rnnoise0 && m_rnnoise1 && frameCount == (unsigned)FRAME_SIZE) {
            float floatIn[RNNOISE_FRAME];
            float floatOut[RNNOISE_FRAME];

            // First half (samples 0..479)
            for (int i = 0; i < RNNOISE_FRAME; i++)
                floatIn[i] = (float)pcmInput[i];
            rnnoise_process_frame(m_rnnoise0, floatOut, floatIn);
            for (int i = 0; i < RNNOISE_FRAME; i++)
                cleanBuf[i] = (opus_int16)std::clamp(floatOut[i], -32768.0f, 32767.0f);

            // Second half (samples 480..959)
            for (int i = 0; i < RNNOISE_FRAME; i++)
                floatIn[i] = (float)pcmInput[RNNOISE_FRAME + i];
            rnnoise_process_frame(m_rnnoise1, floatOut, floatIn);
            for (int i = 0; i < RNNOISE_FRAME; i++)
                cleanBuf[RNNOISE_FRAME + i] = (opus_int16)std::clamp(floatOut[i], -32768.0f, 32767.0f);

            pcmToEncode = cleanBuf;
        }

        // --- VAD: RMS level detection ---
        long long sumSquare = 0;
        for (unsigned int i = 0; i < frameCount; ++i) {
            sumSquare += (long long)pcmToEncode[i] * pcmToEncode[i];
        }
        double rms = sqrt((double)sumSquare / frameCount);
        float level = (float)(rms / 32768.0);
        m_currentLevel = level;

        static int vadHoldCount = 0;

        if (m_vadAuto) {
            if (level < m_noiseFloor * 2.5f) {
                m_noiseFloor = m_noiseFloor * 0.99f + level * 0.01f;
            } else {
                m_noiseFloor = m_noiseFloor * 0.999f + 0.001f * 0.0001f;
            }
            float targetThreshold = m_noiseFloor * 4.0f + 0.005f;
            if (targetThreshold < 0.002f) targetThreshold = 0.002f;
            if (targetThreshold > 0.1f) targetThreshold = 0.1f;
            m_vadThreshold = targetThreshold;
        }

        if (level > m_vadThreshold) {
            vadHoldCount = 20; // Hold for ~20 frames after speech stops
        }

        m_isSpeaking = (vadHoldCount > 0);
        if (vadHoldCount > 0) vadHoldCount--;

        if (m_isSpeaking) {
            unsigned char cbits[MAX_PACKET_SIZE];
            int nbBytes = opus_encode(m_encoder, pcmToEncode, frameCount, cbits, MAX_PACKET_SIZE);
            if (nbBytes > 0) {
                std::vector<uint8_t> encodedPacket(cbits, cbits + nbBytes);
                std::lock_guard<std::mutex> lock(m_outMutex);
                m_outQueue.push(encodedPacket);
            }
        }
    } else {
        m_isSpeaking = false;
        m_currentLevel = 0.0f;
    }

    // ---------------------------------------------------------------
    // 2. PLAYBACK: Opus decode -> output
    // ---------------------------------------------------------------
    if (pOutput != NULL) {
        memset(pOutput, 0, frameCount * CHANNELS * sizeof(opus_int16));
        if (!m_isDeafened) {
            opus_int16* pcmOutput = (opus_int16*)pOutput;
            
            std::vector<uint8_t> encodedPacket;
            bool hasData = false;
            {
                std::lock_guard<std::mutex> lock(m_inMutex);
                if (!m_inQueue.empty()) {
                    encodedPacket = m_inQueue.front();
                    m_inQueue.pop();
                    hasData = true;
                }
            }

            if (hasData) {
                opus_decode(m_decoder, encodedPacket.data(), (opus_int32)encodedPacket.size(), pcmOutput, frameCount, 0);
            }
        }
    }
}

#include "AudioClient.h"
#include <iostream>

#define MINIAUDIO_IMPLEMENTATION
#include "../../deps/miniaudio/miniaudio.h"
#include "../../deps/opus/include/opus.h"

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

    m_context = new ma_context;
    if (ma_context_init(NULL, 0, NULL, m_context) != MA_SUCCESS) {
        std::cerr << "Failed to initialize miniaudio context." << std::endl;
        opus_encoder_destroy(m_encoder);
        opus_decoder_destroy(m_decoder);
        delete m_context;
        return false;
    }

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
    // Set specific frame size so we always get FRAME_SIZE (960) frames per callback
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

    m_isInitialized = false;
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
    if (!m_context) return devices;

    ma_device_info* pCaptureInfos;
    ma_uint32 captureCount;
    if (ma_context_get_devices(m_context, nullptr, nullptr, &pCaptureInfos, &captureCount) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < captureCount; ++i) {
            devices.push_back({ pCaptureInfos[i].name, pCaptureInfos[i].name, (bool)pCaptureInfos[i].isDefault });
        }
    }
    return devices;
}

std::vector<AudioDeviceInfo> AudioClient::GetOutputDevices() {
    std::vector<AudioDeviceInfo> devices;
    if (!m_context) return devices;

    ma_device_info* pPlaybackInfos;
    ma_uint32 playbackCount;
    if (ma_context_get_devices(m_context, &pPlaybackInfos, &playbackCount, nullptr, nullptr) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < playbackCount; ++i) {
            devices.push_back({ pPlaybackInfos[i].name, pPlaybackInfos[i].name, (bool)pPlaybackInfos[i].isDefault });
        }
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
    // 1. Process Capture (Input -> Encode -> m_outQueue)
    if (pInput != NULL && !m_isMuted && !m_isDeafened) {
        const opus_int16* pcmInput = (const opus_int16*)pInput;
        
        // Simple speaking detection (RMS volume)
        long long sumSquare = 0;
        for (unsigned int i = 0; i < frameCount; ++i) {
            sumSquare += (long long)pcmInput[i] * pcmInput[i];
        }
        double rms = sqrt((double)sumSquare / frameCount);
        m_isSpeaking = (rms > 500.0); // Threshold for speaking detection

        unsigned char cbits[MAX_PACKET_SIZE];
        // Ensure frameCount matches Opus expected frame size
        int nbBytes = opus_encode(m_encoder, pcmInput, frameCount, cbits, MAX_PACKET_SIZE);
        if (nbBytes > 0) {
            std::vector<uint8_t> encodedPacket(cbits, cbits + nbBytes);
            std::lock_guard<std::mutex> lock(m_outMutex);
            m_outQueue.push(encodedPacket);
        }
    } else {
        m_isSpeaking = false;
    }

    // 2. Process Playback (m_inQueue -> Decode -> Output)
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

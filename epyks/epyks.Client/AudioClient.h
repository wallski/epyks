#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>

// Forward declarations for opaque types to avoid including deps headers in the header file
struct ma_device;
struct ma_context;
struct OpusEncoder;
struct OpusDecoder;

struct AudioDeviceInfo {
    std::string id;
    std::string name;
    bool isDefault;
};

class AudioClient {
public:
    AudioClient();
    ~AudioClient();

    bool Initialize();
    void Shutdown();

    // Start/stop voice capturing and playback
    bool StartVoice();
    void StopVoice();

    // Push incoming encoded opus voice data to be played
    void PushVoiceData(const std::vector<uint8_t>& data);

    // Retrieve encoded opus voice data to send to the server
    bool GetEncodedVoiceData(std::vector<uint8_t>& outData);

    // Device management
    std::vector<AudioDeviceInfo> GetInputDevices();
    std::vector<AudioDeviceInfo> GetOutputDevices();
    bool SetDevices(const std::string& inputId, const std::string& outputId);

    bool IsVoiceActive() const { return m_isVoiceActive; }

private:
    // miniaudio callback
    static void DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, unsigned int frameCount);

    void ProcessAudio(void* pOutput, const void* pInput, unsigned int frameCount);

private:
    ma_context* m_context;
    ma_device* m_device;

    OpusEncoder* m_encoder;
    OpusDecoder* m_decoder;

    std::atomic<bool> m_isInitialized;
    std::atomic<bool> m_isVoiceActive;

    // Buffer for outgoing encoded packets
    std::mutex m_outMutex;
    std::queue<std::vector<uint8_t>> m_outQueue;

    // Buffer for incoming encoded packets
    std::mutex m_inMutex;
    std::queue<std::vector<uint8_t>> m_inQueue;

    // Configuration constants
    static const int SAMPLE_RATE = 48000;
    static const int CHANNELS = 1;
    static const int FRAME_SIZE = 960; // 20ms at 48kHz
    static const int MAX_PACKET_SIZE = 4000;
};

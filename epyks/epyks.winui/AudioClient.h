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
struct DenoiseState; // RNNoise

struct AudioDeviceInfo {
    std::string id;
    std::string name;
    bool isDefault;
};

namespace epyks_winui {
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
    std::string GetCurrentInputDevice() const { return m_currentInputName; }
    std::string GetCurrentOutputDevice() const { return m_currentOutputName; }

    bool IsVoiceActive() const { return m_isVoiceActive; }
    bool IsSpeaking() const { return m_isSpeaking; }
    void SetMute(bool muted) { m_isMuted = muted; }
    void SetDeafened(bool deafened) { m_isDeafened = deafened; }

    bool GetVadAuto() const { return m_vadAuto; }
    void SetVadAuto(bool v) { m_vadAuto = v; }
    float GetVadThreshold() const { return m_vadThreshold; }
    void SetVadThreshold(float v) { m_vadThreshold = v; }
    float GetCurrentLevel() const { return m_currentLevel; }

    // RNNoise AI noise cancellation
    bool GetRNNoiseEnabled() const { return m_rnnoiseEnabled; }
    void SetRNNoiseEnabled(bool enabled);

private:
    // miniaudio callback
    static void DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, unsigned int frameCount);
    void ProcessAudio(void* pOutput, const void* pInput, unsigned int frameCount);

private:
    ma_context* m_context;
    ma_device* m_device;

    OpusEncoder* m_encoder;
    OpusDecoder* m_decoder;

    // RNNoise state — two instances for 2x 480-sample halves of our 960-sample frame
    DenoiseState* m_rnnoise0 = nullptr;
    DenoiseState* m_rnnoise1 = nullptr;
    std::atomic<bool> m_rnnoiseEnabled{false};

    std::atomic<bool> m_isInitialized;
    std::atomic<bool> m_isVoiceActive;
    std::atomic<bool> m_isSpeaking{false};
    std::string m_currentInputName;
    std::string m_currentOutputName;
    std::atomic<bool> m_isMuted{false};
    std::atomic<bool> m_isDeafened{false};

    std::atomic<bool> m_vadAuto{true};
    std::atomic<float> m_vadThreshold{0.02f};
    std::atomic<float> m_currentLevel{0.0f};
    std::atomic<float> m_noiseFloor{0.01f};

    // Buffer for outgoing encoded packets
    std::mutex m_outMutex;
    std::queue<std::vector<uint8_t>> m_outQueue;

    // Buffer for incoming encoded packets
    std::mutex m_inMutex;
    std::queue<std::vector<uint8_t>> m_inQueue;

    // Configuration constants
    static const int SAMPLE_RATE = 48000;
    static const int CHANNELS = 1;
    static const int FRAME_SIZE = 960;    // 20ms at 48kHz
    static const int MAX_PACKET_SIZE = 4000;
    static const int RNNOISE_FRAME = 480; // RNNoise requires 10ms frames at 48kHz
};
} // namespace epyks_winui

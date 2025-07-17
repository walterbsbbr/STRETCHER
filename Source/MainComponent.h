#pragma once

#include <JuceHeader.h>
#include <soundtouch/SoundTouch.h>

class WaveformComponent : public juce::Component
{
public:
    WaveformComponent();
    ~WaveformComponent() override;
    
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    
    void setWaveformData(const std::vector<float>& peaks, double sampleRate, int totalSamples);
    void setPlayPosition(double positionInSeconds);
    void setDuration(double durationInSeconds);
    void setLooping(bool shouldLoop);
    void setDetectedBPM(double bpm);
    void setWaveformColour(const juce::Colour& colour);
    void setQuantizeValue(int quantizeValue);
    
    std::function<void(double)> onPositionChanged;

private:
    std::vector<float> waveformPeaks;
    double currentPosition;
    double totalDuration;
    double sampleRate;
    int totalSamples;
    bool isLooping;
    double detectedBPM;
    juce::Colour waveformColour;
    int quantizeDivisions;
    
    void updatePositionFromMouse(const juce::MouseEvent& event);
    void drawGrid(juce::Graphics& g, const juce::Rectangle<int>& area);
    void drawBeatLines(juce::Graphics& g, const juce::Rectangle<int>& area);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformComponent)
};

class AudioTrack
{
public:
    AudioTrack();
    ~AudioTrack();
    
    void loadAudioFile(const juce::File& file);
    void setStretchRatio(double ratio);
    void scaleStretchRatio(double scaleFactor);
    void setPosition(double positionInSeconds);
    void reset();
    void setMasterBPM(double masterBPM);
    
    void processBlock(juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    
    bool isLoaded() const { return audioBuffer.getNumSamples() > 0; }
    double getDurationInSeconds() const;
    double getCurrentPosition() const { return currentPosition; }
    double getStretchRatio() const { return stretchRatio; }
    juce::String getFileName() const { return fileName; }
    double getDetectedBPM() const { return detectedBPM; }
    const std::vector<float>& getWaveformPeaks() const { return waveformPeaks; }
    
    void setMuted(bool shouldBeMuted) { muted = shouldBeMuted; }
    void setSolo(bool shouldBeSolo) { solo = shouldBeSolo; }
    void setVolume(float newVolume) { volume = newVolume; }
    void setLooping(bool shouldLoop) { looping = shouldLoop; }
    void autoSyncToMaster();
    
    bool isMuted() const { return muted; }
    bool isSolo() const { return solo; }
    float getVolume() const { return volume; }
    bool isLooping() const { return looping; }

private:
    juce::AudioBuffer<float> audioBuffer;
    std::unique_ptr<soundtouch::SoundTouch> soundTouch;
    juce::AudioFormatManager formatManager;
    std::vector<float> waveformPeaks;
    juce::AudioBuffer<float> stretchedBuffer;
    
    double sampleRate;
    double currentPosition;
    double stretchRatio;
    double detectedBPM;
    double masterBPM;
    juce::String fileName;
    
    bool muted;
    bool solo;
    bool looping;
    float volume;
    
    juce::CriticalSection lock;
    
    double detectBPM();
    void generateWaveformPeaks();
    void processDirectPlayback(juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void processWithSoundTouch(juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    void initializeSoundTouch();
};

class TrackComponent : public juce::Component
{
public:
    TrackComponent(AudioTrack* track, int trackNumber);
    ~TrackComponent() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void updateTrackInfo();
    void updateWaveform();
    
    static juce::Colour getTrackColour(int trackNumber);
    
    std::function<void(double)> onTrackLoaded;

private:
    AudioTrack* audioTrack;
    int trackNum;
    
    std::unique_ptr<WaveformComponent> waveformDisplay;
    juce::TextButton loadButton;
    juce::TextButton muteButton;
    juce::TextButton soloButton;
    juce::TextButton loopButton;
    juce::TextButton quantizeButton;
    juce::Slider volumeSlider;
    juce::Slider stretchSlider;
    juce::Label trackLabel;
    juce::Label fileLabel;
    juce::Label bpmLabel;
    juce::Label stretchLabel;
    juce::Label volumeLabel;
    
    int currentQuantize;
    
    void loadButtonClicked();
    void muteButtonClicked();
    void soloButtonClicked();
    void loopButtonClicked();
    void quantizeButtonClicked();
    void volumeSliderChanged();
    void stretchSliderChanged();
    void onWaveformPositionChanged(double position);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackComponent)
};

class TransportComponent : public juce::Component
{
public:
    TransportComponent();
    ~TransportComponent() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    std::function<void()> onPlay;
    std::function<void()> onStop;
    std::function<void()> onRecord;
    std::function<void(double)> onTempoChanged;
    std::function<void()> onAutoSync;
    std::function<void()> onMetronome;
    
    void setPlaying(bool isPlaying);
    void setRecording(bool isRecording);
    void setTempo(double bpm);
    void setPosition(double positionInSeconds);
    void setMetronomeEnabled(bool enabled);

private:
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::TextButton recordButton;
    juce::TextButton autoSyncButton;
    juce::TextButton metronomeButton;
    juce::Slider tempoSlider;
    juce::Label tempoLabel;
    juce::Label positionLabel;
    juce::Label masterBpmLabel;
    
    bool playing;
    bool recording;
    bool autoSyncEnabled;
    bool metronomeEnabled;
    double currentTempo;
    double currentPosition;
    
    void playButtonClicked();
    void stopButtonClicked();
    void recordButtonClicked();
    void autoSyncButtonClicked();
    void metronomeButtonClicked();
    void tempoSliderChanged();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportComponent)
};

class MainComponent : public juce::AudioAppComponent,
                      public juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void timerCallback() override;
    
    void onTrackLoaded(double trackBPM);

private:
    static constexpr int maxTracks = 8;
    
    std::array<std::unique_ptr<AudioTrack>, maxTracks> audioTracks;
    std::array<std::unique_ptr<TrackComponent>, maxTracks> trackComponents;
    
    std::unique_ptr<TransportComponent> transportComponent;
    juce::Viewport tracksViewport;
    juce::Component tracksContainer;
    
    double masterTempo;
    double previousMasterTempo;
    double currentPlayPosition;
    bool isPlaying;
    bool isRecording;
    bool autoSyncEnabled;
    bool metronomeEnabled;
    
    // Metronome variables
    double metronomePhase;
    double metronomeBeatInterval;
    double lastBeatTime;
    double metronomeVolume;
    
    juce::CriticalSection audioLock;
    
    void play();
    void stop();
    void record();
    void setTempo(double bpm);
    void setInitialMasterBPM(double bpm, AudioTrack* definingTrack);
    void autoSyncAllTracks();
    void updatePlayPosition();
    void setTrackPosition(int trackIndex, double position);
    double findAverageBPM();
    void syncNewTrackToMaster(AudioTrack* track);
    void toggleMetronome();
    void processMetronome(juce::AudioBuffer<float>& buffer, int numSamples);
    float generateClickSound(double phase);
    
    void setupTracks();
    void setupTransport();
    void setupLayout();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

#pragma once

#include <JuceHeader.h>
#include "rubberband/RubberBandStretcher.h"

class AudioTrack
{
public:
    AudioTrack();
    ~AudioTrack();
    
    void loadAudioFile(const juce::File& file);
    void setStretchRatio(double ratio);
    void setPosition(double positionInSeconds);
    void reset();
    
    void processBlock(juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    
    bool isLoaded() const { return audioBuffer.getNumSamples() > 0; }
    double getDurationInSeconds() const;
    juce::String getFileName() const { return fileName; }
    
    void setMuted(bool shouldBeMuted) { muted = shouldBeMuted; }
    void setSolo(bool shouldBeSolo) { solo = shouldBeSolo; }
    void setVolume(float newVolume) { volume = newVolume; }
    
    bool isMuted() const { return muted; }
    bool isSolo() const { return solo; }
    float getVolume() const { return volume; }

private:
    juce::AudioBuffer<float> audioBuffer;
    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher;
    juce::AudioFormatManager formatManager;
    
    double sampleRate;
    double currentPosition;
    double stretchRatio;
    juce::String fileName;
    
    bool muted;
    bool solo;
    float volume;
    
    juce::CriticalSection lock;
    
    void updateStretcher();
};

class TrackComponent : public juce::Component
{
public:
    TrackComponent(AudioTrack* track, int trackNumber);
    ~TrackComponent() override;
    
    void paint(juce::Graphics& g) override;
    void resized() override;
    
    void updateTrackInfo();

private:
    AudioTrack* audioTrack;
    int trackNum;
    
    juce::TextButton loadButton;
    juce::TextButton muteButton;
    juce::TextButton soloButton;
    juce::Slider volumeSlider;
    juce::Slider stretchSlider;
    juce::Label trackLabel;
    juce::Label fileLabel;
    
    void loadButtonClicked();
    void muteButtonClicked();
    void soloButtonClicked();
    void volumeSliderChanged();
    void stretchSliderChanged();
    
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
    
    void setPlaying(bool isPlaying);
    void setRecording(bool isRecording);
    void setTempo(double bpm);
    void setPosition(double positionInSeconds);

private:
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::TextButton recordButton;
    juce::Slider tempoSlider;
    juce::Label tempoLabel;
    juce::Label positionLabel;
    
    bool playing;
    bool recording;
    double currentTempo;
    double currentPosition;
    
    void playButtonClicked();
    void stopButtonClicked();
    void recordButtonClicked();
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

private:
    static constexpr int maxTracks = 8;
    
    std::array<std::unique_ptr<AudioTrack>, maxTracks> audioTracks;
    std::array<std::unique_ptr<TrackComponent>, maxTracks> trackComponents;
    
    std::unique_ptr<TransportComponent> transportComponent;
    juce::Viewport tracksViewport;
    juce::Component tracksContainer;
    
    double masterTempo;
    double currentPlayPosition;
    bool isPlaying;
    bool isRecording;
    
    juce::CriticalSection audioLock;
    
    void play();
    void stop();
    void record();
    void setTempo(double bpm);
    void updatePlayPosition();
    
    void setupTracks();
    void setupTransport();
    void setupLayout();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
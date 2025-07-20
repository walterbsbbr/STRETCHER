#pragma once

#include <JuceHeader.h>
#include <soundtouch/SoundTouch.h>
#include <vector>
#include <memory>
#include <array>

class WaveformComponent : public juce::Component
{
public:
    WaveformComponent();
    ~WaveformComponent() override;
    
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    
    void setWaveformData(const std::vector<float>& peaks, double sampleRate, int totalSamples);
    void setPlayPosition(double positionInSeconds);
    void setDuration(double durationInSeconds);
    void setLooping(bool shouldLoop);
    void setDetectedBPM(double bpm);
    void setWaveformColour(const juce::Colour& colour);
    void setQuantizeValue(int quantizeValue);
    void setZoomFactor(double zoom);
    
    double getZoomFactor() const { return zoomFactor; }
    double getDetectedBPM() const { return detectedBPM; }
    
    // Selection methods
    void setSelectionRange(double startTime, double endTime);
    void clearSelection();
    bool hasValidSelection() const { return hasSelection && selectionEnd > selectionStart; }
    double getSelectionStart() const { return selectionStart; }
    double getSelectionEnd() const { return selectionEnd; }
    
    std::function<void(double)> onPositionChanged;
    std::function<void(double)> onBPMChanged;
    std::function<void(double, double)> onSelectionChanged;

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
    double zoomFactor;
    double viewStartTime;  // For zoom - what time we're viewing from
    
    // Grid dragging for BPM adjustment
    std::vector<double> gridPositions;
    int draggedGridIndex;
    bool isDraggingGrid;
    double initialMouseX;
    double initialGridTime;
    juce::MouseCursor currentCursor;
    
    // Pan/scroll for zoom navigation
    bool isDraggingWaveform;
    double initialViewStartTime;
    int panStartX;
    
    // Selection for loop regions
    bool isSelecting;
    bool hasSelection;
    double selectionStart;
    double selectionEnd;
    double selectionStartX;
    
    // Selection edge resizing
    bool isResizingSelectionStart;
    bool isResizingSelectionEnd;
    double fixedSelectionBound;
    
    void updatePositionFromMouse(const juce::MouseEvent& event);
    void drawGrid(juce::Graphics& g, const juce::Rectangle<int>& area);
    void drawBeatLines(juce::Graphics& g, const juce::Rectangle<int>& area);
    void initializeGridPositions();
    int findGridLineAtPosition(int mouseX, const juce::Rectangle<int>& area);
    void updateBPMFromGrid();
    void updateCursor(const juce::MouseEvent& event);
    double timeToPixel(double timeInSeconds, const juce::Rectangle<int>& area) const;
    double pixelToTime(int pixelX, const juce::Rectangle<int>& area) const;
    bool isNearSelectionEdge(int mouseX, const juce::Rectangle<int>& area, bool& nearStart, bool& nearEnd);
    
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
    void setManualBPM(double bpm);
    
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
    
    // Selection-based looping
    void setLoopRegion(double startTime, double endTime);
    void clearLoopRegion();
    bool hasLoopRegion() const { return hasCustomLoopRegion; }
    double getLoopStart() const { return loopStartTime; }
    double getLoopEnd() const { return loopEndTime; }
    
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
    
    // Loop region selection
    bool hasCustomLoopRegion;
    double loopStartTime;
    double loopEndTime;
    
    juce::CriticalSection lock;
    
    // Improved BPM detection methods
    double detectBPMImproved();
    double detectBPMAutocorrelation();
    std::vector<double> calculateBeatTrack();
    double detectBPMFromOnsets();
    std::vector<float> calculateOnsetStrength();
    double findBestBPMCandidate(const std::vector<double>& onsetTimes);
    
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
    juce::TextButton bpmEditButton;
    juce::TextButton zoomInButton;
    juce::TextButton zoomOutButton;
    juce::TextButton clearSelectionButton;
    juce::Slider volumeSlider;
    juce::Slider stretchSlider;
    juce::Label trackLabel;
    juce::Label fileLabel;
    juce::Label bpmLabel;
    juce::Label stretchLabel;
    juce::Label volumeLabel;
    juce::Label zoomLabel;
    
    int currentQuantize;
    bool editingBPM;
    double currentZoom;
    
    void loadButtonClicked();
    void muteButtonClicked();
    void soloButtonClicked();
    void loopButtonClicked();
    void quantizeButtonClicked();
    void bpmEditButtonClicked();
    void zoomInButtonClicked();
    void zoomOutButtonClicked();
    void clearSelectionButtonClicked();
    void volumeSliderChanged();
    void stretchSliderChanged();
    void onWaveformPositionChanged(double position);
    void onWaveformBPMChanged(double bpm);
    void onWaveformSelectionChanged(double startTime, double endTime);
    void showBPMEditor();
    
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

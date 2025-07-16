#include "MainComponent.h"

// ============================================================================
// AudioTrack Implementation
// ============================================================================

AudioTrack::AudioTrack()
    : sampleRate(44100.0),
      currentPosition(0.0),
      stretchRatio(1.0),
      muted(false),
      solo(false),
      volume(1.0f)
{
    formatManager.registerBasicFormats();
}

AudioTrack::~AudioTrack()
{
    stretcher.reset();
}

void AudioTrack::loadAudioFile(const juce::File& file)
{
    juce::ScopedLock sl(lock);
    
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    
    if (reader != nullptr)
    {
        audioBuffer.setSize(reader->numChannels, static_cast<int>(reader->lengthInSamples));
        reader->read(&audioBuffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
        
        sampleRate = reader->sampleRate;
        fileName = file.getFileNameWithoutExtension();
        currentPosition = 0.0;
        
        updateStretcher();
    }
}

void AudioTrack::setStretchRatio(double ratio)
{
    juce::ScopedLock sl(lock);
    
    if (ratio != stretchRatio)
    {
        stretchRatio = ratio;
        updateStretcher();
    }
}

void AudioTrack::setPosition(double positionInSeconds)
{
    juce::ScopedLock sl(lock);
    currentPosition = positionInSeconds;
}

void AudioTrack::reset()
{
    juce::ScopedLock sl(lock);
    currentPosition = 0.0;
    if (stretcher)
        stretcher->reset();
}

void AudioTrack::processBlock(juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    juce::ScopedLock sl(lock);
    
    if (!isLoaded() || muted || numSamples <= 0)
    {
        return;
    }
    
    if (!stretcher)
    {
        updateStretcher();
        if (!stretcher)
            return;
    }
    
    const int channels = juce::jmin(buffer.getNumChannels(), audioBuffer.getNumChannels());
    const int totalSamples = audioBuffer.getNumSamples();
    
    if (channels <= 0 || totalSamples <= 0)
        return;
    
    // Calculate the current sample position in the original audio
    const double samplesPerSecond = sampleRate / stretchRatio;
    const int currentSample = static_cast<int>(currentPosition * samplesPerSecond);
    
    if (currentSample >= totalSamples)
        return;
    
    // Prepare input buffer for RubberBand
    const int inputSamples = juce::jmin(numSamples, totalSamples - currentSample);
    juce::AudioBuffer<float> inputBuffer(channels, inputSamples);
    
    for (int ch = 0; ch < channels; ++ch)
    {
        inputBuffer.copyFrom(ch, 0, audioBuffer, ch, currentSample, inputSamples);
    }
    
    // Process through RubberBand stretcher
    std::vector<const float*> inputPointers(channels);
    std::vector<float*> outputPointers(channels);
    
    for (int ch = 0; ch < channels; ++ch)
    {
        inputPointers[ch] = inputBuffer.getReadPointer(ch);
        outputPointers[ch] = buffer.getWritePointer(ch, startSample);
    }
    
    stretcher->process(inputPointers.data(), inputSamples, false);
    
    int outputSamples = stretcher->available();
    outputSamples = juce::jmin(outputSamples, numSamples);
    
    if (outputSamples > 0)
    {
        stretcher->retrieve(outputPointers.data(), outputSamples);
        
        // Apply volume
        for (int ch = 0; ch < channels; ++ch)
        {
            buffer.applyGain(ch, startSample, outputSamples, volume);
        }
    }
    
    // Update position
    currentPosition += (double)inputSamples / sampleRate;
}

double AudioTrack::getDurationInSeconds() const
{
    if (audioBuffer.getNumSamples() > 0 && sampleRate > 0)
        return audioBuffer.getNumSamples() / sampleRate;
    return 0.0;
}

void AudioTrack::updateStretcher()
{
    if (audioBuffer.getNumSamples() > 0)
    {
        const int channels = audioBuffer.getNumChannels();
        
        RubberBand::RubberBandStretcher::Options options = 
            RubberBand::RubberBandStretcher::OptionProcessRealTime |
            RubberBand::RubberBandStretcher::OptionStretchElastic |
            RubberBand::RubberBandStretcher::OptionTransientsCrisp |
            RubberBand::RubberBandStretcher::OptionPhaseIndependent;
        
        stretcher.reset(new RubberBand::RubberBandStretcher(
            static_cast<size_t>(sampleRate),
            static_cast<size_t>(channels),
            options,
            stretchRatio,
            1.0
        ));
    }
}

// ============================================================================
// TrackComponent Implementation
// ============================================================================

TrackComponent::TrackComponent(AudioTrack* track, int trackNumber)
    : audioTrack(track),
      trackNum(trackNumber),
      loadButton("Load"),
      muteButton("M"),
      soloButton("S"),
      volumeSlider(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox),
      stretchSlider(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox),
      trackLabel("trackLabel", "Track " + juce::String(trackNumber + 1)),
      fileLabel("fileLabel", "No file loaded")
{
    addAndMakeVisible(loadButton);
    addAndMakeVisible(muteButton);
    addAndMakeVisible(soloButton);
    addAndMakeVisible(volumeSlider);
    addAndMakeVisible(stretchSlider);
    addAndMakeVisible(trackLabel);
    addAndMakeVisible(fileLabel);
    
    loadButton.onClick = [this] { loadButtonClicked(); };
    muteButton.onClick = [this] { muteButtonClicked(); };
    soloButton.onClick = [this] { soloButtonClicked(); };
    
    volumeSlider.setRange(0.0, 1.0, 0.01);
    volumeSlider.setValue(1.0);
    volumeSlider.onValueChange = [this] { volumeSliderChanged(); };
    
    stretchSlider.setRange(0.5, 2.0, 0.01);
    stretchSlider.setValue(1.0);
    stretchSlider.onValueChange = [this] { stretchSliderChanged(); };
    
    muteButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    soloButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    
    trackLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    fileLabel.setFont(juce::Font(12.0f));
    fileLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
}

TrackComponent::~TrackComponent()
{
}

void TrackComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2a2a2a));
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRect(getLocalBounds());
}

void TrackComponent::resized()
{
    juce::Rectangle<int> area = getLocalBounds().reduced(4);
    
    trackLabel.setBounds(area.removeFromTop(20));
    fileLabel.setBounds(area.removeFromTop(20));
    
    area.removeFromTop(5);
    
    juce::Rectangle<int> buttonArea = area.removeFromTop(30);
    loadButton.setBounds(buttonArea.removeFromLeft(60));
    buttonArea.removeFromLeft(5);
    muteButton.setBounds(buttonArea.removeFromLeft(30));
    buttonArea.removeFromLeft(5);
    soloButton.setBounds(buttonArea.removeFromLeft(30));
    
    area.removeFromTop(10);
    
    juce::Rectangle<int> volumeArea = area.removeFromTop(20);
    volumeArea.removeFromLeft(50);
    volumeSlider.setBounds(volumeArea);
    
    area.removeFromTop(5);
    
    juce::Rectangle<int> stretchArea = area.removeFromTop(20);
    stretchArea.removeFromLeft(50);
    stretchSlider.setBounds(stretchArea);
}

void TrackComponent::updateTrackInfo()
{
    if (audioTrack && audioTrack->isLoaded())
    {
        fileLabel.setText(audioTrack->getFileName(), juce::dontSendNotification);
    }
    else
    {
        fileLabel.setText("No file loaded", juce::dontSendNotification);
    }
    
    if (audioTrack)
    {
        muteButton.setToggleState(audioTrack->isMuted(), juce::dontSendNotification);
        soloButton.setToggleState(audioTrack->isSolo(), juce::dontSendNotification);
        volumeSlider.setValue(audioTrack->getVolume(), juce::dontSendNotification);
    }
}

void TrackComponent::loadButtonClicked()
{
    juce::FileChooser chooser("Select an audio file...",
                              juce::File(),
                              "*.wav;*.aiff;*.mp3;*.flac;*.ogg");
    
    chooser.launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                       [this](const juce::FileChooser& fc)
                       {
                           auto file = fc.getResult();
                           if (file.existsAsFile() && audioTrack)
                           {
                               audioTrack->loadAudioFile(file);
                               updateTrackInfo();
                           }
                       });
}

void TrackComponent::muteButtonClicked()
{
    if (audioTrack)
    {
        audioTrack->setMuted(!audioTrack->isMuted());
        muteButton.setToggleState(audioTrack->isMuted(), juce::dontSendNotification);
        muteButton.setColour(juce::TextButton::buttonColourId, 
                           audioTrack->isMuted() ? juce::Colours::red : juce::Colours::darkgrey);
    }
}

void TrackComponent::soloButtonClicked()
{
    if (audioTrack)
    {
        audioTrack->setSolo(!audioTrack->isSolo());
        soloButton.setToggleState(audioTrack->isSolo(), juce::dontSendNotification);
        soloButton.setColour(juce::TextButton::buttonColourId, 
                           audioTrack->isSolo() ? juce::Colours::yellow : juce::Colours::darkgrey);
    }
}

void TrackComponent::volumeSliderChanged()
{
    if (audioTrack)
    {
        audioTrack->setVolume(static_cast<float>(volumeSlider.getValue()));
    }
}

void TrackComponent::stretchSliderChanged()
{
    if (audioTrack)
    {
        audioTrack->setStretchRatio(stretchSlider.getValue());
    }
}

// ============================================================================
// TransportComponent Implementation
// ============================================================================

TransportComponent::TransportComponent()
    : playButton("Play"),
      stopButton("Stop"),
      recordButton("Rec"),
      tempoSlider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight),
      tempoLabel("tempoLabel", "BPM:"),
      positionLabel("positionLabel", "00:00"),
      playing(false),
      recording(false),
      currentTempo(120.0),
      currentPosition(0.0)
{
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(recordButton);
    addAndMakeVisible(tempoSlider);
    addAndMakeVisible(tempoLabel);
    addAndMakeVisible(positionLabel);
    
    playButton.onClick = [this] { playButtonClicked(); };
    stopButton.onClick = [this] { stopButtonClicked(); };
    recordButton.onClick = [this] { recordButtonClicked(); };
    
    tempoSlider.setRange(60.0, 200.0, 1.0);
    tempoSlider.setValue(120.0);
    tempoSlider.onValueChange = [this] { tempoSliderChanged(); };
    
    playButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green.darker());
    stopButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red.darker());
    recordButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red.darker());
    
    tempoLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    positionLabel.setFont(juce::Font(16.0f, juce::Font::bold));
}

TransportComponent::~TransportComponent()
{
}

void TransportComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRect(getLocalBounds());
}

void TransportComponent::resized()
{
    juce::Rectangle<int> area = getLocalBounds().reduced(8);
    
    juce::Rectangle<int> buttonArea = area.removeFromLeft(240);
    playButton.setBounds(buttonArea.removeFromLeft(60));
    buttonArea.removeFromLeft(5);
    stopButton.setBounds(buttonArea.removeFromLeft(60));
    buttonArea.removeFromLeft(5);
    recordButton.setBounds(buttonArea.removeFromLeft(60));
    
    area.removeFromLeft(20);
    
    juce::Rectangle<int> tempoArea = area.removeFromLeft(180);
    tempoLabel.setBounds(tempoArea.removeFromLeft(40));
    tempoSlider.setBounds(tempoArea);
    
    area.removeFromLeft(20);
    
    positionLabel.setBounds(area.removeFromLeft(80));
}

void TransportComponent::setPlaying(bool isPlaying)
{
    playing = isPlaying;
    playButton.setButtonText(playing ? "Pause" : "Play");
    playButton.setColour(juce::TextButton::buttonColourId, 
                        playing ? juce::Colours::orange : juce::Colours::green.darker());
}

void TransportComponent::setRecording(bool isRecording)
{
    recording = isRecording;
    recordButton.setToggleState(recording, juce::dontSendNotification);
    recordButton.setColour(juce::TextButton::buttonColourId, 
                          recording ? juce::Colours::red : juce::Colours::red.darker());
}

void TransportComponent::setTempo(double bpm)
{
    currentTempo = bpm;
    tempoSlider.setValue(bpm, juce::dontSendNotification);
}

void TransportComponent::setPosition(double positionInSeconds)
{
    currentPosition = positionInSeconds;
    
    int minutes = static_cast<int>(positionInSeconds) / 60;
    int seconds = static_cast<int>(positionInSeconds) % 60;
    
    positionLabel.setText(juce::String::formatted("%02d:%02d", minutes, seconds), 
                         juce::dontSendNotification);
}

void TransportComponent::playButtonClicked()
{
    if (onPlay)
        onPlay();
}

void TransportComponent::stopButtonClicked()
{
    if (onStop)
        onStop();
}

void TransportComponent::recordButtonClicked()
{
    if (onRecord)
        onRecord();
}

void TransportComponent::tempoSliderChanged()
{
    if (onTempoChanged)
        onTempoChanged(tempoSlider.getValue());
}

// ============================================================================
// MainComponent Implementation
// ============================================================================

MainComponent::MainComponent()
    : masterTempo(120.0),
      currentPlayPosition(0.0),
      isPlaying(false),
      isRecording(false)
{
    setupTracks();
    setupTransport();
    setupLayout();
    
    setSize(800, 600);
    setAudioChannels(0, 2);
    
    startTimer(50);
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    juce::ignoreUnused(samplesPerBlockExpected, sampleRate);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    juce::ScopedLock sl(audioLock);
    
    bufferToFill.clearActiveBufferRegion();
    
    if (!isPlaying)
        return;
    
    const int numSamples = bufferToFill.numSamples;
    
    // Mix all tracks
    for (auto& track : audioTracks)
    {
        if (track && track->isLoaded() && !track->isMuted())
        {
            track->processBlock(*bufferToFill.buffer, bufferToFill.startSample, numSamples);
        }
    }
    
    // Update play position
    const double sampleRate = 44100.0; // Should get from actual sample rate
    currentPlayPosition += numSamples / sampleRate;
}

void MainComponent::releaseResources()
{
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0f0f0f));
}

void MainComponent::resized()
{
    juce::Rectangle<int> area = getLocalBounds();
    
    transportComponent->setBounds(area.removeFromTop(60));
    
    tracksViewport.setBounds(area);
    
    int trackHeight = 150;
    tracksContainer.setSize(getWidth(), maxTracks * trackHeight);
    
    for (int i = 0; i < maxTracks; ++i)
    {
        trackComponents[i]->setBounds(0, i * trackHeight, getWidth() - 20, trackHeight - 5);
    }
}

void MainComponent::timerCallback()
{
    if (transportComponent)
    {
        transportComponent->setPosition(currentPlayPosition);
    }
    
    for (auto& trackComp : trackComponents)
    {
        if (trackComp)
        {
            trackComp->updateTrackInfo();
        }
    }
}

void MainComponent::play()
{
    juce::ScopedLock sl(audioLock);
    
    if (!isPlaying)
    {
        isPlaying = true;
        
        for (auto& track : audioTracks)
        {
            if (track)
            {
                track->setPosition(currentPlayPosition);
            }
        }
    }
    else
    {
        isPlaying = false;
    }
    
    if (transportComponent)
    {
        transportComponent->setPlaying(isPlaying);
    }
}

void MainComponent::stop()
{
    juce::ScopedLock sl(audioLock);
    
    isPlaying = false;
    currentPlayPosition = 0.0;
    
    for (auto& track : audioTracks)
    {
        if (track)
        {
            track->reset();
        }
    }
    
    if (transportComponent)
    {
        transportComponent->setPlaying(false);
        transportComponent->setPosition(0.0);
    }
}

void MainComponent::record()
{
    isRecording = !isRecording;
    
    if (transportComponent)
    {
        transportComponent->setRecording(isRecording);
    }
}

void MainComponent::setTempo(double bpm)
{
    masterTempo = bpm;
    
    if (transportComponent)
    {
        transportComponent->setTempo(bpm);
    }
}

void MainComponent::updatePlayPosition()
{
    juce::ScopedLock sl(audioLock);
    
    for (auto& track : audioTracks)
    {
        if (track)
        {
            track->setPosition(currentPlayPosition);
        }
    }
}

void MainComponent::setupTracks()
{
    for (int i = 0; i < maxTracks; ++i)
    {
        audioTracks[i] = std::make_unique<AudioTrack>();
        trackComponents[i] = std::make_unique<TrackComponent>(audioTracks[i].get(), i);
        
        tracksContainer.addAndMakeVisible(trackComponents[i].get());
    }
}

void MainComponent::setupTransport()
{
    transportComponent = std::make_unique<TransportComponent>();
    addAndMakeVisible(transportComponent.get());
    
    transportComponent->onPlay = [this] { play(); };
    transportComponent->onStop = [this] { stop(); };
    transportComponent->onRecord = [this] { record(); };
    transportComponent->onTempoChanged = [this](double bpm) { setTempo(bpm); };
}

void MainComponent::setupLayout()
{
    addAndMakeVisible(tracksViewport);
    tracksViewport.setViewedComponent(&tracksContainer, false);
    tracksViewport.setScrollBarsShown(true, false);
}
#include "MainComponent.h"

// ============================================================================
// WaveformComponent Implementation
// ============================================================================

WaveformComponent::WaveformComponent()
    : currentPosition(0.0),
      totalDuration(0.0),
      sampleRate(44100.0),
      totalSamples(0),
      isLooping(true)
{
}

WaveformComponent::~WaveformComponent()
{
    onPositionChanged = nullptr;
}

void WaveformComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));
    
    juce::Rectangle<int> area = getLocalBounds();
    
    // Draw border
    g.setColour(juce::Colours::darkgrey);
    g.drawRect(area, 1);
    
    area = area.reduced(2);
    
    if (waveformPeaks.empty())
    {
        // Draw placeholder text
        g.setColour(juce::Colours::grey);
        g.setFont(12.0f);
        g.drawText("No audio loaded", area, juce::Justification::centred);
        return;
    }
    
    // Draw waveform
    g.setColour(juce::Colour(0xff0080ff));
    
    const int width = area.getWidth();
    const int height = area.getHeight();
    const int centerY = area.getCentreY();
    
    if (width > 0 && !waveformPeaks.empty())
    {
        juce::Path waveformPath;
        bool pathStarted = false;
        
        for (int x = 0; x < width; ++x)
        {
            // Map x position to waveform data
            int peakIndex = juce::jmap(x, 0, width, 0, (int)waveformPeaks.size() - 1);
            peakIndex = juce::jlimit(0, (int)waveformPeaks.size() - 1, peakIndex);
            
            float peak = waveformPeaks[peakIndex];
            int waveHeight = (int)(peak * height * 0.4f); // Scale waveform
            
            int topY = centerY - waveHeight;
            int bottomY = centerY + waveHeight;
            
            if (!pathStarted)
            {
                waveformPath.startNewSubPath(area.getX() + x, topY);
                pathStarted = true;
            }
            else
            {
                waveformPath.lineTo(area.getX() + x, topY);
            }
        }
        
        // Complete the waveform shape
        for (int x = width - 1; x >= 0; --x)
        {
            int peakIndex = juce::jmap(x, 0, width, 0, (int)waveformPeaks.size() - 1);
            peakIndex = juce::jlimit(0, (int)waveformPeaks.size() - 1, peakIndex);
            
            float peak = waveformPeaks[peakIndex];
            int waveHeight = (int)(peak * height * 0.4f);
            int bottomY = centerY + waveHeight;
            
            waveformPath.lineTo(area.getX() + x, bottomY);
        }
        
        waveformPath.closeSubPath();
        g.fillPath(waveformPath);
    }
    
    // Draw playback position
    if (totalDuration > 0.0)
    {
        double normalizedPosition = currentPosition / totalDuration;
        int positionX = area.getX() + (int)(normalizedPosition * width);
        
        g.setColour(juce::Colours::yellow);
        g.drawVerticalLine(positionX, area.getY(), area.getBottom());
        
        // Draw position marker
        g.fillEllipse(positionX - 3, area.getY() - 3, 6, 6);
    }
    
    // Draw loop indicator
    if (isLooping)
    {
        g.setColour(juce::Colours::green.withAlpha(0.3f));
        g.fillRect(area.getX(), area.getBottom() - 3, area.getWidth(), 3);
    }
}

void WaveformComponent::mouseDown(const juce::MouseEvent& event)
{
    updatePositionFromMouse(event);
}

void WaveformComponent::mouseDrag(const juce::MouseEvent& event)
{
    updatePositionFromMouse(event);
}

void WaveformComponent::updatePositionFromMouse(const juce::MouseEvent& event)
{
    if (totalDuration <= 0.0)
        return;
    
    juce::Rectangle<int> area = getLocalBounds().reduced(2);
    int mouseX = event.x - area.getX();
    double normalizedPosition = (double)mouseX / area.getWidth();
    normalizedPosition = juce::jlimit(0.0, 1.0, normalizedPosition);
    
    double newPosition = normalizedPosition * totalDuration;
    
    if (onPositionChanged)
    {
        onPositionChanged(newPosition);
    }
    
    repaint();
}

void WaveformComponent::setWaveformData(const std::vector<float>& peaks, double sr, int samples)
{
    waveformPeaks = peaks;
    sampleRate = sr;
    totalSamples = samples;
    totalDuration = samples / sr;
    repaint();
}

void WaveformComponent::setPlayPosition(double positionInSeconds)
{
    if (currentPosition != positionInSeconds)
    {
        currentPosition = positionInSeconds;
        repaint();
    }
}

void WaveformComponent::setDuration(double durationInSeconds)
{
    totalDuration = durationInSeconds;
}

void WaveformComponent::setLooping(bool shouldLoop)
{
    if (isLooping != shouldLoop)
    {
        isLooping = shouldLoop;
        repaint();
    }
}

// ============================================================================
// AudioTrack Implementation
// ============================================================================

AudioTrack::AudioTrack()
    : sampleRate(44100.0),
      currentPosition(0.0),
      stretchRatio(1.0),
      detectedBPM(0.0),
      masterBPM(120.0),
      muted(false),
      solo(false),
      looping(true),
      volume(1.0f)
{
    formatManager.registerBasicFormats();
}

AudioTrack::~AudioTrack()
{
    juce::ScopedLock sl(lock);
    
    // Clear stretcher first
    if (stretcher)
    {
        stretcher.reset();
    }
    
    // Clear audio buffer
    audioBuffer.clear();
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
        
        // Generate waveform peaks for visualization
        generateWaveformPeaks();
        
        // Detect BPM automatically
        detectedBPM = detectBPM();
        
        // Auto-sync to master BPM if detected
        if (detectedBPM > 0.0)
        {
            autoSyncToMaster();
        }
        
        updateStretcher();
        
        juce::Logger::writeToLog("Loaded: " + fileName + " - Detected BPM: " + juce::String(detectedBPM, 1));
    }
}

void AudioTrack::generateWaveformPeaks()
{
    waveformPeaks.clear();
    
    if (audioBuffer.getNumSamples() == 0)
        return;
    
    const int numSamples = audioBuffer.getNumSamples();
    const int numChannels = audioBuffer.getNumChannels();
    const int peaksPerSecond = 100; // Resolution of waveform
    const int samplesPerPeak = juce::jmax(1, (int)(sampleRate / peaksPerSecond));
    const int numPeaks = (numSamples + samplesPerPeak - 1) / samplesPerPeak;
    
    waveformPeaks.reserve(numPeaks);
    
    for (int peakIndex = 0; peakIndex < numPeaks; ++peakIndex)
    {
        int startSample = peakIndex * samplesPerPeak;
        int endSample = juce::jmin(startSample + samplesPerPeak, numSamples);
        
        float maxPeak = 0.0f;
        
        // Find the maximum peak in this segment across all channels
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float* channelData = audioBuffer.getReadPointer(channel);
            
            for (int sample = startSample; sample < endSample; ++sample)
            {
                maxPeak = juce::jmax(maxPeak, std::abs(channelData[sample]));
            }
        }
        
        waveformPeaks.push_back(maxPeak);
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

void AudioTrack::setMasterBPM(double newMasterBPM)
{
    juce::ScopedLock sl(lock);
    masterBPM = newMasterBPM;
    autoSyncToMaster();
}

void AudioTrack::processBlock(juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    juce::ScopedLock sl(lock);
    
    if (!isLoaded() || muted || numSamples <= 0 || startSample < 0)
    {
        return;
    }
    
    if (!stretcher)
    {
        updateStretcher();
        if (!stretcher)
            return;
    }
    
    const int outputChannels = buffer.getNumChannels();
    const int inputChannels = audioBuffer.getNumChannels();
    const int totalSamples = audioBuffer.getNumSamples();
    
    if (outputChannels <= 0 || inputChannels <= 0 || totalSamples <= 0)
        return;
    
    // Ensure we don't write beyond buffer bounds
    if (startSample + numSamples > buffer.getNumSamples())
        return;
    
    // Calculate the current sample position in the original audio
    const double samplesPerSecond = sampleRate;
    int currentSample = static_cast<int>(currentPosition * samplesPerSecond);
    
    // Handle looping
    if (looping && currentSample >= totalSamples)
    {
        currentSample = currentSample % totalSamples;
        currentPosition = currentSample / samplesPerSecond;
    }
    else if (currentSample >= totalSamples)
    {
        return; // Past end and not looping
    }
    
    // Calculate how many samples we can read from current position
    int samplesToRead = juce::jmin(numSamples, totalSamples - currentSample);
    if (samplesToRead <= 0)
        return;
    
    // Create temporary buffer for audio processing
    juce::AudioBuffer<float> tempBuffer(inputChannels, samplesToRead);
    
    // Copy audio data safely
    for (int ch = 0; ch < inputChannels; ++ch)
    {
        tempBuffer.copyFrom(ch, 0, audioBuffer, ch, currentSample, samplesToRead);
    }
    
    // Simple playback without stretcher for now to avoid crashes
    // Mix directly to output buffer
    const int channelsToProcess = juce::jmin(outputChannels, inputChannels);
    
    for (int ch = 0; ch < channelsToProcess; ++ch)
    {
        // Add to existing buffer content (mixing)
        buffer.addFrom(ch, startSample, tempBuffer, ch, 0, samplesToRead, volume);
    }
    
    // Handle mono to stereo conversion
    if (inputChannels == 1 && outputChannels >= 2)
    {
        buffer.addFrom(1, startSample, tempBuffer, 0, 0, samplesToRead, volume);
    }
    
    // Update position
    currentPosition += (double)samplesToRead / sampleRate;
    
    // Handle looping wrap-around
    if (looping && currentPosition * sampleRate >= totalSamples)
    {
        currentPosition = 0.0;
    }
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

double AudioTrack::detectBPM()
{
    if (!isLoaded() || audioBuffer.getNumSamples() == 0)
        return 0.0;
    
    // Simple BPM detection using onset detection
    const int hopSize = 512;
    const int fftSize = 1024;
    const float* audioData = audioBuffer.getReadPointer(0);
    const int numSamples = audioBuffer.getNumSamples();
    
    std::vector<float> onsetStrengths;
    std::vector<float> prevSpectrum(fftSize / 2, 0.0f);
    
    // Analyze audio in chunks
    for (int i = 0; i < numSamples - fftSize; i += hopSize)
    {
        std::vector<float> window(fftSize);
        
        // Copy windowed audio
        for (int j = 0; j < fftSize; ++j)
        {
            if (i + j < numSamples)
            {
                // Apply Hanning window
                float hannWindow = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * j / (fftSize - 1)));
                window[j] = audioData[i + j] * hannWindow;
            }
        }
        
        // Simple spectral flux calculation
        std::vector<float> spectrum(fftSize / 2, 0.0f);
        for (int j = 0; j < fftSize / 2; ++j)
        {
            spectrum[j] = window[j] * window[j]; // Simple power spectrum approximation
        }
        
        // Calculate onset strength (spectral flux)
        float onsetStrength = 0.0f;
        for (int j = 0; j < fftSize / 2; ++j)
        {
            float diff = spectrum[j] - prevSpectrum[j];
            if (diff > 0.0f)
                onsetStrength += diff;
        }
        
        onsetStrengths.push_back(onsetStrength);
        prevSpectrum = spectrum;
    }
    
    // Find peaks in onset strength
    std::vector<int> peakIndices;
    const float threshold = *std::max_element(onsetStrengths.begin(), onsetStrengths.end()) * 0.3f;
    
    for (int i = 1; i < onsetStrengths.size() - 1; ++i)
    {
        if (onsetStrengths[i] > threshold &&
            onsetStrengths[i] > onsetStrengths[i-1] &&
            onsetStrengths[i] > onsetStrengths[i+1])
        {
            peakIndices.push_back(i);
        }
    }
    
    // Calculate intervals between peaks
    if (peakIndices.size() < 4)
        return 120.0; // Default BPM if detection fails
    
    std::vector<double> intervals;
    for (int i = 1; i < peakIndices.size(); ++i)
    {
        double interval = (peakIndices[i] - peakIndices[i-1]) * hopSize / sampleRate;
        if (interval > 0.2 && interval < 2.0) // Reasonable beat intervals
            intervals.push_back(interval);
    }
    
    if (intervals.empty())
        return 120.0;
    
    // Find most common interval
    std::sort(intervals.begin(), intervals.end());
    double medianInterval = intervals[intervals.size() / 2];
    
    // Convert to BPM
    double bpm = 60.0 / medianInterval;
    
    // Snap to reasonable BPM range
    if (bpm < 60.0) bpm *= 2.0;
    if (bpm > 200.0) bpm /= 2.0;
    if (bpm < 60.0) bpm = 120.0;
    if (bpm > 200.0) bpm = 120.0;
    
    return bpm;
}

void AudioTrack::autoSyncToMaster()
{
    if (detectedBPM > 0.0 && masterBPM > 0.0)
    {
        double newStretchRatio = detectedBPM / masterBPM;
        setStretchRatio(newStretchRatio);
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
      loopButton("Loop"),
      volumeSlider(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox),
      stretchSlider(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox),
      trackLabel("trackLabel", "Track " + juce::String(trackNumber + 1)),
      fileLabel("fileLabel", "No file loaded"),
      bpmLabel("bpmLabel", "BPM: --"),
      stretchLabel("stretchLabel", "Stretch: 1.00x"),
      volumeLabel("volumeLabel", "Vol")
{
    // Create waveform display
    waveformDisplay = std::make_unique<WaveformComponent>();
    addAndMakeVisible(waveformDisplay.get());
    
    addAndMakeVisible(loadButton);
    addAndMakeVisible(muteButton);
    addAndMakeVisible(soloButton);
    addAndMakeVisible(loopButton);
    addAndMakeVisible(volumeSlider);
    addAndMakeVisible(stretchSlider);
    addAndMakeVisible(trackLabel);
    addAndMakeVisible(fileLabel);
    addAndMakeVisible(bpmLabel);
    addAndMakeVisible(stretchLabel);
    addAndMakeVisible(volumeLabel);
    
    loadButton.onClick = [this] { loadButtonClicked(); };
    muteButton.onClick = [this] { muteButtonClicked(); };
    soloButton.onClick = [this] { soloButtonClicked(); };
    loopButton.onClick = [this] { loopButtonClicked(); };
    
    volumeSlider.setRange(0.0, 1.0, 0.01);
    volumeSlider.setValue(1.0);
    volumeSlider.onValueChange = [this] { volumeSliderChanged(); };
    
    stretchSlider.setRange(0.5, 2.0, 0.01);
    stretchSlider.setValue(1.0);
    stretchSlider.onValueChange = [this] { stretchSliderChanged(); };
    
    // Setup waveform callback
    waveformDisplay->onPositionChanged = [this](double position) { onWaveformPositionChanged(position); };
    
    muteButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    soloButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    loopButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green.darker());
    
    trackLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    fileLabel.setFont(juce::Font(12.0f));
    bpmLabel.setFont(juce::Font(11.0f));
    stretchLabel.setFont(juce::Font(11.0f));
    volumeLabel.setFont(juce::Font(11.0f));
    
    fileLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    bpmLabel.setColour(juce::Label::textColourId, juce::Colours::lightblue);
    stretchLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    volumeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    
    loopButton.setToggleState(true, juce::dontSendNotification);
}

TrackComponent::~TrackComponent()
{
    // Clear all button callbacks to prevent dangling references
    loadButton.onClick = nullptr;
    muteButton.onClick = nullptr;
    soloButton.onClick = nullptr;
    loopButton.onClick = nullptr;
    volumeSlider.onValueChange = nullptr;
    stretchSlider.onValueChange = nullptr;
    
    if (waveformDisplay)
    {
        waveformDisplay->onPositionChanged = nullptr;
    }
    
    // Clear audio track reference
    audioTrack = nullptr;
}

void TrackComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2a2a2a));
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRect(getLocalBounds());
    
    // Draw level indicator if audio is loaded
    if (audioTrack && audioTrack->isLoaded())
    {
        g.setColour(juce::Colours::green.withAlpha(0.3f));
        g.fillRect(2, 2, getWidth() - 4, 3);
    }
}

void TrackComponent::resized()
{
    juce::Rectangle<int> area = getLocalBounds().reduced(4);
    
    trackLabel.setBounds(area.removeFromTop(18));
    fileLabel.setBounds(area.removeFromTop(16));
    
    juce::Rectangle<int> infoArea = area.removeFromTop(16);
    bpmLabel.setBounds(infoArea.removeFromLeft(infoArea.getWidth() / 2));
    stretchLabel.setBounds(infoArea);
    
    area.removeFromTop(5);
    
    // Waveform display - main visual area
    waveformDisplay->setBounds(area.removeFromTop(80));
    
    area.removeFromTop(5);
    
    juce::Rectangle<int> buttonArea = area.removeFromTop(30);
    loadButton.setBounds(buttonArea.removeFromLeft(50));
    buttonArea.removeFromLeft(3);
    muteButton.setBounds(buttonArea.removeFromLeft(25));
    buttonArea.removeFromLeft(3);
    soloButton.setBounds(buttonArea.removeFromLeft(25));
    buttonArea.removeFromLeft(3);
    loopButton.setBounds(buttonArea.removeFromLeft(40));
    
    area.removeFromTop(8);
    
    juce::Rectangle<int> volumeArea = area.removeFromTop(20);
    volumeLabel.setBounds(volumeArea.removeFromLeft(30));
    volumeSlider.setBounds(volumeArea);
    
    area.removeFromTop(5);
    
    juce::Rectangle<int> stretchArea = area.removeFromTop(20);
    auto strLabel = stretchArea.removeFromLeft(35);
    stretchSlider.setBounds(stretchArea);
}

void TrackComponent::updateTrackInfo()
{
    if (audioTrack && audioTrack->isLoaded())
    {
        fileLabel.setText(audioTrack->getFileName(), juce::dontSendNotification);
        
        double bpm = audioTrack->getDetectedBPM();
        if (bpm > 0.0)
            bpmLabel.setText("BPM: " + juce::String(bpm, 1), juce::dontSendNotification);
        else
            bpmLabel.setText("BPM: --", juce::dontSendNotification);
            
        // Update waveform position
        waveformDisplay->setPlayPosition(audioTrack->getCurrentPosition());
    }
    else
    {
        fileLabel.setText("No file loaded", juce::dontSendNotification);
        bpmLabel.setText("BPM: --", juce::dontSendNotification);
    }
    
    if (audioTrack)
    {
        muteButton.setToggleState(audioTrack->isMuted(), juce::dontSendNotification);
        soloButton.setToggleState(audioTrack->isSolo(), juce::dontSendNotification);
        loopButton.setToggleState(audioTrack->isLooping(), juce::dontSendNotification);
        volumeSlider.setValue(audioTrack->getVolume(), juce::dontSendNotification);
        
        waveformDisplay->setLooping(audioTrack->isLooping());
        
        stretchLabel.setText("Stretch: " + juce::String(stretchSlider.getValue(), 2) + "x",
                           juce::dontSendNotification);
    }
}

void TrackComponent::updateWaveform()
{
    if (audioTrack && audioTrack->isLoaded())
    {
        waveformDisplay->setWaveformData(audioTrack->getWaveformPeaks(),
                                       44100.0, // Sample rate
                                       audioTrack->getDurationInSeconds() * 44100.0);
        waveformDisplay->setDuration(audioTrack->getDurationInSeconds());
    }
}

void TrackComponent::loadButtonClicked()
{
    // Use a safer FileChooser approach without raw pointers
    auto chooser = std::make_shared<juce::FileChooser>("Select an audio file...",
                                                       juce::File(),
                                                       "*.wav;*.aiff;*.mp3;*.flac;*.ogg");
    
    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    
    chooser->launchAsync(chooserFlags, [this, chooser](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file.existsAsFile() && audioTrack)
        {
            audioTrack->loadAudioFile(file);
            updateTrackInfo();
            updateWaveform();
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

void TrackComponent::loopButtonClicked()
{
    if (audioTrack)
    {
        audioTrack->setLooping(!audioTrack->isLooping());
        loopButton.setToggleState(audioTrack->isLooping(), juce::dontSendNotification);
        loopButton.setColour(juce::TextButton::buttonColourId,
                           audioTrack->isLooping() ? juce::Colours::green : juce::Colours::darkgrey);
                           
        waveformDisplay->setLooping(audioTrack->isLooping());
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
        stretchLabel.setText("Stretch: " + juce::String(stretchSlider.getValue(), 2) + "x",
                           juce::dontSendNotification);
    }
}

void TrackComponent::onWaveformPositionChanged(double position)
{
    if (audioTrack)
    {
        audioTrack->setPosition(position);
    }
}

// ============================================================================
// TransportComponent Implementation
// ============================================================================

TransportComponent::TransportComponent()
    : playButton("Play"),
      stopButton("Stop"),
      recordButton("Rec"),
      autoSyncButton("Auto Sync"),
      tempoSlider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight),
      tempoLabel("tempoLabel", "Master BPM:"),
      positionLabel("positionLabel", "00:00"),
      masterBpmLabel("masterBpmLabel", "120.0"),
      playing(false),
      recording(false),
      autoSyncEnabled(true),
      currentTempo(120.0),
      currentPosition(0.0)
{
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(recordButton);
    addAndMakeVisible(autoSyncButton);
    addAndMakeVisible(tempoSlider);
    addAndMakeVisible(tempoLabel);
    addAndMakeVisible(positionLabel);
    addAndMakeVisible(masterBpmLabel);
    
    playButton.onClick = [this] { playButtonClicked(); };
    stopButton.onClick = [this] { stopButtonClicked(); };
    recordButton.onClick = [this] { recordButtonClicked(); };
    autoSyncButton.onClick = [this] { autoSyncButtonClicked(); };
    
    tempoSlider.setRange(60.0, 200.0, 1.0);
    tempoSlider.setValue(120.0);
    tempoSlider.onValueChange = [this] { tempoSliderChanged(); };
    
    playButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green.darker());
    stopButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red.darker());
    recordButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red.darker());
    autoSyncButton.setColour(juce::TextButton::buttonColourId, juce::Colours::blue.darker());
    
    tempoLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    positionLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    masterBpmLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    
    autoSyncButton.setToggleState(true, juce::dontSendNotification);
    masterBpmLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
}

TransportComponent::~TransportComponent()
{
    // Clear all callbacks to prevent dangling references
    onPlay = nullptr;
    onStop = nullptr;
    onRecord = nullptr;
    onTempoChanged = nullptr;
    onAutoSync = nullptr;
    
    // Clear button callbacks
    playButton.onClick = nullptr;
    stopButton.onClick = nullptr;
    recordButton.onClick = nullptr;
    autoSyncButton.onClick = nullptr;
    tempoSlider.onValueChange = nullptr;
}

void TransportComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRect(getLocalBounds());
    
    // Draw sync indicator
    if (autoSyncEnabled)
    {
        g.setColour(juce::Colours::green.withAlpha(0.3f));
        g.fillRect(getWidth() - 20, 5, 15, 8);
    }
}

void TransportComponent::resized()
{
    juce::Rectangle<int> area = getLocalBounds().reduced(8);
    
    juce::Rectangle<int> buttonArea = area.removeFromLeft(280);
    playButton.setBounds(buttonArea.removeFromLeft(60));
    buttonArea.removeFromLeft(5);
    stopButton.setBounds(buttonArea.removeFromLeft(60));
    buttonArea.removeFromLeft(5);
    recordButton.setBounds(buttonArea.removeFromLeft(60));
    buttonArea.removeFromLeft(5);
    autoSyncButton.setBounds(buttonArea.removeFromLeft(80));
    
    area.removeFromLeft(20);
    
    juce::Rectangle<int> tempoArea = area.removeFromLeft(220);
    tempoLabel.setBounds(tempoArea.removeFromTop(20));
    auto sliderArea = tempoArea.removeFromTop(25);
    tempoSlider.setBounds(sliderArea.removeFromLeft(140));
    sliderArea.removeFromLeft(10);
    masterBpmLabel.setBounds(sliderArea);
    
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
    masterBpmLabel.setText(juce::String(bpm, 1), juce::dontSendNotification);
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

void TransportComponent::autoSyncButtonClicked()
{
    autoSyncEnabled = !autoSyncEnabled;
    autoSyncButton.setToggleState(autoSyncEnabled, juce::dontSendNotification);
    autoSyncButton.setColour(juce::TextButton::buttonColourId,
                           autoSyncEnabled ? juce::Colours::blue : juce::Colours::darkgrey);
    
    if (onAutoSync)
        onAutoSync();
    
    repaint();
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
      isRecording(false),
      autoSyncEnabled(true)
{
    setupTracks();
    setupTransport();
    setupLayout();
    
    setSize(900, 800);
    setAudioChannels(0, 2);
    
    startTimer(50); // Faster timer for smooth waveform updates
}

MainComponent::~MainComponent()
{
    // Stop timer first to prevent callbacks during destruction
    stopTimer();
    
    // Shutdown audio system
    shutdownAudio();
    
    // Clear transport callbacks to avoid dangling references
    if (transportComponent)
    {
        transportComponent->onPlay = nullptr;
        transportComponent->onStop = nullptr;
        transportComponent->onRecord = nullptr;
        transportComponent->onTempoChanged = nullptr;
        transportComponent->onAutoSync = nullptr;
    }
    
    // Clear all track references
    for (auto& track : audioTracks)
    {
        if (track)
        {
            track.reset();
        }
    }
    
    // Clear all component references
    for (auto& trackComp : trackComponents)
    {
        if (trackComp)
        {
            trackComp.reset();
        }
    }
    
    // Clear transport component
    transportComponent.reset();
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
    
    // Check for solo tracks
    bool hasSolo = false;
    for (auto& track : audioTracks)
    {
        if (track && track->isSolo())
        {
            hasSolo = true;
            break;
        }
    }
    
    // Mix all tracks
    for (auto& track : audioTracks)
    {
        if (track && track->isLoaded())
        {
            // Skip muted tracks, or non-solo tracks when solo is active
            if (track->isMuted() || (hasSolo && !track->isSolo()))
                continue;
                
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
    
    // Draw title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText("STRETCHER - Multitrack Audio Looper with Waveforms", 10, 5, 400, 20, juce::Justification::left);
}

void MainComponent::resized()
{
    juce::Rectangle<int> area = getLocalBounds();
    
    area.removeFromTop(25); // Title area
    
    transportComponent->setBounds(area.removeFromTop(80));
    
    tracksViewport.setBounds(area);
    
    int trackHeight = 220; // Increased height for waveform display
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
    
    // Auto-sync tempo if enabled
    if (autoSyncEnabled && !isPlaying)
    {
        double avgBPM = findAverageBPM();
        if (avgBPM > 0.0 && std::abs(avgBPM - masterTempo) > 1.0)
        {
            setTempo(avgBPM);
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
    
    // Update all tracks with new master BPM
    for (auto& track : audioTracks)
    {
        if (track)
        {
            track->setMasterBPM(bpm);
        }
    }
    
    if (transportComponent)
    {
        transportComponent->setTempo(bpm);
    }
}

void MainComponent::autoSyncAllTracks()
{
    autoSyncEnabled = !autoSyncEnabled;
    
    if (autoSyncEnabled)
    {
        // Find average BPM and sync all tracks
        double avgBPM = findAverageBPM();
        if (avgBPM > 0.0)
        {
            setTempo(avgBPM);
        }
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

void MainComponent::setTrackPosition(int trackIndex, double position)
{
    if (trackIndex >= 0 && trackIndex < maxTracks && audioTracks[trackIndex])
    {
        audioTracks[trackIndex]->setPosition(position);
        currentPlayPosition = position;
    }
}

double MainComponent::findAverageBPM()
{
    std::vector<double> bpms;
    
    for (auto& track : audioTracks)
    {
        if (track && track->isLoaded())
        {
            double bpm = track->getDetectedBPM();
            if (bpm > 60.0 && bpm < 200.0)
            {
                bpms.push_back(bpm);
            }
        }
    }
    
    if (bpms.empty())
        return 0.0;
    
    double sum = 0.0;
    for (double bpm : bpms)
    {
        sum += bpm;
    }
    
    return sum / bpms.size();
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
    transportComponent->onAutoSync = [this] { autoSyncAllTracks(); };
}

void MainComponent::setupLayout()
{
    addAndMakeVisible(tracksViewport);
    tracksViewport.setViewedComponent(&tracksContainer, false);
    tracksViewport.setScrollBarsShown(true, false);
}

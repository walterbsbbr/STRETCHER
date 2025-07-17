#include "MainComponent.h"

// ============================================================================
// WaveformComponent Implementation
// ============================================================================

WaveformComponent::WaveformComponent()
    : currentPosition(0.0),
      totalDuration(0.0),
      sampleRate(44100.0),
      totalSamples(0),
      isLooping(true),
      detectedBPM(120.0),
      waveformColour(juce::Colour(0xff0080ff)),
      quantizeDivisions(8)
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
    
    // Draw grid first (behind waveform)
    drawGrid(g, area);
    
    if (waveformPeaks.empty())
    {
        // Draw placeholder text
        g.setColour(juce::Colours::grey);
        g.setFont(12.0f);
        g.drawText("No audio loaded", area, juce::Justification::centred);
        return;
    }
    
    // Draw waveform with track-specific color
    g.setColour(waveformColour);
    
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
    
    // Draw beat lines on top of waveform
    drawBeatLines(g, area);
    
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

void WaveformComponent::drawGrid(juce::Graphics& g, const juce::Rectangle<int>& area)
{
    // Draw amplitude grid lines (horizontal)
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    
    const int centerY = area.getCentreY();
    const int quarterHeight = area.getHeight() / 4;
    
    // Center line
    g.drawHorizontalLine(centerY, area.getX(), area.getRight());
    
    // Quarter lines
    g.drawHorizontalLine(centerY - quarterHeight, area.getX(), area.getRight());
    g.drawHorizontalLine(centerY + quarterHeight, area.getX(), area.getRight());
    
    // Half lines
    g.drawHorizontalLine(centerY - quarterHeight / 2, area.getX(), area.getRight());
    g.drawHorizontalLine(centerY + quarterHeight / 2, area.getX(), area.getRight());
}

void WaveformComponent::drawBeatLines(juce::Graphics& g, const juce::Rectangle<int>& area)
{
    if (totalDuration <= 0.0 || quantizeDivisions <= 0)
        return;
    
    // Calculate exact interval by dividing total duration by chosen divisions
    double divisionInterval = totalDuration / (double)quantizeDivisions;
    
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    
    const int width = area.getWidth();
    
    // Draw vertical lines at each division (excluding start and end)
    for (int i = 1; i < quantizeDivisions; ++i)
    {
        double divisionTime = i * divisionInterval;
        double normalizedPosition = divisionTime / totalDuration;
        int divisionX = area.getX() + (int)(normalizedPosition * width);
        
        if (divisionX >= area.getX() && divisionX < area.getRight())
        {
            // Draw stronger line for every 4th division (downbeats)
            if (i % 4 == 0)
            {
                g.setColour(juce::Colours::white.withAlpha(0.25f));
                g.drawVerticalLine(divisionX, area.getY(), area.getBottom());
                g.setColour(juce::Colours::white.withAlpha(0.15f));
            }
            else
            {
                g.drawVerticalLine(divisionX, area.getY(), area.getBottom());
            }
        }
    }
}

void WaveformComponent::setQuantizeValue(int quantizeValue)
{
    if (quantizeDivisions != quantizeValue)
    {
        quantizeDivisions = quantizeValue;
        repaint();
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
    repaint();
}

void WaveformComponent::setLooping(bool shouldLoop)
{
    if (isLooping != shouldLoop)
    {
        isLooping = shouldLoop;
        repaint();
    }
}

void WaveformComponent::setDetectedBPM(double bpm)
{
    if (detectedBPM != bpm)
    {
        detectedBPM = bpm;
        repaint();
    }
}

void WaveformComponent::setWaveformColour(const juce::Colour& colour)
{
    if (waveformColour != colour)
    {
        waveformColour = colour;
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
    soundTouch = std::make_unique<soundtouch::SoundTouch>();
}

AudioTrack::~AudioTrack()
{
    juce::ScopedLock sl(lock);
    
    if (soundTouch)
    {
        soundTouch.reset();
    }
    
    audioBuffer.clear();
    stretchedBuffer.clear();
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
        stretchRatio = 1.0;
        
        generateWaveformPeaks();
        detectedBPM = detectBPM();
        initializeSoundTouch();
        
        stretchedBuffer.setSize(reader->numChannels, 8192, false, false, true);
        
        juce::Logger::writeToLog("Loaded: " + fileName +
                                " - Detected BPM: " + juce::String(detectedBPM, 1) +
                                " - Stretch Ratio: " + juce::String(stretchRatio, 3) +
                                " (SoundTouch mode)");
    }
}

void AudioTrack::initializeSoundTouch()
{
    if (soundTouch && audioBuffer.getNumSamples() > 0)
    {
        soundTouch->setSampleRate(static_cast<uint32_t>(sampleRate));
        soundTouch->setChannels(audioBuffer.getNumChannels());
        soundTouch->setTempo(stretchRatio);
        soundTouch->setPitch(1.0);
        soundTouch->clear();
    }
}

void AudioTrack::generateWaveformPeaks()
{
    waveformPeaks.clear();
    
    if (audioBuffer.getNumSamples() == 0)
        return;
    
    const int numSamples = audioBuffer.getNumSamples();
    const int numChannels = audioBuffer.getNumChannels();
    const int peaksPerSecond = 100;
    const int samplesPerPeak = juce::jmax(1, (int)(sampleRate / peaksPerSecond));
    const int numPeaks = (numSamples + samplesPerPeak - 1) / samplesPerPeak;
    
    waveformPeaks.reserve(numPeaks);
    
    for (int peakIndex = 0; peakIndex < numPeaks; ++peakIndex)
    {
        int startSample = peakIndex * samplesPerPeak;
        int endSample = juce::jmin(startSample + samplesPerPeak, numSamples);
        
        float maxPeak = 0.0f;
        
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
    
    double newRatio = juce::jlimit(0.25, 4.0, ratio);
    if (std::abs(newRatio - stretchRatio) > 0.001)
    {
        stretchRatio = newRatio;
    }
}

void AudioTrack::scaleStretchRatio(double scaleFactor)
{
    juce::ScopedLock sl(lock);
    
    double newRatio = stretchRatio * scaleFactor;
    newRatio = juce::jlimit(0.25, 4.0, newRatio);
    
    if (std::abs(newRatio - stretchRatio) > 0.001)
    {
        stretchRatio = newRatio;
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
    if (soundTouch)
        soundTouch->clear();
}

void AudioTrack::setMasterBPM(double newMasterBPM)
{
    juce::ScopedLock sl(lock);
    masterBPM = newMasterBPM;
}

void AudioTrack::processBlock(juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    juce::ScopedLock sl(lock);
    
    if (!isLoaded() || muted || numSamples <= 0 || startSample < 0)
    {
        return;
    }
    
    const int outputChannels = buffer.getNumChannels();
    const int inputChannels = audioBuffer.getNumChannels();
    const int totalSamples = audioBuffer.getNumSamples();
    
    if (outputChannels <= 0 || inputChannels <= 0 || totalSamples <= 0)
        return;
    
    if (startSample + numSamples > buffer.getNumSamples())
        return;
    
    if (std::abs(stretchRatio - 1.0) < 0.02)
    {
        processDirectPlayback(buffer, startSample, numSamples);
    }
    else
    {
        processWithSoundTouch(buffer, startSample, numSamples);
    }
}

void AudioTrack::processDirectPlayback(juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    const int outputChannels = buffer.getNumChannels();
    const int inputChannels = audioBuffer.getNumChannels();
    const int totalSamples = audioBuffer.getNumSamples();
    const int channelsToProcess = juce::jmin(outputChannels, inputChannels);
    
    int currentSample = static_cast<int>(currentPosition * sampleRate);
    
    if (looping && currentSample >= totalSamples)
    {
        currentSample = currentSample % totalSamples;
        currentPosition = currentSample / sampleRate;
    }
    else if (currentSample >= totalSamples)
    {
        return;
    }
    
    int samplesToRead = juce::jmin(numSamples, totalSamples - currentSample);
    if (samplesToRead <= 0)
        return;
    
    for (int ch = 0; ch < channelsToProcess; ++ch)
    {
        buffer.addFrom(ch, startSample, audioBuffer, ch, currentSample, samplesToRead, volume);
    }
    
    if (inputChannels == 1 && outputChannels >= 2)
    {
        buffer.addFrom(1, startSample, audioBuffer, 0, currentSample, samplesToRead, volume);
    }
    
    currentPosition += (double)samplesToRead / sampleRate;
    
    if (looping && currentPosition * sampleRate >= totalSamples)
    {
        currentPosition = 0.0;
    }
}

void AudioTrack::processWithSoundTouch(juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    const int outputChannels = buffer.getNumChannels();
    const int inputChannels = audioBuffer.getNumChannels();
    const int totalSamples = audioBuffer.getNumSamples();
    const int channelsToProcess = juce::jmin(outputChannels, inputChannels);
    
    if (!soundTouch)
        return;
    
    soundTouch->setTempo(stretchRatio);
    
    int currentSample = static_cast<int>(currentPosition * sampleRate);
    
    if (looping && currentSample >= totalSamples)
    {
        currentSample = currentSample % totalSamples;
        currentPosition = currentSample / sampleRate;
        soundTouch->clear();
    }
    else if (currentSample >= totalSamples)
    {
        return;
    }
    
    int samplesToRead = juce::jmin(numSamples * 2, totalSamples - currentSample);
    if (samplesToRead <= 0)
        return;
    
    juce::AudioBuffer<float> inputBuffer(inputChannels, samplesToRead);
    for (int ch = 0; ch < inputChannels; ++ch)
    {
        inputBuffer.copyFrom(ch, 0, audioBuffer, ch, currentSample, samplesToRead);
    }
    
    if (inputChannels == 1)
    {
        const float* input = inputBuffer.getReadPointer(0);
        soundTouch->putSamples(input, samplesToRead);
    }
    else
    {
        juce::AudioBuffer<float> interleavedInput(1, samplesToRead * inputChannels);
        float* interleaved = interleavedInput.getWritePointer(0);
        
        for (int sample = 0; sample < samplesToRead; ++sample)
        {
            for (int ch = 0; ch < inputChannels; ++ch)
            {
                interleaved[sample * inputChannels + ch] = inputBuffer.getSample(ch, sample);
            }
        }
        
        soundTouch->putSamples(interleaved, samplesToRead);
    }
    
    uint32_t receivedSamples = soundTouch->receiveSamples(stretchedBuffer.getWritePointer(0), numSamples);
    
    if (receivedSamples > 0)
    {
        if (stretchedBuffer.getNumChannels() != inputChannels ||
            stretchedBuffer.getNumSamples() < (int)receivedSamples)
        {
            stretchedBuffer.setSize(inputChannels, receivedSamples, false, false, true);
        }
        
        if (inputChannels == 1)
        {
            for (int i = 0; i < (int)receivedSamples && i < numSamples; ++i)
            {
                float sample = stretchedBuffer.getSample(0, i);
                buffer.addSample(0, startSample + i, sample * volume);
                
                if (outputChannels >= 2)
                {
                    buffer.addSample(1, startSample + i, sample * volume);
                }
            }
        }
        else
        {
            for (int i = 0; i < (int)receivedSamples && i < numSamples; ++i)
            {
                for (int ch = 0; ch < channelsToProcess; ++ch)
                {
                    float sample = stretchedBuffer.getSample(0, i * inputChannels + ch);
                    buffer.addSample(ch, startSample + i, sample * volume);
                }
            }
        }
    }
    
    currentPosition += (double)samplesToRead / sampleRate;
    
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

double AudioTrack::detectBPM()
{
    if (!isLoaded() || audioBuffer.getNumSamples() == 0)
        return 0.0;
    
    const int hopSize = 512;
    const int fftSize = 1024;
    const float* audioData = audioBuffer.getReadPointer(0);
    const int numSamples = audioBuffer.getNumSamples();
    
    std::vector<float> onsetStrengths;
    std::vector<float> prevSpectrum(fftSize / 2, 0.0f);
    
    for (int i = 0; i < numSamples - fftSize; i += hopSize)
    {
        std::vector<float> window(fftSize);
        
        for (int j = 0; j < fftSize; ++j)
        {
            if (i + j < numSamples)
            {
                float hannWindow = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * j / (fftSize - 1)));
                window[j] = audioData[i + j] * hannWindow;
            }
        }
        
        std::vector<float> spectrum(fftSize / 2, 0.0f);
        for (int j = 0; j < fftSize / 2; ++j)
        {
            spectrum[j] = window[j] * window[j];
        }
        
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
    
    if (peakIndices.size() < 4)
        return 120.0;
    
    std::vector<double> intervals;
    for (int i = 1; i < peakIndices.size(); ++i)
    {
        double interval = (peakIndices[i] - peakIndices[i-1]) * hopSize / sampleRate;
        if (interval > 0.2 && interval < 2.0)
            intervals.push_back(interval);
    }
    
    if (intervals.empty())
        return 120.0;
    
    std::sort(intervals.begin(), intervals.end());
    double medianInterval = intervals[intervals.size() / 2];
    
    double bpm = 60.0 / medianInterval;
    
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
        double syncRatio = detectedBPM / masterBPM;
        setStretchRatio(syncRatio);
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
      quantizeButton("Q:8"),
      volumeSlider(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox),
      stretchSlider(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox),
      trackLabel("trackLabel", "Track " + juce::String(trackNumber + 1)),
      fileLabel("fileLabel", "No file loaded"),
      bpmLabel("bpmLabel", "BPM: --"),
      stretchLabel("stretchLabel", "Stretch: 1.00x"),
      volumeLabel("volumeLabel", "Vol"),
      currentQuantize(8)
{
    waveformDisplay = std::make_unique<WaveformComponent>();
    addAndMakeVisible(waveformDisplay.get());
    
    // Set track-specific color for waveform
    waveformDisplay->setWaveformColour(getTrackColour(trackNumber));
    waveformDisplay->setQuantizeValue(currentQuantize);
    
    addAndMakeVisible(loadButton);
    addAndMakeVisible(muteButton);
    addAndMakeVisible(soloButton);
    addAndMakeVisible(loopButton);
    addAndMakeVisible(quantizeButton);
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
    quantizeButton.onClick = [this] { quantizeButtonClicked(); };
    
    volumeSlider.setRange(0.0, 1.0, 0.01);
    volumeSlider.setValue(1.0);
    volumeSlider.onValueChange = [this] { volumeSliderChanged(); };
    
    stretchSlider.setRange(0.25, 4.0, 0.01);
    stretchSlider.setValue(1.0);
    stretchSlider.onValueChange = [this] { stretchSliderChanged(); };
    
    waveformDisplay->onPositionChanged = [this](double position) { onWaveformPositionChanged(position); };
    
    muteButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    soloButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    loopButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green.darker());
    quantizeButton.setColour(juce::TextButton::buttonColourId, juce::Colours::purple.darker());
    
    trackLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    fileLabel.setFont(juce::Font(12.0f));
    bpmLabel.setFont(juce::Font(11.0f));
    stretchLabel.setFont(juce::Font(11.0f));
    volumeLabel.setFont(juce::Font(11.0f));
    
    fileLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    bpmLabel.setColour(juce::Label::textColourId, juce::Colours::lightblue);
    stretchLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    volumeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    
    // Set track label color to match waveform
    trackLabel.setColour(juce::Label::textColourId, getTrackColour(trackNumber));
    
    loopButton.setToggleState(true, juce::dontSendNotification);
}

TrackComponent::~TrackComponent()
{
    loadButton.onClick = nullptr;
    muteButton.onClick = nullptr;
    soloButton.onClick = nullptr;
    loopButton.onClick = nullptr;
    quantizeButton.onClick = nullptr;
    volumeSlider.onValueChange = nullptr;
    stretchSlider.onValueChange = nullptr;
    onTrackLoaded = nullptr;
    
    if (waveformDisplay)
    {
        waveformDisplay->onPositionChanged = nullptr;
    }
    
    audioTrack = nullptr;
}

juce::Colour TrackComponent::getTrackColour(int trackNumber)
{
    // 8 distinct colors for 8 tracks
    static const juce::Colour trackColours[8] = {
        juce::Colour(0xff0080ff),  // Blue
        juce::Colour(0xff00ff80),  // Green
        juce::Colour(0xffff8000),  // Orange
        juce::Colour(0xffff0080),  // Pink
        juce::Colour(0xff8000ff),  // Purple
        juce::Colour(0xff00ffff),  // Cyan
        juce::Colour(0xffffff00),  // Yellow
        juce::Colour(0xffff4040)   // Red
    };
    
    return trackColours[trackNumber % 8];
}

void TrackComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2a2a2a));
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRect(getLocalBounds());
    
    if (audioTrack && audioTrack->isLoaded())
    {
        g.setColour(getTrackColour(trackNum).withAlpha(0.3f));
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
    buttonArea.removeFromLeft(3);
    quantizeButton.setBounds(buttonArea.removeFromLeft(35));
    
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
        {
            bpmLabel.setText("BPM: " + juce::String(bpm, 1), juce::dontSendNotification);
            // Update waveform with detected BPM for grid display
            waveformDisplay->setDetectedBPM(bpm);
        }
        else
        {
            bpmLabel.setText("BPM: --", juce::dontSendNotification);
        }
            
        waveformDisplay->setPlayPosition(audioTrack->getCurrentPosition());
        stretchSlider.setValue(audioTrack->getStretchRatio(), juce::dontSendNotification);
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
        
        stretchLabel.setText("Stretch: " + juce::String(audioTrack->getStretchRatio(), 2) + "x",
                           juce::dontSendNotification);
    }
}

void TrackComponent::updateWaveform()
{
    if (audioTrack && audioTrack->isLoaded())
    {
        waveformDisplay->setWaveformData(audioTrack->getWaveformPeaks(),
                                       44100.0,
                                       audioTrack->getDurationInSeconds() * 44100.0);
        waveformDisplay->setDuration(audioTrack->getDurationInSeconds());
        waveformDisplay->setDetectedBPM(audioTrack->getDetectedBPM());
    }
}

void TrackComponent::loadButtonClicked()
{
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
            
            // Notify parent about track loaded with detected BPM FIRST
            // This will set Master BPM and adjust this track appropriately
            if (onTrackLoaded && audioTrack->getDetectedBPM() > 0.0)
            {
                onTrackLoaded(audioTrack->getDetectedBPM());
            }
            
            // THEN update UI
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

void TrackComponent::quantizeButtonClicked()
{
    // Cycle through quantize values: 4 -> 8 -> 16 -> 32 -> 4
    switch (currentQuantize)
    {
        case 4:  currentQuantize = 8;  break;
        case 8:  currentQuantize = 16; break;
        case 16: currentQuantize = 32; break;
        case 32: currentQuantize = 4;  break;
        default: currentQuantize = 8;  break;
    }
    
    quantizeButton.setButtonText("Q:" + juce::String(currentQuantize));
    
    if (waveformDisplay)
    {
        waveformDisplay->setQuantizeValue(currentQuantize);
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
      metronomeButton("Metro"),
      tempoSlider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight),
      tempoLabel("tempoLabel", "Master BPM:"),
      positionLabel("positionLabel", "00:00"),
      masterBpmLabel("masterBpmLabel", "120.0"),
      playing(false),
      recording(false),
      autoSyncEnabled(true),
      metronomeEnabled(false),
      currentTempo(120.0),
      currentPosition(0.0)
{
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(recordButton);
    addAndMakeVisible(autoSyncButton);
    addAndMakeVisible(metronomeButton);
    addAndMakeVisible(tempoSlider);
    addAndMakeVisible(tempoLabel);
    addAndMakeVisible(positionLabel);
    addAndMakeVisible(masterBpmLabel);
    
    playButton.onClick = [this] { playButtonClicked(); };
    stopButton.onClick = [this] { stopButtonClicked(); };
    recordButton.onClick = [this] { recordButtonClicked(); };
    autoSyncButton.onClick = [this] { autoSyncButtonClicked(); };
    metronomeButton.onClick = [this] { metronomeButtonClicked(); };
    
    tempoSlider.setRange(60.0, 200.0, 1.0);
    tempoSlider.setValue(120.0);
    tempoSlider.onValueChange = [this] { tempoSliderChanged(); };
    
    playButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green.darker());
    stopButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red.darker());
    recordButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red.darker());
    autoSyncButton.setColour(juce::TextButton::buttonColourId, juce::Colours::blue.darker());
    metronomeButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    
    tempoLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    positionLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    masterBpmLabel.setFont(juce::Font(12.0f, juce::Font::bold));
    
    autoSyncButton.setToggleState(true, juce::dontSendNotification);
    masterBpmLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
}

TransportComponent::~TransportComponent()
{
    onPlay = nullptr;
    onStop = nullptr;
    onRecord = nullptr;
    onTempoChanged = nullptr;
    onAutoSync = nullptr;
    onMetronome = nullptr;
    
    playButton.onClick = nullptr;
    stopButton.onClick = nullptr;
    recordButton.onClick = nullptr;
    autoSyncButton.onClick = nullptr;
    metronomeButton.onClick = nullptr;
    tempoSlider.onValueChange = nullptr;
}

void TransportComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRect(getLocalBounds());
    
    if (autoSyncEnabled)
    {
        g.setColour(juce::Colours::green.withAlpha(0.3f));
        g.fillRect(getWidth() - 20, 5, 15, 8);
    }
}

void TransportComponent::resized()
{
    juce::Rectangle<int> area = getLocalBounds().reduced(8);
    
    juce::Rectangle<int> buttonArea = area.removeFromLeft(350);
    playButton.setBounds(buttonArea.removeFromLeft(60));
    buttonArea.removeFromLeft(5);
    stopButton.setBounds(buttonArea.removeFromLeft(60));
    buttonArea.removeFromLeft(5);
    recordButton.setBounds(buttonArea.removeFromLeft(60));
    buttonArea.removeFromLeft(5);
    autoSyncButton.setBounds(buttonArea.removeFromLeft(80));
    buttonArea.removeFromLeft(5);
    metronomeButton.setBounds(buttonArea.removeFromLeft(60));
    
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

void TransportComponent::metronomeButtonClicked()
{
    metronomeEnabled = !metronomeEnabled;
    metronomeButton.setToggleState(metronomeEnabled, juce::dontSendNotification);
    metronomeButton.setColour(juce::TextButton::buttonColourId,
                             metronomeEnabled ? juce::Colours::orange : juce::Colours::darkgrey);
    
    if (onMetronome)
        onMetronome();
}

void TransportComponent::setMetronomeEnabled(bool enabled)
{
    metronomeEnabled = enabled;
    metronomeButton.setToggleState(enabled, juce::dontSendNotification);
    metronomeButton.setColour(juce::TextButton::buttonColourId,
                             enabled ? juce::Colours::orange : juce::Colours::darkgrey);
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
      previousMasterTempo(120.0),
      currentPlayPosition(0.0),
      isPlaying(false),
      isRecording(false),
      autoSyncEnabled(true),
      metronomeEnabled(false),
      metronomePhase(0.0),
      metronomeBeatInterval(60.0 / 120.0),
      lastBeatTime(0.0),
      metronomeVolume(0.5f)
{
    setupTracks();
    setupTransport();
    setupLayout();
    
    setSize(1000, 800);
    setAudioChannels(0, 2);
    
    startTimer(50);
}

MainComponent::~MainComponent()
{
    stopTimer();
    shutdownAudio();
    
    if (transportComponent)
    {
        transportComponent->onPlay = nullptr;
        transportComponent->onStop = nullptr;
        transportComponent->onRecord = nullptr;
        transportComponent->onTempoChanged = nullptr;
        transportComponent->onAutoSync = nullptr;
        transportComponent->onMetronome = nullptr;
    }
    
    for (auto& track : audioTracks)
    {
        if (track)
        {
            track.reset();
        }
    }
    
    for (auto& trackComp : trackComponents)
    {
        if (trackComp)
        {
            trackComp.reset();
        }
    }
    
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
    
    bool hasSolo = false;
    for (auto& track : audioTracks)
    {
        if (track && track->isSolo())
        {
            hasSolo = true;
            break;
        }
    }
    
    for (auto& track : audioTracks)
    {
        if (track && track->isLoaded())
        {
            if (track->isMuted() || (hasSolo && !track->isSolo()))
                continue;
                
            track->processBlock(*bufferToFill.buffer, bufferToFill.startSample, numSamples);
        }
    }
    
    // Process metronome
    if (metronomeEnabled)
    {
        processMetronome(*bufferToFill.buffer, numSamples);
    }
    
    const double sampleRate = 44100.0;
    currentPlayPosition += numSamples / sampleRate;
}

void MainComponent::releaseResources()
{
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0f0f0f));
    
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText("STRETCHER - Multitrack Audio Looper (SoundTouch Pitch-Preserving)", 10, 5, 650, 20, juce::Justification::left);
}

void MainComponent::resized()
{
    juce::Rectangle<int> area = getLocalBounds();
    
    area.removeFromTop(25);
    
    transportComponent->setBounds(area.removeFromTop(80));
    
    tracksViewport.setBounds(area);
    
    int trackHeight = 220;
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
    juce::ScopedLock sl(audioLock);
    
    double scaleFactor = bpm / previousMasterTempo;
    
    for (auto& track : audioTracks)
    {
        if (track && track->isLoaded())
        {
            track->scaleStretchRatio(scaleFactor);
        }
    }
    
    previousMasterTempo = masterTempo;
    masterTempo = bpm;
    
    // Update metronome beat interval
    metronomeBeatInterval = 60.0 / bpm;
    
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

void MainComponent::setInitialMasterBPM(double bpm, AudioTrack* definingTrack)
{
    juce::ScopedLock sl(audioLock);
    
    // Set Master BPM without affecting stretch ratios
    previousMasterTempo = masterTempo;
    masterTempo = bpm;
    
    // Update metronome beat interval
    metronomeBeatInterval = 60.0 / bpm;
    
    // Ensure the defining track stays at 1.0 stretch and is set to the new master BPM
    if (definingTrack)
    {
        definingTrack->setStretchRatio(1.0);
        definingTrack->setMasterBPM(bpm);
    }
    
    // Update all tracks' master BPM (but don't change their stretch ratios)
    for (auto& track : audioTracks)
    {
        if (track && track.get() != definingTrack)
        {
            track->setMasterBPM(bpm);
        }
    }
    
    // Update transport display
    if (transportComponent)
    {
        transportComponent->setTempo(bpm);
    }
    
    juce::Logger::writeToLog("Initial Master BPM set to: " + juce::String(bpm, 1) +
                            " by first loaded track (stretch factor: 1.00)");
}

void MainComponent::autoSyncAllTracks()
{
    autoSyncEnabled = !autoSyncEnabled;
    
    if (autoSyncEnabled)
    {
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

void MainComponent::syncNewTrackToMaster(AudioTrack* track)
{
    if (track && track->isLoaded())
    {
        track->setMasterBPM(masterTempo);
        track->autoSyncToMaster();
    }
}

void MainComponent::toggleMetronome()
{
    metronomeEnabled = !metronomeEnabled;
    
    if (transportComponent)
    {
        transportComponent->setMetronomeEnabled(metronomeEnabled);
    }
    
    // Reset metronome timing when toggled
    metronomePhase = 0.0;
    lastBeatTime = 0.0;
}

void MainComponent::processMetronome(juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (!metronomeEnabled || !isPlaying)
        return;
    
    const double sampleRate = 44100.0;
    metronomeBeatInterval = 60.0 / masterTempo;
    
    for (int sample = 0; sample < numSamples; ++sample)
    {
        double currentTime = currentPlayPosition + (sample / sampleRate);
        
        // Check if we've hit a beat
        double timeSinceLastBeat = currentTime - lastBeatTime;
        if (timeSinceLastBeat >= metronomeBeatInterval)
        {
            lastBeatTime = currentTime;
            metronomePhase = 0.0; // Reset phase for new click
        }
        
        // Generate click sound
        float clickSample = generateClickSound(metronomePhase);
        
        // Add to both channels
        if (buffer.getNumChannels() >= 1)
            buffer.addSample(0, sample, clickSample * metronomeVolume);
        if (buffer.getNumChannels() >= 2)
            buffer.addSample(1, sample, clickSample * metronomeVolume);
        
        // Update phase
        metronomePhase += 1.0 / sampleRate;
    }
}

float MainComponent::generateClickSound(double phase)
{
    // Generate a short, dry click sound
    const double clickDuration = 0.01; // 10ms click
    
    if (phase > clickDuration)
        return 0.0f;
    
    // Create a short sine wave burst with envelope
    const double frequency = 2000.0; // 2kHz click frequency
    double envelope = 1.0 - (phase / clickDuration); // Linear decay
    envelope = envelope * envelope; // Square for sharper decay
    
    double sineWave = std::sin(2.0 * juce::MathConstants<double>::pi * frequency * phase);
    
    return static_cast<float>(sineWave * envelope * 0.3); // Scale down volume
}

void MainComponent::onTrackLoaded(double trackBPM)
{
    // Count how many tracks have audio loaded and find the loaded track
    int tracksWithAudio = 0;
    AudioTrack* loadedTrack = nullptr;
    
    for (auto& track : audioTracks)
    {
        if (track && track->isLoaded())
        {
            tracksWithAudio++;
            loadedTrack = track.get(); // This will be the most recently loaded track
        }
    }
    
    // If this is the first track loaded in the session (only 1 track has audio),
    // set Master BPM to track's BPM using the special method that preserves stretch factor 1.0
    if (tracksWithAudio == 1 && trackBPM > 0.0 && loadedTrack)
    {
        setInitialMasterBPM(trackBPM, loadedTrack);
    }
}

void MainComponent::setupTracks()
{
    for (int i = 0; i < maxTracks; ++i)
    {
        audioTracks[i] = std::make_unique<AudioTrack>();
        audioTracks[i]->setMasterBPM(masterTempo);
        trackComponents[i] = std::make_unique<TrackComponent>(audioTracks[i].get(), i);
        
        // Set callback for when track loads audio
        trackComponents[i]->onTrackLoaded = [this](double bpm) { onTrackLoaded(bpm); };
        
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
    transportComponent->onMetronome = [this] { toggleMetronome(); };
}

void MainComponent::setupLayout()
{
    addAndMakeVisible(tracksViewport);
    tracksViewport.setViewedComponent(&tracksContainer, false);
    tracksViewport.setScrollBarsShown(true, false);
}

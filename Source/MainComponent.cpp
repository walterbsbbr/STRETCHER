#include "MainComponent.h"
#include <algorithm>
#include <cmath>

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
      quantizeDivisions(8),
      zoomFactor(1.0),
      viewStartTime(0.0),
      draggedGridIndex(-1),
      isDraggingGrid(false),
      initialMouseX(0.0),
      initialGridTime(0.0),
      currentCursor(juce::MouseCursor::NormalCursor)
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

WaveformComponent::~WaveformComponent()
{
    onPositionChanged = nullptr;
    onBPMChanged = nullptr;
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
        
        // Calculate visible time range based on zoom
        double visibleDuration = totalDuration / zoomFactor;
        double endTime = juce::jmin(viewStartTime + visibleDuration, totalDuration);
        
        for (int x = 0; x < width; ++x)
        {
            // Map x position to time considering zoom and view offset
            double timePosition = viewStartTime + (x / (double)width) * (endTime - viewStartTime);
            double normalizedPos = timePosition / totalDuration;
            
            if (normalizedPos >= 0.0 && normalizedPos <= 1.0)
            {
                int peakIndex = (int)(normalizedPos * (waveformPeaks.size() - 1));
                peakIndex = juce::jlimit(0, (int)waveformPeaks.size() - 1, peakIndex);
                
                float peak = waveformPeaks[peakIndex];
                int waveHeight = (int)(peak * height * 0.4f);
                
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
        }
        
        // Complete the waveform shape
        for (int x = width - 1; x >= 0; --x)
        {
            double timePosition = viewStartTime + (x / (double)width) * (endTime - viewStartTime);
            double normalizedPos = timePosition / totalDuration;
            
            if (normalizedPos >= 0.0 && normalizedPos <= 1.0)
            {
                int peakIndex = (int)(normalizedPos * (waveformPeaks.size() - 1));
                peakIndex = juce::jlimit(0, (int)waveformPeaks.size() - 1, peakIndex);
                
                float peak = waveformPeaks[peakIndex];
                int waveHeight = (int)(peak * height * 0.4f);
                int bottomY = centerY + waveHeight;
                
                waveformPath.lineTo(area.getX() + x, bottomY);
            }
        }
        
        waveformPath.closeSubPath();
        g.fillPath(waveformPath);
    }
    
    // Draw beat lines on top of waveform
    drawBeatLines(g, area);
    
    // Draw playback position
    if (totalDuration > 0.0)
    {
        int positionX = (int)timeToPixel(currentPosition, area);
        
        if (positionX >= area.getX() && positionX <= area.getRight())
        {
            g.setColour(juce::Colours::yellow);
            g.drawVerticalLine(positionX, area.getY(), area.getBottom());
            
            // Draw position marker
            g.fillEllipse(positionX - 3, area.getY() - 3, 6, 6);
        }
    }
    
    // Draw loop indicator
    if (isLooping)
    {
        g.setColour(juce::Colours::green.withAlpha(0.3f));
        g.fillRect(area.getX(), area.getBottom() - 3, area.getWidth(), 3);
    }
    
    // Draw zoom info
    if (zoomFactor > 1.01)
    {
        g.setColour(juce::Colours::cyan.withAlpha(0.8f));
        g.setFont(10.0f);
        g.drawText("Zoom: " + juce::String(zoomFactor, 1) + "x",
                  area.getX() + 5, area.getY() + 5, 80, 15, juce::Justification::left);
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
    if (totalDuration <= 0.0 || quantizeDivisions <= 0 || detectedBPM <= 0.0)
        return;
    
    // Calculate beat interval from BPM
    double beatInterval = 60.0 / detectedBPM;
    
    // Calculate how many beats fit in the current view
    double visibleDuration = totalDuration / zoomFactor;
    double endTime = juce::jmin(viewStartTime + visibleDuration, totalDuration);
    
    // Find first beat visible in current view
    double firstBeat = std::ceil(viewStartTime / beatInterval) * beatInterval;
    
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    
    // Draw beat lines
    for (double beatTime = firstBeat; beatTime < endTime; beatTime += beatInterval)
    {
        if (beatTime >= viewStartTime && beatTime <= endTime)
        {
            int beatX = (int)timeToPixel(beatTime, area);
            
            if (beatX >= area.getX() && beatX < area.getRight())
            {
                // Check if this is a downbeat (every 4 beats)
                int beatNumber = (int)std::round(beatTime / beatInterval);
                bool isDownbeat = (beatNumber % 4 == 0);
                
                if (isDownbeat)
                {
                    g.setColour(juce::Colours::white.withAlpha(0.35f));
                    g.drawVerticalLine(beatX, area.getY(), area.getBottom());
                    g.setColour(juce::Colours::white.withAlpha(0.15f));
                }
                else
                {
                    g.drawVerticalLine(beatX, area.getY(), area.getBottom());
                }
            }
        }
    }
    
    // Highlight draggable grid lines
    if (isDraggingGrid && draggedGridIndex >= 0)
    {
        g.setColour(juce::Colours::yellow.withAlpha(0.6f));
        double draggedBeatTime = draggedGridIndex * beatInterval;
        int draggedX = (int)timeToPixel(draggedBeatTime, area);
        g.drawVerticalLine(draggedX, area.getY(), area.getBottom());
    }
}

void WaveformComponent::initializeGridPositions()
{
    gridPositions.clear();
    
    if (detectedBPM <= 0.0 || totalDuration <= 0.0)
        return;
    
    double beatInterval = 60.0 / detectedBPM;
    
    for (double beatTime = 0.0; beatTime < totalDuration; beatTime += beatInterval)
    {
        gridPositions.push_back(beatTime);
    }
}

int WaveformComponent::findGridLineAtPosition(int mouseX, const juce::Rectangle<int>& area)
{
    if (detectedBPM <= 0.0)
        return -1;
    
    double mouseTime = pixelToTime(mouseX, area);
    double beatInterval = 60.0 / detectedBPM;
    
    // Find closest beat line
    int closestBeat = (int)std::round(mouseTime / beatInterval);
    double closestBeatTime = closestBeat * beatInterval;
    
    // Check if mouse is close enough to this beat line
    int beatX = (int)timeToPixel(closestBeatTime, area);
    
    if (std::abs(mouseX - beatX) <= 5) // 5 pixel tolerance
    {
        return closestBeat;
    }
    
    return -1;
}

void WaveformComponent::updateBPMFromGrid()
{
    if (draggedGridIndex <= 0 || !isDraggingGrid)
        return;
    
    // Calculate new BPM based on dragged grid position
    double draggedBeatTime = gridPositions[draggedGridIndex];
    double newBeatInterval = draggedBeatTime / draggedGridIndex;
    double newBPM = 60.0 / newBeatInterval;
    
    // Constrain to reasonable BPM range
    newBPM = juce::jlimit(60.0, 200.0, newBPM);
    
    if (std::abs(newBPM - detectedBPM) > 0.1)
    {
        detectedBPM = newBPM;
        initializeGridPositions();
        
        if (onBPMChanged)
        {
            onBPMChanged(detectedBPM);
        }
        
        repaint();
    }
}

void WaveformComponent::updateCursor(const juce::MouseEvent& event)
{
    juce::Rectangle<int> area = getLocalBounds().reduced(2);
    int gridIndex = findGridLineAtPosition(event.x, area);
    
    if (gridIndex >= 0)
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    else
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

double WaveformComponent::timeToPixel(double timeInSeconds, const juce::Rectangle<int>& area) const
{
    if (totalDuration <= 0.0)
        return area.getX();
    
    double visibleDuration = totalDuration / zoomFactor;
    double endTime = juce::jmin(viewStartTime + visibleDuration, totalDuration);
    
    if (timeInSeconds < viewStartTime || timeInSeconds > endTime)
        return -1; // Outside visible range
    
    double normalizedPosition = (timeInSeconds - viewStartTime) / (endTime - viewStartTime);
    return area.getX() + normalizedPosition * area.getWidth();
}

double WaveformComponent::pixelToTime(int pixelX, const juce::Rectangle<int>& area) const
{
    if (totalDuration <= 0.0)
        return 0.0;
    
    double visibleDuration = totalDuration / zoomFactor;
    double endTime = juce::jmin(viewStartTime + visibleDuration, totalDuration);
    
    double normalizedPosition = (double)(pixelX - area.getX()) / area.getWidth();
    normalizedPosition = juce::jlimit(0.0, 1.0, normalizedPosition);
    
    return viewStartTime + normalizedPosition * (endTime - viewStartTime);
}

void WaveformComponent::mouseDown(const juce::MouseEvent& event)
{
    juce::Rectangle<int> area = getLocalBounds().reduced(2);
    
    // Check if clicking on a grid line
    int gridIndex = findGridLineAtPosition(event.x, area);
    
    if (gridIndex >= 0)
    {
        isDraggingGrid = true;
        draggedGridIndex = gridIndex;
        initialMouseX = event.x;
        
        if (gridIndex < (int)gridPositions.size())
        {
            initialGridTime = gridPositions[gridIndex];
        }
    }
    else
    {
        // Normal position update
        updatePositionFromMouse(event);
    }
}

void WaveformComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (isDraggingGrid && draggedGridIndex >= 0)
    {
        juce::Rectangle<int> area = getLocalBounds().reduced(2);
        
        // Update grid position based on drag
        double newTime = pixelToTime(event.x, area);
        newTime = juce::jlimit(0.0, totalDuration, newTime);
        
        if (draggedGridIndex < (int)gridPositions.size())
        {
            gridPositions[draggedGridIndex] = newTime;
            updateBPMFromGrid();
        }
    }
    else
    {
        updatePositionFromMouse(event);
    }
}

void WaveformComponent::mouseMove(const juce::MouseEvent& event)
{
    updateCursor(event);
}

void WaveformComponent::mouseExit(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    isDraggingGrid = false;
    draggedGridIndex = -1;
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void WaveformComponent::updatePositionFromMouse(const juce::MouseEvent& event)
{
    if (totalDuration <= 0.0 || isDraggingGrid)
        return;
    
    juce::Rectangle<int> area = getLocalBounds().reduced(2);
    double newPosition = pixelToTime(event.x, area);
    newPosition = juce::jlimit(0.0, totalDuration, newPosition);
    
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
    viewStartTime = 0.0;
    initializeGridPositions();
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
    initializeGridPositions();
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
        initializeGridPositions();
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

void WaveformComponent::setQuantizeValue(int quantizeValue)
{
    if (quantizeDivisions != quantizeValue)
    {
        quantizeDivisions = quantizeValue;
        repaint();
    }
}

void WaveformComponent::setZoomFactor(double zoom)
{
    double newZoom = juce::jlimit(0.1, 10.0, zoom);
    
    if (std::abs(zoomFactor - newZoom) > 0.01)
    {
        // Adjust view start time to keep zoom centered
        double visibleDuration = totalDuration / zoomFactor;
        double centerTime = viewStartTime + visibleDuration * 0.5;
        
        zoomFactor = newZoom;
        
        double newVisibleDuration = totalDuration / zoomFactor;
        viewStartTime = centerTime - newVisibleDuration * 0.5;
        viewStartTime = juce::jlimit(0.0, totalDuration - newVisibleDuration, viewStartTime);
        
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
        
        // Advanced BPM detection
        detectedBPM = detectBPMFromOnsets();
        
        // Fallback to autocorrelation if onset detection fails
        if (detectedBPM < 60.0 || detectedBPM > 200.0)
        {
            detectedBPM = detectBPMAutocorrelation();
        }
        
        // Final fallback to pattern-based detection
        if (detectedBPM < 60.0 || detectedBPM > 200.0)
        {
            detectedBPM = detectBPMImproved();
        }
        
        // Ultimate fallback
        if (detectedBPM < 60.0 || detectedBPM > 200.0)
        {
            detectedBPM = 120.0;
            juce::Logger::writeToLog("BPM detection failed for " + fileName + " - using 120 BPM default. Use manual grid adjustment.");
        }
        
        initializeSoundTouch();
        stretchedBuffer.setSize(reader->numChannels, 8192, false, false, true);
        
        juce::Logger::writeToLog("Loaded: " + fileName +
                                " - BPM: " + juce::String(detectedBPM, 1) +
                                " (Advanced detection with manual adjustment available)");
    }
}

double AudioTrack::detectBPMFromOnsets()
{
    if (!isLoaded() || audioBuffer.getNumSamples() < (int)sampleRate)
        return 120.0;
    
    std::vector<float> onsetStrength = calculateOnsetStrength();
    
    if (onsetStrength.size() < 10)
        return 120.0;
    
    // Find peaks in onset strength
    std::vector<double> onsetTimes;
    const double hopSize = 512.0;
    const double threshold = 0.3;
    
    if (!onsetStrength.empty())
    {
        float maxOnset = *std::max_element(onsetStrength.begin(), onsetStrength.end());
        float adaptiveThreshold = maxOnset * threshold;
        
        for (int i = 1; i < (int)onsetStrength.size() - 1; ++i)
        {
            if (onsetStrength[i] > adaptiveThreshold &&
                onsetStrength[i] > onsetStrength[i-1] &&
                onsetStrength[i] > onsetStrength[i+1])
            {
                double onsetTime = (i * hopSize) / sampleRate;
                onsetTimes.push_back(onsetTime);
            }
        }
    }
    
    if (onsetTimes.size() < 4)
        return 120.0;
    
    return findBestBPMCandidate(onsetTimes);
}

std::vector<float> AudioTrack::calculateOnsetStrength()
{
    const int hopSize = 512;
    const int frameSize = 1024;
    const int numSamples = audioBuffer.getNumSamples();
    const int numChannels = audioBuffer.getNumChannels();
    
    std::vector<float> onsetStrength;
    std::vector<float> prevSpectrum(frameSize / 2, 0.0f);
    
    for (int pos = 0; pos < numSamples - frameSize; pos += hopSize)
    {
        std::vector<float> currentSpectrum(frameSize / 2, 0.0f);
        
        // Simple spectral magnitude calculation (without FFT for simplicity)
        for (int bin = 0; bin < frameSize / 2; ++bin)
        {
            float magnitude = 0.0f;
            
            for (int ch = 0; ch < numChannels; ++ch)
            {
                if (pos + bin < numSamples)
                {
                    float sample = audioBuffer.getSample(ch, pos + bin);
                    magnitude += std::abs(sample);
                }
            }
            
            currentSpectrum[bin] = magnitude / numChannels;
        }
        
        // Calculate spectral flux (onset strength)
        float flux = 0.0f;
        for (int bin = 0; bin < frameSize / 2; ++bin)
        {
            float diff = currentSpectrum[bin] - prevSpectrum[bin];
            if (diff > 0)
                flux += diff;
        }
        
        onsetStrength.push_back(flux);
        prevSpectrum = currentSpectrum;
    }
    
    return onsetStrength;
}

double AudioTrack::findBestBPMCandidate(const std::vector<double>& onsetTimes)
{
    if (onsetTimes.size() < 4)
        return 120.0;
    
    // Calculate intervals between consecutive onsets
    std::vector<double> intervals;
    for (size_t i = 1; i < onsetTimes.size(); ++i)
    {
        double interval = onsetTimes[i] - onsetTimes[i-1];
        if (interval > 0.1 && interval < 2.0) // Reasonable beat interval range
        {
            intervals.push_back(interval);
        }
    }
    
    if (intervals.empty())
        return 120.0;
    
    // Find most common interval (histogram approach)
    std::sort(intervals.begin(), intervals.end());
    
    const double tolerance = 0.05;
    double bestInterval = 0.0;
    int maxCount = 0;
    
    for (size_t i = 0; i < intervals.size(); ++i)
    {
        int count = 1;
        double currentInterval = intervals[i];
        
        for (size_t j = i + 1; j < intervals.size(); ++j)
        {
            if (std::abs(intervals[j] - currentInterval) <= tolerance)
            {
                count++;
            }
            else
            {
                break;
            }
        }
        
        if (count > maxCount)
        {
            maxCount = count;
            bestInterval = currentInterval;
        }
    }
    
    if (bestInterval > 0.0)
    {
        double bpm = 60.0 / bestInterval;
        
        // Octave correction for musical tempos
        while (bpm < 70.0 && bpm > 0.0) bpm *= 2.0;
        while (bpm > 180.0) bpm /= 2.0;
        
        return bpm;
    }
    
    return 120.0;
}

double AudioTrack::detectBPMAutocorrelation()
{
    const int numSamples = audioBuffer.getNumSamples();
    if (numSamples < (int)sampleRate) return 120.0;
    
    // Use mono sum for analysis
    std::vector<float> monoSignal(numSamples);
    const int numChannels = audioBuffer.getNumChannels();
    
    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            sum += audioBuffer.getSample(ch, i);
        }
        monoSignal[i] = sum / numChannels;
    }
    
    // Calculate onset strength function
    const int hopSize = 512;
    const int frameSize = 1024;
    std::vector<float> onsetStrength;
    
    for (int pos = 0; pos < numSamples - frameSize; pos += hopSize)
    {
        float energy = 0.0f;
        float prevEnergy = 0.0f;
        
        // Current frame energy
        for (int i = 0; i < frameSize; ++i)
        {
            if (pos + i < numSamples)
                energy += monoSignal[pos + i] * monoSignal[pos + i];
        }
        
        // Previous frame energy
        for (int i = 0; i < frameSize; ++i)
        {
            if (pos - hopSize + i >= 0 && pos - hopSize + i < numSamples)
                prevEnergy += monoSignal[pos - hopSize + i] * monoSignal[pos - hopSize + i];
        }
        
        float strength = juce::jmax(0.0f, energy - prevEnergy);
        onsetStrength.push_back(strength);
    }
    
    if (onsetStrength.size() < 10) return 120.0;
    
    // Autocorrelation on onset strength
    const int minLag = (int)(60.0 * sampleRate / (200.0 * hopSize)); // 200 BPM max
    const int maxLag = (int)(60.0 * sampleRate / (60.0 * hopSize));   // 60 BPM min
    
    double bestCorr = 0.0;
    int bestLag = minLag;
    
    for (int lag = minLag; lag < maxLag && lag < (int)onsetStrength.size() / 2; ++lag)
    {
        double correlation = 0.0;
        int count = 0;
        
        for (int i = 0; i < (int)onsetStrength.size() - lag; ++i)
        {
            correlation += onsetStrength[i] * onsetStrength[i + lag];
            count++;
        }
        
        if (count > 0)
        {
            correlation /= count;
            
            if (correlation > bestCorr)
            {
                bestCorr = correlation;
                bestLag = lag;
            }
        }
    }
    
    // Convert lag to BPM
    double beatInterval = (bestLag * hopSize) / sampleRate;
    double bpm = 60.0 / beatInterval;
    
    // Octave correction for musical tempos
    while (bpm < 70.0 && bpm > 0.0) bpm *= 2.0;
    while (bpm > 180.0) bpm /= 2.0;
    
    return bpm;
}

std::vector<double> AudioTrack::calculateBeatTrack()
{
    std::vector<double> beatTimes;
    
    if (!isLoaded() || audioBuffer.getNumSamples() == 0)
        return beatTimes;
    
    const int hopSize = 512;
    const int frameSize = 1024;
    const int numSamples = audioBuffer.getNumSamples();
    
    // Calculate spectral flux
    std::vector<float> spectralFlux;
    std::vector<float> prevMagnitudes(frameSize / 2, 0.0f);
    
    for (int pos = 0; pos < numSamples - frameSize; pos += hopSize)
    {
        std::vector<float> magnitudes(frameSize / 2, 0.0f);
        
        for (int i = 0; i < frameSize / 2; ++i)
        {
            if (pos + i < numSamples)
            {
                float sample = audioBuffer.getSample(0, pos + i);
                magnitudes[i] = std::abs(sample);
            }
        }
        
        float flux = 0.0f;
        for (int i = 0; i < frameSize / 2; ++i)
        {
            float diff = magnitudes[i] - prevMagnitudes[i];
            if (diff > 0) flux += diff;
        }
        
        spectralFlux.push_back(flux);
        prevMagnitudes = magnitudes;
    }
    
    // Peak picking on spectral flux
    const float threshold = 0.3f;
    if (!spectralFlux.empty())
    {
        float maxFlux = *std::max_element(spectralFlux.begin(), spectralFlux.end());
        float adaptiveThreshold = maxFlux * threshold;
        
        for (int i = 1; i < (int)spectralFlux.size() - 1; ++i)
        {
            if (spectralFlux[i] > adaptiveThreshold &&
                spectralFlux[i] > spectralFlux[i-1] &&
                spectralFlux[i] > spectralFlux[i+1])
            {
                double beatTime = (i * hopSize) / sampleRate;
                beatTimes.push_back(beatTime);
            }
        }
    }
    
    return beatTimes;
}

double AudioTrack::detectBPMImproved()
{
    if (!isLoaded() || audioBuffer.getNumSamples() == 0)
        return 120.0;
    
    const double duration = getDurationInSeconds();
    
    // For common musical loop patterns (4, 8, 16, 32 beats)
    std::vector<double> possibleBPMs;
    
    for (int beats : {4, 8, 16, 32})
    {
        double bpm = (beats * 60.0) / duration;
        if (bpm >= 60.0 && bpm <= 200.0)
        {
            possibleBPMs.push_back(bpm);
        }
    }
    
    if (!possibleBPMs.empty())
    {
        for (double bpm : possibleBPMs)
        {
            if (bpm >= 65.0 && bpm <= 150.0)
            {
                return bpm;
            }
        }
        return possibleBPMs[0];
    }
    
    return 120.0;
}

void AudioTrack::setManualBPM(double bpm)
{
    juce::ScopedLock sl(lock);
    
    if (bpm >= 60.0 && bpm <= 200.0)
    {
        detectedBPM = bpm;
        juce::Logger::writeToLog("Manual BPM set to: " + juce::String(bpm, 1) + " for " + fileName);
    }
}

void AudioTrack::autoSyncToMaster()
{
    if (detectedBPM > 0.0 && masterBPM > 0.0)
    {
        double syncRatio = detectedBPM / masterBPM;
        setStretchRatio(syncRatio);
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
      bpmEditButton("Edit"),
      zoomInButton("+"),
      zoomOutButton("-"),
      volumeSlider(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox),
      stretchSlider(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox),
      trackLabel("trackLabel", "Track " + juce::String(trackNumber + 1)),
      fileLabel("fileLabel", "No file loaded"),
      bpmLabel("bpmLabel", "BPM: --"),
      stretchLabel("stretchLabel", "Stretch: 1.00x"),
      volumeLabel("volumeLabel", "Vol"),
      zoomLabel("zoomLabel", "Zoom"),
      currentQuantize(8),
      editingBPM(false),
      currentZoom(1.0)
{
    waveformDisplay = std::make_unique<WaveformComponent>();
    addAndMakeVisible(waveformDisplay.get());
    
    waveformDisplay->setWaveformColour(getTrackColour(trackNumber));
    waveformDisplay->setQuantizeValue(currentQuantize);
    
    addAndMakeVisible(loadButton);
    addAndMakeVisible(muteButton);
    addAndMakeVisible(soloButton);
    addAndMakeVisible(loopButton);
    addAndMakeVisible(quantizeButton);
    addAndMakeVisible(bpmEditButton);
    addAndMakeVisible(zoomInButton);
    addAndMakeVisible(zoomOutButton);
    addAndMakeVisible(volumeSlider);
    addAndMakeVisible(stretchSlider);
    addAndMakeVisible(trackLabel);
    addAndMakeVisible(fileLabel);
    addAndMakeVisible(bpmLabel);
    addAndMakeVisible(stretchLabel);
    addAndMakeVisible(volumeLabel);
    addAndMakeVisible(zoomLabel);
    
    loadButton.onClick = [this] { loadButtonClicked(); };
    muteButton.onClick = [this] { muteButtonClicked(); };
    soloButton.onClick = [this] { soloButtonClicked(); };
    loopButton.onClick = [this] { loopButtonClicked(); };
    quantizeButton.onClick = [this] { quantizeButtonClicked(); };
    bpmEditButton.onClick = [this] { bpmEditButtonClicked(); };
    zoomInButton.onClick = [this] { zoomInButtonClicked(); };
    zoomOutButton.onClick = [this] { zoomOutButtonClicked(); };
    
    volumeSlider.setRange(0.0, 1.0, 0.01);
    volumeSlider.setValue(1.0);
    volumeSlider.onValueChange = [this] { volumeSliderChanged(); };
    
    stretchSlider.setRange(0.25, 4.0, 0.01);
    stretchSlider.setValue(1.0);
    stretchSlider.onValueChange = [this] { stretchSliderChanged(); };
    
    waveformDisplay->onPositionChanged = [this](double position) { onWaveformPositionChanged(position); };
    waveformDisplay->onBPMChanged = [this](double bpm) { onWaveformBPMChanged(bpm); };
    
    muteButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    soloButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
    loopButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green.darker());
    quantizeButton.setColour(juce::TextButton::buttonColourId, juce::Colours::purple.darker());
    bpmEditButton.setColour(juce::TextButton::buttonColourId, juce::Colours::orange.darker());
    zoomInButton.setColour(juce::TextButton::buttonColourId, juce::Colours::blue.darker());
    zoomOutButton.setColour(juce::TextButton::buttonColourId, juce::Colours::blue.darker());
    
    trackLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    fileLabel.setFont(juce::Font(12.0f));
    bpmLabel.setFont(juce::Font(11.0f));
    stretchLabel.setFont(juce::Font(11.0f));
    volumeLabel.setFont(juce::Font(11.0f));
    zoomLabel.setFont(juce::Font(11.0f));
    
    fileLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    bpmLabel.setColour(juce::Label::textColourId, juce::Colours::lightblue);
    stretchLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    volumeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    zoomLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
    
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
    bpmEditButton.onClick = nullptr;
    zoomInButton.onClick = nullptr;
    zoomOutButton.onClick = nullptr;
    volumeSlider.onValueChange = nullptr;
    stretchSlider.onValueChange = nullptr;
    onTrackLoaded = nullptr;
    
    if (waveformDisplay)
    {
        waveformDisplay->onPositionChanged = nullptr;
        waveformDisplay->onBPMChanged = nullptr;
    }
    
    audioTrack = nullptr;
}

juce::Colour TrackComponent::getTrackColour(int trackNumber)
{
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
    
    // Zoom controls below waveform
    juce::Rectangle<int> zoomArea = area.removeFromTop(25);
    zoomLabel.setBounds(zoomArea.removeFromLeft(40));
    zoomOutButton.setBounds(zoomArea.removeFromLeft(25));
    zoomArea.removeFromLeft(2);
    zoomInButton.setBounds(zoomArea.removeFromLeft(25));
    
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
    buttonArea.removeFromLeft(3);
    bpmEditButton.setBounds(buttonArea.removeFromLeft(35));
    
    area.removeFromTop(8);
    
    juce::Rectangle<int> volumeArea = area.removeFromTop(20);
    volumeLabel.setBounds(volumeArea.removeFromLeft(30));
    volumeSlider.setBounds(volumeArea);
    
    area.removeFromTop(5);
    
    juce::Rectangle<int> stretchArea = area.removeFromTop(20);
    auto strLabel = stretchArea.removeFromLeft(35);
    stretchSlider.setBounds(stretchArea);
}

void TrackComponent::zoomInButtonClicked()
{
    currentZoom *= 1.5;
    currentZoom = juce::jmin(currentZoom, 10.0);
    
    if (waveformDisplay)
    {
        waveformDisplay->setZoomFactor(currentZoom);
    }
    
    juce::Logger::writeToLog("Track " + juce::String(trackNum + 1) + " zoom: " + juce::String(currentZoom, 1) + "x");
}

void TrackComponent::zoomOutButtonClicked()
{
    currentZoom /= 1.5;
    currentZoom = juce::jmax(currentZoom, 0.1);
    
    if (waveformDisplay)
    {
        waveformDisplay->setZoomFactor(currentZoom);
    }
    
    juce::Logger::writeToLog("Track " + juce::String(trackNum + 1) + " zoom: " + juce::String(currentZoom, 1) + "x");
}

void TrackComponent::onWaveformBPMChanged(double bpm)
{
    if (audioTrack)
    {
        audioTrack->setManualBPM(bpm);
        updateTrackInfo();
        
        juce::Logger::writeToLog("Track " + juce::String(trackNum + 1) +
                                " BPM manually adjusted to: " + juce::String(bpm, 1));
        
        if (onTrackLoaded)
        {
            onTrackLoaded(bpm);
        }
    }
}

void TrackComponent::bpmEditButtonClicked()
{
    if (audioTrack && audioTrack->isLoaded())
    {
        showBPMEditor();
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                             "No Audio Loaded",
                                             "Load an audio file first to edit BPM.");
    }
}

void TrackComponent::showBPMEditor()
{
    if (!audioTrack) return;
    
    double currentBPM = audioTrack->getDetectedBPM();
    
    juce::String message = "Current detected BPM: " + juce::String(currentBPM, 1) +
                          "\n\nEnter the correct BPM for this track:" +
                          "\n(You can also drag the grid lines on the waveform for fine adjustment)";
    
    auto* alertWindow = new juce::AlertWindow("Edit BPM",
                                            message,
                                            juce::AlertWindow::QuestionIcon);
    
    alertWindow->addTextEditor("bpmInput", juce::String(currentBPM, 1), "BPM:");
    alertWindow->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alertWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    
    alertWindow->enterModalState(true,
        juce::ModalCallbackFunction::create([this, alertWindow](int result) {
            if (result == 1)
            {
                juce::String bpmText = alertWindow->getTextEditorContents("bpmInput");
                double newBPM = bpmText.getDoubleValue();
                
                if (newBPM >= 60.0 && newBPM <= 200.0)
                {
                    if (audioTrack)
                    {
                        audioTrack->setManualBPM(newBPM);
                        updateTrackInfo();
                        updateWaveform();
                        
                        if (onTrackLoaded)
                        {
                            onTrackLoaded(newBPM);
                        }
                    }
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                         "Invalid BPM",
                                                         "Please enter a BPM between 60 and 200.");
                }
            }
            delete alertWindow;
        }), true);
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
            
            if (onTrackLoaded && audioTrack->getDetectedBPM() > 0.0)
            {
                onTrackLoaded(audioTrack->getDetectedBPM());
            }
            
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
    
    setSize(1200, 900);
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
    g.drawText("STRETCHER - Advanced Multitrack Audio Looper with Precision BPM Detection",
               10, 5, 700, 20, juce::Justification::left);
}

void MainComponent::resized()
{
    juce::Rectangle<int> area = getLocalBounds();
    
    area.removeFromTop(25);
    
    transportComponent->setBounds(area.removeFromTop(80));
    
    tracksViewport.setBounds(area);
    
    int trackHeight = 250;
    tracksContainer.setSize(getWidth(), maxTracks * trackHeight);
    
    for (int i = 0; i < maxTracks; ++i)
    {
        trackComponents[i]->setBounds(0, i * trackHeight, getWidth() - 20, trackHeight - 10);
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
    
    previousMasterTempo = masterTempo;
    masterTempo = bpm;
    
    metronomeBeatInterval = 60.0 / bpm;
    
    if (definingTrack)
    {
        definingTrack->setStretchRatio(1.0);
        definingTrack->setMasterBPM(bpm);
    }
    
    for (auto& track : audioTracks)
    {
        if (track && track.get() != definingTrack)
        {
            track->setMasterBPM(bpm);
        }
    }
    
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
        
        double timeSinceLastBeat = currentTime - lastBeatTime;
        if (timeSinceLastBeat >= metronomeBeatInterval)
        {
            lastBeatTime = currentTime;
            metronomePhase = 0.0;
        }
        
        float clickSample = generateClickSound(metronomePhase);
        
        if (buffer.getNumChannels() >= 1)
            buffer.addSample(0, sample, clickSample * metronomeVolume);
        if (buffer.getNumChannels() >= 2)
            buffer.addSample(1, sample, clickSample * metronomeVolume);
        
        metronomePhase += 1.0 / sampleRate;
    }
}

float MainComponent::generateClickSound(double phase)
{
    const double clickDuration = 0.01;
    
    if (phase > clickDuration)
        return 0.0f;
    
    const double frequency = 2000.0;
    double envelope = 1.0 - (phase / clickDuration);
    envelope = envelope * envelope;
    
    double sineWave = std::sin(2.0 * juce::MathConstants<double>::pi * frequency * phase);
    
    return static_cast<float>(sineWave * envelope * 0.3);
}

void MainComponent::onTrackLoaded(double trackBPM)
{
    int tracksWithAudio = 0;
    AudioTrack* loadedTrack = nullptr;
    
    for (auto& track : audioTracks)
    {
        if (track && track->isLoaded())
        {
            tracksWithAudio++;
            loadedTrack = track.get();
        }
    }
    
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

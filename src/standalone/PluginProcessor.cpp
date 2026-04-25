#include "PluginProcessor.h"
#include "PluginEditor.h"

SPU94AudioProcessor::SPU94AudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

SPU94AudioProcessor::~SPU94AudioProcessor() = default;

const juce::String SPU94AudioProcessor::getName() const
{
    return juce::String("SPU-94");
}

void SPU94AudioProcessor::prepareToPlay(double /*sampleRate*/, int /*samplesPerBlock*/)
{
    // Plan 02 fills this with spu94_init + work buffer allocation.
}

void SPU94AudioProcessor::releaseResources()
{
    // Plan 02 fills this with spu94_destroy + work buffer teardown.
}

void SPU94AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& /*midiMessages*/)
{
    // Plan 02 fills this with float->int16 conversion + spu94_process + int16->float.
    buffer.clear();
}

juce::AudioProcessorEditor* SPU94AudioProcessor::createEditor()
{
    return new SPU94AudioProcessorEditor(*this);
}

int SPU94AudioProcessor::getNumPrograms()
{
    return 1;
}

int SPU94AudioProcessor::getCurrentProgram()
{
    return 0;
}

void SPU94AudioProcessor::setCurrentProgram(int /*index*/)
{
}

const juce::String SPU94AudioProcessor::getProgramName(int /*index*/)
{
    return {};
}

void SPU94AudioProcessor::changeProgramName(int /*index*/,
                                             const juce::String& /*newName*/)
{
}

void SPU94AudioProcessor::getStateInformation(juce::MemoryBlock& /*destData*/)
{
    // Plan 03 fills this with register state serialization.
}

void SPU94AudioProcessor::setStateInformation(const void* /*data*/, int /*sizeInBytes*/)
{
    // Plan 03 fills this with register state deserialization.
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SPU94AudioProcessor();
}

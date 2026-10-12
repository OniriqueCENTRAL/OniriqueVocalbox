#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace onirique;

OniriqueVocalBoxAudioProcessor::OniriqueVocalBoxAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pInput     = apvts.getRawParameterValue (ids::input);
    pLow       = apvts.getRawParameterValue (ids::low);
    pMud       = apvts.getRawParameterValue (ids::mud);
    pHigh      = apvts.getRawParameterValue (ids::high);
    pDeessFreq = apvts.getRawParameterValue (ids::deessFreq);
    pDeessAmt  = apvts.getRawParameterValue (ids::deessAmt);
    pDrive     = apvts.getRawParameterValue (ids::drive);
    pSatType   = apvts.getRawParameterValue (ids::satType);
    pLowFreq   = apvts.getRawParameterValue (ids::lowFreq);
    pHighFreq  = apvts.getRawParameterValue (ids::highFreq);
    pSmartS    = apvts.getRawParameterValue (ids::smartS);
    pLowCut    = apvts.getRawParameterValue (ids::lowCut);
    pMbOn      = apvts.getRawParameterValue (ids::mbOn);
    pCompMaster = apvts.getRawParameterValue (ids::compMaster);
    pParOn      = apvts.getRawParameterValue (ids::parOn);
    pParAmount  = apvts.getRawParameterValue (ids::parAmount);
    pParAuto    = apvts.getRawParameterValue (ids::parAuto);
    pParThresh  = apvts.getRawParameterValue (ids::parThresh);
    pParAttack  = apvts.getRawParameterValue (ids::parAttack);
    pParRelease = apvts.getRawParameterValue (ids::parRelease);
    pDeess2On   = apvts.getRawParameterValue (ids::deess2On);
    pDeess2Freq = apvts.getRawParameterValue (ids::deess2Freq);
    pDeess2Amt  = apvts.getRawParameterValue (ids::deess2Amt);
    pMbAttack  = apvts.getRawParameterValue (ids::mbAttack);
    pMbRelease = apvts.getRawParameterValue (ids::mbRelease);
    for (int b = 0; b < 6; ++b)
    {
        const char* id[5] { ids::peqOn[b], ids::peqType[b], ids::peqFreq[b], ids::peqGain[b], ids::peqQ[b] };
        for (int k = 0; k < 5; ++k) pPeq[b][k] = apvts.getRawParameterValue (id[k]);
    }
    for (int i = 0; i < 3; ++i) pMbX[i] = apvts.getRawParameterValue (ids::mbX[i]);
    for (int b = 0; b < 4; ++b)
    {
        pMbThr[b]  = apvts.getRawParameterValue (ids::mbThr[b]);
        pMbGain[b] = apvts.getRawParameterValue (ids::mbGain[b]);
    }
    {
        const char* compIds[] { ids::compOn, ids::compMode, ids::compOrder, ids::fetIn, ids::fetOut, ids::fetAttack,
                                ids::fetRelease, ids::fetRatio, ids::laGain, ids::laPeak, ids::laLimit };
        for (int i = 0; i < 11; ++i) pComp[i] = apvts.getRawParameterValue (compIds[i]);
    }
    pResoAmt   = apvts.getRawParameterValue (ids::resoAmt);
    pResoLow   = apvts.getRawParameterValue (ids::resoLow);
    pResoMid   = apvts.getRawParameterValue (ids::resoMid);
    pResoHigh  = apvts.getRawParameterValue (ids::resoHigh);
    for (auto [i, f, w] : { std::tuple { 0, ids::resoLowFreq, ids::resoLowWidth }, std::tuple { 1, ids::resoMidFreq, ids::resoMidWidth },
                            std::tuple { 2, ids::resoHighFreq, ids::resoHighWidth } })
    {
        pResoFreq[i]  = apvts.getRawParameterValue (f);
        pResoWidth[i] = apvts.getRawParameterValue (w);
    }
    pEqOn      = apvts.getRawParameterValue (ids::eqOn);
    pResoOn    = apvts.getRawParameterValue (ids::resoOn);
    pDeessOn   = apvts.getRawParameterValue (ids::deessOn);
    pSatOn     = apvts.getRawParameterValue (ids::satOn);
    pOutput    = apvts.getRawParameterValue (ids::output);
    pAutoGain  = apvts.getRawParameterValue (ids::autoGain);
    pBypass    = apvts.getRawParameterValue (ids::bypass);
    pLive         = apvts.getRawParameterValue (ids::liveMode);
    pOversampling = apvts.getRawParameterValue (ids::oversampling);
    pPlosive      = apvts.getRawParameterValue (ids::plosive);
    pResoListen   = apvts.getRawParameterValue (ids::resoListen);
    pChainOrder   = apvts.getRawParameterValue (ids::chainOrder);
    for (int b = 0; b < 4; ++b) pMbSolo[b] = apvts.getRawParameterValue (ids::mbSolo[b]);
    apvts.addParameterListener (ids::liveMode, this);
    apvts.addParameterListener (ids::oversampling, this);
}

VocalChain::Params OniriqueVocalBoxAudioProcessor::readParams() const noexcept
{
    VocalChain::Params p;
    p.inputDb   = pInput->load();
    p.low       = pLow->load();
    p.mud       = pMud->load();
    p.high      = pHigh->load();
    p.deessFreq = pDeessFreq->load();
    p.deessAmt  = pDeessAmt->load();
    p.drive     = pDrive->load();
    p.satType   = juce::roundToInt (pSatType->load());
    static constexpr float lowFreqs[] { 80.0f, 100.0f, 125.0f };
    p.lowFreqHz = lowFreqs[juce::jlimit (0, 2, juce::roundToInt (pLowFreq->load()))];
    static constexpr float highFreqs[] { 8000.0f, 10000.0f, 20000.0f };
    p.highFreqHz = highFreqs[juce::jlimit (0, 2, juce::roundToInt (pHighFreq->load()))];
    p.smartS    = pSmartS->load() > 0.5f;
    p.lowCut    = pLowCut->load() > 0.5f;
    p.mbOn      = pMbOn->load() > 0.5f;
    p.compMaster = pCompMaster->load() > 0.5f;
    p.parOn      = pParOn->load() > 0.5f;
    p.parAmount  = pParAmount->load();
    p.parAuto    = pParAuto->load() > 0.5f;
    p.parThresh  = pParThresh->load();
    p.parAttack  = pParAttack->load();
    p.parRelease = pParRelease->load();
    p.deess2On   = pDeess2On->load() > 0.5f;
    p.deess2Freq = pDeess2Freq->load();
    p.deess2Amt  = pDeess2Amt->load();
    p.mb.x1 = pMbX[0]->load(); p.mb.x2 = pMbX[1]->load(); p.mb.x3 = pMbX[2]->load();
    for (int b = 0; b < 4; ++b) { p.mb.thr[(size_t) b] = pMbThr[b]->load(); p.mb.gain[(size_t) b] = pMbGain[b]->load(); }
    p.mb.attackMs  = pMbAttack->load();
    p.mb.releaseMs = pMbRelease->load();
    for (int b = 0; b < 6; ++b)
    {
        auto& band = p.peq[(size_t) b];
        band.on = pPeq[b][0]->load() > 0.5f;
        band.type = juce::roundToInt (pPeq[b][1]->load());
        band.freq = pPeq[b][2]->load();
        band.gainDb = pPeq[b][3]->load();
        band.q = pPeq[b][4]->load();
    }
    p.compOn    = pComp[0]->load() > 0.5f;
    p.compMode  = juce::roundToInt (pComp[1]->load());
    p.compOrder = juce::roundToInt (pComp[2]->load());
    p.fetIn     = pComp[3]->load();
    p.fetOut    = pComp[4]->load();
    p.fetAttack = pComp[5]->load();
    p.fetRelease = pComp[6]->load();
    p.fetRatio  = juce::roundToInt (pComp[7]->load());
    p.laGain    = pComp[8]->load();
    p.laPeak    = pComp[9]->load();
    p.laLimit   = pComp[10]->load() > 0.5f;
    p.resoAmt   = pResoAmt->load();
    p.resoLow   = pResoLow->load();
    p.resoMid   = pResoMid->load();
    p.resoHigh  = pResoHigh->load();
    for (int b = 0; b < 3; ++b)
    {
        p.resoFreq[b]  = pResoFreq[b]->load();
        p.resoWidth[b] = pResoWidth[b]->load();
    }
    p.eqOn      = pEqOn->load() > 0.5f;
    p.resoOn    = pResoOn->load() > 0.5f;
    p.deessOn   = pDeessOn->load() > 0.5f;
    p.satOn     = pSatOn->load() > 0.5f;
    p.outputDb  = pOutput->load();
    p.autoGain  = pAutoGain->load() > 0.5f;
    p.bypass    = pBypass->load() > 0.5f;
    p.plosive   = pPlosive->load();
    p.resoListen = pResoListen->load() > 0.5f;
    p.chainOrder = juce::roundToInt (pChainOrder->load());
    for (int b = 0; b < 4; ++b) p.mbSolo[(size_t) b] = pMbSolo[b]->load() > 0.5f;
    return p;
}

bool OniriqueVocalBoxAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& in = l.getMainInputChannelSet();
    const auto& out = l.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

OniriqueVocalBoxAudioProcessor::~OniriqueVocalBoxAudioProcessor()
{
    cancelPendingUpdate();
    apvts.removeParameterListener (onirique::ids::liveMode, this);
    apvts.removeParameterListener (onirique::ids::oversampling, this);
}

// (re)construit la chaîne selon MIX / LIVE et le suréchantillonnage, et annonce la latence au DAW
void OniriqueVocalBoxAudioProcessor::configureChain (double sampleRate)
{
    liveNow = pLive->load() > 0.5f;
    osNow = juce::jlimit (0, 3, juce::roundToInt (pOversampling->load()));
    chain.prepare (sampleRate, readParams(), liveNow, osNow);
    setLatencySamples (chain.getLatencySamples());
}

void OniriqueVocalBoxAudioProcessor::handleAsyncUpdate()
{
    const bool live = pLive->load() > 0.5f;
    const int os = juce::jlimit (0, 3, juce::roundToInt (pOversampling->load()));
    if (currentRate <= 0.0 || (live == liveNow && (os == osNow || live)))
    {
        liveNow = live;
        osNow = os;
        return;   // rien qui change le traitement (en LIVE, le suréchantillonnage est de toute façon coupé)
    }
    suspendProcessing (true);    // le thread audio attend la fin de la reconfiguration (allocation hors temps réel)
    configureChain (currentRate);
    suspendProcessing (false);
}

void OniriqueVocalBoxAudioProcessor::prepareToPlay (double sr, int)
{
    license.refresh();   // beaucoup de DAW rappellent prepareToPlay juste avant un export

    // Toute l'allocation se fait ici ; processBlock n'alloue rien.
    currentRate = sr;
    configureChain (sr);
}

void OniriqueVocalBoxAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int total = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, total);

    if (numCh < 1 || total < 1)
        return;

    chain.setParams (readParams());

    float* ch[2] { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };
    inputTap.push (ch[0], ch[1], total);    // analyseur : signal d'entrée
    chain.process (ch, numCh, total);
    outputTap.push (ch[0], ch[1], total);   // analyseur : signal de sortie
    license.process (buffer, getSampleRate(), isNonRealtime());

    // crête de sortie + correction d'Auto Gain pour l'interface
    float pk = 0.0f;
    for (int c = 0; c < numCh; ++c)
        pk = juce::jmax (pk, buffer.getMagnitude (c, 0, total));
    if (pk > outPeak.load (std::memory_order_relaxed))
        outPeak.store (pk, std::memory_order_relaxed);
    autoGainDbShown.store (pBypass->load() > 0.5f ? 0.0f : chain.getAutoGainDb(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* OniriqueVocalBoxAudioProcessor::createEditor()
{
    return new OniriqueVocalBoxAudioProcessorEditor (*this);
}

void OniriqueVocalBoxAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void OniriqueVocalBoxAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            presets.takeSnapshot();   // l'état rechargé sert de référence pour l'indicateur "modifié"
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OniriqueVocalBoxAudioProcessor();
}

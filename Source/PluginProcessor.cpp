#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace onirique;

namespace
{
// NOISE SUPPRESSOR, écoute d'une bande de l'EQ, puis la page FX (même ordre que readExtras)
const char* const kFxIds[] {
    ids::nsAmount, ids::peqListen, ids::peqListenBand, ids::mbOutput, ids::parDeessAmt, ids::parDeessFreq,
    ids::chorusOn, ids::chorusRate, ids::chorusDepth, ids::chorusMix,
    ids::flangerOn, ids::flangerRate, ids::flangerDepth, ids::flangerFeedback, ids::flangerMix,
    ids::delayOn, ids::delaySync, ids::delayTime, ids::delayNote, ids::delayFeedback, ids::delayMix, ids::delayPingPong,
    ids::delayLowCut, ids::delayHighCut, ids::delayDucking,
    ids::reverbOn, ids::reverbMode, ids::reverbPreDelay, ids::reverbDecay, ids::reverbSize, ids::reverbDiffusion,
    ids::reverbModRate, ids::reverbModDepth, ids::reverbMix, ids::reverbLowCut, ids::reverbHighCut, ids::reverbDucking,
    ids::reverbWidth, ids::reverbClean, ids::phoneOn, ids::phoneAmount, ids::doublerOn, ids::doublerAmount };
static_assert (sizeof (kFxIds) / sizeof (kFxIds[0]) == OniriqueVocalBoxAudioProcessor::kNumFxParams, "liste des réglages FX");
} // namespace

void OniriqueVocalBoxAudioProcessor::readExtras (VocalChain::Params& p) const noexcept
{
    int k = 0;
    auto f = [&] { return pFx[k++]->load(); };
    auto b = [&] { return pFx[k++]->load() > 0.5f; };
    auto i = [&] { return juce::roundToInt (pFx[k++]->load()); };
    p.nsAmount = f();
    // casque armé : on n'entend la bande que pendant qu'on la tient (clic sur son point, dans l'analyseur)
    p.peqListen = b() && peqListenHold.load (std::memory_order_relaxed);
    p.peqListenBand = i();
    p.mbOutDb = f(); p.parDeessAmt = f(); p.parDeessFreq = f();
    auto& x = p.fx;
    x.chorusOn = b(); x.chorusRate = f(); x.chorusDepth = f() * 0.01f; x.chorusMix = f() * 0.01f;
    x.flangerOn = b(); x.flangerRate = f(); x.flangerDepth = f() * 0.01f; x.flangerFeedback = f() * 0.01f; x.flangerMix = f() * 0.01f;
    x.delayOn = b();
    const bool sync = b();
    const float timeMs = f();
    const int note = i();
    const double bpm = juce::jlimit (30.0, 300.0, hostBpm.load (std::memory_order_relaxed));
    x.delayTimeSec = sync ? (float) (60.0 / bpm * delayNoteBeats (note)) : timeMs * 0.001f;
    x.delayFeedback = f() * 0.01f; x.delayMix = f() * 0.01f; x.delayPingPong = b();
    x.delayLowCut = f(); x.delayHighCut = f(); x.delayDucking = f() * 0.01f;
    auto& r = x.reverb;
    r.on = b(); r.mode = i(); r.preDelayMs = f(); r.decay = f(); r.size = f() * 0.01f; r.diffusion = f() * 0.01f;
    r.modRate = f(); r.modDepth = f() * 0.01f; r.mix = f() * 0.01f; r.lowCut = f(); r.highCut = f(); r.ducking = f() * 0.01f;
    r.width = f() * 0.01f; r.clean = f() * 0.01f;
    x.phoneOn = b(); x.phoneAmount = f() * 0.01f;
    x.doublerOn = b(); x.doublerAmount = f() * 0.01f;
}

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
    for (int b = 0; b < 8; ++b)
    {
        const char* id[8] { ids::peqOn[b], ids::peqType[b], ids::peqFreq[b], ids::peqGain[b], ids::peqQ[b],
                            ids::peqSlope[b], ids::peqDyn[b], ids::peqThr[b] };
        for (int k = 0; k < 8; ++k) pPeq[b][k] = apvts.getRawParameterValue (id[k]);
    }
    pPeqPower = apvts.getRawParameterValue (ids::peqPower);
    {
        const char* g[7] { ids::gateOn, ids::gateThr, ids::gateRange, ids::gateAttack, ids::gateHold, ids::gateRelease, ids::gateHyst };
        for (int i = 0; i < 7; ++i) pGate[i] = apvts.getRawParameterValue (g[i]);
    }
    for (int i = 0; i < kNumFx; ++i) pFx[i] = apvts.getRawParameterValue (kFxIds[i]);
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
    for (int b = 0; b < 8; ++b)
    {
        auto& band = p.peq[(size_t) b];
        band.on = pPeq[b][0]->load() > 0.5f;
        band.type = juce::roundToInt (pPeq[b][1]->load());
        band.freq = pPeq[b][2]->load();
        band.gainDb = pPeq[b][3]->load();
        band.q = pPeq[b][4]->load();
        band.slope = juce::roundToInt (pPeq[b][5]->load());
        band.dyn = pPeq[b][6]->load() > 0.5f;
        band.thrDb = pPeq[b][7]->load();
    }
    p.peqOn = pPeqPower->load() > 0.5f;
    p.gateOn = pGate[0]->load() > 0.5f;
    p.gateThr = pGate[1]->load(); p.gateRange = pGate[2]->load(); p.gateAttack = pGate[3]->load();
    p.gateHold = pGate[4]->load(); p.gateRelease = pGate[5]->load(); p.gateHyst = pGate[6]->load();
    readExtras (p);
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

    // tempo de l'hôte (DELAY calé sur le tempo) ; sans transport, on garde le dernier connu
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
            if (const auto bpm = pos->getBpm(); bpm.hasValue() && *bpm > 1.0)
                hostBpm.store (*bpm, std::memory_order_relaxed);

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
            // projets d'avant la séparation EQ paramétrique / EQ ANALOG : l'ordre à 6 modules est converti
            // (l'EQ ANALOG se place juste après le MULTIBAND) et l'EQ paramétrique suit l'ancien interrupteur EQ
            auto findParam = [&xml] (const char* id) -> juce::XmlElement*
            {
                for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
                    if (e->getStringAttribute ("id") == id) return e;
                return nullptr;
            };
            if (findParam (ids::chainOrder) == nullptr)
            {
                auto* e = xml->createNewChildElement ("PARAM");
                e->setAttribute ("id", ids::chainOrder);
                const auto* old = findParam (ids::legacyChainOrder);
                e->setAttribute ("value", old != nullptr ? convertLegacyOrder (old->getIntAttribute ("value")) : 0);
            }
            if (findParam (ids::peqPower) == nullptr)
                if (const auto* eq = findParam (ids::eqOn))
                {
                    auto* e = xml->createNewChildElement ("PARAM");
                    e->setAttribute ("id", ids::peqPower);
                    e->setAttribute ("value", eq->getDoubleAttribute ("value"));
                }
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            presets.takeSnapshot();   // l'état rechargé sert de référence pour l'indicateur "modifié"
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OniriqueVocalBoxAudioProcessor();
}

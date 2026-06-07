#include "MosesPlugin.hpp"
#include "DspMath.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

START_NAMESPACE_DISTRHO

static void setHints(Parameter& p, bool isBoolean, bool isOutput, bool isLogarithmic)
{
    p.hints = 0;
    p.hints |= kParameterIsAutomatable;
    if (isBoolean)     p.hints |= kParameterIsBoolean;
    if (isOutput)      p.hints |= kParameterIsOutput;
    if (isLogarithmic) p.hints |= kParameterIsLogarithmic;
}

MosesPlugin::MosesPlugin()
    : Plugin(kNumParameters, 0, 0)
{
    // Seed paramValues_ with each parameter's declared default.
    Parameter tmp;
    for (uint32_t i = 0; i < kNumParameters; ++i) {
        initParameter(i, tmp);
        paramValues_[i] = tmp.ranges.def;
    }
}

void MosesPlugin::initParameter(uint32_t index, Parameter& parameter)
{
    auto initFloat = [&](const char* name, const char* symbol,
                         float lo, float hi, float def, const char* unit,
                         bool logScale = false, bool isOutput = false)
    {
        parameter.name   = name;
        parameter.symbol = symbol;
        parameter.unit   = unit;
        parameter.ranges.def = def;
        parameter.ranges.min = lo;
        parameter.ranges.max = hi;
        setHints(parameter, false, isOutput, logScale);
    };
    auto initBool = [&](const char* name, const char* symbol, float def)
    {
        parameter.name   = name;
        parameter.symbol = symbol;
        parameter.unit   = "";
        parameter.ranges.def = def;
        parameter.ranges.min = 0.f;
        parameter.ranges.max = 1.f;
        setHints(parameter, true, false, false);
    };

    if (index == kParamCrossoverB) { initFloat("Crossover B", "xoB", 20.f, 15000.f, 75.f,   "Hz", true); return; }
    if (index == kParamCrossoverA) { initFloat("Crossover A", "xoA", 20.f, 15000.f, 250.f,  "Hz", true); return; }
    if (index == kParamCrossoverC) { initFloat("Crossover C", "xoC", 20.f, 15000.f, 5000.f, "Hz", true); return; }
    if (index == kParamStereo)     { initBool ("Stereo",      "stereo", 1.f); return; }

    if (index >= kParamBand1Base && index < kParamGr1) {
        const uint32_t bandIdx   = (index - kParamBand1Base) / 7;
        const uint32_t fieldIdx  = (index - kParamBand1Base) % 7;
        char buf[64]; char sym[16];
        switch (fieldIdx) {
            case kFieldListen:
                std::snprintf(buf, sizeof buf, "Band %u Listen", bandIdx+1);
                std::snprintf(sym, sizeof sym, "listen%u", bandIdx+1);
                initBool(buf, sym, 0.f); return;
            case kFieldKill:
                std::snprintf(buf, sizeof buf, "Band %u Kill", bandIdx+1);
                std::snprintf(sym, sizeof sym, "kill%u", bandIdx+1);
                initBool(buf, sym, 0.f); return;
            case kFieldThreshold:
                std::snprintf(buf, sizeof buf, "Band %u Threshold", bandIdx+1);
                std::snprintf(sym, sizeof sym, "threshold%u", bandIdx+1);
                initFloat(buf, sym, -40.f, 0.f, 0.f, "dB"); return;
            case kFieldAttack:
                std::snprintf(buf, sizeof buf, "Band %u Attack", bandIdx+1);
                std::snprintf(sym, sizeof sym, "attack%u", bandIdx+1);
                initFloat(buf, sym, 0.5f, 100.f, 10.f, "ms"); return;
            case kFieldRelease:
                std::snprintf(buf, sizeof buf, "Band %u Release", bandIdx+1);
                std::snprintf(sym, sizeof sym, "release%u", bandIdx+1);
                initFloat(buf, sym, 1.f, 1100.f, 50.f, "ms"); return;
            case kFieldRatio:
                std::snprintf(buf, sizeof buf, "Band %u Ratio", bandIdx+1);
                std::snprintf(sym, sizeof sym, "ratio%u", bandIdx+1);
                initFloat(buf, sym, 1.f, 16.f, 4.f, ":1"); return;
            case kFieldMakeup:
                std::snprintf(buf, sizeof buf, "Band %u Makeup", bandIdx+1);
                std::snprintf(sym, sizeof sym, "makeup%u", bandIdx+1);
                initFloat(buf, sym, -10.f, 20.f, 0.f, "dB"); return;
        }
    }

    if (index >= kParamGr1 && index <= kParamGr4) {
        const uint32_t b = index - kParamGr1 + 1;
        char buf[32], sym[16];
        std::snprintf(buf, sizeof buf, "GR %u", b);
        std::snprintf(sym, sizeof sym, "gr%u", b);
        initFloat(buf, sym, 0.f, 40.f, 0.f, "dB", false, true); return;
    }
    if (index >= kParamOut1 && index <= kParamOut4) {
        const uint32_t b = index - kParamOut1 + 1;
        char buf[32], sym[16];
        std::snprintf(buf, sizeof buf, "OUT %u", b);
        std::snprintf(sym, sizeof sym, "out%u", b);
        initFloat(buf, sym, -60.f, 1.f, -60.f, "dB", false, true); return;
    }
}

float MosesPlugin::getParameterValue(uint32_t index) const
{
    if (index >= kNumParameters) return 0.f;
    return paramValues_[index];
}

void MosesPlugin::setParameterValue(uint32_t index, float value)
{
    if (index >= kNumParameters) return;
    paramValues_[index] = value;
    pushParamsToDsp();
}

void MosesPlugin::activate()
{
    dsp_.prepare(float(getSampleRate()),
                 DISTRHO_PLUGIN_NUM_INPUTS,
                 getBufferSize());
    pushParamsToDsp();
}

void MosesPlugin::pushParamsToDsp()
{
    dsp_.setCrossovers(paramValues_[kParamCrossoverB],
                       paramValues_[kParamCrossoverA],
                       paramValues_[kParamCrossoverC]);
    // Reflect any anti-overlap clamping back into the cache so the host sees it.
    paramValues_[kParamCrossoverB] = dsp_.crossoverFreqB();
    paramValues_[kParamCrossoverA] = dsp_.crossoverFreqA();
    paramValues_[kParamCrossoverC] = dsp_.crossoverFreqC();

    dsp_.setStereoLink(paramValues_[kParamStereo] > 0.5f);
    for (int b = 0; b < moses::kNumBands; ++b) {
        const uint32_t base = kParamBand1Base + b * 7;
        dsp_.setBandListen   (b, paramValues_[base + kFieldListen]    > 0.5f);
        dsp_.setBandKill     (b, paramValues_[base + kFieldKill]      > 0.5f);
        dsp_.setBandThresholdDb(b, paramValues_[base + kFieldThreshold]);
        dsp_.setBandAttackMs (b, paramValues_[base + kFieldAttack]);
        dsp_.setBandReleaseMs(b, paramValues_[base + kFieldRelease]);
        dsp_.setBandRatio    (b, paramValues_[base + kFieldRatio]);
        dsp_.setBandMakeupDb (b, paramValues_[base + kFieldMakeup]);
    }
}

void MosesPlugin::run(const float** inputs, float** outputs, uint32_t frames)
{
    dsp_.process(inputs, outputs, frames);
    // Write meters back so the host sees them on next poll.
    for (int b = 0; b < moses::kNumBands; ++b) {
        const float grDb   = dsp_.bandGainReductionDb(b);
        const float grAbs  = std::min(40.f, std::max(0.f, -grDb));
        paramValues_[kParamGr1 + b]  = grAbs;
        const float peakL  = dsp_.bandOutputLevel(b, 0);
        const float peakR  = dsp_.bandOutputLevel(b, 1);
        const float peakDb = moses::gainToDb(std::max(peakL, peakR));
        paramValues_[kParamOut1 + b] = moses::clamp(peakDb, -60.f, 1.f);
    }
}

Plugin* createPlugin() { return new MosesPlugin(); }

END_NAMESPACE_DISTRHO

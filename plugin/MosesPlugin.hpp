#pragma once
#include "DistrhoPlugin.hpp"
#include "MultibandCompressor.hpp"
#include "MeterShm.hpp"
#include <atomic>

START_NAMESPACE_DISTRHO

enum ParamId : uint32_t {
    kParamCrossoverB = 0,
    kParamCrossoverA,
    kParamCrossoverC,
    kParamStereo,
    kParamBand1Base = 4,
    kParamBand2Base = kParamBand1Base + 7,
    kParamBand3Base = kParamBand1Base + 14,
    kParamBand4Base = kParamBand1Base + 21,
    kParamGr1  = kParamBand1Base + 28,
    kParamGr2,
    kParamGr3,
    kParamGr4,
    kParamOut1,
    kParamOut2,
    kParamOut3,
    kParamOut4,
    // Shm meter slot (dsp/MeterShm.hpp contract), appended at the END (index
    // 40) so existing param indices / normalized configs stay valid.
    // -1 = metering off (default); 0..31 = publish per-band block meters into
    // that slot of /nexus-meters. Linear VST3 norm = (slot + 1) / 32.
    kParamMeterSlot,
    kNumParameters
};

enum BandFieldOffset : uint32_t {
    kFieldListen    = 0,
    kFieldKill      = 1,
    kFieldThreshold = 2,
    kFieldAttack    = 3,
    kFieldRelease   = 4,
    kFieldRatio     = 5,
    kFieldMakeup    = 6,
};

class MosesPlugin : public Plugin
{
public:
    MosesPlugin();

    const char* getLabel()       const override { return "Moses"; }
    const char* getDescription() const override { return "4-band multiband compressor / crossover"; }
    const char* getMaker()       const override { return "Nexus"; }
    const char* getHomePage()    const override { return "https://github.com/lfabre/moses-dpf"; }
    const char* getLicense()     const override { return "GPL-3.0-or-later"; }
    uint32_t    getVersion()     const override { return d_version(0, 1, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('M','s','e','s'); }

    void initParameter(uint32_t index, Parameter& parameter) override;
    float getParameterValue(uint32_t index) const override;
    void  setParameterValue(uint32_t index, float value) override;

    void activate() override;
    void run(const float** inputs, float** outputs, uint32_t frames) override;

private:
    moses::MultibandCompressor dsp_;
    float paramValues_[kNumParameters] {};
    void pushParamsToDsp();

    // Shm metering. The segment is mapped unconditionally in the constructor
    // (non-RT, 4 KiB; failure -> writer disabled forever). The slot is read
    // each run() from meterSlot_ and claimed lazily (RT-safe atomic RMW), so
    // it works no matter when sushi applies initial_state relative to
    // activate(). A slot change never unclaims the old slot (see MeterShm.hpp).
    nxmeter::MeterShmWriter meter_;
    std::atomic<int> meterSlot_ { -1 };  // param cache, written by setParameterValue
    int meterClaimedSlot_ = -1;          // run()-local: last slot claimed

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MosesPlugin)
};

END_NAMESPACE_DISTRHO

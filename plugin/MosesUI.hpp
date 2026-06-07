#pragma once
#include "DistrhoUI.hpp"
#include "MosesPlugin.hpp"   // for ParamId / BandFieldOffset enums
#include <vector>

START_NAMESPACE_DISTRHO

class MosesUI : public UI
{
public:
    MosesUI();

protected:
    void onNanoDisplay() override;
    bool onMouse(const MouseEvent& ev) override;
    bool onMotion(const MotionEvent& ev) override;
    void parameterChanged(uint32_t index, float value) override;

private:
    struct ParamRange { float min, max; bool isBool, isLog; const char* unit; };
    struct KnobRect   { float x, y, r; uint32_t paramId; };

    static ParamRange rangeFor(uint32_t paramId);
    void  drawKnob(const KnobRect& k);
    void  drawMeter(float x, float y, float w, float h, float valueDb,
                    float minDb, float maxDb);
    bool  hitTestKnob(const KnobRect& k, float mx, float my, float fudge = 1.0f);

    static constexpr float kWidth  = 880.f;
    static constexpr float kHeight = 540.f;

    float values_[kNumParameters] {};
    std::vector<KnobRect> knobs_;
    int  draggingIdx_  = -1;
    float dragStartY_  = 0.f;
    float dragStartV_  = 0.f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MosesUI)
};

END_NAMESPACE_DISTRHO

#include "MosesUI.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;

MosesUI::ParamRange MosesUI::rangeFor(uint32_t paramId)
{
    switch (paramId) {
        case kParamCrossoverB:
        case kParamCrossoverA:
        case kParamCrossoverC: return { 20.f,  15000.f, false, true,  "Hz" };
        case kParamStereo:     return { 0.f,   1.f,     true,  false, "" };
        default: break;
    }
    if (paramId >= kParamBand1Base && paramId < kParamGr1) {
        const uint32_t field = (paramId - kParamBand1Base) % 7;
        switch (field) {
            case kFieldListen:    return { 0.f,  1.f,    true,  false, "" };
            case kFieldKill:      return { 0.f,  1.f,    true,  false, "" };
            case kFieldThreshold: return { -40.f, 0.f,   false, false, "dB" };
            case kFieldAttack:    return { 0.5f, 100.f,  false, false, "ms" };
            case kFieldRelease:   return { 1.f,  1100.f, false, false, "ms" };
            case kFieldRatio:     return { 1.f,  16.f,   false, false, ":1" };
            case kFieldMakeup:    return { -10.f, 20.f,  false, false, "dB" };
        }
    }
    if (paramId >= kParamGr1 && paramId <= kParamGr4)
        return { 0.f, 40.f, false, false, "dB" };
    if (paramId >= kParamOut1 && paramId <= kParamOut4)
        return { -60.f, 1.f, false, false, "dB" };
    return { 0.f, 1.f, false, false, "" };
}

static const char* labelFor(uint32_t paramId)
{
    switch (paramId) {
        case kParamCrossoverB: return "Xover B";
        case kParamCrossoverA: return "Xover A";
        case kParamCrossoverC: return "Xover C";
        case kParamStereo:     return "Stereo";
        default: break;
    }
    if (paramId >= kParamBand1Base && paramId < kParamGr1) {
        const uint32_t field = (paramId - kParamBand1Base) % 7;
        switch (field) {
            case kFieldListen:    return "Lst";
            case kFieldKill:      return "Kill";
            case kFieldThreshold: return "Thr";
            case kFieldAttack:    return "Atk";
            case kFieldRelease:   return "Rel";
            case kFieldRatio:     return "Ratio";
            case kFieldMakeup:    return "Gain";
        }
    }
    return "";
}

MosesUI::MosesUI() : UI(kWidth, kHeight)
{
    setSize(kWidth, kHeight);

    // Top row: 3 crossover knobs.
    const float topY = 70.f;
    const float xoX[]    = { 160.f, 360.f, 560.f };
    const uint32_t xoIds[] = { kParamCrossoverB, kParamCrossoverA, kParamCrossoverC };
    for (int i = 0; i < 3; ++i)
        knobs_.push_back({xoX[i], topY, 28.f, xoIds[i]});

    // 4 columns × 5 rows of band knobs.
    const float colX[]  = { 110.f, 300.f, 490.f, 680.f };
    const float rowY[]  = { 170.f, 230.f, 290.f, 350.f, 410.f };
    const BandFieldOffset fields[] = {
        kFieldThreshold, kFieldAttack, kFieldRelease, kFieldRatio, kFieldMakeup
    };
    for (int b = 0; b < 4; ++b) {
        for (int f = 0; f < 5; ++f) {
            const uint32_t pid = kParamBand1Base + b * 7 + fields[f];
            knobs_.push_back({colX[b], rowY[f], 22.f, pid});
        }
        knobs_.push_back({colX[b] - 18.f, 470.f, 12.f, uint32_t(kParamBand1Base + b * 7 + kFieldListen)});
        knobs_.push_back({colX[b] + 18.f, 470.f, 12.f, uint32_t(kParamBand1Base + b * 7 + kFieldKill)});
    }
    knobs_.push_back({800.f, 30.f, 14.f, kParamStereo});

    // Seed UI values from the plugin defaults (mirrors initParameter ranges).
    for (uint32_t i = 0; i < kNumParameters; ++i) {
        const ParamRange r = rangeFor(i);
        // Sensible visual default = midpoint, host will push real values via parameterChanged.
        values_[i] = r.isBool ? 0.f : 0.5f * (r.min + r.max);
    }
    values_[kParamStereo]     = 1.f;
    values_[kParamCrossoverB] = 75.f;
    values_[kParamCrossoverA] = 250.f;
    values_[kParamCrossoverC] = 5000.f;
}

void MosesUI::parameterChanged(uint32_t index, float value)
{
    if (index < kNumParameters) {
        values_[index] = value;
        repaint();
    }
}

void MosesUI::onNanoDisplay()
{
    beginPath();
    rect(0.f, 0.f, kWidth, kHeight);
    fillColor(Color(26, 26, 31));
    fill();

    fontSize(18.f);
    textAlign(ALIGN_LEFT | ALIGN_BASELINE);
    fillColor(Color(217, 217, 217));
    text(20.f, 30.f, "Moses", nullptr);

    for (const auto& k : knobs_) drawKnob(k);

    for (int b = 0; b < 4; ++b) {
        const float colX = 110.f + b * 190.f;
        drawMeter(colX + 60.f, 170.f, 14.f, 240.f, values_[kParamGr1 + b],  0.f,   40.f);
        drawMeter(colX + 80.f, 170.f, 14.f, 240.f, values_[kParamOut1 + b], -60.f, 1.f);
    }
}

void MosesUI::drawKnob(const KnobRect& k)
{
    const ParamRange r = rangeFor(k.paramId);
    const float value  = values_[k.paramId];
    const float n = std::clamp((value - r.min) / std::max(1e-9f, r.max - r.min), 0.f, 1.f);

    beginPath();
    circle(k.x, k.y, k.r);
    if (r.isBool) {
        fillColor(value > 0.5f ? Color(102, 204, 102) : Color(64, 64, 71));
    } else {
        fillColor(Color(46, 46, 56));
    }
    fill();
    strokeColor(Color(153, 153, 153));
    stroke();

    if (!r.isBool) {
        const float ang = -2.356f + n * 4.712f;
        beginPath();
        moveTo(k.x, k.y);
        lineTo(k.x + std::cos(ang) * k.r * 0.9f,
               k.y + std::sin(ang) * k.r * 0.9f);
        strokeColor(Color(242, 242, 242));
        strokeWidth(2.f);
        stroke();
    }
    fontSize(10.f);
    textAlign(ALIGN_CENTER | ALIGN_BASELINE);
    fillColor(Color(217, 217, 217));
    text(k.x, k.y + k.r + 14.f, labelFor(k.paramId), nullptr);
}

void MosesUI::drawMeter(float x, float y, float w, float h, float valueDb,
                        float minDb, float maxDb)
{
    beginPath();
    rect(x, y, w, h);
    fillColor(Color(13, 13, 18));
    fill();
    strokeColor(Color(128, 128, 128));
    stroke();

    const float n = std::clamp((valueDb - minDb) / (maxDb - minDb), 0.f, 1.f);
    beginPath();
    rect(x + 1.f, y + h - n * (h - 2.f), w - 2.f, n * (h - 2.f));
    fillColor(Color(102, 178, 102));
    fill();
}

bool MosesUI::hitTestKnob(const KnobRect& k, float mx, float my, float fudge)
{
    const float dx = mx - k.x, dy = my - k.y;
    return (dx * dx + dy * dy) <= (k.r * k.r * fudge);
}

bool MosesUI::onMouse(const MouseEvent& ev)
{
    if (ev.button != 1) return false;
    if (ev.press) {
        for (std::size_t i = 0; i < knobs_.size(); ++i) {
            if (hitTestKnob(knobs_[i], ev.pos.getX(), ev.pos.getY(), 1.2f)) {
                const ParamRange r = rangeFor(knobs_[i].paramId);
                if (r.isBool) {
                    values_[knobs_[i].paramId] = (values_[knobs_[i].paramId] > 0.5f) ? 0.f : 1.f;
                    setParameterValue(knobs_[i].paramId, values_[knobs_[i].paramId]);
                    repaint();
                    return true;
                }
                draggingIdx_ = int(i);
                dragStartY_  = ev.pos.getY();
                dragStartV_  = values_[knobs_[i].paramId];
                return true;
            }
        }
        return false;
    }
    draggingIdx_ = -1;
    return true;
}

bool MosesUI::onMotion(const MotionEvent& ev)
{
    if (draggingIdx_ < 0) return false;
    auto& k = knobs_[draggingIdx_];
    const ParamRange r = rangeFor(k.paramId);
    const float range = r.max - r.min;
    const float dy    = dragStartY_ - ev.pos.getY();
    float v = dragStartV_ + (dy / 200.f) * range;
    v = std::clamp(v, r.min, r.max);
    values_[k.paramId] = v;
    setParameterValue(k.paramId, v);
    repaint();
    return true;
}

UI* createUI() { return new MosesUI(); }

END_NAMESPACE_DISTRHO

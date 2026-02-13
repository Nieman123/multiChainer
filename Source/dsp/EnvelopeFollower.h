#pragma once

#include <JuceHeader.h>

namespace multichainer::dsp
{
struct EnvelopeParams
{
    float depthDb = 0.0f;     // Positive attenuation amount in dB.
    float delayMs = 0.0f;
    float attackMs = 20.0f;
    float holdMs = 30.0f;
    float releaseMs = 160.0f;
    float curveShape = 1.0f;  // 0.1 - 10.0
    float smoothing = 0.2f;   // 0 - 1
};

class EnvelopeFollower
{
public:
    void prepare (double sampleRateToUse);
    void reset();

    void setParameters (const EnvelopeParams& newParameters);
    void noteTriggered();

    float processSample (bool triggerNow);

private:
    enum class Stage
    {
        idle,
        delay,
        attack,
        hold,
        release
    };

    void enterAttack();
    void enterHold();
    void enterRelease();
    float calculateTargetEnvelope();

    static float clampCurve (float shape);

    double sampleRate = 44100.0;
    EnvelopeParams parameters;

    int delaySamples = 0;
    int attackSamples = 1;
    int holdSamples = 0;
    int releaseSamples = 1;

    float depthGain = 1.0f;
    float smoothingCoefficient = 0.2f;

    Stage stage = Stage::idle;
    int stagePosition = 0;

    float attackStartEnvelope = 0.0f;
    float releaseStartEnvelope = 1.0f;

    float targetEnvelope = 0.0f;
    float smoothedEnvelope = 0.0f;
};
} // namespace multichainer::dsp

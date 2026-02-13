#include "EnvelopeFollower.h"

#include <cmath>

namespace multichainer::dsp
{
namespace
{
constexpr float minCurveShape = 0.1f;
constexpr float maxCurveShape = 10.0f;
constexpr float maxSmoothing = 0.995f;
} // namespace

void EnvelopeFollower::prepare (double sampleRateToUse)
{
    sampleRate = juce::jmax (1.0, sampleRateToUse);
    reset();
}

void EnvelopeFollower::reset()
{
    stage = Stage::idle;
    stagePosition = 0;
    attackStartEnvelope = 0.0f;
    releaseStartEnvelope = 1.0f;
    targetEnvelope = 0.0f;
    smoothedEnvelope = 0.0f;
}

void EnvelopeFollower::setParameters (const EnvelopeParams& newParameters)
{
    parameters = newParameters;

    parameters.depthDb = juce::jlimit (0.0f, 60.0f, parameters.depthDb);
    parameters.delayMs = juce::jlimit (0.0f, 200.0f, parameters.delayMs);
    parameters.attackMs = juce::jlimit (0.0f, 2000.0f, parameters.attackMs);
    parameters.holdMs = juce::jlimit (0.0f, 2000.0f, parameters.holdMs);
    parameters.releaseMs = juce::jlimit (1.0f, 5000.0f, parameters.releaseMs);
    parameters.curveShape = clampCurve (parameters.curveShape);
    parameters.smoothing = juce::jlimit (0.0f, 1.0f, parameters.smoothing);

    delaySamples = juce::roundToInt (parameters.delayMs * 0.001f * static_cast<float> (sampleRate));
    attackSamples = juce::jmax (1, juce::roundToInt (parameters.attackMs * 0.001f * static_cast<float> (sampleRate)));
    holdSamples = juce::jmax (0, juce::roundToInt (parameters.holdMs * 0.001f * static_cast<float> (sampleRate)));
    releaseSamples = juce::jmax (1, juce::roundToInt (parameters.releaseMs * 0.001f * static_cast<float> (sampleRate)));

    depthGain = juce::Decibels::decibelsToGain (-parameters.depthDb);
    smoothingCoefficient = juce::jlimit (0.0f, maxSmoothing, parameters.smoothing);
}

void EnvelopeFollower::noteTriggered()
{
    attackStartEnvelope = smoothedEnvelope;
    stagePosition = 0;

    if (delaySamples > 0)
    {
        stage = Stage::delay;
        return;
    }

    enterAttack();
}

float EnvelopeFollower::processSample (bool triggerNow)
{
    if (triggerNow)
        noteTriggered();

    targetEnvelope = calculateTargetEnvelope();
    smoothedEnvelope += (targetEnvelope - smoothedEnvelope) * (1.0f - smoothingCoefficient);

    const auto gain = 1.0f - (smoothedEnvelope * (1.0f - depthGain));
    return juce::jlimit (0.0f, 1.0f, gain);
}

void EnvelopeFollower::enterAttack()
{
    stage = Stage::attack;
    stagePosition = 0;

    if (attackSamples <= 1)
        enterHold();
}

void EnvelopeFollower::enterHold()
{
    stage = Stage::hold;
    stagePosition = 0;

    if (holdSamples <= 0)
        enterRelease();
}

void EnvelopeFollower::enterRelease()
{
    stage = Stage::release;
    stagePosition = 0;
    releaseStartEnvelope = juce::jlimit (0.0f, 1.0f, targetEnvelope);

    if (releaseSamples <= 1)
    {
        stage = Stage::idle;
        targetEnvelope = 0.0f;
    }
}

float EnvelopeFollower::calculateTargetEnvelope()
{
    switch (stage)
    {
        case Stage::idle:
            return 0.0f;

        case Stage::delay:
        {
            const auto value = attackStartEnvelope;
            ++stagePosition;

            if (stagePosition >= delaySamples)
                enterAttack();

            return value;
        }

        case Stage::attack:
        {
            const auto progress = static_cast<float> (stagePosition) / static_cast<float> (juce::jmax (1, attackSamples - 1));
            const auto shaped = std::pow (juce::jlimit (0.0f, 1.0f, progress), parameters.curveShape);
            const auto value = juce::jmap (shaped, attackStartEnvelope, 1.0f);

            ++stagePosition;
            if (stagePosition >= attackSamples)
            {
                targetEnvelope = 1.0f;
                enterHold();
            }

            return value;
        }

        case Stage::hold:
        {
            ++stagePosition;
            if (stagePosition >= holdSamples)
                enterRelease();

            return 1.0f;
        }

        case Stage::release:
        {
            const auto progress = static_cast<float> (stagePosition) / static_cast<float> (juce::jmax (1, releaseSamples - 1));
            const auto shaped = std::pow (1.0f - juce::jlimit (0.0f, 1.0f, progress), parameters.curveShape);
            const auto value = releaseStartEnvelope * shaped;

            ++stagePosition;
            if (stagePosition >= releaseSamples)
            {
                stage = Stage::idle;
                targetEnvelope = 0.0f;
            }

            return value;
        }
    }

    return 0.0f;
}

float EnvelopeFollower::clampCurve (float shape)
{
    return juce::jlimit (minCurveShape, maxCurveShape, shape);
}
} // namespace multichainer::dsp

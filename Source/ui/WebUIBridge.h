#pragma once

#include <JuceHeader.h>

class MultiChainerAudioProcessor;

namespace multichainer::ui
{
class WebUIBridge final : public juce::Component,
                          private juce::Timer
{
public:
    explicit WebUIBridge (MultiChainerAudioProcessor& processor);
    ~WebUIBridge() override;

    void resized() override;

    void sendFullStateToFrontend();

private:
    static std::optional<juce::WebBrowserComponent::Resource> loadAssetResource (const juce::String& path);

#if JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
    static juce::WebBrowserComponent::ResourceProvider createResourceProvider();
#endif

    void handleParameterChangeEvent (const juce::var& payload);
    void pushSpectrumToFrontend();
    void pushMidiStatusToFrontend();

    void timerCallback() override;

    MultiChainerAudioProcessor& processor;
    std::unique_ptr<juce::WebBrowserComponent> browser;

    std::vector<float> fftFrame;
    int stateBroadcastCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebUIBridge)
};
} // namespace multichainer::ui

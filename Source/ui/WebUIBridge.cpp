#include "WebUIBridge.h"

#include <BinaryData.h>

#include "PluginProcessor.h"

#include <cstring>

namespace multichainer::ui
{
namespace
{
std::optional<juce::WebBrowserComponent::Resource> makeResource (const char* data,
                                                                 int dataSize,
                                                                 const juce::String& mimeType)
{
    if (data == nullptr || dataSize <= 0)
        return std::nullopt;

    std::vector<std::byte> bytes (static_cast<size_t> (dataSize));
    std::memcpy (bytes.data(), data, static_cast<size_t> (dataSize));

    juce::WebBrowserComponent::Resource resource;
    resource.data = std::move (bytes);
    resource.mimeType = mimeType;

    return resource;
}

float varToFloat (const juce::var& value)
{
    if (value.isDouble() || value.isInt() || value.isInt64() || value.isBool())
        return static_cast<float> (static_cast<double> (value));

    return static_cast<float> (value.toString().getDoubleValue());
}
} // namespace

WebUIBridge::WebUIBridge (MultiChainerAudioProcessor& processorIn)
    : processor (processorIn)
{
    juce::WebBrowserComponent::Options options;

    options = options.withNativeIntegrationEnabled (true)
                     .withEventListener ("paramChange", [this] (const juce::var& payload)
                     {
                         handleParameterChangeEvent (payload);
                     })
                     .withEventListener ("requestState", [this] (const juce::var&)
                     {
                         sendFullStateToFrontend();
                     });

#if JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
    options = options.withResourceProvider (createResourceProvider());
#endif

    browser = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*browser);

#if JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
    browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
#else
    browser->goToURL ("about:blank");
#endif

    startTimerHz (30);
    sendFullStateToFrontend();
}

WebUIBridge::~WebUIBridge()
{
    stopTimer();
}

void WebUIBridge::resized()
{
    if (browser != nullptr)
        browser->setBounds (getLocalBounds());
}

void WebUIBridge::sendFullStateToFrontend()
{
    if (browser == nullptr)
        return;

    browser->emitEventIfBrowserIsVisible ("state", processor.buildParameterSnapshot());
    browser->emitEventIfBrowserIsVisible ("midiStatus", processor.buildMidiInputSnapshot());
}

void WebUIBridge::handleParameterChangeEvent (const juce::var& payload)
{
    const auto* object = payload.getDynamicObject();

    if (object == nullptr)
        return;

    const auto applySingleUpdate = [this] (const juce::var& update)
    {
        if (const auto* updateObject = update.getDynamicObject())
        {
            const auto parameterID = updateObject->getProperty ("id").toString();

            if (parameterID.isNotEmpty())
            {
                const auto value = varToFloat (updateObject->getProperty ("value"));
                processor.setParameterFromUI (parameterID, value);
            }
        }
    };

    if (object->hasProperty ("updates"))
    {
        if (const auto* updatesArray = object->getProperty ("updates").getArray())
        {
            for (const auto& update : *updatesArray)
                applySingleUpdate (update);
        }

        return;
    }

    applySingleUpdate (payload);
}

void WebUIBridge::pushSpectrumToFrontend()
{
    if (browser == nullptr)
        return;

    if (! processor.getFFTAnalyzer().popLatestFrame (fftFrame))
        return;

    juce::Array<juce::var> bins;
    bins.ensureStorageAllocated (static_cast<int> (fftFrame.size()));

    for (auto bin : fftFrame)
        bins.add (bin);

    auto payload = std::make_unique<juce::DynamicObject>();
    payload->setProperty ("bins", juce::var (bins));

    browser->emitEventIfBrowserIsVisible ("fft", juce::var (payload.release()));
}

void WebUIBridge::pushMidiStatusToFrontend()
{
    if (browser == nullptr)
        return;

    browser->emitEventIfBrowserIsVisible ("midiStatus", processor.buildMidiInputSnapshot());
}

void WebUIBridge::timerCallback()
{
    pushSpectrumToFrontend();
    pushMidiStatusToFrontend();

    ++stateBroadcastCounter;
    if (stateBroadcastCounter >= 10)
    {
        stateBroadcastCounter = 0;
        sendFullStateToFrontend();
    }
}

std::optional<juce::WebBrowserComponent::Resource> WebUIBridge::loadAssetResource (const juce::String& path)
{
    auto normalisedPath = path;

    if (normalisedPath.startsWithChar ('/'))
        normalisedPath = normalisedPath.fromFirstOccurrenceOf ("/", false, false);

    if (normalisedPath.isEmpty() || normalisedPath == "index.html")
        return makeResource (BinaryData::index_html, BinaryData::index_htmlSize, "text/html");

    if (normalisedPath == "styles.css")
        return makeResource (BinaryData::styles_css, BinaryData::styles_cssSize, "text/css");

    if (normalisedPath == "app.js")
        return makeResource (BinaryData::app_js, BinaryData::app_jsSize, "text/javascript");

    return std::nullopt;
}

#if JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
juce::WebBrowserComponent::ResourceProvider WebUIBridge::createResourceProvider()
{
    return [] (const juce::String& path) -> std::optional<juce::WebBrowserComponent::Resource>
    {
        return loadAssetResource (path);
    };
}
#endif
} // namespace multichainer::ui

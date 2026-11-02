#include "AutomationController.h"
#include "../Model/ItemTime.h"

namespace
{
    const char* const keyLiveInterval = "liveInterval";
}

AutomationController::AutomationController (Project& p, PluginHost& h)
    : project (p), host (h)
{
    project.getState().addListener (this);

    host.getEngine().setLiveInterval (host.appProperties.getUserSettings()->getIntValue (keyLiveInterval, 0));

    // The plain patch is what gets saved, never a chased state.
    project.beforeStoringPluginState = [this]
    {
        if (! isPatchMode())
        {
            host.getEngine().setWritesPaused (true);
            restoreBaseValues();
        }
    };

    project.afterStoringPluginState = [this]
    {
        if (! isPatchMode())
        {
            host.getEngine().forceResend();
            host.getEngine().setWritesPaused (false);
        }
    };

    startTimerHz (30);
}

AutomationController::~AutomationController()
{
    stopTimer();
    project.beforeStoringPluginState = nullptr;
    project.afterStoringPluginState = nullptr;
    project.getState().removeListener (this);
}

//==============================================================================
void AutomationController::pluginReady()
{
    baseValues.clear();
    host.getEngine().setPatchMode (true);
    host.getEngine().stop();
    rebuild();
}

int AutomationController::resolveParameterIndex (const juce::ValueTree& lane) const
{
    auto* plugin = host.getPlugin();
    if (plugin == nullptr)
        return -1;

    auto& params = plugin->getParameters();
    const int index = lane[IDs::paramIndex];
    const auto name = lane[IDs::paramName].toString();

    if (juce::isPositiveAndBelow (index, params.size())
        && (name.isEmpty() || params[index]->getName (128) == name))
        return index;

    // The plugin's parameter order changed: find it by name.
    for (int i = 0; i < params.size(); ++i)
        if (params[i]->getName (128) == name)
            return i;

    return -1;
}

void AutomationController::rebuild()
{
    rebuildPending = false;

    auto* plugin = host.getPlugin();
    auto& engine = host.getEngine();
    engine.setProjectLength (project.getLengthSeconds());
    applyLoopRange();

    if (plugin == nullptr)
    {
        engine.setSnapshot (nullptr);
        return;
    }

    auto& params = plugin->getParameters();
    auto snapshot = std::make_shared<Automation::Snapshot>();
    std::set<int> inUse;

    bool anySolo = false;
    for (const auto& laneTree : project.getLanes())
        anySolo = anySolo || (bool) laneTree[IDs::solo];

    for (const auto& laneTree : project.getLanes())
    {
        if (! laneTree.hasType (IDs::LANE))
            continue;

        const int index = resolveParameterIndex (laneTree);
        if (index < 0 || inUse.count (index) > 0)
            continue;

        inUse.insert (index);

        // The patch value: remembered while chasing, read fresh in patch mode.
        if (isPatchMode() || baseValues.find (index) == baseValues.end())
            baseValues[index] = params[index]->getValue();

        Automation::Lane lane;
        lane.paramIndex = index;
        lane.baseValue = baseValues[index];

        // A muted lane (or one left out by solo) plays its patch value.
        const bool silent = (bool) laneTree[IDs::mute] || (anySolo && ! (bool) laneTree[IDs::solo]);

        if (! silent)
            buildItems (laneTree, lane);

        snapshot->lanes.push_back (std::move (lane));
    }

    // Parameters whose lane was removed go back to their patch value.
    for (auto it = baseValues.begin(); it != baseValues.end();)
    {
        if (inUse.count (it->first) == 0)
        {
            if (juce::isPositiveAndBelow (it->first, params.size()))
                params[it->first]->setValue (it->second);
            it = baseValues.erase (it);
        }
        else
        {
            ++it;
        }
    }

    engine.setSnapshot (snapshot);
}

void AutomationController::buildItems (const juce::ValueTree& laneTree, Automation::Lane& lane)
{
    for (const auto& itemTree : laneTree)
    {
        if (! itemTree.hasType (IDs::ITEM))
            continue;

        ItemTime time (itemTree);
        Automation::Item item;
        item.start = itemTree[IDs::start];
        item.end = item.start + (double) itemTree[IDs::length];
        item.musical = time.isMusical();
        item.tempo = time.tempo;
        item.localEnd = time.localLength();
        item.points = Project::getPoints (itemTree);   // already in the item's own units
        item.generator = ItemTime::readGenerator (itemTree);

        lane.items.push_back (std::move (item));
    }

    std::sort (lane.items.begin(), lane.items.end(),
               [] (const auto& a, const auto& b) { return a.start < b.start; });
}

float AutomationController::getBaseValue (int paramIndex) const
{
    auto it = baseValues.find (paramIndex);
    if (it != baseValues.end() && ! isPatchMode())
        return it->second;
    if (auto* plugin = host.getPlugin())
        if (auto* param = plugin->getParameters()[paramIndex])
            return param->getValue();
    return 0.0f;
}

void AutomationController::captureBaseValues()
{
    if (auto* plugin = host.getPlugin())
    {
        auto& params = plugin->getParameters();
        for (auto& [index, value] : baseValues)
            if (juce::isPositiveAndBelow (index, params.size()))
                value = params[index]->getValue();
    }
}

void AutomationController::restoreBaseValues()
{
    if (auto* plugin = host.getPlugin())
    {
        auto& params = plugin->getParameters();
        for (auto& [index, value] : baseValues)
            if (juce::isPositiveAndBelow (index, params.size()))
                params[index]->setValue (value);
    }
}

//==============================================================================
void AutomationController::setPatchMode (bool shouldBeInPatchMode)
{
    auto& engine = host.getEngine();

    if (shouldBeInPatchMode == engine.isPatchMode())
        return;

    if (shouldBeInPatchMode)
    {
        engine.stop();
        engine.setWritesPaused (true);      // so the audio thread cannot overwrite what we restore
        restoreBaseValues();
        engine.setPatchMode (true);
        engine.setWritesPaused (false);
    }
    else
    {
        captureBaseValues();                // the patch as it is now becomes the starting state
        rebuild();
        engine.setPatchMode (false);
        engine.forceResend();
    }
}

bool AutomationController::isPatchMode() const
{
    return host.getEngine().isPatchMode();
}

void AutomationController::play()
{
    setPatchMode (false);
    auto& engine = host.getEngine();
    if (engine.getPositionSeconds() >= project.getLengthSeconds() - 0.001 && ! engine.isLooping())
        engine.setPositionSeconds (0.0);
    engine.play();
}

void AutomationController::stop()
{
    host.getEngine().stop();
}

void AutomationController::togglePlay()
{
    if (host.getEngine().isPlaying())
        stop();
    else
        play();
}

void AutomationController::locate (double seconds)
{
    host.getEngine().setPositionSeconds (juce::jlimit (0.0, project.getLengthSeconds(), seconds));
    setPatchMode (false);
}

void AutomationController::setLooping (bool shouldLoop)
{
    host.getEngine().setLoop (shouldLoop, 0.0, project.getLengthSeconds());
}

void AutomationController::applyLoopRange()
{
    auto& engine = host.getEngine();
    engine.setLoop (engine.isLooping(), 0.0, project.getLengthSeconds());
}

void AutomationController::setLiveInterval (int samples)
{
    host.getEngine().setLiveInterval (samples);
    auto* settings = host.appProperties.getUserSettings();
    settings->setValue (keyLiveInterval, samples);
    settings->saveIfNeeded();
}

int AutomationController::getLiveInterval() const
{
    return host.getEngine().getLiveInterval();
}

//==============================================================================
void AutomationController::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    // Held-note changes don't affect automation; everything else might.
    if (! tree.hasType (IDs::HELDNOTE) && ! (tree.hasType (IDs::PLUGIN) && property == IDs::state))
        triggerRebuild();
}

void AutomationController::timerCallback()
{
    if (rebuildPending)
        rebuild();

    host.getEngine().collectGarbage();
}

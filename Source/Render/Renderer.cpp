#include "Renderer.h"
#include "../UI/EditorState.h"

//==============================================================================
void RenderSettings::load (juce::PropertiesFile& p)
{
    sampleRate     = p.getDoubleValue ("render.sampleRate", 48000.0);
    bitDepth       = p.getIntValue ("render.bitDepth", 24);
    tail           = p.getDoubleValue ("render.tail", 2.0);
    interval       = p.getIntValue ("render.interval", 16);
    writeCsv       = p.getBoolValue ("render.writeCsv", false);
    csvStep        = p.getDoubleValue ("render.csvStep", 0.01);
    csvPluginUnits = p.getBoolValue ("render.csvPluginUnits", false);
}

void RenderSettings::save (juce::PropertiesFile& p) const
{
    p.setValue ("render.sampleRate", sampleRate);
    p.setValue ("render.bitDepth", bitDepth);
    p.setValue ("render.tail", tail);
    p.setValue ("render.interval", interval);
    p.setValue ("render.writeCsv", writeCsv);
    p.setValue ("render.csvStep", csvStep);
    p.setValue ("render.csvPluginUnits", csvPluginUnits);
    p.saveIfNeeded();
}

//==============================================================================
namespace
{
    juce::String csvQuote (const juce::String& s)
    {
        if (s.containsAnyOf (",\"\n\r"))
            return "\"" + s.replace ("\"", "\"\"") + "\"";
        return s;
    }

    /** The plugin's text for a value, as a plain number when it is one. */
    juce::String unitsText (juce::AudioProcessorParameter& p, float v)
    {
        auto text = p.getText (v, 64).trim();
        const auto label = p.getLabel().trim();
        if (label.isNotEmpty() && text.endsWith (label))
            text = text.dropLastCharacters (label.length()).trim();
        return csvQuote (text);
    }
}

juce::String AutomationCsv::write (const juce::File& file, Project& project, PluginHost& host,
                                   AutomationController& controller, const RenderSettings& s)
{
    auto* plugin = host.getPlugin();
    const auto& params = plugin != nullptr ? plugin->getParameters() : juce::Array<juce::AudioProcessorParameter*>();

    struct Column
    {
        juce::String name;
        Automation::Lane lane;
        juce::AudioProcessorParameter* param = nullptr;
    };
    std::vector<Column> columns;

    // Every lane, muted or not: the CSV shows what is written in the score.
    for (const auto& laneTree : project.getLanes())
    {
        if (! laneTree.hasType (IDs::LANE))
            continue;
        Column c;
        c.name = laneTree[IDs::name].toString();
        const int index = controller.resolveParameterIndex (laneTree);
        c.lane.paramIndex = index;
        if (index >= 0)
        {
            c.param = params[index];
            c.lane.baseValue = controller.getBaseValue (index);
        }
        AutomationController::buildItems (laneTree, c.lane);
        columns.push_back (std::move (c));
    }

    if (columns.empty())
        return "There are no lanes to export.";

    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk())
        return "Could not write " + file.getFullPathName();

    juce::String header ("time_s");
    for (auto& c : columns)
        header << "," << csvQuote (c.name);
    out << header << "\n";

    const double step = juce::jmax (0.0001, s.csvStep);
    const auto rows = (juce::int64) std::floor ((s.end - s.start) / step + 1.0e-9) + 1;
    const int decimals = step < 0.001 ? 4 : 3;

    for (juce::int64 r = 0; r < rows; ++r)
    {
        const double t = s.start + (double) r * step;
        juce::String line (t, decimals);
        for (auto& c : columns)
        {
            line << ",";
            // Before its first item a lane plays the patch value; unknown without the plugin.
            if (c.param == nullptr && (c.lane.items.empty() || t < c.lane.items.front().start))
                continue;
            const float v = juce::jlimit (0.0f, 1.0f, Automation::laneValueAt (c.lane, t));
            if (s.csvPluginUnits && c.param != nullptr)
                line << unitsText (*c.param, v);
            else
                line << juce::String (v, 6);
        }
        out << line << "\n";
    }

    out.flush();
    return out.getStatus().wasOk() ? juce::String() : out.getStatus().getErrorMessage();
}

//==============================================================================
void RenderJob::start (Project& p, PluginHost& h, AutomationController& c, const RenderSettings& s,
                       juce::Component* centreAround, std::function<void (const juce::String&)> onFinished)
{
    if (h.getPlugin() == nullptr)
    {
        if (onFinished) onFinished ("Load a plugin first: there is nothing to render.");
        return;
    }

    auto* job = new RenderJob (p, h, c, s, centreAround);
    job->onFinished = std::move (onFinished);
    job->prepare();
    job->launchThread();
}

RenderJob::RenderJob (Project& p, PluginHost& h, AutomationController& c, const RenderSettings& s, juce::Component* centreAround)
    : ThreadWithProgressWindow ("Rendering " + s.file.getFileName(), true, true, 10000, "Cancel", centreAround),
      project (p), host (h), controller (c), settings (s)
{
}

void RenderJob::prepare()
{
    auto& engine = host.getEngine();

    wasPatchMode = controller.isPatchMode();
    wasLooping = engine.isLooping();
    oldPosition = engine.getPositionSeconds();

    heldNoteOn   = host.isHeldNoteOn();
    heldNote     = host.getHeldNoteNumber();
    heldVelocity = host.getHeldVelocity();

    controller.stop();
    controller.setPatchMode (false);    // remembers the patch values, starts writing automation
    controller.rebuildNow();

    host.beginOfflineRender();

    engine.setLoop (false, 0.0, project.getLengthSeconds());
    engine.setLiveInterval (juce::jmax (1, settings.interval));
    engine.setNonRealtime (true);
    engine.prepareToPlay (settings.sampleRate, 512);
    engine.resetPlugin();
}

void RenderJob::restore()
{
    auto& engine = host.getEngine();
    engine.stop();
    engine.setNonRealtime (false);
    engine.setLiveInterval (controller.getLiveInterval());
    controller.setLooping (wasLooping);

    host.endOfflineRender();

    controller.locate (oldPosition);
    if (wasPatchMode)
        controller.setPatchMode (true);
}

void RenderJob::run()
{
    auto& engine = host.getEngine();
    const double rate = settings.sampleRate;
    constexpr int blockSize = 512;

    auto& file = settings.file;
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk())
    {
        error = "Could not write " + file.getFullPathName();
        return;
    }

    using Options = juce::AudioFormatWriterOptions;
    const bool isFloat = settings.bitDepth == 32;
    auto options = Options().withSampleRate (rate)
                            .withNumChannels (2)
                            .withBitsPerSample (settings.bitDepth)
                            .withSampleFormat (isFloat ? Options::SampleFormat::floatingPoint : Options::SampleFormat::integral);

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
    {
        error = "Could not create a WAV file with these settings.";
        return;
    }

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    // Warm-up: the plugin settles on the values at the start, without sound.
    engine.setPositionSeconds (settings.start);
    {
        midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        midi.addEvent (juce::MidiMessage::allSoundOff (1), 0);
        for (int i = 0; i < (int) (0.25 * rate) / blockSize + 1 && ! threadShouldExit(); ++i)
        {
            buffer.clear();
            engine.processBlock (buffer, midi);
            midi.clear();
        }
        engine.resetPlugin();
    }

    // The score itself, then the tail (transport stopped, values held).
    const auto scoreSamples = (juce::int64) std::llround ((settings.end - settings.start) * rate);
    const auto tailSamples  = (juce::int64) std::llround (juce::jmax (0.0, settings.tail) * rate);
    const auto total = scoreSamples + tailSamples;

    engine.setPositionSeconds (settings.start);
    engine.play();
    if (heldNoteOn)
        midi.addEvent (juce::MidiMessage::noteOn (1, heldNote, (juce::uint8) heldVelocity), 0);

    juce::int64 done = 0;
    bool noteReleased = false;

    while (done < total)
    {
        if (threadShouldExit())
            break;

        // Blocks end exactly at the end of the score, so the transport stops there.
        int len = (int) juce::jmin ((juce::int64) blockSize, total - done);
        if (done < scoreSamples)
            len = (int) juce::jmin ((juce::int64) len, scoreSamples - done);

        if (done >= scoreSamples && ! noteReleased)
        {
            engine.stop();
            if (heldNoteOn)
                midi.addEvent (juce::MidiMessage::noteOff (1, heldNote), 0);
            noteReleased = true;
        }

        buffer.setSize (2, len, false, false, true);
        buffer.clear();
        engine.processBlock (buffer, midi);
        midi.clear();

        peak = juce::jmax (peak, buffer.getMagnitude (0, len));
        if (! writer->writeFromAudioSampleBuffer (buffer, 0, len))
        {
            error = "Writing the file failed (is the disk full?).";
            break;
        }

        done += len;
        samplesWritten = done;

        if ((done / blockSize) % 32 == 0)
        {
            setProgress ((double) done / (double) juce::jmax ((juce::int64) 1, total));
            setStatusMessage ("Rendering " + TimeFormat::format ((double) juce::jmin (done, scoreSamples) / rate, 1)
                              + " of " + TimeFormat::format ((double) scoreSamples / rate, 1)
                              + (done > scoreSamples ? "  (tail)" : ""));
        }
    }

    writer.reset();     // finishes the file header
    seconds = (double) samplesWritten / rate;
}

void RenderJob::threadComplete (bool userPressedCancel)
{
    restore();

    juce::String result;
    if (userPressedCancel || samplesWritten == 0 || error.isNotEmpty())
    {
        settings.file.deleteFile();
        result = error.isNotEmpty() ? "Render failed: " + error : juce::String ("Render cancelled.");
    }
    else
    {
        const float db = juce::Decibels::gainToDecibels (peak, -120.0f);
        result << "Rendered " << TimeFormat::format (seconds, 2) << " to " << settings.file.getFileName()
                << " (" << juce::String (settings.sampleRate / 1000.0, 1) << " kHz, "
                << (settings.bitDepth == 32 ? juce::String ("32-bit float") : juce::String (settings.bitDepth) + "-bit")
                << "). Peak " << juce::String (db, 1) << " dBFS.";
        if (peak > 1.0f && settings.bitDepth != 32)
            result << " Warning: the sound clipped; lower the level in the patch or render as 32-bit float.";
        else if (peak > 1.0f)
            result << " The peak is above 0 dBFS; the float file keeps it, but lower it before converting.";

        if (settings.writeCsv)
        {
            const auto csvFile = settings.file.withFileExtension ("csv");
            const auto csvError = AutomationCsv::write (csvFile, project, host, controller, settings);
            result << (csvError.isEmpty() ? " Automation written to " + csvFile.getFileName() + "."
                                           : " CSV failed: " + csvError);
        }
    }

    auto done = std::move (onFinished);
    delete this;
    if (done) done (result);
}

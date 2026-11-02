#include "Exporter.h"
#include "../Model/ItemTime.h"

//==============================================================================
void ExportSettings::load (juce::PropertiesFile& p)
{
    format       = (Format) juce::jlimit (0, 2, p.getIntValue ("export.format", 0));
    lanes        = (Lanes) juce::jlimit (0, 2, p.getIntValue ("export.lanes", 0));
    step         = p.getDoubleValue ("export.step", 0.01);
    exactCurves  = p.getBoolValue ("export.exact", false);
    firstCC      = p.getIntValue ("export.firstCC", 20);
    fourteenBit  = p.getBoolValue ("export.14bit", false);
    midiChannel  = p.getIntValue ("export.channel", 1);
    reaperTempo  = p.getDoubleValue ("export.tempo", 120.0);
    reaperTemplate = juce::File (p.getValue ("export.template"));
}

void ExportSettings::save (juce::PropertiesFile& p) const
{
    p.setValue ("export.format", (int) format);
    p.setValue ("export.lanes", (int) lanes);
    p.setValue ("export.step", step);
    p.setValue ("export.exact", exactCurves);
    p.setValue ("export.firstCC", firstCC);
    p.setValue ("export.14bit", fourteenBit);
    p.setValue ("export.channel", midiChannel);
    p.setValue ("export.tempo", reaperTempo);
    p.setValue ("export.template", reaperTemplate.getFullPathName());
    p.saveIfNeeded();
}

//==============================================================================
namespace
{
    // Ramer-Douglas-Peucker on (time, value) samples: drops points that hardly change the line.
    void thin (const std::vector<Exporter::EnvPoint>& in, size_t a, size_t b, float tolerance, std::vector<bool>& keep)
    {
        if (b <= a + 1) return;
        float worst = 0.0f;
        size_t index = a;
        const double ta = in[a].time, tb = in[b].time;
        for (size_t i = a + 1; i < b; ++i)
        {
            const double f = tb > ta ? (in[i].time - ta) / (tb - ta) : 0.0;
            const float lineValue = in[a].value + (float) f * (in[b].value - in[a].value);
            const float d = std::abs (in[i].value - lineValue);
            if (d > worst) { worst = d; index = i; }
        }
        if (worst > tolerance)
        {
            keep[index] = true;
            thin (in, a, index, tolerance, keep);
            thin (in, index, b, tolerance, keep);
        }
    }

    void appendSampled (std::vector<Exporter::EnvPoint>& out, const Automation::Item& item, double from, double to, double step)
    {
        std::vector<Exporter::EnvPoint> samples;
        for (double t = from; t < to - 1.0e-9; t += step)
            samples.push_back ({ t, item.valueAtLocal (item.toLocal (t)), 0 });
        samples.push_back ({ to, item.valueAtLocal (juce::jmin (item.localEnd, item.toLocal (to))), 0 });

        std::vector<bool> keep (samples.size(), false);
        keep.front() = keep.back() = true;
        thin (samples, 0, samples.size() - 1, 0.001f, keep);
        for (size_t i = 0; i < samples.size(); ++i)
            if (keep[i]) out.push_back (samples[i]);
    }

    juce::String num (double v, int decimals = 6)    { return juce::String (v, decimals); }

    juce::String safeFileName (const juce::String& s)
    {
        return juce::File::createLegalFileName (s.isEmpty() ? juce::String ("lane") : s);
    }

    juce::Array<juce::ValueTree> chooseLanes (Project& project, const ExportSettings& s, const juce::ValueTree& active)
    {
        juce::Array<juce::ValueTree> result;
        if (s.lanes == ExportSettings::Lanes::active)
        {
            if (active.isValid()) result.add (active);
            return result;
        }
        if (s.lanes == ExportSettings::Lanes::group)
        {
            auto group = project.groupOf (active);
            if (group.isValid()) return project.lanesInGroup (group);
            if (active.isValid()) result.add (active);
            return result;
        }
        for (auto lane : project.getLanes())
            if (lane.hasType (IDs::LANE))
                result.add (lane);
        return result;
    }

    float baseValueFor (AutomationController& controller, const juce::ValueTree& lane)
    {
        const int index = controller.resolveParameterIndex (lane);
        if (index >= 0) return controller.getBaseValue (index);
        // Without the plugin: the value the lane starts with.
        Automation::Lane l;
        AutomationController::buildItems (lane, l);
        return l.items.empty() ? 0.0f : l.items.front().valueAtLocal (0.0);
    }

    int parameterIndexFor (AutomationController& controller, const juce::ValueTree& lane)
    {
        const int index = controller.resolveParameterIndex (lane);
        return index >= 0 ? index : (int) lane[IDs::paramIndex];
    }

    //==========================================================================
    juce::String writeMidi (const ExportSettings& s, Project& project, AutomationController& controller,
                            const juce::Array<juce::ValueTree>& lanes, juce::String& summary)
    {
        constexpr int ppq = 960;
        constexpr double ticksPerSecond = ppq * 2.0;     // the file is at 120 bpm: 2 beats per second
        const int channel = juce::jlimit (1, 16, s.midiChannel);
        const double length = project.getLengthSeconds();

        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0.0);
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, "Trajet export"), 0.0);

        int cc = s.firstCC, automationLanes = 0, noteCount = 0;
        juce::StringArray mapping;

        for (auto& laneTree : lanes)
        {
            if (Project::isNoteLane (laneTree))
            {
                for (auto item : laneTree)
                    if (item.hasType (IDs::ITEM))
                        for (auto& n : Project::getNotes (item))
                        {
                            seq.addEvent (juce::MidiMessage::noteOn (channel, n.pitch, (juce::uint8) n.velocity), n.start * ticksPerSecond);
                            seq.addEvent (juce::MidiMessage::noteOff (channel, n.pitch), n.end * ticksPerSecond);
                            ++noteCount;
                        }
                continue;
            }

            const int maxCC = s.fourteenBit ? 31 : 119;
            if (cc > maxCC)
                return "Too many lanes for the controllers available from CC " + juce::String (s.firstCC)
                       + (s.fourteenBit ? " (14-bit uses CC 0-31, with 32-63 as the fine part)." : ".");

            Automation::Lane lane;
            AutomationController::buildItems (laneTree, lane);
            lane.baseValue = baseValueFor (controller, laneTree);

            mapping.add ("CC" + juce::String (cc) + (s.fourteenBit ? "/" + juce::String (cc + 32) : juce::String()) + " = " + laneTree[IDs::name].toString());
            seq.addEvent (juce::MidiMessage::textMetaEvent (1, mapping[mapping.size() - 1]), 0.0);

            int lastCoarse = -1, lastValue = -1;
            for (double t = 0.0; t <= length + 1.0e-9; t += s.step)
            {
                const float v = juce::jlimit (0.0f, 1.0f, Automation::laneValueAt (lane, t));
                const double tick = t * ticksPerSecond;
                if (s.fourteenBit)
                {
                    const int v14 = juce::roundToInt (v * 16383.0f);
                    if (v14 == lastValue) continue;
                    const int coarse = v14 >> 7, fine = v14 & 127;
                    if (coarse != lastCoarse)
                        seq.addEvent (juce::MidiMessage::controllerEvent (channel, cc, coarse), tick);
                    seq.addEvent (juce::MidiMessage::controllerEvent (channel, cc + 32, fine), tick);
                    lastCoarse = coarse;
                    lastValue = v14;
                }
                else
                {
                    const int v7 = juce::roundToInt (v * 127.0f);
                    if (v7 == lastValue) continue;
                    seq.addEvent (juce::MidiMessage::controllerEvent (channel, cc, v7), tick);
                    lastValue = v7;
                }
            }
            ++cc;
            ++automationLanes;
        }

        seq.addEvent (juce::MidiMessage::endOfTrack(), length * ticksPerSecond);
        seq.sort();
        seq.updateMatchedPairs();

        juce::MidiFile file;
        file.setTicksPerQuarterNote (ppq);
        file.addTrack (seq);

        s.output.deleteFile();
        juce::FileOutputStream out (s.output);
        if (! out.openedOk() || ! file.writeTo (out, 1))
            return "Could not write " + s.output.getFullPathName();

        summary << "MIDI file " << s.output.getFileName() << ": " << automationLanes << " lane(s) as "
                << (s.fourteenBit ? "14-bit" : "7-bit") << " controllers on channel " << channel;
        if (noteCount > 0) summary << ", " << noteCount << " notes";
        summary << ". " << mapping.joinIntoString (", ") << ". The file runs at 120 bpm, so times match in any DAW.";
        return {};
    }

    //==========================================================================
    juce::String writeReaperItems (const ExportSettings& s, Project& project, AutomationController& controller,
                                   const juce::Array<juce::ValueTree>& lanes, juce::String& summary)
    {
        if (! s.output.isDirectory() && ! s.output.createDirectory())
            return "Could not create the folder " + s.output.getFullPathName();

        const double beatsPerSecond = juce::jmax (1.0, s.reaperTempo) / 60.0;
        const double length = project.getLengthSeconds();
        juce::StringArray written;

        for (auto& lane : lanes)
        {
            if (Project::isNoteLane (lane)) continue;

            auto points = Exporter::envelopePoints (lane, baseValueFor (controller, lane), length, s.step, s.exactCurves);
            juce::String text;
            text << "SRCLEN " << num (length * beatsPerSecond) << "\n";
            text << "LFO 0 0 0 0 0 0 0\n";
            for (auto& p : points)
                text << "PPT " << num (p.time * beatsPerSecond) << " " << num (p.value) << " " << p.shape << "\n";

            auto file = s.output.getChildFile (safeFileName (lane[IDs::name].toString()) + ".ReaperAutoItem");
            if (! file.replaceWithText (text))
                return "Could not write " + file.getFullPathName();
            written.add (file.getFileName());
        }

        if (written.isEmpty())
            return "There are no automation lanes to export.";
        summary << written.size() << " automation item(s) in " << s.output.getFileName() << " (at " << num (s.reaperTempo, 1)
                << " bpm). In REAPER, show the parameter's envelope, right-click it and choose Insert automation item, then Load.";
        return {};
    }

    //==========================================================================
    // REAPER project from a template: lines, and helpers to find blocks in them.
    int blockEnd (const juce::StringArray& lines, int start)
    {
        int depth = 0;
        for (int i = start; i < lines.size(); ++i)
        {
            const auto t = lines[i].trimStart();
            if (t.startsWithChar ('<')) ++depth;
            else if (t == ">") { if (--depth == 0) return i; }
        }
        return -1;
    }

    int findLine (const juce::StringArray& lines, int from, int to, const juce::String& prefix)
    {
        for (int i = from; i < to && i < lines.size(); ++i)
            if (lines[i].trimStart().startsWith (prefix))
                return i;
        return -1;
    }

    juce::String writeReaperProject (const ExportSettings& s, Project& project, AutomationController& controller,
                                     const juce::Array<juce::ValueTree>& lanes, juce::String& summary)
    {
        if (! s.reaperTemplate.existsAsFile())
            return "Choose a template: a REAPER project in which you put the plugin on the first track and saved it.";

        juce::StringArray lines;
        lines.addLines (s.reaperTemplate.loadFileAsString());

        // Project tempo (for the MIDI item).
        double tempo = 120.0;
        if (auto i = findLine (lines, 0, lines.size(), "TEMPO "); i >= 0)
            tempo = juce::StringArray::fromTokens (lines[i].trim(), " ", "\"")[1].getDoubleValue();
        if (tempo <= 0.0) tempo = 120.0;

        const int track = findLine (lines, 0, lines.size(), "<TRACK");
        const int trackEnd = track >= 0 ? blockEnd (lines, track) : -1;
        if (track < 0 || trackEnd < 0)
            return "The template has no track.";

        const int chain = findLine (lines, track, trackEnd, "<FXCHAIN");
        const int chainEnd = chain >= 0 ? blockEnd (lines, chain) : -1;
        int plugin = -1;
        for (auto prefix : { "<VST", "<AU", "<CLAP", "<JS", "<DX", "<LV2" })
            if (auto i = findLine (lines, chain + 1, chainEnd, prefix); i >= 0 && (plugin < 0 || i < plugin))
                plugin = i;
        if (chain < 0 || plugin < 0)
            return "The template's first track has no plugin. In REAPER, put the plugin on the first track, save, and choose that file.";

        const int pluginEnd = blockEnd (lines, plugin);
        int wak = findLine (lines, pluginEnd + 1, chainEnd, "WAK");
        if (wak < 0) wak = chainEnd;

        // Remove envelopes the template already has for this plugin.
        for (int i = wak; --i > pluginEnd;)
            if (lines[i].trimStart().startsWith ("<PARMENV"))
            {
                const int e = blockEnd (lines, i);
                lines.removeRange (i, e - i + 1);
                wak -= (e - i + 1);
            }

        const auto indent = lines[plugin].upToFirstOccurrenceOf ("<", false, false);
        const double length = project.getLengthSeconds();
        juce::StringArray envelopes;
        int envCount = 0;

        for (auto& lane : lanes)
        {
            if (Project::isNoteLane (lane)) continue;
            const float base = baseValueFor (controller, lane);
            auto points = Exporter::envelopePoints (lane, base, length, s.step, s.exactCurves);

            envelopes.add (indent + "<PARMENV " + juce::String (parameterIndexFor (controller, lane)) + " 0 1 " + num (base));
            for (auto l : { "ACT 1 -1", "VIS 1 1 1", "LANEHEIGHT 0 0", "ARM 0", "DEFSHAPE 0 -1 -1" })
                envelopes.add (indent + "  " + l);
            for (auto& p : points)
                envelopes.add (indent + "  PT " + num (p.time) + " " + num (p.value) + " " + juce::String (p.shape));
            envelopes.add (indent + ">");
            ++envCount;
        }
        for (int i = envelopes.size(); --i >= 0;)
            lines.insert (wak, envelopes[i]);

        // Notes: one MIDI item at the start of the track.
        int noteCount = 0;
        {
            struct Ev { juce::int64 tick; int order; juce::String text; };
            std::vector<Ev> events;
            const double ticksPerSecond = 960.0 * tempo / 60.0;
            for (auto& lane : lanes)
            {
                if (! Project::isNoteLane (lane)) continue;
                for (auto item : lane)
                    if (item.hasType (IDs::ITEM))
                        for (auto& n : Project::getNotes (item))
                        {
                            events.push_back ({ (juce::int64) std::llround (n.start * ticksPerSecond), 1, "90 " + juce::String::toHexString (n.pitch).paddedLeft ('0', 2) + " " + juce::String::toHexString (n.velocity).paddedLeft ('0', 2) });
                            events.push_back ({ (juce::int64) std::llround (n.end * ticksPerSecond), 0, "80 " + juce::String::toHexString (n.pitch).paddedLeft ('0', 2) + " 00" });
                            ++noteCount;
                        }
            }
            if (noteCount > 0)
            {
                std::stable_sort (events.begin(), events.end(), [] (const Ev& a, const Ev& b)
                                  { return a.tick < b.tick || (a.tick == b.tick && a.order < b.order); });
                const int insertAt = blockEnd (lines, findLine (lines, 0, lines.size(), "<TRACK"));
                juce::StringArray item;
                item.add ("    <ITEM");
                item.add ("      POSITION 0");
                item.add ("      LENGTH " + num (length));
                item.add ("      NAME \"Trajet notes\"");
                item.add ("      <SOURCE MIDI");
                item.add ("        HASDATA 1 960 QN");
                juce::int64 last = 0;
                for (auto& e : events)
                {
                    item.add ("        E " + juce::String (e.tick - last) + " " + e.text);
                    last = e.tick;
                }
                item.add ("        E " + juce::String (juce::jmax ((juce::int64) 0, (juce::int64) std::llround (length * ticksPerSecond) - last)) + " b0 7b 00");
                item.add ("      >");
                item.add ("    >");
                for (int i = item.size(); --i >= 0;)
                    lines.insert (insertAt, item[i]);
            }
        }

        if (! s.output.replaceWithText (lines.joinIntoString ("\n") + "\n"))
            return "Could not write " + s.output.getFullPathName();

        summary << "REAPER project " << s.output.getFileName() << ": " << envCount << " envelope(s) on the template's plugin";
        if (noteCount > 0) summary << " and " << noteCount << " notes in a MIDI item";
        summary << ". Check in REAPER that each envelope sits on the right parameter.";
        return {};
    }
}

//==============================================================================
std::vector<Exporter::EnvPoint> Exporter::envelopePoints (const juce::ValueTree& laneTree, float baseValue,
                                                          double length, double step, bool exact)
{
    std::vector<EnvPoint> out;
    Automation::Lane lane;
    AutomationController::buildItems (laneTree, lane);
    auto trees = Project::getSortedItems (laneTree);

    if (lane.items.empty() || lane.items.front().start > 1.0e-9)
        out.push_back ({ 0.0, baseValue, 1 });

    for (size_t i = 0; i < lane.items.size() && (int) i < trees.size(); ++i)
    {
        const auto& item = lane.items[i];
        const auto& tree = trees.getReference ((int) i);
        if (item.start >= length) break;
        const double end = juce::jmin (item.end, length);
        const bool hasCurve = item.generator.type != Automation::GeneratorType::none || ! item.points.empty();
        if (! hasCurve) continue;   // an empty item holds the previous value, like in Trajet

        const size_t before = out.size();
        bool ramped = false;
        for (auto& tp : item.tempo.points) ramped = ramped || tp.ramp;

        if (exact || item.generator.type != Automation::GeneratorType::none || ramped)
        {
            appendSampled (out, item, item.start, end, step);
        }
        else
        {
            const ItemTime it (tree);
            const auto& pts = item.points;
            if (pts.front().time > 1.0e-9)
                out.push_back ({ item.start, pts.front().value, 1 });

            for (size_t k = 0; k < pts.size(); ++k)
            {
                const double t = it.absolute (pts[k].time);
                if (t > end + 1.0e-9) break;
                const bool lastPoint = k + 1 == pts.size();
                switch (pts[k].shape)
                {
                    case Automation::Shape::hold:   out.push_back ({ t, pts[k].value, 1 }); break;
                    case Automation::Shape::smooth: out.push_back ({ t, pts[k].value, 2 }); break;
                    case Automation::Shape::curve:
                    {
                        // REAPER has no matching curve: sample this segment.
                        const double t1 = lastPoint ? end : juce::jmin (end, it.absolute (pts[k + 1].time));
                        appendSampled (out, item, t, t1, step);
                        if (! lastPoint) out.pop_back();   // the next point follows
                        break;
                    }
                    case Automation::Shape::linear:
                    default:                        out.push_back ({ t, pts[k].value, 0 }); break;
                }
            }
        }

        // After its last point the item (and the gap after it) holds the value.
        if (out.size() > before)
            out.back().shape = 1;
    }

    if (out.empty())
        out.push_back ({ 0.0, baseValue, 1 });
    return out;
}

juce::String Exporter::run (const ExportSettings& s, Project& project, AutomationController& controller,
                            const juce::ValueTree& activeLane, juce::String& summary)
{
    auto lanes = chooseLanes (project, s, activeLane);
    if (lanes.isEmpty())
        return "No lanes to export (click a lane first to export just that one or its group).";

    switch (s.format)
    {
        case ExportSettings::Format::midi:          return writeMidi (s, project, controller, lanes, summary);
        case ExportSettings::Format::reaperItems:   return writeReaperItems (s, project, controller, lanes, summary);
        case ExportSettings::Format::reaperProject: return writeReaperProject (s, project, controller, lanes, summary);
    }
    return {};
}

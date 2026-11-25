// PresetForge — Pure Devotion content tool.
//   PresetForge generate --out <dir> [--neutral-names] [--limit N] [--threads N]
//   PresetForge render   --preset <file.pdvo> --out <file.wav> [--seconds S]
//   PresetForge export-wavetables --out <dir>
//   PresetForge export-samples --out <dir>
//   PresetForge export-art --out <dir>          (theme PNGs @1x/@2x, dog sprite sheets)
//   PresetForge screenshots --out <dir>         (editor screenshots per tab/theme)
//   PresetForge bench
#include "../src/PluginProcessor.h"
#include "../src/presets/PresetGenerator.h"
#include "../src/ui/DogVisualizer.h"
#include "../src/ui/Theme.h"
#include "../src/PluginEditor.h"

using namespace pd;

namespace
{
juce::String arg (const juce::StringArray& a, const juce::String& key, const juce::String& def = {})
{
    const int i = a.indexOf (key);
    return i >= 0 && i + 1 < a.size() ? a[i + 1] : def;
}

struct RenderResult
{
    float peak = 0, rms = 0;
    bool finite = true;
    std::array<float, kNumFeatures> features {};
};

RenderResult renderPreset (PureDevotionProcessor& proc, const std::vector<int>& notes, double seconds, double sr,
                           juce::AudioBuffer<float>* capture = nullptr)
{
    const int block = 512;
    const int total = (int) (seconds * sr);
    const int noteOffAt = (int) (total * 0.7);
    juce::AudioBuffer<float> buf (2, block);
    if (capture) capture->setSize (2, total);
    RenderResult r;
    double sum = 0; int count = 0;
    // feature accumulators
    float lpS = 0, hpPrev = 0, hpS = 0; double lowE = 0, highE = 0, sideE = 0, midE = 0;
    int zc = 0; float prevM = 0;
    std::vector<float> frameRms;
    double frameAcc = 0; int frameN = 0;
    const int frameLen = (int) (sr * 0.01);
    float envMax = 0; int attackIdx = -1;
    std::vector<float> envTrace;

    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (2, n, false, false, true);
        buf.clear();
        juce::MidiBuffer midi;
        if (pos == 0) for (int nn : notes) midi.addEvent (juce::MidiMessage::noteOn (1, nn, (juce::uint8) 100), 0);
        if (pos <= noteOffAt && noteOffAt < pos + n) for (int nn : notes) midi.addEvent (juce::MidiMessage::noteOff (1, nn), noteOffAt - pos);
        proc.processBlock (buf, midi);
        const float* L = buf.getReadPointer (0); const float* R = buf.getReadPointer (1);
        if (capture) { capture->copyFrom (0, pos, buf, 0, 0, n); capture->copyFrom (1, pos, buf, 1, 0, n); }
        for (int i = 0; i < n; ++i)
        {
            if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) { r.finite = false; continue; }
            r.peak = juce::jmax (r.peak, std::abs (L[i]), std::abs (R[i]));
            const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]);
            if (pos + i < noteOffAt) { sum += (double) m * m + (double) s * s; ++count; }
            midE += m * m; sideE += s * s;
            lpS += 0.02f * (m - lpS); lowE += lpS * lpS;
            hpS = 0.6f * (hpS + m - hpPrev); hpPrev = m; highE += hpS * hpS;
            if ((m > 0) != (prevM > 0)) ++zc;
            prevM = m;
            frameAcc += m * m; if (++frameN >= frameLen) { const float fr = (float) std::sqrt (frameAcc / frameN); frameRms.push_back (fr); envTrace.push_back (fr); frameAcc = 0; frameN = 0; }
        }
    }
    r.rms = (float) std::sqrt (sum / juce::jmax (1, count));
    for (size_t k = 0; k < envTrace.size(); ++k) envMax = juce::jmax (envMax, envTrace[k]);
    for (size_t k = 0; k < envTrace.size(); ++k) if (envTrace[k] >= envMax * 0.9f) { attackIdx = (int) k; break; }
    const double totalE = midE + sideE + 1e-12;
    const size_t onFrames = (size_t) (noteOffAt / juce::jmax (1, frameLen));
    auto avg = [&] (size_t a, size_t b) { double s = 0; int c = 0; for (size_t k = a; k < b && k < frameRms.size(); ++k) { s += frameRms[k]; ++c; } return c ? s / c : 0.0; };
    const double first = avg (0, onFrames * 3 / 10), last = avg (onFrames * 7 / 10, onFrames);
    double mean = avg (onFrames / 5, onFrames), var = 0; int vc = 0;
    for (size_t k = onFrames / 5; k < onFrames && k < frameRms.size(); ++k) { var += juce::square (frameRms[k] - mean); ++vc; }
    r.features = {
        juce::Decibels::gainToDecibels (r.peak + 1e-9f) - juce::Decibels::gainToDecibels (r.rms + 1e-9f),   // crest factor
        (float) std::log2 (1.0 + zc / seconds),                                                                // brightness via ZCR
        (float) std::log2 (1.0 + juce::jmax (0, attackIdx) * 10.0),                                            // attack (ms, log)
        (float) (last / (first + 1e-9)),                                                                       // sustain/decay ratio
        (float) std::sqrt (sideE / (midE + 1e-12)),                                                            // stereo width
        (float) (lowE / totalE),                                                                               // low-end weight
        (float) (highE / totalE),                                                                              // high-end weight
        (float) (vc ? std::sqrt (var / vc) / (mean + 1e-9) : 0.0) };                                          // movement
    return r;
}

double secondsFor (int category)
{
    switch (category) { case 4: case 11: case 12: case 13: case 14: return 4.0; case 9: return 1.5; default: return 2.5; }
}

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr, int bits = 24)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, (unsigned) b.getNumChannels(), bits, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// ------------------------------------------------------------ generate
int cmdGenerate (const juce::StringArray& args)
{
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (arg (args, "--out", "factory_presets")));
    const bool neutral = args.contains ("--neutral-names");
    const int limit = arg (args, "--limit", "0").getIntValue();
    const int threads = juce::jlimit (1, 16, arg (args, "--threads", juce::String (juce::SystemStats::getNumCpus())).getIntValue());
    auto plan = PresetGenerator::plan (neutral);
    if (limit > 0)
    {   // keep a spread across categories when limiting
        std::vector<GenSpec> sub;
        for (size_t i = 0; i < plan.size() && (int) sub.size() < limit; i += juce::jmax ((size_t) 1, plan.size() / (size_t) limit)) sub.push_back (plan[i]);
        plan = sub;
    }
    std::cout << "Generating " << plan.size() << " presets into " << out.getFullPathName() << " with " << threads << " threads" << std::endl;
    out.createDirectory();

    juce::SharedResourcePointer<SampleBank> samples; samples->waitUntilReady();
    juce::SharedResourcePointer<WavetableBank> tables;
    for (int t = 0; t < WavetableBank::numFactoryTables(); ++t) tables->getBlocking (t);

    struct Row { juce::String name, category, file; float peakDb, rmsDb; int variant; bool ok; };
    std::vector<Row> rows (plan.size());
    std::atomic<int> next { 0 }, done { 0 }, rerolls { 0 }, failures { 0 };
    const double sr = 48000.0;

    auto worker = [&] {
        PureDevotionProcessor proc;
        proc.setPlayConfigDetails (0, 2, sr, 512);
        proc.prepareToPlay (sr, 512);
        for (;;)
        {
            const int i = next.fetch_add (1);
            if (i >= (int) plan.size()) break;
            const auto& spec = plan[(size_t) i];
            PresetData d; RenderResult rr; int variant = 0; bool ok = false;
            for (; variant < 5 && ! ok; ++variant)
            {
                d = PresetGenerator::generate (spec, variant);
                d.params["master_vol"] = -24.f;
                proc.applyPreset (d);
                proc.prepareToPlay (sr, 512);   // reset all voices / tails between presets
                rr = renderPreset (proc, PresetGenerator::testNotes (spec.category), secondsFor (spec.category), sr);
                const float rmsDb = juce::Decibels::gainToDecibels (rr.rms + 1e-12f);
                ok = rr.finite && rmsDb > -80.f && rr.peak > 1.0e-4f;
                if (! ok) rerolls.fetch_add (1);
            }
            --variant;
            const float peakDb = juce::Decibels::gainToDecibels (rr.peak + 1e-12f);
            const float rmsDb = juce::Decibels::gainToDecibels (rr.rms + 1e-12f);
            const bool drums = spec.category == 9 || spec.category == 10;
            const float targetRms = spec.category == 4 || spec.category == 12 ? -19.f : (drums ? -15.f : -16.f);
            float gainDb = juce::jmin (targetRms - rmsDb, -1.5f - peakDb);
            const float master = juce::jlimit (-60.f, 6.f, -24.f + gainDb);
            gainDb = master + 24.f;
            d.params["master_vol"] = master;
            d.meta.peakDb = peakDb + gainDb;
            d.meta.rmsDb = rmsDb + gainDb;
            d.meta.features = rr.features;
            if (! ok) failures.fetch_add (1);
            const auto dir = out.getChildFile (juce::File::createLegalFileName (spec.bank))
                                .getChildFile (juce::File::createLegalFileName (PresetGenerator::categories()[spec.category]));
            const auto file = dir.getChildFile (juce::File::createLegalFileName (spec.name) + PresetFormat::fileExtension());
            d.meta.file = file;
            // sparse storage: only non-default parameters (the loader fills in defaults)
            for (auto it = d.params.begin(); it != d.params.end();)
            {
                const int pi = ParamTable::get().indexOf (it->first);
                if (pi >= 0 && it->first != "master_vol" && std::abs (it->second - ParamTable::get()[pi].def) < 1.0e-6f) it = d.params.erase (it);
                else ++it;
            }
            PresetFormat::save (file, d);
            rows[(size_t) i] = { spec.name, PresetGenerator::categories()[spec.category], file.getRelativePathFrom (out), d.meta.peakDb, d.meta.rmsDb, variant, ok };
            const int dn = done.fetch_add (1) + 1;
            if (dn % 100 == 0) std::cout << "  " << dn << " / " << plan.size() << std::endl;
        }
    };
    std::vector<std::unique_ptr<std::thread>> pool;
    for (int t = 0; t < threads; ++t) pool.push_back (std::make_unique<std::thread> (worker));
    for (auto& t : pool) t->join();

    // QA report
    juce::String csv = "name,category,file,peak_dbfs,rms_dbfs,variant,ok\n";
    int clipping = 0, silent = 0;
    for (auto& r : rows)
    {
        csv << "\"" << r.name << "\",\"" << r.category << "\",\"" << r.file << "\"," << juce::String (r.peakDb, 2) << ","
            << juce::String (r.rmsDb, 2) << "," << r.variant << "," << (r.ok ? "1" : "0") << "\n";
        if (r.peakDb > -0.5f) ++clipping;
        if (! r.ok || r.rmsDb < -60.f) ++silent;
    }
    out.getChildFile ("qa_report.csv").replaceWithText (csv);
    std::cout << "Done: " << rows.size() << " presets, " << rerolls.load() << " re-rolls, " << failures.load() << " failures, "
              << clipping << " over -0.5 dBFS, " << silent << " silent" << std::endl;
    return failures.load() == 0 && clipping == 0 && silent == 0 ? 0 : 2;
}

int cmdRender (const juce::StringArray& args)
{
    juce::SharedResourcePointer<SampleBank> samples; samples->waitUntilReady();
    juce::SharedResourcePointer<WavetableBank> tables;
    if (! args.contains ("--lazy-tables")) for (int t = 0; t < WavetableBank::numFactoryTables(); ++t) tables->getBlocking (t);
    PureDevotionProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000, 512);
    proc.prepareToPlay (48000, 512);
    PresetData d; juce::String err;
    if (! PresetFormat::load (juce::File::getCurrentWorkingDirectory().getChildFile (arg (args, "--preset")), d, err)) { std::cerr << err << std::endl; return 1; }
    proc.applyPreset (d);
    juce::AudioBuffer<float> cap;
    const int cat = PresetGenerator::categories().indexOf (d.meta.category);
    auto r = renderPreset (proc, PresetGenerator::testNotes (juce::jmax (0, cat)), arg (args, "--seconds", "3").getDoubleValue(), 48000, &cap);
    writeWav (juce::File::getCurrentWorkingDirectory().getChildFile (arg (args, "--out", "render.wav")), cap, 48000);
    std::cout << "peak " << juce::Decibels::gainToDecibels (r.peak) << " dBFS, rms " << juce::Decibels::gainToDecibels (r.rms) << " dBFS" << std::endl;
    return 0;
}

int cmdQaCheck (const juce::StringArray& args)
{
    juce::SharedResourcePointer<SampleBank> samples; samples->waitUntilReady();
    juce::SharedResourcePointer<WavetableBank> tables;
    for (int t = 0; t < WavetableBank::numFactoryTables(); ++t) tables->getBlocking (t);
    PureDevotionProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000, 512);
    proc.prepareToPlay (48000, 512);
    auto run = [&] (const juce::String& path) {
        PresetData d; juce::String err;
        PresetFormat::load (juce::File::getCurrentWorkingDirectory().getChildFile (path), d, err);
        d.params["master_vol"] = -24.f;
        proc.applyPreset (d);
        proc.prepareToPlay (48000, 512);
        const int cat = PresetGenerator::categories().indexOf (d.meta.category);
        auto r = renderPreset (proc, PresetGenerator::testNotes (juce::jmax (0, cat)), 2.5, 48000);
        std::cout << d.meta.name << ": peak " << juce::Decibels::gainToDecibels (r.peak) << " rms " << juce::Decibels::gainToDecibels (r.rms) << std::endl;
    };
    for (int i = 1; i < args.size(); ++i) if (args[i] != "--preset") run (args[i]);
    return 0;
}

// Loads the built VST3 through JUCE's VST3 host and exercises it the way plugin validators do.
int cmdHostTest (const juce::StringArray& args)
{
   #if JUCE_PLUGINHOST_VST3
    const juce::File plug (arg (args, "--plugin"));
    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> descs;
    fmt.findAllTypesForFile (descs, plug.getFullPathName());
    if (descs.isEmpty()) { std::cerr << "no plugin found in " << plug.getFullPathName() << std::endl; return 1; }
    const auto& desc = *descs[0];
    std::cout << "Loaded: " << desc.name << " by " << desc.manufacturerName << " (" << desc.pluginFormatName << ", instrument=" << desc.isInstrument << ")" << std::endl;
    int failures = 0;
    auto check = [&failures] (bool ok, const juce::String& what) { std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl; if (! ok) ++failures; };
    juce::String err;
    auto inst = fmt.createInstanceFromDescription (desc, 48000, 512, err);
    check (inst != nullptr, "instantiate " + err);
    if (! inst) return 1;
    check (inst->getTotalNumOutputChannels() == 2, "stereo output");
    check (inst->acceptsMidi(), "accepts MIDI");
    const auto& params = inst->getParameters();
    check (params.size() > 400, "parameters exposed: " + juce::String (params.size()));
    juce::Random rnd (42);
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int bs : { 1, 32, 64, 511, 1024 })
        {
            inst->releaseResources();
            inst->setRateAndBufferSizeDetails (sr, bs);
            inst->prepareToPlay (sr, bs);
            juce::AudioBuffer<float> buf (juce::jmax (2, inst->getTotalNumInputChannels()), bs);
            bool finite = true; float peak = 0;
            const int blocks = (int) (sr * 1.0 / bs);
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer midi;
                if (b % juce::jmax (1, blocks / 6) == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36 + rnd.nextInt (48), (juce::uint8) (40 + rnd.nextInt (87))), 0);
                if (b % juce::jmax (1, blocks / 5) == 0) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                // random automation (pluginval-style fuzz), avoiding master volume + oversampling flips every block
                for (int k = 0; k < 8; ++k) params[rnd.nextInt (params.size())]->setValue (rnd.nextFloat());
                buf.clear();
                inst->processBlock (buf, midi);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) { const float v = buf.getSample (c, i); if (! std::isfinite (v)) finite = false; peak = juce::jmax (peak, std::abs (v)); }
            }
            check (finite && peak <= 1.0001f, "process " + juce::String (sr, 0) + " Hz / " + juce::String (bs) + " samples with random automation (peak " + juce::String (peak, 3) + ")");
        }
    // state round trip
    for (int k = 0; k < params.size(); ++k) params[k]->setValue (rnd.nextFloat());
    juce::MemoryBlock state; inst->getStateInformation (state);
    auto inst2 = fmt.createInstanceFromDescription (desc, 48000, 512, err);
    inst2->setStateInformation (state.getData(), (int) state.getSize());
    int mismatches = 0, compared = 0;
    for (int k = 0; k < params.size(); ++k)
    {
        const auto name = params[k]->getName (64);
        if (name.startsWith ("MIDI CC") || name.containsIgnoreCase ("Bypass")) continue;   // VST3 wrapper proxies, not plugin state
        ++compared;
        // the host caches the raw (unsnapped) value it sent for stepped params, so equal display text counts as a match
        if (params[k]->getCurrentValueAsText() != inst2->getParameters()[k]->getCurrentValueAsText()
            && std::abs (params[k]->getValue() - inst2->getParameters()[k]->getValue()) > 1.0e-4f)
        {
            if (mismatches < 6) std::cout << "    mismatch: " << name << " " << params[k]->getCurrentValueAsText() << " vs " << inst2->getParameters()[k]->getCurrentValueAsText() << std::endl;
            ++mismatches;
        }
    }
    std::cout << "    compared " << compared << " parameters" << std::endl;
    check (mismatches == 0, "state round trip (" + juce::String (mismatches) + " mismatches, " + juce::String ((int) state.getSize()) + " bytes)");
    // editor
    if (inst->hasEditor())
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
        check (ed != nullptr && ed->getWidth() > 0, "editor opens (" + juce::String (ed ? ed->getWidth() : 0) + "x" + juce::String (ed ? ed->getHeight() : 0) + ")");
        ed.reset();
    }
    // automation from another thread while processing
    {
        inst->prepareToPlay (48000, 256);
        std::atomic<bool> stop { false };
        std::thread t ([&] { juce::Random r (7); while (! stop) params[r.nextInt (params.size())]->setValueNotifyingHost (r.nextFloat()); });
        juce::AudioBuffer<float> buf (2, 256); bool finite = true;
        for (int b = 0; b < 400; ++b)
        {
            juce::MidiBuffer m; if (b % 40 == 0) m.addEvent (juce::MidiMessage::noteOn (1, 48 + b % 24, (juce::uint8) 100), 0);
            buf.clear(); inst->processBlock (buf, m);
            for (int i = 0; i < 256; ++i) if (! std::isfinite (buf.getSample (0, i))) finite = false;
        }
        stop = true; t.join();
        check (finite, "concurrent parameter changes from a second thread");
    }
    inst->releaseResources();
    inst.reset(); inst2.reset();
    std::cout << (failures == 0 ? "ALL HOST TESTS PASSED" : "HOST TESTS FAILED: " + juce::String (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
   #else
    juce::ignoreUnused (args);
    std::cerr << "built without VST3 hosting" << std::endl; return 1;
   #endif
}

int cmdExportWavetables (const juce::StringArray& args)
{
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (arg (args, "--out", "wavetables")));
    out.createDirectory();
    std::vector<float> frames;
    for (int t = 0; t < WavetableBank::numFactoryTables(); ++t)
    {
        WavetableBank::generateFactory (t, frames);
        juce::AudioBuffer<float> b (1, (int) frames.size());
        // normalise each frame like the engine does
        for (int f = 0; f < kWtFrames; ++f)
        {
            float pk = 1e-6f; for (int i = 0; i < kWtSourceSize; ++i) pk = juce::jmax (pk, std::abs (frames[(size_t) (f * kWtSourceSize + i)]));
            for (int i = 0; i < kWtSourceSize; ++i) b.setSample (0, f * kWtSourceSize + i, 0.95f * frames[(size_t) (f * kWtSourceSize + i)] / pk);
        }
        writeWav (out.getChildFile (juce::String (t + 1).paddedLeft ('0', 2) + " " + WavetableBank::factoryName (t) + ".wav"), b, 48000, 32);
    }
    std::cout << "Exported " << WavetableBank::numFactoryTables() << " wavetables (256 x 2048 frames)" << std::endl;
    return 0;
}

int cmdExportSamples (const juce::StringArray& args)
{
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (arg (args, "--out", "samples")));
    out.createDirectory();
    for (int i = 0; i < SampleBank::kNumFactory; ++i)
    {
        auto s = SampleFactory::make (i);
        juce::AudioBuffer<float> b (s->stereo() ? 2 : 1, s->length());
        b.copyFrom (0, 0, s->L.data(), s->length());
        if (s->stereo()) b.copyFrom (1, 0, s->R.data(), s->length());
        auto dir = out.getChildFile (s->category);
        dir.createDirectory();
        writeWav (dir.getChildFile (juce::File::createLegalFileName (s->name) + ".wav"), b, s->sampleRate);
    }
    std::cout << "Exported " << SampleBank::kNumFactory << " samples" << std::endl;
    return 0;
}

int cmdBench (const juce::StringArray&)
{
    juce::SharedResourcePointer<SampleBank> samples; samples->waitUntilReady();
    PureDevotionProcessor proc;
    const double sr = 48000; const int block = 256;
    proc.setPlayConfigDetails (0, 2, sr, block);
    proc.prepareToPlay (sr, block);
    struct Case { const char* name; int cat; int voices; };
    const Case cases[] = { { "Bass (mono)", 0, 1 }, { "Hoover lead x 6 notes", 2, 6 }, { "Pad x 8 notes", 4, 8 }, { "Vocal spectral", 5, 3 } };
    auto plan = PresetGenerator::plan (false);
    for (auto& c : cases)
    {
        GenSpec spec; for (auto& p : plan) if (p.category == c.cat) { spec = p; break; }
        auto d = PresetGenerator::generate (spec);
        proc.applyPreset (d);
        proc.prepareToPlay (sr, block);
        juce::AudioBuffer<float> buf (2, block);
        const int blocks = (int) (sr * 10 / block);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer m;
            if (b == 0) for (int v = 0; v < c.voices; ++v) m.addEvent (juce::MidiMessage::noteOn (1, 48 + v * 3, (juce::uint8) 100), 0);
            buf.clear(); proc.processBlock (buf, m);
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::cout << c.name << ": " << juce::String (ms / 100.0, 2) << "% of one core (10 s rendered in " << juce::String (ms, 0) << " ms)" << std::endl;
    }
    return 0;
}

int cmdExportArt (const juce::StringArray& args)
{
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (arg (args, "--out", "art")));
    auto themesDir = out.getChildFile ("themes"); themesDir.createDirectory();
    auto spritesDir = out.getChildFile ("sprites"); spritesDir.createDirectory();
    juce::PNGImageFormat png;
    auto save = [&png] (const juce::Image& img, const juce::File& f) {
        f.deleteFile(); if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os); };
    for (int t = 0; t < ThemeManager::numThemes(); ++t)
        for (int scale : { 1, 2 })
        {
            juce::Image img (juce::Image::ARGB, 1280 * scale, 800 * scale, true);
            juce::Graphics g (img);
            g.addTransform (juce::AffineTransform::scale ((float) scale));
            ThemeManager::paintBackground (g, juce::Rectangle<float> (0, 0, 1280, 800), ThemeManager::theme (t), 0.f);
            save (img, themesDir.getChildFile (ThemeManager::theme (t).key + (scale == 2 ? "@2x.png" : ".png")));
        }
    // logo
    for (int scale : { 1, 2 })
    {
        juce::Image img (juce::Image::ARGB, 240 * scale, 64 * scale, true);
        juce::Graphics g (img); g.addTransform (juce::AffineTransform::scale ((float) scale));
        drawRaygunLogo (g, { 0, 0, 240, 64 }, juce::Colour (0xffF3F1EC), juce::Colour (0xffE8492A));
        save (img, out.getChildFile (scale == 2 ? "raygun_logo@2x.png" : "raygun_logo.png"));
    }
    // dog sprite sheets: 8 x 4 frames per scene
    const int fw = 256, fh = 160;
    for (int s = 0; s < DogVisualizer::numScenes(); ++s)
        for (int scale : { 1, 2 })
        {
            juce::Image sheet (juce::Image::ARGB, fw * 8 * scale, fh * 4 * scale, true);
            juce::Graphics g (sheet);
            for (int f = 0; f < 32; ++f)
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.addTransform (juce::AffineTransform::scale ((float) scale).translated ((float) ((f % 8) * fw * scale), (float) ((f / 8) * fh * scale)));
                DogVisualizer::Features feat;
                feat.time = (float) f / 32.f * 2.f; feat.rms = 0.3f + 0.2f * std::sin ((float) f * 0.4f); feat.env = 0.5f + 0.5f * std::sin ((float) f * 0.3f);
                feat.phase = (float) f / 32.f; feat.pitch = 60.f + 6.f * std::sin ((float) f * 0.2f); feat.tempo = 128.f; feat.lfoRate = 2.f;
                feat.earTwitch = (f % 8 == 0) ? 1.f : 0.f; feat.glint = (f % 5 == 0) ? 1.f : 0.f;
                for (int k = 0; k < 64; ++k) feat.wave[(size_t) k] = std::sin ((float) k / 64.f * kTwoPi * 2.f + (float) f * 0.3f) * 0.5f;
                DogVisualizer::paintScene (g, s, juce::Rectangle<float> (0, 0, (float) fw, (float) fh), feat, ThemeManager::theme (0));
            }
            save (sheet, spritesDir.getChildFile ("dogs_" + DogVisualizer::sceneKey (s) + (scale == 2 ? "@2x.png" : ".png")));
        }
    std::cout << "Exported theme backgrounds, logo and " << DogVisualizer::numScenes() << " dog sprite sheets" << std::endl;
    return 0;
}

int cmdScreenshots (const juce::StringArray& args)
{
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (arg (args, "--out", "screenshots")));
    out.createDirectory();
    juce::SharedResourcePointer<SampleBank> samples; samples->waitUntilReady();
    PureDevotionProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000, 512);
    proc.prepareToPlay (48000, 512);
    const auto presetPath = arg (args, "--preset");
    if (presetPath.isNotEmpty()) proc.loadPresetFile (juce::File::getCurrentWorkingDirectory().getChildFile (presetPath));
    // play a chord so the visualisers have something to react to
    juce::AudioBuffer<float> buf (2, 512);
    for (int b = 0; b < 40; ++b)
    {
        juce::MidiBuffer m; if (b == 0) for (int nn : { 48, 55, 60, 63 }) m.addEvent (juce::MidiMessage::noteOn (1, nn, (juce::uint8) 100), 0);
        buf.clear(); proc.processBlock (buf, m);
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<PureDevotionEditor*> (ed.get());
    juce::PNGImageFormat png;
    const juce::StringArray themesArg = juce::StringArray::fromTokens (arg (args, "--themes", "0"), ",", {});
    for (auto& ts : themesArg)
    {
        const int t = ts.getIntValue();
        editor->setTheme (t);
        for (int tab = 0; tab < 6; ++tab)
        {
            editor->showTab (tab);
            editor->setSize (1280, 800);
            for (int k = 0; k < 4; ++k) editor->tickForScreenshot();
            auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            auto f = out.getChildFile ("theme" + juce::String (t) + "_tab" + juce::String (tab) + ".png");
            f.deleteFile(); if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        }
    }
    std::cout << "Screenshots written to " << out.getFullPathName() << std::endl;
    return 0;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (juce::String::fromUTF8 (argv[i]));
    if (args.isEmpty()) { std::cout << "usage: PresetForge generate|render|export-wavetables|export-samples|export-art|screenshots|bench" << std::endl; return 1; }
    const auto cmd = args[0];
    int rc = 1;
    if (cmd == "generate") rc = cmdGenerate (args);
    else if (cmd == "render") rc = cmdRender (args);
    else if (cmd == "export-wavetables") rc = cmdExportWavetables (args);
    else if (cmd == "export-samples") rc = cmdExportSamples (args);
    else if (cmd == "export-art") rc = cmdExportArt (args);
    else if (cmd == "screenshots") rc = cmdScreenshots (args);
    else if (cmd == "bench") rc = cmdBench (args);
    else if (cmd == "qa-check") rc = cmdQaCheck (args);
    else if (cmd == "host-test") rc = cmdHostTest (args);
    else std::cout << "unknown command " << cmd << std::endl;
    juce::DeletedAtShutdown::deleteAll();
    return rc;
}

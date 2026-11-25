#include "Pages.h"
#include "../PluginProcessor.h"
#include "../presets/PresetGenerator.h"

namespace pd
{
BrowserPage::BrowserPage (PureDevotionProcessor& p) : proc (p)
{
    search.setTextToShowWhenEmpty ("Search presets, tracks, tags...", ThemeManager::current().textDim);
    search.onTextChange = [this] { similarMode = false; refresh(); };
    addAndMakeVisible (search);
    for (auto* c : { &bank, &track, &category, &genre, &mood, &type, &folder, &rating })
    {
        addAndMakeVisible (c);
        c->onChange = [this, c] { if (c == &bank) fillCombos(); similarMode = false; refresh(); };
    }
    for (auto* t : { &favOnly, &mpeOnly, &cpuOnly, &macroOnly })
    {
        addAndMakeVisible (t);
        t->onClick = [this] { similarMode = false; refresh(); };
    }
    addAndMakeVisible (audition);
    audition.setToggleState ((bool) proc.uiState.getProperty ("audition", true), juce::dontSendNotification);
    audition.onClick = [this] { proc.uiState.setProperty ("audition", audition.getToggleState(), nullptr); };
    for (auto* b : { &loadBtn, &similarBtn, &saveBtn, &folderBtn, &rescanBtn, &favBtn, &clearBtn }) addAndMakeVisible (b);
    loadBtn.onClick = [this] { loadRow (list.getSelectedRow()); };
    similarBtn.onClick = [this] {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= (int) results.size()) return;
        const int base = results[(size_t) row];
        results = { base };
        for (int i : proc.library().similar (base, 40)) results.push_back (i);
        similarMode = true;
        list.updateContent(); list.selectRow (0); repaint();
    };
    saveBtn.onClick = [this] { savePreset(); };
    folderBtn.onClick = [this] {
        auto* w = new juce::AlertWindow ("New Folder", "Folder name inside your user presets:", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", "My Sounds");
        w->addButton ("Create", 1); w->addButton ("Cancel", 0);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r) {
            if (r == 1) { proc.library().createUserFolder (w->getTextEditorContents ("name")); fillCombos(); }
            delete w;
        }), false);
    };
    rescanBtn.onClick = [this] { status = "Scanning..."; repaint(); proc.library().rescanAsync ([safe = juce::Component::SafePointer<BrowserPage> (this)] { if (safe) { safe->fillCombos(); safe->refresh(); } }); };
    favBtn.onClick = [this] {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= (int) results.size()) return;
        const auto m = proc.library().get (results[(size_t) row]);
        proc.library().setFavourite (m, ! proc.library().isFavourite (m));
        list.repaintRow (row);
    };
    clearBtn.onClick = [this] {
        search.clear();
        for (auto* c : { &bank, &track, &category, &genre, &mood, &type, &folder, &rating }) c->setSelectedId (1, juce::dontSendNotification);
        for (auto* t : { &favOnly, &mpeOnly, &cpuOnly, &macroOnly }) t->setToggleState (false, juce::dontSendNotification);
        similarMode = false; fillCombos(); refresh();
    };
    for (int s = 1; s <= 5; ++s)
    {
        auto* b = stars.add (new juce::TextButton (juce::String::fromUTF8 ("\xe2\x98\x85")));
        b->setTooltip ("Rate " + juce::String (s) + " star" + (s > 1 ? "s" : ""));
        b->onClick = [this, s] {
            const int row = list.getSelectedRow();
            if (row < 0 || row >= (int) results.size()) return;
            const auto m = proc.library().get (results[(size_t) row]);
            proc.library().setRating (m, proc.library().rating (m) == s ? 0 : s);
            list.repaintRow (row);
        };
        addAndMakeVisible (b);
    }
    list.setModel (this);
    list.setRowHeight (26);
    list.setMultipleSelectionEnabled (false);
    addAndMakeVisible (list);
    if (proc.library().size() == 0 && ! proc.library().isScanning())
    {
        status = "Scanning presets...";
        proc.library().rescanAsync ([safe = juce::Component::SafePointer<BrowserPage> (this)] { if (safe) { safe->status.clear(); safe->fillCombos(); safe->refresh(); } });
    }
    fillCombos();
    startTimerHz (10);
}

BrowserPage::~BrowserPage() { stopTimer(); list.setModel (nullptr); }

void BrowserPage::fillCombos()
{
    auto fill = [] (juce::ComboBox& c, const juce::String& all, const juce::StringArray& items) {
        const auto cur = c.getText();
        c.clear (juce::dontSendNotification);
        c.addItem (all, 1);
        for (int i = 0; i < items.size(); ++i) c.addItem (items[i], i + 2);
        const int idx = items.indexOf (cur);
        c.setSelectedId (idx >= 0 ? idx + 2 : 1, juce::dontSendNotification);
    };
    auto& lib = proc.library();
    fill (bank, "All banks", lib.banks());
    fill (track, "All tracks", lib.tracks (bank.getSelectedId() > 1 ? bank.getText() : juce::String()));
    fill (category, "All categories", lib.categories());
    fill (genre, "Any genre", { "UK Garage", "Breakbeat", "Techno", "Bass", "Ambient" });
    fill (mood, "Any mood", { "Euphoric", "Melancholy", "Dark", "Driving" });
    fill (type, "Any type", { "Bass", "Lead", "Pad", "Vocal", "Pluck", "Stab", "Arp", "Drum", "Loop", "FX", "Texture", "Modular", "Scene" });
    fill (folder, "All folders", lib.userFolders());
    fill (rating, "Any rating", { "1+ stars", "2+ stars", "3+ stars", "4+ stars", "5 stars" });
}

void BrowserPage::refresh()
{
    if (similarMode) return;
    PresetLibrary::Filter f;
    f.text = search.getText();
    auto val = [] (juce::ComboBox& c) { return c.getSelectedId() > 1 ? c.getText() : juce::String(); };
    f.bank = val (bank); f.track = val (track); f.category = val (category); f.genre = val (genre); f.mood = val (mood); f.type = val (type); f.folder = val (folder);
    f.minRating = juce::jmax (0, rating.getSelectedId() - 1);
    f.favouritesOnly = favOnly.getToggleState(); f.mpeOnly = mpeOnly.getToggleState(); f.cpuLightOnly = cpuOnly.getToggleState(); f.macrosOnly = macroOnly.getToggleState();
    results = proc.library().query (f);
    list.updateContent();
    repaint();
}

void BrowserPage::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) results.size()) return;
    const auto& t = ThemeManager::current();
    const auto m = proc.library().get (results[(size_t) row]);
    if (selected) { g.setColour (t.accent.withAlpha (0.35f)); g.fillRoundedRectangle (0, 1, (float) w, (float) h - 2, 4.f); }
    else if (row & 1) { g.setColour (t.panel.withMultipliedAlpha (0.5f)); g.fillRect (0, 0, w, h); }
    const bool fav = proc.library().isFavourite (m);
    const int stars_ = proc.library().rating (m);
    g.setColour (fav ? t.accent : t.textDim.withAlpha (0.3f));
    g.setFont (Fonts::mono (13.f));
    g.drawText (juce::String::fromUTF8 (fav ? "\xe2\x99\xa5" : "\xe2\x99\xa1"), 4, 0, 18, h, juce::Justification::centred);
    g.setColour (t.text);
    g.setFont (Fonts::header (14.f));
    g.drawText (m.name, 26, 0, w / 2 - 30, h, juce::Justification::centredLeft);
    g.setColour (t.textDim);
    g.setFont (Fonts::mono (10.f));
    g.drawText (m.category, w / 2, 0, w / 4, h, juce::Justification::centredLeft);
    g.drawText (m.track + (m.release.isNotEmpty() ? "  (" + m.release + ")" : ""), w * 3 / 4 - 20, 0, w / 4 - 60, h, juce::Justification::centredLeft);
    juce::String st; for (int s = 0; s < 5; ++s) st += juce::String::fromUTF8 (s < stars_ ? "\xe2\x98\x85" : "\xe2\x98\x86");
    g.setColour (stars_ > 0 ? t.accent3 : t.textDim.withAlpha (0.3f));
    g.drawText (st, w - 70, 0, 66, h, juce::Justification::centredRight);
}

void BrowserPage::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.x < 24 && row >= 0 && row < (int) results.size())
    {
        const auto m = proc.library().get (results[(size_t) row]);
        proc.library().setFavourite (m, ! proc.library().isFavourite (m));
        list.repaintRow (row);
    }
}

void BrowserPage::listBoxItemDoubleClicked (int row, const juce::MouseEvent&) { loadRow (row); }

void BrowserPage::selectedRowsChanged (int row)
{
    if (audition.getToggleState() && row >= 0) loadRow (row);
    repaint();
}

void BrowserPage::loadRow (int row)
{
    if (row < 0 || row >= (int) results.size()) return;
    const auto m = proc.library().get (results[(size_t) row]);
    if (! proc.loadPresetFile (m.file)) { status = "Could not load " + m.file.getFileName(); repaint(); return; }
    status = "Loaded " + m.name;
    if (audition.getToggleState())
    {
        if (auditionNote >= 0) proc.auditionNote (auditionNote, 0, false);
        const int cat = PresetGenerator::categories().indexOf (m.category);
        auditionNote = cat >= 0 ? PresetGenerator::testNotes (cat)[0] : 60;
        proc.auditionNote (auditionNote, 0.8f, true);
        auditionTicks = 12;
    }
    if (onPresetLoaded) onPresetLoaded();
    repaint();
}

void BrowserPage::loadRelative (int delta)
{
    if (results.empty()) refresh();
    if (results.empty()) return;
    int cur = list.getSelectedRow();
    if (cur < 0)
    {
        const int idx = proc.library().indexOfFile (proc.currentPresetFile);
        for (size_t i = 0; i < results.size(); ++i) if (results[i] == idx) cur = (int) i;
    }
    const int next = (cur + delta + (int) results.size()) % (int) results.size();
    list.selectRow (next, false, true);
    if (! audition.getToggleState()) loadRow (next);
}

void BrowserPage::timerCallback()
{
    if (auditionTicks > 0 && --auditionTicks == 0 && auditionNote >= 0) { proc.auditionNote (auditionNote, 0, false); auditionNote = -1; }
    if (! status.isEmpty() && status.startsWith ("Scanning") && ! proc.library().isScanning()) { status.clear(); fillCombos(); refresh(); }
}

void BrowserPage::savePreset()
{
    auto* w = new juce::AlertWindow ("Save Preset", "Save the current sound to your user presets.", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", proc.currentPresetName);
    juce::StringArray folders { "(root)" }; folders.addArray (proc.library().userFolders());
    w->addComboBox ("folder", folders, "Folder");
    w->addComboBox ("category", PresetGenerator::categories(), "Category");
    w->addButton ("Save", 1); w->addButton ("Cancel", 0);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r) {
        if (r == 1)
        {
            PresetMeta meta;
            meta.name = w->getTextEditorContents ("name").trim();
            if (meta.name.isEmpty()) meta.name = "Untitled";
            const auto folderName = w->getComboBoxComponent ("folder")->getText();
            const int cat = w->getComboBoxComponent ("category")->getSelectedItemIndex();
            meta.category = PresetGenerator::categories()[juce::jmax (0, cat)];
            meta.type = PresetGenerator::categoryType (juce::jmax (0, cat));
            meta.bank = "User"; meta.author = "Raygun"; meta.release = "User";
            meta.macrosReady = true;
            auto dir = PresetLibrary::userDir();
            if (folderName != "(root)") dir = dir.getChildFile (folderName);
            auto file = dir.getChildFile (juce::File::createLegalFileName (meta.name) + PresetFormat::fileExtension());
            if (proc.savePresetFile (file, meta))
            {
                status = "Saved " + meta.name;
                proc.library().rescanAsync ([safe = juce::Component::SafePointer<BrowserPage> (this)] { if (safe) { safe->fillCombos(); safe->refresh(); } });
            }
            else status = "Save failed";
            repaint();
        }
        delete w;
    }), false);
}

void BrowserPage::paint (juce::Graphics& g)
{
    const auto& t = ThemeManager::current();
    auto r = getLocalBounds().toFloat();
    Panel::paintPanel (g, r.removeFromLeft (250), "Filters");
    r.removeFromLeft (8);
    auto info = r.removeFromRight (300);
    Panel::paintPanel (g, info, "Preset");
    Panel::paintPanel (g, r.reduced (0, 0), similarMode ? "Similar Sounds" : juce::String (results.size()) + " presets");
    const int row = list.getSelectedRow();
    auto ir = info.reduced (14, 30);
    ir.removeFromBottom (120);
    if (row >= 0 && row < (int) results.size())
    {
        const auto m = proc.library().get (results[(size_t) row]);
        g.setColour (t.text);
        g.setFont (Fonts::header (20.f));
        g.drawFittedText (m.name, ir.removeFromTop (48).toNearestInt(), juce::Justification::topLeft, 2);
        g.setFont (Fonts::mono (11.f));
        auto line = [&] (const juce::String& k, const juce::String& v) {
            g.setColour (t.textDim); g.drawText (k, ir.removeFromTop (16).toNearestInt(), juce::Justification::left);
            g.setColour (t.text); g.drawFittedText (v, ir.removeFromTop (18).toNearestInt(), juce::Justification::left, 1);
        };
        line ("BANK", m.bank); line ("TRACK", m.track); line ("RELEASE", m.release); line ("CATEGORY", m.category);
        line ("GENRE / MOOD", m.genres.joinIntoString (", ") + "  /  " + m.moods.joinIntoString (", "));
        line ("TAGS", m.tags.joinIntoString (", "));
        line ("QA", "peak " + juce::String (m.peakDb, 1) + " dBFS  rms " + juce::String (m.rmsDb, 1) + " dBFS");
        // feature fingerprint (used for "similar sound")
        auto fr = ir.removeFromTop (60);
        const float bw = fr.getWidth() / kNumFeatures;
        for (int k = 0; k < kNumFeatures; ++k)
        {
            const float v = juce::jlimit (0.f, 1.f, std::abs (m.features[(size_t) k]) / (k == 0 ? 24.f : k == 1 ? 14.f : k == 2 ? 12.f : 1.f));
            g.setColour (t.accent.withAlpha (0.8f));
            g.fillRect (fr.getX() + (float) k * bw + 2, fr.getBottom() - v * fr.getHeight(), bw - 4, v * fr.getHeight());
        }
    }
    if (status.isNotEmpty())
    {
        g.setColour (t.textDim); g.setFont (Fonts::mono (11.f));
        g.drawText (status, getLocalBounds().removeFromBottom (18).withTrimmedLeft (260), juce::Justification::centredLeft);
    }
}

void BrowserPage::resized()
{
    auto r = getLocalBounds();
    auto left = r.removeFromLeft (250).reduced (12, 8);
    left.removeFromTop (22);
    search.setBounds (left.removeFromTop (28)); left.removeFromTop (6);
    for (auto* c : { &bank, &track, &category, &genre, &mood, &type, &folder, &rating }) { c->setBounds (left.removeFromTop (28).reduced (0, 2)); }
    left.removeFromTop (6);
    for (auto* t : { &favOnly, &mpeOnly, &cpuOnly, &macroOnly }) t->setBounds (left.removeFromTop (24));
    left.removeFromTop (6);
    clearBtn.setBounds (left.removeFromTop (28).reduced (0, 2));
    rescanBtn.setBounds (left.removeFromTop (28).reduced (0, 2));
    r.removeFromLeft (8);
    auto info = r.removeFromRight (300).reduced (14, 10);
    auto buttons = info.removeFromBottom (120);
    auto b1 = buttons.removeFromTop (30); layoutRow (b1, { &loadBtn, &similarBtn }, 4);
    auto b2 = buttons.removeFromTop (30); layoutRow (b2, { &saveBtn, &folderBtn }, 4);
    auto b3 = buttons.removeFromTop (30); layoutRow (b3, { &favBtn, &audition }, 4);
    auto b4 = buttons.removeFromTop (30);
    layoutRow (b4, { stars[0], stars[1], stars[2], stars[3], stars[4] }, 2);
    r.removeFromRight (8);
    list.setBounds (r.reduced (8, 0).withTrimmedTop (28).withTrimmedBottom (8));
}
} // namespace pd

#include "PluginEditor.h"

using namespace pd;

PureDevotionEditor::PureDevotionEditor (PureDevotionProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p),
      oscPage (p), patchPage (p), fxPage (p), modPage (p), performPage (p), browserPage (p)
{
    ThemeManager::setCurrent ((int) proc.uiState.getProperty ("theme", 0));
    lnf.refresh();
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    

    addAndMakeVisible (content);
    pages = { &oscPage, &patchPage, &fxPage, &modPage, &performPage, &browserPage };
    const char* names[] = { "OSC", "PATCH", "FX", "MOD", "PERFORM", "BROWSER" };
    for (int i = 0; i < 6; ++i)
    {
        auto* b = tabs.add (new juce::TextButton (names[i]));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { showTab (i); };
        content.addAndMakeVisible (b);
        content.addChildComponent (pages[(size_t) i]);
    }
    for (auto* b : { &prevBtn, &nextBtn, &presetName, &aBtn, &bBtn, &copyAB, &settingsBtn }) content.addAndMakeVisible (b);
    prevBtn.onClick = [this] { browserPage.loadRelative (-1); refreshPresetName(); };
    nextBtn.onClick = [this] { browserPage.loadRelative (1); refreshPresetName(); };
    presetName.onClick = [this] { showTab (5); };
    aBtn.onClick = [this] { proc.abSelect (0); refreshPresetName(); };
    bBtn.onClick = [this] { proc.abSelect (1); refreshPresetName(); };
    copyAB.onClick = [this] { proc.abCopyToOther(); };
    copyAB.setTooltip ("Copy the current sound into the other A/B slot");
    settingsBtn.onClick = [this] { showSettingsMenu(); };
    browserPage.onPresetLoaded = [this] { refreshPresetName(); fxPage.rebuild(); };

    for (int t = 0; t < ThemeManager::numThemes(); ++t) themeBox.addItem (ThemeManager::theme (t).name, t + 1);
    themeBox.setSelectedId (ThemeManager::currentIndex() + 1, juce::dontSendNotification);
    themeBox.onChange = [this] { setTheme (themeBox.getSelectedId() - 1); };
    content.addAndMakeVisible (themeBox);
    const int scales[] = { 75, 100, 125, 150, 175, 200 };
    for (int i = 0; i < 6; ++i) scaleBox.addItem (juce::String (scales[i]) + "%", scales[i]);
    scaleBox.onChange = [this] { setScale ((float) scaleBox.getSelectedId() / 100.f); };
    content.addAndMakeVisible (scaleBox);

    keyboard.setAvailableRange (24, 108);
    auto themeKeys = [this] {
        const auto& t = ThemeManager::current();
        keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, t.lightPanels ? juce::Colour (0xffFBF8F1) : t.chrome);
        keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, Palette::deepNight);
        keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colours::black.withAlpha (0.4f));
        keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, t.accent.withAlpha (0.8f));
        keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, t.accent.withAlpha (0.3f));
        keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha (0.3f));
        keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, juce::Colours::black.withAlpha (0.6f));
    };
    themeKeys();
    keyboard.setOctaveForMiddleC (4);
    keyState.addListener (this);
    content.addAndMakeVisible (keyboard);

    performPage.onFullScreenVisualizer = [this] (bool on) { setPerformFullScreen (on); };
    ThemeManager::addListener (this, [this, themeKeys] { lnf.refresh(); themeKeys(); sendLookAndFeelChange(); repaint(); });

    // GPU-accelerated rendering (OpenGL backs Metal-era macOS via the system GL layer; Windows uses WGL)
   #if ! JUCE_LINUX
    if ((bool) proc.uiState.getProperty ("gpu", true)) gl.attachTo (*this);
   #endif

    setResizable (true, true);
    setResizeLimits ((int) (kBaseW * 0.75f), (int) (kBaseH * 0.75f), kBaseW * 2, kBaseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kBaseW / kBaseH);
    scale = (float) (double) proc.uiState.getProperty ("scale", 1.0);
    scaleBox.setSelectedId ((int) std::round (scale * 100.f), juce::dontSendNotification);
    showTab ((int) proc.uiState.getProperty ("tab", 0));
    refreshPresetName();
    setSize ((int) (kBaseW * scale), (int) (kBaseH * scale));
    startTimerHz (20);
}

PureDevotionEditor::~PureDevotionEditor()
{
    stopTimer();
    keyState.removeListener (this);
    ThemeManager::removeListener (this);
    gl.detach();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void PureDevotionEditor::setScale (float s)
{
    scale = juce::jlimit (0.75f, 2.f, s);
    proc.uiState.setProperty ("scale", scale, nullptr);
    setSize ((int) (kBaseW * scale), (int) (kBaseH * scale));
}

void PureDevotionEditor::setTheme (int t)
{
    ThemeManager::setCurrent (t);
    proc.uiState.setProperty ("theme", t, nullptr);
    themeBox.setSelectedId (t + 1, juce::dontSendNotification);
}

void PureDevotionEditor::showTab (int t)
{
    currentTab = juce::jlimit (0, 5, t);
    for (int i = 0; i < 6; ++i)
    {
        pages[(size_t) i]->setVisible (i == currentTab);
        tabs[i]->setToggleState (i == currentTab, juce::dontSendNotification);
    }
    proc.uiState.setProperty ("tab", currentTab, nullptr);
    if (currentTab == 5) browserPage.refresh();
    if (currentTab == 2) fxPage.rebuild();
}

void PureDevotionEditor::refreshPresetName()
{
    presetName.setButtonText (proc.currentPresetName);
    aBtn.setToggleState (proc.abCurrent() == 0, juce::dontSendNotification);
    bBtn.setToggleState (proc.abCurrent() == 1, juce::dontSendNotification);
}

void PureDevotionEditor::resized()
{
    scale = (float) getWidth() / (float) kBaseW;
    content.setBounds (0, 0, kBaseW, kBaseH);
    content.setTransform (juce::AffineTransform::scale (scale));
    auto r = juce::Rectangle<int> (0, 0, kBaseW, kBaseH);
    auto header = r.removeFromTop (60);
    auto footer = r.removeFromBottom (56);
    header.removeFromLeft (300);   // logotype area
    header.removeFromRight (120);  // RAYGUN mark
    auto tabsArea = header.removeFromLeft (480).reduced (0, 14);
    for (auto* t : tabs) { t->setBounds (tabsArea.removeFromLeft (78)); tabsArea.removeFromLeft (2); }
    header.removeFromLeft (10);
    auto nav = header.reduced (0, 15);
    prevBtn.setBounds (nav.removeFromLeft (24));
    presetName.setBounds (nav.removeFromLeft (170).reduced (2, 0));
    nextBtn.setBounds (nav.removeFromLeft (24));
    nav.removeFromLeft (6);
    aBtn.setBounds (nav.removeFromLeft (24)); bBtn.setBounds (nav.removeFromLeft (24)); copyAB.setBounds (nav.removeFromLeft (36));
    footer.reduce (8, 6);
    settingsBtn.setBounds (footer.removeFromRight (84).reduced (2, 8));
    scaleBox.setBounds (footer.removeFromRight (74).reduced (2, 9));
    themeBox.setBounds (footer.removeFromRight (130).reduced (2, 9));
    footer.removeFromRight (8);
    keyboard.setBounds (footer);
    for (auto* p : pages) p->setBounds (r.reduced (8, 4));
    if (fullVis) fullVis->setBounds (r);
}

void PureDevotionEditor::paint (juce::Graphics& g)
{
    const auto& t = ThemeManager::current();
    g.addTransform (juce::AffineTransform::scale (scale));
    ThemeManager::paintBackground (g, { 0, 0, (float) kBaseW, (float) kBaseH }, t, animTime);
    // header strip
    auto header = juce::Rectangle<float> (0, 0, (float) kBaseW, 60);
    g.setColour (t.lightPanels ? juce::Colours::white.withAlpha (0.15f) : juce::Colours::black.withAlpha (0.3f));
    g.fillRect (header);
    g.setColour (t.accent);
    g.fillRect (0.f, 59.f, (float) kBaseW, 1.f);
    g.setColour (t.text);
    drawTracked (g, "PURE DEVOTION", { 18, 8, 280, 44 }, Fonts::header (30.f), 0.22f, juce::Justification::centredLeft);
    // RAYGUN small + right-aligned, with logo glyph
    auto mark = juce::Rectangle<float> ((float) kBaseW - 118, 12, 108, 36);
    drawRaygunLogo (g, mark.removeFromTop (22).withTrimmedLeft (20), t.text, t.accent);
    g.setColour (t.textDim);
    drawTracked (g, "RAYGUN", mark, Fonts::header (11.f), 0.4f, juce::Justification::centredRight);
    // footer
    g.setColour (t.lightPanels ? juce::Colours::white.withAlpha (0.12f) : juce::Colours::black.withAlpha (0.25f));
    g.fillRect (0.f, (float) kBaseH - 56.f, (float) kBaseW, 56.f);
}

void PureDevotionEditor::paintOverChildren (juce::Graphics& g)
{
    const float grain = (float) (double) proc.uiState.getProperty ("grain", 0.03);
    if (grain > 0.001f) ThemeManager::paintGrain (g, getLocalBounds(), juce::jlimit (0.f, 0.04f, grain), grainFrame);
}

void PureDevotionEditor::timerCallback()
{
    animTime += 0.05f;
    if (++grainFrame % 3 == 0 && (double) proc.uiState.getProperty ("grain", 0.03) > 0.001) repaint();
    if (presetName.getButtonText() != proc.currentPresetName) refreshPresetName();
}

void PureDevotionEditor::tickForScreenshot()
{
    for (auto* v : oscPage.visualizers()) v->tick (0.1f);
    if (fullVis) fullVis->tick (0.1f);
    animTime += 0.1f;
}

void PureDevotionEditor::handleNoteOn (juce::MidiKeyboardState*, int, int note, float vel) { proc.auditionNote (note, vel, true); }
void PureDevotionEditor::handleNoteOff (juce::MidiKeyboardState*, int, int note, float) { proc.auditionNote (note, 0.f, false); }

void PureDevotionEditor::setPerformFullScreen (bool on)
{
    if (on && fullVis == nullptr)
    {
        fullVis = std::make_unique<DogVisualizer> (proc, 0);
        fullVis->setFullScreen (true);
        fullVis->setScene ((int) proc.uiState.getProperty ("vis_full_scene", 4));
        fullVis->setInterceptsMouseClicks (true, false);
        content.addAndMakeVisible (*fullVis);
        resized();
        fullVis->addMouseListener (this, false);
    }
    else if (! on) fullVis.reset();
}

void PureDevotionEditor::mouseDown (const juce::MouseEvent& e)
{
    if (fullVis != nullptr && e.eventComponent == fullVis.get())
    {
        setPerformFullScreen (false);
        performPage.repaint();
    }
}

void PureDevotionEditor::showSettingsMenu()
{
    juce::PopupMenu m, vis, grain, os, scenes;
    const bool visOn = (bool) proc.uiState.getProperty ("vis_enabled", true);
    vis.addItem (100, "Show visualisers", true, visOn);
    const double op = proc.uiState.getProperty ("vis_opacity", 0.35);
    for (int k = 0; k <= 6; ++k) vis.addItem (110 + k, "Opacity " + juce::String (k * 10) + "%", true, std::abs (op - k * 0.1) < 0.01);
    for (int s = 0; s < DogVisualizer::numScenes(); ++s) scenes.addItem (130 + s, DogVisualizer::sceneName (s), true, (int) proc.uiState.getProperty ("vis_full_scene", 4) == s);
    vis.addSubMenu ("Full-screen scene", scenes);
    m.addSubMenu ("Two Dogs visualisers", vis);
    const double gr = proc.uiState.getProperty ("grain", 0.03);
    for (int k = 0; k <= 4; ++k) grain.addItem (200 + k, k == 0 ? juce::String ("Off") : juce::String (k) + "%", true, std::abs (gr - k * 0.01) < 0.001);
    m.addSubMenu ("Film grain", grain);
    m.addItem (300, "GPU rendering (reopen editor)", true, (bool) proc.uiState.getProperty ("gpu", true));
    const int cur = (int) proc.getParamPlain (IX().osamp);
    for (int k = 0; k < 4; ++k) os.addItem (400 + k, choices::oversampling()[k], true, cur == k);
    m.addSubMenu ("Oversampling", os);
    m.addItem (410, "Eco mode", true, proc.getParamPlain (IX().eco) > 0.5f);
    m.addItem (420, "MPE", true, proc.getParamPlain (IX().mpe) > 0.5f);
    m.addItem (430, "Send macros as MIDI CC 20-27", true, proc.getEngineState().macroCCOut);
    m.addSeparator();
    m.addItem (500, "About Pure Devotion");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsBtn), [this] (int r) {
        if (r == 100) proc.uiState.setProperty ("vis_enabled", ! (bool) proc.uiState.getProperty ("vis_enabled", true), nullptr);
        else if (r >= 110 && r <= 116) proc.uiState.setProperty ("vis_opacity", (r - 110) * 0.1, nullptr);
        else if (r >= 130 && r < 140) { proc.uiState.setProperty ("vis_full_scene", r - 130, nullptr); if (fullVis) fullVis->setScene (r - 130); }
        else if (r >= 200 && r <= 204) proc.uiState.setProperty ("grain", (r - 200) * 0.01, nullptr);
        else if (r == 300) proc.uiState.setProperty ("gpu", ! (bool) proc.uiState.getProperty ("gpu", true), nullptr);
        else if (r >= 400 && r < 404) proc.setParamPlain (IX().osamp, (float) (r - 400));
        else if (r == 410) proc.setParamPlain (IX().eco, proc.getParamPlain (IX().eco) > 0.5f ? 0.f : 1.f);
        else if (r == 420) proc.setParamPlain (IX().mpe, proc.getParamPlain (IX().mpe) > 0.5f ? 0.f : 1.f);
        else if (r == 430) { auto es = proc.getEngineState(); es.macroCCOut = ! es.macroCCOut; proc.setEngineState (es); }
        else if (r == 500)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "Pure Devotion",
                "Pure Devotion v" PD_VERSION_STRING "\nHybrid wavetable + semi-modular performance synthesizer.\n\nBy Raygun.\n\n"
                "Fonts: Roboto Condensed (Apache 2.0), JetBrains Mono (OFL 1.1).\nBuilt with JUCE.");
        for (auto* v : oscPage.visualizers()) { v->visibilityChanged(); v->repaint(); }
        repaint();
    });
}

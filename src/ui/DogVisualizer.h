#pragma once
#include "Theme.h"
#include <array>

class PureDevotionProcessor;

namespace pd
{
// "Two Dogs" — original illustrated Doberman scenes that react to the audio.
class DogVisualizer : public juce::Component, private juce::Timer
{
public:
    struct Features
    {
        float time = 0, rms = 0, env = 0, phase = 0, pitch = 60, tempo = 120, lfoRate = 1;
        float earTwitch = 0, glint = 0;
        std::array<float, 64> wave {};
    };

    DogVisualizer (PureDevotionProcessor& p, int oscIndex);
    ~DogVisualizer() override;
    void paint (juce::Graphics&) override;
    void visibilityChanged() override;
    void setScene (int s) { scene = s; repaint(); }
    int getScene() const { return scene; }
    void setOpacity (float o) { opacity = o; repaint(); }
    void setFullScreen (bool f) { fullScreen = f; }
    void tick (float dt);

    static int numScenes() { return 7; }
    static juce::String sceneName (int s);
    static juce::String sceneKey (int s);
    static void paintScene (juce::Graphics& g, int scene, juce::Rectangle<float> r, const Features& f, const Theme& t);

private:
    void timerCallback() override { tick (1.f / 30.f); }
    PureDevotionProcessor& proc;
    int osc;
    int scene = 0;
    float opacity = 0.35f;
    bool fullScreen = false;
    Features feat;
    int lastTransients = 0;
};
} // namespace pd

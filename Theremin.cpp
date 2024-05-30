#include <cstdio>  // for printing to stdout

// http://www.thereminworld.com/Forums/T/32167/theremin-like-sound-synthesis

#include "Gamma/Analysis.h"
#include "Gamma/Delay.h"
#include "Gamma/Effects.h"
#include "Gamma/Envelope.h"
#include "Gamma/Filter.h"
#include "Gamma/Oscillator.h"
#include "al/app/al_App.hpp"
#include "al/graphics/al_Font.hpp"
#include "al/graphics/al_Shapes.hpp"
#include "al/scene/al_PolySynth.hpp"
#include "al/scene/al_SynthSequencer.hpp"
#include "al/ui/al_ControlGUI.hpp"
#include "al/ui/al_Parameter.hpp"

// using namespace gam;
using namespace al;

// This example shows how to use SynthVoice and SynthManagerto create an audio
// visual synthesizer. In a class that inherits from SynthVoice you will
// define the synth's voice parameters and the sound and graphic generation
// processes in the onProcess() functions.

// The helper function used to visualize which keys pressed or released on a
// virtual piano.

float clamp(float value, float min, float max) {
    if (value > max)
        return max;
    if (value < min)
        return min;
    return value;
}

struct NotePair {
    std::string note;
    float freq;
    NotePair(std::string n, float f) {
        note = n;
        freq = f;
    }
};

class Theremin : public SynthVoice {
   public:
    // Unit generators
    gam::Pan<> mPan;

    gam::Saw<> mOsc;
    gam::Sine<> mOsc2;

    gam::Env<3> mAmpEnv;

    // Vibrato
    gam::Sine<> mVib;
    gam::ADSR<> mVibEnv;

    gam::OnePole<> lpf;
    gam::OnePole<> hpf;

    float vibValue;

    // envelope follower to connect audio output to graphics
    gam::EnvFollow<> mEnvFollow;

    Mesh mMesh;

    // Initialize voice. This function will only be called once per voice when
    // it is created. Voices will be reused if they are idle.
    void
    init() override {
        // Intialize envelope
        mAmpEnv.curve(0);  // make segments lines
        mAmpEnv.levels(0, 1, 1, 0);
        mAmpEnv.sustainPoint(
            2);  // Make point 2 sustain until a release is issued

        mVibEnv.curve(0);

        lpf.type(gam::LOW_PASS);
        lpf.freq(1800);

        hpf.type(gam::SMOOTHING);
        hpf.freq(4000);

        addRect(mMesh, 1, 1, 0.5, 0.5);
        // This is a quick way to create parameters for the voice. Trigger
        // parameters are meant to be set only when the voice starts, i.e. they
        // are expected to be constant within a voice instance. (You can
        // actually change them while you are prototyping, but their changes
        // will only be stored and aplied when a note is triggered.)

        createInternalTriggerParameter("amplitude", 0.3, 0.0, 1.0);
        createInternalTriggerParameter("frequency", 60, 20, 5000);
        createInternalTriggerParameter("attackTime", 0.01, 0.01, 3.0);
        createInternalTriggerParameter("releaseTime", 0.1, 0.1, 10.0);
        createInternalTriggerParameter("pan", 0.0, -1.0, 1.0);

        createInternalTriggerParameter("vibRate1", 3.5, 0.2, 20);
        createInternalTriggerParameter("vibRate2", 8, 0.2, 20);
        createInternalTriggerParameter("vibRise", 0.5, 0.1, 2);
        createInternalTriggerParameter("vibDepth", 0.005, 0.0, 0.3);

        createInternalTriggerParameter("lowPassFilter", 800, 0, 44000);
        createInternalTriggerParameter("highPassFilter", 900, 0, 44000);
    }

    // The audio processing function
    void
    onProcess(AudioIOData &io) override {
        // Get the values from the parameters and apply them to the
        // corresponding unit generators. You could place these lines in the
        // onTrigger() function, but placing them here allows for realtime
        // prototyping on a running voice, rather than having to trigger a new
        // voice to hear the changes. Parameters will update values once per
        // audio callback because they are outside the sample processing loop.
        float oscFreq = getInternalParameterValue("frequency");
        float vibDepth = getInternalParameterValue("vibDepth");
        mAmpEnv.lengths()[0] = getInternalParameterValue("attackTime");
        mAmpEnv.lengths()[2] = getInternalParameterValue("releaseTime");

        lpf.freq(getInternalParameter("lowPassFilter"));
        hpf.freq(getInternalParameter("highPassFilter"));

        mPan.pos(getInternalParameterValue("pan"));
        while (io()) {
            mVib.freq(mVibEnv());
            vibValue = mVib();
            mOsc.freq(oscFreq + vibValue * vibDepth * oscFreq);
            mOsc2.freq(oscFreq + 3 + vibValue * vibDepth * oscFreq);

            float s1 = (mOsc() + mOsc2()) / 2 * mAmpEnv() * getInternalParameterValue("amplitude");

            s1 = hpf(lpf(s1));
            float s2;
            mEnvFollow(s1);
            mPan(s1, s1, s2);
            io.out(0) += s1;
            io.out(1) += s2;
        }
        // We need to let the synth know that this voice is done
        // by calling the free(). This takes the voice out of the
        // rendering chain
        if (mAmpEnv.done() && (mEnvFollow.value() < 0.001f))
            free();
    }

    // The graphics processing function
    void
    onProcess(Graphics &g) override {
    }

    // The triggering functions just need to tell the envelope to start or
    // release The audio processing function checks when the envelope is done
    // to remove the voice from the processing chain.
    void
    onTriggerOn() override {
        mAmpEnv.reset();
        mVibEnv.reset();

        mVibEnv.levels(getInternalParameterValue("vibRate1"),
            getInternalParameterValue("vibRate2"),
            getInternalParameterValue("vibRate2"),
            getInternalParameterValue("vibRate1"));

        /*
        mVibEnv.lengths()[0] = getInternalParameterValue("vibRise");
        mVibEnv.lengths()[1] = getInternalParameterValue("vibRise");
        mVibEnv.lengths()[3] = getInternalParameterValue("vibRise");
        */
    }

    void
    onTriggerOff() override {
        mAmpEnv.release();
    }
};

// We make an app.
class MyApp : public App {
   public:
    SynthGUIManager<Theremin> synthManager{"Theremin"};

    FontRenderer fontRender;
    int fontSize = 16;

    Theremin *instrument;

    std::vector<NotePair> notes = {
        NotePair("E", 415.3), NotePair("F", 440), NotePair("F#", 466.2), NotePair("G", 493.9), NotePair("G#", 523.3), NotePair("A", 554.4), NotePair("A#", 587.3), NotePair("B", 622.3), NotePair("C", 659.3), NotePair("C#", 698.5), NotePair("D", 739.9), NotePair("D#", 783.9), NotePair("E", 830.6), NotePair("F", 880), NotePair("F#", 932.3), NotePair("G", 987.8), NotePair("G#", 1046.5), NotePair("A", 1108.7), NotePair("A#", 1174.7), NotePair("B", 1244.5), NotePair("C", 1318.5), NotePair("C#", 1396.9), NotePair("D", 1479.9), NotePair("D#", 1567.9), NotePair("E", 1661.2)
    };


    void
    onCreate() override {
        navControl().active(
            false);  // Disable navigation via keyboard, since we
                     // will be using keyboard for note triggering

        // Set sampling rate for Gamma objects from app's audio
        gam::sampleRate(audioIO().framesPerSecond());

        imguiInit();

        instrument = synthManager.voice();

        synthManager.triggerOn();

        // Set the font renderer
        fontRender.load(Font::defaultFont().c_str(), 60, 1024);

        // Play example sequence. Comment this line to start from scratch
        synthManager.synthRecorder().verbose(true);
    }

    // The audio callback function. Called when audio hardware requires data
    void
    onSound(AudioIOData &io) override {
        synthManager.render(io);  // Render audio
    }

    void
    onAnimate(double dt) override {
        // The GUI is prepared here
        imguiBeginFrame();
        // Draw a window that contains the synth control panel
        synthManager.drawSynthControlPanel();
        imguiEndFrame();
    }

    bool
    onMouseMove(const Mouse &m) override {
        // Get the mouse position
        int x = m.x();
        int y = m.y();
        // Print the mouse position
        instrument->setInternalParameterValue("frequency", x + 400);

        // std::cout << "pos: " << x << ", " << y << std::endl;

        instrument->setInternalParameterValue("amplitude", clamp((float)(height() - (y + 50)) / (height() * 0.8f), 0, 1));

        // instrument->triggerOn();

        return true;
    }

    // The graphics callback function.
    void
    onDraw(Graphics &g) override {
        g.clear();

        // This example uses only the orthogonal projection for 2D drawing
        g.camera(Viewpoint::ORTHO_FOR_2D);  // Ortho [0:width] x [0:height]

        // Render the synth's graphics
        synthManager.render(g);

        drawRect(g, 0, 50, width(), 2);

        for (int i = 0; i < notes.size(); i++) {
            drawRect(g, notes[i].freq - 400, 70, 2, 40);
        }

        // For some reason rects won't draw after prints?
        for (int i = 0; i < notes.size(); i++) {
            print(g, notes[i].note, notes[i].freq - 8 - 400, 15);
        }

        // GUI is drawn here
        imguiDraw();
    }

    // Whenever a key is pressed, this function is called
    bool
    onKeyDown(Keyboard const &k) override {
        if (ParameterGUI::usingKeyboard()) {  // Ignore keys if GUI is using
            // keyboard
            return true;
        }

        // Control lpf and hpf
        int button = k.key();
        std::cout << button << std::endl;

        if (button == 49) {  // 1
            instrument->setInternalParameterValue("lowPassFilter", instrument->getInternalParameter("lowPassFilter") - 100);
        } else if (button == 50) {
            instrument->setInternalParameterValue("lowPassFilter", instrument->getInternalParameter("lowPassFilter") + 100);
        } else if (button == 51) {  // 3
            instrument->setInternalParameterValue("highPassFilter", instrument->getInternalParameter("highPassFilter") - 100);
        } else if (button == 52) {
            instrument->setInternalParameterValue("highPassFilter", instrument->getInternalParameter("highPassFilter") + 100);
        }

        return true;
    }

    // Whenever a key is released this function is called
    bool
    onKeyUp(Keyboard const &k) override {
        return true;
    }

    // Whenever the window size changes this function is called
    void
    onResize(int w, int h) override {
    }

    void
    onExit() override {
        imguiShutdown();
    }

    void
    drawRect(Graphics &g, int x, int y, int width, int height) {
        g.tint(1, 1, 1);
        Mesh mesh;
        addRect(mesh, width, height, x + width / 2, y - height / 2);
        g.draw(mesh);
    }

    void
    print(Graphics &g, std::string text, double x, double y) {
        g.pushMatrix();
        fontRender.write(text.c_str(), fontSize);
        fontRender.renderAt(g, {x, y, 0.0});
        g.popMatrix();
        g.tint(1, 1, 1);
    }
};

int main() {
    // Create app instance
    MyApp app;

    // Set window size
    app.dimensions(1200, 600);

    // Set up audio
    app.configureAudio(48000., 512, 2, 0);
    app.start();
    return 0;
}

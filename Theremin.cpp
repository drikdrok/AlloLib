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
#include "al/io/al_MIDI.hpp"
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

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

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
        createInternalTriggerParameter("baseAmplitude", 0.3, 0.0, 1.0);

        createInternalTriggerParameter("frequency", 60, 20, 5000);
        createInternalTriggerParameter("targetFrequency", 60, 20, 5000);
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

struct CallbackData {
    Theremin *instrument;
    bool *mousePlay;
    float* timeSinceLastNote;
};

void midiCallback(double deltaTime, std::vector<unsigned char> *msg, void *userData) {
    unsigned numBytes = msg->size();

    if (numBytes > 0) {
        // The first byte is the status byte indicating the message type
        unsigned char status = msg->at(0);

        CallbackData *data = static_cast<CallbackData *>(userData);
        Theremin *instrument = data->instrument;
        bool *mousePlay = data->mousePlay;
        float* timeSinceLastNote = data->timeSinceLastNote;

        printf("%s: ", MIDIByte::messageTypeString(status));

        // Check if we received a channel message
        if (MIDIByte::isChannelMessage(status)) {
            unsigned char type = status & MIDIByte::MESSAGE_MASK;
            unsigned char chan = status & MIDIByte::CHANNEL_MASK;

            // Here we demonstrate how to parse to common channel messages
            switch (type) {
                case MIDIByte::NOTE_ON:
                    // printf("Note %u, Vel %u \n", msg->at(1), msg->at(2));
                    *mousePlay = false;
                    if (*timeSinceLastNote > 0.6f){
                        *timeSinceLastNote = 0;
                    }

                    instrument->setInternalParameterValue("targetFrequency", ::pow(2.f, ((msg->at(1)) - 69.f) / 12.f) * 432.f);

                    break;

                case MIDIByte::NOTE_OFF:
                    // printf("Note %u, Vel %u \n", msg->at(1), msg->at(2));

                    break;

                case MIDIByte::PITCH_BEND:
                    // printf("Value %u",MIDIByte::convertPitchBend(msg->at(1), msg->at(2)));
                    break;

                // Control messages need to be parsed again...
                case MIDIByte::CONTROL_CHANGE:
                    // printf("%s ", MIDIByte::controlNumberString(msg->at(1)));
                    switch (msg->at(1)) {
                        case MIDIByte::MODULATION:
                            printf("%u", msg->at(2));
                            break;
                    }
                    break;
                default:;
            }

            printf(" (MIDI chan %u)", chan + 1);
        }

        printf("\n");

        printf("\tBytes = ");
        for (unsigned i = 0; i < numBytes; ++i) {
            printf("%3u ", (int)msg->at(i));
        }
        printf(", stamp = %g\n", deltaTime);
    }
}

// We make an app.
class MyApp : public App {
   public:
    SynthGUIManager<Theremin> synthManager{"Theremin"};

    FontRenderer fontRender;
    int fontSize = 16;

    bool mousePlay = true;

    float timeSinceLastNote = 0;

    float timer = 0;

    Theremin *instrument;

    std::vector<NotePair> notes = {
        NotePair("G", 391.995),
        NotePair("G#", 415.305),
        NotePair("A", 440),
        NotePair("A#", 466.164),
        NotePair("B", 493.883),
        NotePair("C", 523.251),
        NotePair("C#", 554.365),
        NotePair("D", 587.330),
        NotePair("D#", 622.254),
        NotePair("E", 659.255),
        NotePair("F", 698.456),
        NotePair("F#", 739.989),
        NotePair("G", 783.991),
        NotePair("G#", 830.609),
        NotePair("A", 880),
        NotePair("A#", 932.328),
        NotePair("B", 987.767),
        NotePair("C", 1046.5),
        NotePair("C#", 1108.73),
        NotePair("D", 1173.66),
        NotePair("D#", 1244.61),
        NotePair("E", 1318.51),
        NotePair("F", 1396.91),
        NotePair("F#", 1497.98),
        NotePair("G", 1567.98),
        NotePair("G#", 1661.22)};

    RtMidiIn RtMidiIn;

    void onCreate() override {
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

        // Check available ports vs. specified
        unsigned portToOpen = 0;
        unsigned numPorts = RtMidiIn.getPortCount();

        if (portToOpen >= numPorts) {
            printf("Invalid port specifier!\n");
        }

        try {
            // Print out names of available input ports
            for (unsigned i = 0; i < numPorts; ++i) {
                printf("Port %u: %s\n", i, RtMidiIn.getPortName(i).c_str());
            }

            // Open the port specified above
            RtMidiIn.openPort(portToOpen);
        } catch (RtMidiError &error) {
            error.printMessage();
            // exit(1);
        }

        // Set our callback function.  This should be done immediately after
        // opening the port to avoid having incoming messages written to the
        // queue instead of sent to the callback function.
        CallbackData *cbd = new CallbackData();
        cbd->instrument = instrument;
        cbd->mousePlay = &mousePlay;
        cbd->timeSinceLastNote = &timeSinceLastNote;
        RtMidiIn.setCallback(&midiCallback, cbd);  //&synthManager

        // Don't ignore sysex, timing, or active sensing messages.
        RtMidiIn.ignoreTypes(false, false, false);
    }

    // The audio callback function. Called when audio hardware requires data
    void onSound(AudioIOData &io) override {
        synthManager.render(io);  // Render audio
    }

    void onAnimate(double dt) override {
        // The GUI is prepared here
        imguiBeginFrame();
        // Draw a window that contains the synth control panel
        synthManager.drawSynthControlPanel();
        imguiEndFrame();

        timer += dt;
        timeSinceLastNote += dt;

        if (!mousePlay) {
            float newFreq = instrument->getInternalParameter("frequency");
            float targetFreq = instrument->getInternalParameter("targetFrequency");

            float newAmp = instrument->getInternalParameter("baseAmplitude");
            float currentAmp = instrument->getInternalParameter("amplitude");

            if (abs(newFreq - targetFreq) > 10) {
            }


            float attack = 0;

            if (timeSinceLastNote <= 0.4f){
                attack = sinf(timeSinceLastNote * 3) * 0.3f;
                newFreq = lerp(newFreq, targetFreq, dt * 4);
            }else if (timeSinceLastNote <= 1.0f){
                newFreq = lerp(newFreq, targetFreq, dt * 4);
                newAmp = lerp(currentAmp, newAmp, dt * 4);
            } else{
                newAmp = lerp(currentAmp, newAmp, dt * 4);
            }

            newAmp += attack;


            
            //newFreq += rand() % 10 - 4.5f;
            newFreq += sinf(timer * 40) * 600 * dt;

            instrument->setInternalParameterValue("frequency", newFreq);
            instrument->setInternalParameterValue("amplitude", newAmp);
        }
    }

    bool onMouseMove(const Mouse &m) override {
        // Get the mouse position
        int x = m.x();
        int y = m.y();
        // Print the mouse position
        instrument->setInternalParameterValue("frequency", x + 400);

        // std::cout << "pos: " << x << ", " << y << std::endl;

        mousePlay = true;
        instrument->setInternalParameterValue("amplitude", clamp((float)(height() - (y + 50)) / (height() * 0.8f), 0, 1));
        instrument->setInternalParameterValue("abseAmpltidue", instrument->getInternalParameter("amplitude"));
        // instrument->triggerOn();

        return true;
    }

    // The graphics callback function.
    void onDraw(Graphics &g) override {
        g.clear();

        // This example uses only the orthogonal projection for 2D drawing
        g.camera(Viewpoint::ORTHO_FOR_2D);  // Ortho [0:width] x [0:height]

        // Render the synth's graphics
        synthManager.render(g);

        drawRect(g, 0, 50, width(), 2);

        for (int i = 0; i < notes.size(); i++) {
            drawRect(g, notes[i].freq - 400, 70, 2, 40);
        }
        drawRect(g, instrument->getInternalParameter("frequency") - 400, instrument->getInternalParameter("amplitude") * height() * 0.8 + 50, 4, 4);

        // For some reason rects won't draw after prints?
        for (int i = 0; i < notes.size(); i++) {
            print(g, notes[i].note, notes[i].freq - 8 - 400, 15);
        }

        // GUI is drawn here
        imguiDraw();
    }

    // Whenever a key is pressed, this function is called
    bool onKeyDown(Keyboard const &k) override {
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

    void onExit() override {
        imguiShutdown();
    }

    void drawRect(Graphics &g, int x, int y, int width, int height) {
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

#include <cstdio>  // for printing to stdout
#include <iostream>
#include <string>

#include "Gamma/Analysis.h"
#include "Gamma/Effects.h"
#include "Gamma/Envelope.h"
#include "Gamma/Oscillator.h"

#include "al/app/al_App.hpp"
#include "al/graphics/al_Shapes.hpp"
#include "al/scene/al_PolySynth.hpp"
#include "al/scene/al_SynthSequencer.hpp"
#include "al/ui/al_ControlGUI.hpp"
#include "al/ui/al_Parameter.hpp"

#include "al/graphics/al_Shapes.hpp"
#include "al/graphics/al_Font.hpp"

#include "al/io/al_MIDI.hpp"


// using namespace gam;
using namespace al;

class SineEnv : public SynthVoice {
 public:
  // Unit generators
  gam::Pan<> mPan;
  gam::Sine<> mOsc;
  gam::Env<3> mAmpEnv;
  // envelope follower to connect audio output to graphics
  gam::EnvFollow<> mEnvFollow;

  Mesh mMesh;

  // Initialize voice. This function will only be called once per voice when
  // it is created. Voices will be reused if they are idle.
  void init() override {
    // Intialize envelope
    mAmpEnv.curve(0);  // make segments lines
    mAmpEnv.levels(0, 1, 1, 0);
    mAmpEnv.sustainPoint(2);  // Make point 2 sustain until a release is issued

    createInternalTriggerParameter("amplitude", 0.3, 0.0, 1.0);
    createInternalTriggerParameter("frequency", 60, 20, 5000);
    createInternalTriggerParameter("attackTime", 0.0, 0.01, 3.0);
    createInternalTriggerParameter("releaseTime", 0.4, 0.1, 10.0);
    createInternalTriggerParameter("pan", 0.0, -1.0, 1.0);
    
    createInternalTriggerParameter("pianoKeyX");
    createInternalTriggerParameter("pianoKeyY");
    createInternalTriggerParameter("pianoKeyWidth");
    createInternalTriggerParameter("pianoKeyHeight");
  }

  // The audio processing function
  void onProcess(AudioIOData& io) override {
    // Get the values from the parameters and apply them to the corresponding
    // unit generators. You could place these lines in the onTrigger() function,
    // but placing them here allows for realtime prototyping on a running
    // voice, rather than having to trigger a new voice to hear the changes.
    // Parameters will update values once per audio callback because they
    // are outside the sample processing loop.
    mOsc.freq(getInternalParameterValue("frequency"));
    mAmpEnv.lengths()[0] = getInternalParameterValue("attackTime");
    mAmpEnv.lengths()[2] = getInternalParameterValue("releaseTime");
    mPan.pos(getInternalParameterValue("pan"));
    while (io()) {
      float s1 = mOsc() * mAmpEnv() * getInternalParameterValue("amplitude");
      float s2;
      mEnvFollow(s1);
      mPan(s1, s1, s2);
      io.out(0) += s1;
      io.out(1) += s2;
    }
    // We need to let the synth know that this voice is done
    // by calling the free(). This takes the voice out of the
    // rendering chain
    if (mAmpEnv.done() && (mEnvFollow.value() < 0.001f)) free();
  }

  // The graphics processing function
  void onProcess(Graphics& g) override {
  }

  // The triggering functions just need to tell the envelope to start or release
  // The audio processing function checks when the envelope is done to remove
  // the voice from the processing chain.
  void onTriggerOn() override { mAmpEnv.reset(); }

  void onTriggerOff() override { mAmpEnv.release(); }
};






void midiCallback(double deltaTime, std::vector<unsigned char> *msg,
                  void *userData) {
  unsigned numBytes = msg->size();

  SynthGUIManager<SineEnv>* synthManager = static_cast<SynthGUIManager<SineEnv>*>(userData);

  std::cout << "MIDI MESSAGE" << std::endl;
  std::string *userDataString = static_cast<std::string*>(userData);
  if (userDataString != nullptr){
    std::cout << "userData " << *userDataString << std::endl;
  }else{
    std::cout << "userData could not be retrived" << std::endl;
  }

  if (numBytes > 0) {
    // The first byte is the status byte indicating the message type
    unsigned char status = msg->at(0);

    printf("%s: ", MIDIByte::messageTypeString(status));

    // Check if we received a channel message
    if (MIDIByte::isChannelMessage(status)) {
      unsigned char type = status & MIDIByte::MESSAGE_MASK;
      unsigned char chan = status & MIDIByte::CHANNEL_MASK;

      // Here we demonstrate how to parse to common channel messages
      switch (type) {
        case MIDIByte::NOTE_ON:
          printf("Note %u, Vel %u", msg->at(1), msg->at(2));

          synthManager->voice()->setInternalParameterValue(
            "frequency", ::pow(2.f, ((msg->at(1)) - 69.f) / 12.f) * 432.f);

          synthManager->triggerOn((int)msg->at(1));
          break;

        case MIDIByte::NOTE_OFF:
          printf("Note %u, Vel %u", msg->at(1), msg->at(2));
          synthManager->triggerOff((int)msg->at(1));
          break;

        case MIDIByte::PITCH_BEND:
          printf("Value %u",
                 MIDIByte::convertPitchBend(msg->at(1), msg->at(2)));
          break;

        // Control messages need to be parsed again...
        case MIDIByte::CONTROL_CHANGE:
          printf("%s ", MIDIByte::controlNumberString(msg->at(1)));
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
  // GUI manager for SineEnv voices
  // The name provided determines the name of the directory
  // where the presets and sequences are stored
  SynthGUIManager<SineEnv> synthManager{"SineEnv_Piano"};
  RtMidiIn RtMidiIn;
  
  // Mesh and variables for drawing piano keys
  Mesh meshKey;
  void onCreate() override {
    // Set sampling rate for Gamma objects from app's audio
    gam::sampleRate(audioIO().framesPerSecond());

    imguiInit();



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
      exit(1);
    }

    // Set our callback function.  This should be done immediately after
    // opening the port to avoid having incoming messages written to the
    // queue instead of sent to the callback function.
    std::string message = "hallo";
    RtMidiIn.setCallback(&midiCallback, &synthManager);

    // Don't ignore sysex, timing, or active sensing messages.
    RtMidiIn.ignoreTypes(false, false, false);

  }

  // The audio callback function. Called when audio hardware requires data
  void onSound(AudioIOData& io) override {
    synthManager.render(io);  // Render audio
  }

  void onAnimate(double dt) override {
    // The GUI is prepared here
    imguiBeginFrame();
    // Draw a window that contains the synth control panel
    imguiEndFrame();
  }

  // The graphics callback function.
  void onDraw(Graphics& g) override {
    g.clear();

    // This example uses only the orthogonal projection for 2D drawing
    g.camera(Viewpoint::ORTHO_FOR_2D);  // Ortho [0:width] x [0:height]

    synthManager.render(g);

  }

  // Whenever a key is pressed, this function is called
  void onExit() override { imguiShutdown(); }
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

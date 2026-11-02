// Main.cpp — the application object and its main window.
//
// In Max terms: this is the "patcher window" that JUCE opens when the app
// starts. The actual contents live in MainComponent.

#include <JuceHeader.h>
#include "MainComponent.h"

class TrajetApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        // Ask to save unsaved changes first.
        if (mainWindow != nullptr)
            if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
            {
                main->tryToQuit ([] { juce::JUCEApplication::quit(); });
                return;
            }

        quit();
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (800, 500, 4000, 3000);
            centreWithSize (1200, 760);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (TrajetApplication)

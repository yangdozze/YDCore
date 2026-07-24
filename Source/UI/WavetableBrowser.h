// GLOBUS v1.2 — compact wavetable browser overlay.
//
// Factory / User sources, category filter, search, metadata panel, import.
// Opening the browser NEVER changes the sound — a bank is applied only when the
// user clicks a row (or presses Enter). Import runs on a worker thread through
// the processor; failures surface here without touching the current table.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Controls.h"
#include "../PluginProcessor.h"
#include "../Engine/WavetableImport.h"

namespace ydc
{
class WavetableBrowser : public juce::Component,
                         private juce::ListBoxModel,
                         private juce::ChangeListener
{
public:
    WavetableBrowser (YDCoreAudioProcessor& p, int oscNum)
        : proc (p), oscIndex (oscNum - 1)
    {
        title = "OSC " + juce::String (oscNum) + "  WAVETABLES";

        categories.add ("All");
        for (const auto& b : WavetableLibrary::get().banks())
            categories.addIfNotAlreadyThere (b->category);
        categories.add ("User");

        catModel.owner = this;          // must precede setModel (it queries rows synchronously)
        catList.setModel (&catModel);
        catList.setRowHeight (24);
        catList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (catList);

        search.setTextToShowWhenEmpty ("search...", theme::textDim);
        search.setFont (LookAndFeelYD::uiFont (12.0f));
        search.onTextChange = [this] { rebuildList(); };
        search.setEscapeAndReturnKeysConsumed (false);
        addAndMakeVisible (search);

        bankList.setModel (this);
        bankList.setRowHeight (26);
        bankList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (bankList);

        importButton.setButtonText ("IMPORT WAV...");
        importButton.onClick = [this] { launchImport(); };
        addAndMakeVisible (importButton);

        closeButton.setButtonText ("CLOSE");
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);

        proc.wtEvents.addChangeListener (this);
        rebuildList();
    }

    ~WavetableBrowser() override
    {
        proc.wtEvents.removeChangeListener (this);
    }

    std::function<void()> onClose;

    void visibilityChanged() override
    {
        if (isVisible())
        {
            rebuildList();
            selectCurrentRow();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (theme::bg.withAlpha (0.97f));
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (theme::accent.withAlpha (0.6f));
        g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.4f);

        g.setColour (theme::text);
        g.setFont (LookAndFeelYD::uiFont (13.0f, true));
        g.drawText (title, 14, 8, 300, 18, juce::Justification::centredLeft);

        // metadata panel (right column)
        auto meta = metaArea;
        g.setColour (theme::panel);
        g.fillRoundedRectangle (meta.toFloat(), 6.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (meta.toFloat(), 6.0f, 1.0f);

        meta.reduce (10, 8);
        const auto* bank = selectedBank();
        g.setColour (theme::text);
        g.setFont (LookAndFeelYD::uiFont (12.5f, true));
        g.drawText (bank != nullptr ? displayName (bank->name) : "-", meta.removeFromTop (18),
                    juce::Justification::centredLeft);
        g.setColour (theme::accent2);
        g.setFont (LookAndFeelYD::uiFont (10.5f));
        g.drawText (bank != nullptr ? bank->category + "  |  " + juce::String (bank->numFrames) + " frames"
                                    : "", meta.removeFromTop (16), juce::Justification::centredLeft);
        meta.removeFromTop (6);
        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (11.0f));
        g.drawFittedText (bank != nullptr ? bank->description : "", meta.removeFromTop (64),
                          juce::Justification::topLeft, 4);

        const auto err = proc.getLastWavetableImportError();
        if (err.isNotEmpty())
        {
            g.setColour (theme::warn);
            g.setFont (LookAndFeelYD::uiFont (10.5f));
            g.drawFittedText ("Import: " + err, meta.removeFromBottom (46), juce::Justification::bottomLeft, 3);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (10).withTrimmedTop (24);
        closeButton.setBounds (getWidth() - 76, 6, 66, 20);

        auto left = b.removeFromLeft (118);
        catList.setBounds (left);
        b.removeFromLeft (8);

        auto right = b.removeFromRight (190);
        importButton.setBounds (right.removeFromBottom (26));
        right.removeFromBottom (8);
        metaArea = right;
        b.removeFromRight (8);

        search.setBounds (b.removeFromTop (24));
        b.removeFromTop (6);
        bankList.setBounds (b);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey)
        {
            if (onClose) onClose();
            return true;
        }
        if (k == juce::KeyPress::returnKey)
        {
            applyRow (bankList.getSelectedRow());
            return true;
        }
        return false;
    }

    /** Step selection relative to the current bank (used by the ◀ ▶ arrows). */
    void stepSelection (int delta)
    {
        rebuildList();
        const auto current = proc.getOscWavetableName (oscIndex);
        int idx = visible.indexOf (current);
        if (idx < 0) idx = 0;
        idx = (idx + delta + visible.size()) % juce::jmax (1, visible.size());
        applyRow (idx);
    }

private:
    // ---- category list model
    struct CatModel : public juce::ListBoxModel
    {
        WavetableBrowser* owner = nullptr;
        int getNumRows() override { return owner->categories.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (selected)
            {
                g.setColour (theme::accent.withAlpha (0.18f));
                g.fillRoundedRectangle (2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 4.0f);
            }
            g.setColour (selected ? theme::text : theme::textDim);
            g.setFont (LookAndFeelYD::uiFont (11.5f, selected));
            g.drawText (owner->categories[row], 10, 0, w - 14, h, juce::Justification::centredLeft);
        }
        void selectedRowsChanged (int) override { owner->rebuildList(); }
    };

    int getNumRows() override { return visible.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= visible.size())
            return;
        const auto& name = visible[row];
        const bool isCurrent = name == proc.getOscWavetableName (oscIndex);
        if (selected || isCurrent)
        {
            g.setColour ((isCurrent ? theme::accent2 : theme::accent).withAlpha (selected ? 0.22f : 0.12f));
            g.fillRoundedRectangle (2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 4.0f);
        }
        g.setColour (isCurrent ? theme::accent2 : theme::text);
        g.setFont (LookAndFeelYD::uiFont (12.0f, isCurrent));
        g.drawText (displayName (name), 10, 0, w - 90, h, juce::Justification::centredLeft);

        const auto* bank = bankFor (name);
        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (10.0f));
        g.drawText (bank != nullptr ? bank->category : "", w - 86, 0, 80, h, juce::Justification::centredRight);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override { applyRow (row); }
    void returnKeyPressed (int row) override { applyRow (row); }
    void selectedRowsChanged (int) override { repaint(); }

    void applyRow (int row)
    {
        if (row >= 0 && row < visible.size())
        {
            proc.setOscWavetableByName (oscIndex, visible[row]);
            bankList.selectRow (row);
            repaint();
        }
    }

    void rebuildList()
    {
        visible.clear();
        const auto cat = categories[juce::jmax (0, catList.getSelectedRow())];
        const auto needle = search.getText().trim().toLowerCase();
        auto matches = [&] (const juce::String& name, const juce::String& bankCat)
        {
            if (cat == "User" && ! name.startsWith (userWavetablePrefix())) return false;
            if (cat != "All" && cat != "User" && bankCat != cat) return false;
            return needle.isEmpty() || displayName (name).toLowerCase().contains (needle);
        };
        for (const auto& b : WavetableLibrary::get().banks())
            if (matches (b->name, b->category))
                visible.add (b->name);
        for (const auto& n : proc.getUserWavetableNames())
            if (matches (n, "User"))
                visible.add (n);
        bankList.updateContent();
        repaint();
    }

    void selectCurrentRow()
    {
        const int idx = visible.indexOf (proc.getOscWavetableName (oscIndex));
        if (idx >= 0)
            bankList.selectRow (idx);
    }

    const WavetableBank* bankFor (const juce::String& name) const
    {
        if (const auto* b = WavetableLibrary::get().byName (name))
            return b;
        return nullptr;   // user banks: metadata comes from the registry only when loaded
    }

    const WavetableBank* selectedBank() const
    {
        const int row = bankList.getSelectedRow();
        if (row >= 0 && row < visible.size())
            return bankFor (visible[row]);
        return bankFor (proc.getOscWavetableName (oscIndex));
    }

    static juce::String displayName (const juce::String& name)
    {
        return name.startsWith (userWavetablePrefix())
             ? name.fromFirstOccurrenceOf (userWavetablePrefix(), false, false)
             : name;
    }

    void launchImport()
    {
        chooser = std::make_unique<juce::FileChooser> ("Import a wavetable WAV",
                                                       userWavetableDirectory(), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file.existsAsFile())
                    proc.importWavetableAsync (oscIndex, file);
            });
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        rebuildList();
        selectCurrentRow();
    }

    YDCoreAudioProcessor& proc;
    int oscIndex;
    juce::String title;
    juce::StringArray categories;
    juce::StringArray visible;
    CatModel catModel;
    juce::ListBox catList { "cats" }, bankList { "banks" };
    juce::TextEditor search;
    juce::TextButton importButton, closeButton;
    juce::Rectangle<int> metaArea;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace ydc

// GLOBUS — preset bank page (PRESETS tab). Three columns: sources & categories,
// searchable preset list, and an info/actions panel. Message-thread only.
#pragma once
#include "Controls.h"
#include "../PluginProcessor.h"

namespace ydc
{
class PresetBankPage : public juce::Component
{
public:
    explicit PresetBankPage (YDCoreAudioProcessor& p) : processor (p)
    {
        sourceModel.owner = this;
        listModel.owner = this;

        sourceList.setModel (&sourceModel);
        sourceList.setRowHeight (30);
        sourceList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (sourceList);

        presetList.setModel (&listModel);
        presetList.setRowHeight (30);
        presetList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (presetList);

        search.setTextToShowWhenEmpty ("Search presets...", theme::textDim);
        search.setFont (LookAndFeelYD::uiFont (14.0f));
        search.onTextChange = [this] { rebuildList(); };
        search.setEscapeAndReturnKeysConsumed (false);
        addAndMakeVisible (search);

        prevButton.setButtonText ("<");
        prevButton.setTooltip ("Previous preset (whole bank order)");
        prevButton.onClick = [this] { processor.getPresetManager().loadNext (false); syncSelectionToCurrent(); };
        addAndMakeVisible (prevButton);

        nextButton.setButtonText (">");
        nextButton.setTooltip ("Next preset (whole bank order)");
        nextButton.onClick = [this] { processor.getPresetManager().loadNext (true); syncSelectionToCurrent(); };
        addAndMakeVisible (nextButton);

        countLabel.setJustificationType (juce::Justification::centredRight);
        countLabel.setFont (LookAndFeelYD::uiFont (11.5f));
        countLabel.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (countLabel);

        // ---- right column widgets
        favButton.setButtonText ("Favorite");
        favButton.setTooltip ("Toggle favorite for the selected preset");
        favButton.onClick = [this] { toggleFavoriteSelected(); };
        addAndMakeVisible (favButton);

        loadButton.setButtonText ("LOAD");
        loadButton.setTooltip ("Load the selected preset");
        loadButton.onClick = [this] { loadSelected(); };
        addAndMakeVisible (loadButton);

        deleteButton.setButtonText ("DELETE");
        deleteButton.setTooltip ("Delete the selected user preset (factory presets cannot be deleted)");
        deleteButton.onClick = [this] { deleteSelected(); };
        addAndMakeVisible (deleteButton);

        initButton.setButtonText ("INIT");
        initButton.setTooltip ("Reset to the init patch");
        initButton.onClick = [this] { processor.getPresetManager().initPatch(); refreshInfo(); };
        addAndMakeVisible (initButton);

        randButton.setButtonText ("RANDOM");
        randButton.setTooltip ("Generate a random patch");
        randButton.onClick = [this] { processor.getPresetManager().randomizePatch(); refreshInfo(); };
        addAndMakeVisible (randButton);

        nameEditor.setTextToShowWhenEmpty ("New preset name...", theme::textDim);
        nameEditor.setFont (LookAndFeelYD::uiFont (13.0f));
        addAndMakeVisible (nameEditor);

        for (const auto& c : PresetManager::categoryOrder())
            categoryBox.addItem (c, categoryBox.getNumItems() + 1);
        categoryBox.addItem ("User", categoryBox.getNumItems() + 1);
        categoryBox.setSelectedItemIndex (0, juce::dontSendNotification);
        categoryBox.setTooltip ("Category for Save As");
        addAndMakeVisible (categoryBox);

        saveAsButton.setButtonText ("SAVE AS");
        saveAsButton.setTooltip ("Save the current sound under the name above");
        saveAsButton.onClick = [this] { saveAs(); };
        addAndMakeVisible (saveAsButton);

        setWantsKeyboardFocus (true);
    }

    std::function<void()> onClose;   // Escape → back to the OSC page

    /** Called when the tab becomes visible. Never touches the current sound. */
    void refresh (bool focusSaveField = false)
    {
        processor.getPresetManager().rescan();
        rebuildSources();
        rebuildList();
        syncSelectionToCurrent();
        if (focusSaveField)
        {
            nameEditor.setText (processor.getPresetManager().getCurrentName(), juce::dontSendNotification);
            const auto cat = processor.getPresetManager().getCurrentCategory();
            for (int i = 0; i < categoryBox.getNumItems(); ++i)
                if (categoryBox.getItemText (i) == cat)
                    categoryBox.setSelectedItemIndex (i, juce::dontSendNotification);
            nameEditor.grabKeyboardFocus();
        }
        else
        {
            grabKeyboardFocus();
        }
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (onClose) onClose();
            return true;
        }
        if (key == juce::KeyPress::returnKey && presetList.getSelectedRow() >= 0)
        {
            loadSelected();
            return true;
        }
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
        {
            const int n = (int) filtered.size();
            if (n > 0)
            {
                int row = presetList.getSelectedRow();
                row = juce::jlimit (0, n - 1, row + (key == juce::KeyPress::downKey ? 1 : -1));
                presetList.selectRow (row);
            }
            return true;
        }
        return false;
    }

    void paint (juce::Graphics& g) override
    {
        // column backgrounds
        auto paintPanel = [&g] (juce::Rectangle<int> r)
        {
            g.setColour (theme::panel);
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
            g.setColour (theme::outline);
            g.drawRoundedRectangle (r.toFloat(), 6.0f, 1.0f);
        };
        paintPanel (leftCol);
        paintPanel (centreCol);
        paintPanel (rightCol);

        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (10.5f, true));
        g.drawText ("BANK", leftCol.getX() + 12, leftCol.getY() + 8, 100, 14, juce::Justification::centredLeft);

        // ---- right column: info card
        auto info = rightCol.reduced (16).withTrimmedBottom (rightCol.getHeight() - 240);
        const auto* sel = selectedInfo();
        g.setColour (theme::text);
        g.setFont (LookAndFeelYD::uiFont (20.0f, true));
        g.drawText (sel != nullptr ? sel->name : processor.getPresetManager().getCurrentName(),
                    info.removeFromTop (30), juce::Justification::centredLeft);

        g.setFont (LookAndFeelYD::uiFont (12.0f));
        g.setColour (theme::accent);
        g.drawText (sel != nullptr ? (sel->category + (sel->isFactory ? "   |   FACTORY" : "   |   USER"))
                                   : processor.getPresetManager().getCurrentCategory(),
                    info.removeFromTop (20), juce::Justification::centredLeft);

        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (12.0f));
        const auto author = sel != nullptr && sel->author.isNotEmpty() ? sel->author : juce::String ("-");
        g.drawText ("Author:  " + author, info.removeFromTop (22), juce::Justification::centredLeft);

        info.removeFromTop (6);
        g.setColour (theme::text.withAlpha (0.85f));
        g.setFont (LookAndFeelYD::uiFont (12.5f));
        const auto desc = sel != nullptr && sel->description.isNotEmpty()
                        ? sel->description
                        : juce::String ("Select a preset to see its description.");
        g.drawFittedText (desc, info.removeFromTop (60), juce::Justification::topLeft, 3);

        // favorite state text
        if (sel != nullptr)
        {
            const bool fav = processor.getPresetManager().isFavorite (sel->name);
            g.setColour (fav ? theme::warn : theme::textDim);
            g.setFont (LookAndFeelYD::uiFont (12.0f));
            g.drawText (fav ? juce::String::fromUTF8 ("\xe2\x98\x85") + "  In favorites"
                            : juce::String::fromUTF8 ("\xe2\x98\x86") + "  Not a favorite",
                        info.removeFromTop (20), juce::Justification::centredLeft);
        }

        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (10.5f, true));
        g.drawText ("SAVE AS", saveLabelBounds, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (8, 4);
        leftCol = b.removeFromLeft (196);
        b.removeFromLeft (8);
        rightCol = b.removeFromRight (392);
        b.removeFromRight (8);
        centreCol = b;

        sourceList.setBounds (leftCol.reduced (8).withTrimmedTop (18));

        auto c = centreCol.reduced (10);
        auto top = c.removeFromTop (30);
        prevButton.setBounds (top.removeFromLeft (30));
        top.removeFromLeft (6);
        nextButton.setBounds (top.removeFromLeft (30));
        top.removeFromLeft (8);
        countLabel.setBounds (top.removeFromRight (110));
        search.setBounds (top);
        c.removeFromTop (8);
        presetList.setBounds (c);

        auto r = rightCol.reduced (16);
        r.removeFromTop (244);
        favButton.setBounds (r.removeFromTop (30).removeFromLeft (150));
        r.removeFromTop (12);

        auto row = r.removeFromTop (34);
        loadButton.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (8);
        deleteButton.setBounds (row.removeFromLeft (110));
        r.removeFromTop (10);
        row = r.removeFromTop (34);
        initButton.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (8);
        randButton.setBounds (row.removeFromLeft (110));

        r.removeFromTop (22);
        saveLabelBounds = r.removeFromTop (16);
        r.removeFromTop (4);
        nameEditor.setBounds (r.removeFromTop (28));
        r.removeFromTop (8);
        row = r.removeFromTop (30);
        categoryBox.setBounds (row.removeFromLeft (170));
        row.removeFromLeft (8);
        saveAsButton.setBounds (row.removeFromLeft (100));
    }

    void visibilityChanged() override
    {
        if (isVisible())
            refresh (false);
    }

private:
    //==========================================================================
    struct SourceRow { juce::String label; bool isCategory = false; };

    void rebuildSources()
    {
        sources.clear();
        sources.push_back ({ "All Presets", false });
        sources.push_back ({ "Factory", false });
        sources.push_back ({ "User", false });
        sources.push_back ({ "Favorites", false });

        juce::StringArray present;
        for (const auto& info : processor.getPresetManager().getPresets())
            present.addIfNotAlreadyThere (info.category);
        for (const auto& c : PresetManager::categoryOrder())
            if (present.contains (c))
                sources.push_back ({ c, true });
        for (const auto& c : present)
        {
            bool known = c == "Init";
            for (const auto& s : sources)
                if (s.label == c) { known = true; break; }
            if (! known)
                sources.push_back ({ c, true });
        }
        sourceList.updateContent();
        if (sourceList.getSelectedRow() < 0)
            sourceList.selectRow (0);
    }

    void rebuildList()
    {
        filtered.clear();
        auto& pm = processor.getPresetManager();
        const int srcRow = juce::jmax (0, sourceList.getSelectedRow());
        const auto src = srcRow < (int) sources.size() ? sources[(size_t) srcRow] : SourceRow { "All Presets", false };
        const auto needle = search.getText().trim().toLowerCase();

        const auto& all = pm.getPresets();
        for (int i = 0; i < (int) all.size(); ++i)
        {
            const auto& info = all[(size_t) i];
            if (src.label == "Factory"   && ! info.isFactory) continue;
            if (src.label == "User"      &&   info.isFactory) continue;
            if (src.label == "Favorites" && ! pm.isFavorite (info.name)) continue;
            if (src.isCategory && info.category != src.label) continue;
            if (needle.isNotEmpty() && ! info.name.toLowerCase().contains (needle)
                && ! info.category.toLowerCase().contains (needle)) continue;
            filtered.push_back (i);
        }
        presetList.updateContent();
        presetList.repaint();
        countLabel.setText (juce::String ((int) filtered.size()) + " presets", juce::dontSendNotification);
        repaint();
    }

    const PresetManager::PresetInfo* selectedInfo() const
    {
        const int row = presetList.getSelectedRow();
        if (row >= 0 && row < (int) filtered.size())
            return &processor.getPresetManager().getPresets()[(size_t) filtered[(size_t) row]];
        return nullptr;
    }

    void syncSelectionToCurrent()
    {
        const int cur = processor.getPresetManager().getCurrentIndex();
        for (size_t i = 0; i < filtered.size(); ++i)
            if (filtered[i] == cur)
            {
                presetList.selectRow ((int) i);
                presetList.scrollToEnsureRowIsOnscreen ((int) i);
                break;
            }
        refreshInfo();
    }

    void refreshInfo() { repaint(); }

    void loadSelected()
    {
        const int row = presetList.getSelectedRow();
        if (row >= 0 && row < (int) filtered.size())
        {
            processor.getPresetManager().loadPresetAt (filtered[(size_t) row]);
            refreshInfo();
        }
    }

    void deleteSelected()
    {
        const int row = presetList.getSelectedRow();
        if (row >= 0 && row < (int) filtered.size())
        {
            processor.getPresetManager().deleteUserPreset (filtered[(size_t) row]);
            refresh (false);
        }
    }

    void toggleFavoriteSelected()
    {
        if (const auto* sel = selectedInfo())
        {
            processor.getPresetManager().toggleFavorite (sel->name);
            presetList.repaint();
            repaint();
        }
    }

    void saveAs()
    {
        const auto name = nameEditor.getText().trim();
        if (name.isEmpty())
        {
            nameEditor.grabKeyboardFocus();
            return;
        }
        processor.getPresetManager().saveUserPreset (name, categoryBox.getText());
        refresh (false);
    }

    //==========================================================================
    struct SourceModel : juce::ListBoxModel
    {
        PresetBankPage* owner = nullptr;
        int getNumRows() override { return (int) owner->sources.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (row < 0 || row >= (int) owner->sources.size()) return;
            const auto& s = owner->sources[(size_t) row];
            if (selected)
            {
                g.setColour (theme::accent.withAlpha (0.16f));
                g.fillRoundedRectangle (2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 4.0f);
                g.setColour (theme::accent);
                g.fillRoundedRectangle (2.0f, (float) h * 0.5f - 6.0f, 3.0f, 12.0f, 1.5f);
            }
            g.setColour (selected ? theme::text : theme::textDim);
            g.setFont (LookAndFeelYD::uiFont (13.0f, selected));
            g.drawText (s.label, s.isCategory ? 22 : 10, 0, w - 26, h, juce::Justification::centredLeft);
        }
        void selectedRowsChanged (int) override { owner->rebuildList(); }
    };

    struct ListModel : juce::ListBoxModel
    {
        PresetBankPage* owner = nullptr;
        int getNumRows() override { return (int) owner->filtered.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (row < 0 || row >= (int) owner->filtered.size()) return;
            auto& pm = owner->processor.getPresetManager();
            const auto& info = pm.getPresets()[(size_t) owner->filtered[(size_t) row]];

            const bool hovered = owner->presetList.getRowContainingPosition (
                owner->presetList.getMouseXYRelative().x,
                owner->presetList.getMouseXYRelative().y) == row;

            if (selected)
            {
                g.setGradientFill (juce::ColourGradient (theme::accent.withAlpha (0.22f), 0, 0,
                                                         theme::accent2.withAlpha (0.14f), (float) w, 0, false));
                g.fillRoundedRectangle (2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 4.0f);
            }
            else if (hovered)
            {
                g.setColour (theme::panelLight.withAlpha (0.7f));
                g.fillRoundedRectangle (2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 4.0f);
            }

            const bool fav = pm.isFavorite (info.name);
            g.setColour (fav ? theme::warn : theme::track);
            g.setFont (LookAndFeelYD::uiFont (15.0f));
            g.drawText (fav ? juce::String::fromUTF8 ("\xe2\x98\x85") : juce::String::fromUTF8 ("\xe2\x98\x86"),
                        8, 0, 20, h, juce::Justification::centred);

            g.setColour (selected ? theme::text : theme::text.withAlpha (0.88f));
            g.setFont (LookAndFeelYD::uiFont (13.5f));
            g.drawText (info.name, 34, 0, w - 190, h, juce::Justification::centredLeft);

            g.setColour (theme::textDim);
            g.setFont (LookAndFeelYD::uiFont (11.0f));
            g.drawText (info.category + (info.isFactory ? "" : "  (user)"), w - 150, 0, 142, h,
                        juce::Justification::centredRight);
        }
        void listBoxItemClicked (int row, const juce::MouseEvent& e) override
        {
            if (e.x < 30 && row >= 0 && row < (int) owner->filtered.size())
            {
                auto& pm = owner->processor.getPresetManager();
                pm.toggleFavorite (pm.getPresets()[(size_t) owner->filtered[(size_t) row]].name);
                owner->presetList.repaint();
            }
            owner->repaint();   // refresh the info card
        }
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
        {
            juce::ignoreUnused (row);
            owner->loadSelected();
        }
        void selectedRowsChanged (int) override { owner->repaint(); }
    };

    YDCoreAudioProcessor& processor;
    juce::ListBox sourceList { "sources" }, presetList { "presets" };
    SourceModel sourceModel;
    ListModel listModel;
    juce::TextEditor search, nameEditor;
    juce::ComboBox categoryBox;
    juce::TextButton prevButton, nextButton, favButton, loadButton, deleteButton, initButton, randButton, saveAsButton;
    juce::Label countLabel;
    std::vector<SourceRow> sources;
    std::vector<int> filtered;
    juce::Rectangle<int> leftCol, centreCol, rightCol, saveLabelBounds;
};

} // namespace ydc

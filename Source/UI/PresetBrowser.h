// YD Core — preset browser overlay: categories, search, favorites,
// load / save-as / delete. Message-thread only.
#pragma once
#include "Controls.h"
#include "../PluginProcessor.h"

namespace ydc
{
class PresetBrowser : public juce::Component
{
public:
    explicit PresetBrowser (YDCoreAudioProcessor& p) : processor (p)
    {
        catModel.owner = this;
        listModel.owner = this;

        categoryList.setModel (&catModel);
        categoryList.setRowHeight (26);
        categoryList.setColour (juce::ListBox::backgroundColourId, theme::panel);
        addAndMakeVisible (categoryList);

        presetList.setModel (&listModel);
        presetList.setRowHeight (24);
        presetList.setColour (juce::ListBox::backgroundColourId, theme::panel);
        addAndMakeVisible (presetList);

        search.setTextToShowWhenEmpty ("Search presets...", theme::textDim);
        search.setFont (LookAndFeelYD::uiFont (13.0f));
        search.onTextChange = [this] { rebuildList(); };
        addAndMakeVisible (search);

        loadButton.setButtonText ("LOAD");
        loadButton.onClick = [this] { loadSelected(); };
        addAndMakeVisible (loadButton);

        deleteButton.setButtonText ("DELETE");
        deleteButton.setTooltip ("Delete the selected user preset (factory presets cannot be deleted)");
        deleteButton.onClick = [this] { deleteSelected(); };
        addAndMakeVisible (deleteButton);

        closeButton.setButtonText ("CLOSE");
        closeButton.onClick = [this] { setVisible (false); };
        addAndMakeVisible (closeButton);

        // save-as row
        nameEditor.setTextToShowWhenEmpty ("Preset name...", theme::textDim);
        nameEditor.setFont (LookAndFeelYD::uiFont (13.0f));
        addAndMakeVisible (nameEditor);
        for (const auto& c : PresetManager::categoryOrder())
            categoryBox.addItem (c, categoryBox.getNumItems() + 1);
        categoryBox.addItem ("User", categoryBox.getNumItems() + 1);
        categoryBox.setSelectedItemIndex (0, juce::dontSendNotification);
        addAndMakeVisible (categoryBox);
        saveAsButton.setButtonText ("SAVE AS");
        saveAsButton.onClick = [this] { saveAs(); };
        addAndMakeVisible (saveAsButton);

        setWantsKeyboardFocus (true);
    }

    /** Show the browser (optionally jumping into save mode). */
    void open (bool focusSave)
    {
        refresh();
        setVisible (true);
        toFront (true);
        if (focusSave)
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
            search.grabKeyboardFocus();
        }
    }

    void refresh()
    {
        processor.getPresetManager().rescan();
        rebuildCategories();
        rebuildList();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            setVisible (false);
            return true;
        }
        if (key == juce::KeyPress::returnKey && presetList.getSelectedRow() >= 0)
        {
            loadSelected();
            return true;
        }
        return false;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! panelBounds().contains (e.getPosition()))
            setVisible (false);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.65f));
        auto p = panelBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (p, 8.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (p, 8.0f, 1.2f);

        g.setColour (theme::text);
        g.setFont (LookAndFeelYD::uiFont (17.0f, true));
        g.drawText ("PRESET BROWSER", panelBounds().removeFromTop (40).reduced (18, 0),
                    juce::Justification::centredLeft);

        g.setColour (theme::textDim);
        g.setFont (LookAndFeelYD::uiFont (11.0f));
        g.drawText (juce::String (filtered.size()) + " presets",
                    panelBounds().removeFromTop (40).reduced (18, 0), juce::Justification::centredRight);
    }

    void resized() override
    {
        auto p = panelBounds().reduced (14);
        p.removeFromTop (30);

        auto bottom = p.removeFromBottom (30);
        auto saveRow = p.removeFromBottom (30).withTrimmedTop (4);
        p.removeFromBottom (6);

        auto left = p.removeFromLeft (150);
        categoryList.setBounds (left);
        p.removeFromLeft (10);

        search.setBounds (p.removeFromTop (26));
        p.removeFromTop (6);
        presetList.setBounds (p);

        // save row spans the right side
        nameEditor.setBounds (saveRow.removeFromLeft (260));
        saveRow.removeFromLeft (6);
        categoryBox.setBounds (saveRow.removeFromLeft (140));
        saveRow.removeFromLeft (6);
        saveAsButton.setBounds (saveRow.removeFromLeft (86));

        loadButton.setBounds (bottom.removeFromLeft (90));
        bottom.removeFromLeft (6);
        deleteButton.setBounds (bottom.removeFromLeft (90));
        closeButton.setBounds (bottom.removeFromRight (90));
    }

private:
    juce::Rectangle<int> panelBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (760, getWidth() - 60),
                                                       juce::jmin (520, getHeight() - 60));
    }

    //==========================================================================
    void rebuildCategories()
    {
        categories.clear();
        categories.add ("All");
        categories.add ("Favorites");
        juce::StringArray present;
        for (const auto& info : processor.getPresetManager().getPresets())
            present.addIfNotAlreadyThere (info.category);
        for (const auto& c : PresetManager::categoryOrder())
            if (present.contains (c))
                categories.add (c);
        for (const auto& c : present)
            if (! categories.contains (c))
                categories.add (c);
        categoryList.updateContent();
        if (categoryList.getSelectedRow() < 0)
            categoryList.selectRow (0);
    }

    void rebuildList()
    {
        filtered.clear();
        auto& pm = processor.getPresetManager();
        const int catRow = juce::jmax (0, categoryList.getSelectedRow());
        const auto cat = categories[catRow];
        const auto needle = search.getText().trim().toLowerCase();

        const auto& all = pm.getPresets();
        for (int i = 0; i < (int) all.size(); ++i)
        {
            const auto& info = all[(size_t) i];
            if (cat == "Favorites" && ! pm.isFavorite (info.name)) continue;
            if (cat != "All" && cat != "Favorites" && info.category != cat) continue;
            if (needle.isNotEmpty() && ! info.name.toLowerCase().contains (needle)
                && ! info.category.toLowerCase().contains (needle)) continue;
            filtered.push_back (i);
        }
        presetList.updateContent();
        presetList.repaint();
        repaint();
    }

    void loadSelected()
    {
        const int row = presetList.getSelectedRow();
        if (row >= 0 && row < (int) filtered.size())
            processor.getPresetManager().loadPresetAt (filtered[(size_t) row]);
    }

    void deleteSelected()
    {
        const int row = presetList.getSelectedRow();
        if (row >= 0 && row < (int) filtered.size())
        {
            processor.getPresetManager().deleteUserPreset (filtered[(size_t) row]);
            refresh();
        }
    }

    void saveAs()
    {
        const auto name = nameEditor.getText().trim();
        if (name.isEmpty())
            return;
        processor.getPresetManager().saveUserPreset (name, categoryBox.getText());
        refresh();
    }

    //==========================================================================
    struct CatModel : juce::ListBoxModel
    {
        PresetBrowser* owner = nullptr;
        int getNumRows() override { return owner->categories.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (selected)
            {
                g.setColour (theme::accent.withAlpha (0.18f));
                g.fillRoundedRectangle (2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 4.0f);
            }
            g.setColour (selected ? theme::text : theme::textDim);
            g.setFont (LookAndFeelYD::uiFont (13.0f, selected));
            g.drawText (owner->categories[row], 10, 0, w - 14, h, juce::Justification::centredLeft);
        }
        void selectedRowsChanged (int) override { owner->rebuildList(); }
    };

    struct ListModel : juce::ListBoxModel
    {
        PresetBrowser* owner = nullptr;
        int getNumRows() override { return (int) owner->filtered.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (row < 0 || row >= (int) owner->filtered.size()) return;
            auto& pm = owner->processor.getPresetManager();
            const auto& info = pm.getPresets()[(size_t) owner->filtered[(size_t) row]];

            if (selected)
            {
                g.setColour (theme::accent.withAlpha (0.18f));
                g.fillRoundedRectangle (2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 4.0f);
            }
            // favorite star
            const bool fav = pm.isFavorite (info.name);
            g.setColour (fav ? theme::warn : theme::track);
            g.setFont (LookAndFeelYD::uiFont (14.0f));
            g.drawText (fav ? juce::String::fromUTF8 ("\xe2\x98\x85") : juce::String::fromUTF8 ("\xe2\x98\x86"),
                        6, 0, 20, h, juce::Justification::centred);

            g.setColour (selected ? theme::text : theme::text.withAlpha (0.85f));
            g.setFont (LookAndFeelYD::uiFont (13.0f));
            g.drawText (info.name, 30, 0, w - 160, h, juce::Justification::centredLeft);

            g.setColour (theme::textDim);
            g.setFont (LookAndFeelYD::uiFont (11.0f));
            g.drawText (info.category + (info.isFactory ? "" : "  (user)"), w - 128, 0, 122, h,
                        juce::Justification::centredRight);
        }
        void listBoxItemClicked (int row, const juce::MouseEvent& e) override
        {
            if (e.x < 28 && row >= 0 && row < (int) owner->filtered.size())
            {
                auto& pm = owner->processor.getPresetManager();
                pm.toggleFavorite (pm.getPresets()[(size_t) owner->filtered[(size_t) row]].name);
                owner->presetList.repaint();
            }
        }
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
        {
            juce::ignoreUnused (row);
            owner->loadSelected();
        }
    };

    YDCoreAudioProcessor& processor;
    juce::ListBox categoryList { "categories" }, presetList { "presets" };
    CatModel catModel;
    ListModel listModel;
    juce::TextEditor search, nameEditor;
    juce::ComboBox categoryBox;
    juce::TextButton loadButton, deleteButton, closeButton, saveAsButton;
    juce::StringArray categories;
    std::vector<int> filtered;
};

} // namespace ydc

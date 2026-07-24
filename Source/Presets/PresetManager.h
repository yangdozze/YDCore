// GLOBUS — preset system (Ninth Parallel Audio). Factory presets are embedded
// JSON (BinaryData) and also shipped as portable .json files; user presets
// live in Documents/GLOBUS/Presets (the legacy YDCore folder is still read).
// All calls are message-thread only.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace ydc
{
class PresetManager
{
public:
    PresetManager (juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& apvts);

    struct PresetInfo
    {
        juce::String name;
        juce::String category;
        juce::String author;
        juce::String description;
        bool isFactory = false;
        int binaryIndex = -1;     // factory: index into BinaryData
        juce::File file;          // user preset file
    };

    static const juce::StringArray& categoryOrder();

    void rescan();
    const std::vector<PresetInfo>& getPresets() const noexcept { return presets; }
    int getNumPresets() const noexcept { return (int) presets.size(); }

    // current preset (name/category live inside the APVTS state → saved with the project)
    juce::String getCurrentName() const;
    juce::String getCurrentCategory() const;
    int getCurrentIndex() const;    // -1 when the current name is not in the list

    bool loadPresetAt (int index);
    void loadNext (bool forward);
    bool saveUserPreset (const juce::String& name, const juce::String& category); // save/save-as
    bool deleteUserPreset (int index);
    void initPatch();

    //==========================================================================
    // Randomizer (message-thread only)
    //==========================================================================
    enum class Strength { Subtle, Normal, Wild };
    enum Section { LockOsc = 0, LockFilter, LockEnv, LockLfoMatrix, LockFx, NumLocks };

    void randomizePatch (Strength strength = Strength::Normal);
    bool canUndoRandomize() const noexcept { return hasUndo; }
    void undoRandomize();

    /** Section locks: locked sections are preserved exactly by the randomizer.
        UI-session state only — not host-automatable, not saved into presets. */
    bool isSectionLocked (Section s) const noexcept { return locks[(size_t) s].load(); }
    void setSectionLocked (Section s, bool locked) noexcept { locks[(size_t) s].store (locked); }

    /** True when any parameter differs from the last loaded/saved snapshot. */
    bool isModified() const;

    bool isFavorite (const juce::String& name) const;
    void toggleFavorite (const juce::String& name);

    /** Marks state dirty-name (called after manual tweaks if desired). */
    void setCurrentInfo (const juce::String& name, const juce::String& category);

    static juce::File userPresetDirectory();
    static juce::File legacyUserPresetDirectory();

private:
    bool applyPresetJson (const juce::var& root);
    void setAllParametersToDefaults();
    void setParamReal (const juce::String& paramID, float realValue);
    int  sectionOfParam (const juce::String& paramID) const;   // -1 = never randomized
    void captureUndoSnapshot();
    void captureCleanSnapshot();                                // for the modified indicator
    void applyRandomRecipe (Strength strength, juce::Random& rnd,
                            const std::map<juce::String, float>& preservedNorm);
    juce::var buildPresetJson (const juce::String& name, const juce::String& category) const;
    void loadFavorites();
    void saveFavorites() const;
    static juce::File favoritesFile();

    juce::AudioProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;
    std::vector<PresetInfo> presets;
    juce::StringArray favorites;

    std::array<std::atomic<bool>, NumLocks> locks { false, false, false, false, false };
    std::vector<std::pair<juce::String, float>> undoSnapshot;   // paramID -> normalized
    juce::String undoName, undoCategory;
    bool hasUndo = false;
    std::vector<float> cleanSnapshot;                            // normalized, in getParameters() order

    JUCE_DECLARE_NON_COPYABLE (PresetManager)
};

} // namespace ydc

#include "PresetManager.h"
#include "../Parameters.h"
#include "BinaryData.h"

namespace ydc
{
static constexpr const char* kNameProp     = "presetName";
static constexpr const char* kCategoryProp = "presetCategory";

PresetManager::PresetManager (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s)
    : proc (p), apvts (s)
{
    rescan();
    loadFavorites();
    if (! apvts.state.hasProperty (kNameProp))
        setCurrentInfo ("Init", "Init");
}

const juce::StringArray& PresetManager::categoryOrder()
{
    static const juce::StringArray order { "Bass", "Sub Bass", "Lead", "Pluck", "Keys",
                                           "Pad", "Atmosphere", "Arpeggio", "FX", "Experimental" };
    return order;
}

juce::File PresetManager::userPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
              .getChildFile ("GLOBUS").getChildFile ("Presets");
}

juce::File PresetManager::legacyUserPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
              .getChildFile ("YDCore").getChildFile ("Presets");
}

juce::File PresetManager::favoritesFile()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
              .getChildFile ("GLOBUS").getChildFile ("favorites.json");
}

//==============================================================================
void PresetManager::rescan()
{
    presets.clear();

    // ---- factory presets from BinaryData
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        int dataSize = 0;
        const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], dataSize);
        if (data == nullptr || dataSize <= 0)
            continue;
        const auto parsed = juce::JSON::parse (juce::String::fromUTF8 (data, dataSize));
        if (auto* obj = parsed.getDynamicObject())
        {
            PresetInfo info;
            info.name        = obj->getProperty ("name").toString();
            info.category    = obj->getProperty ("category").toString();
            info.author      = obj->getProperty ("author").toString();
            info.description = obj->getProperty ("description").toString();
            info.isFactory   = true;
            info.binaryIndex = i;
            if (info.name.isNotEmpty())
                presets.push_back (std::move (info));
        }
    }

    // ---- user presets from disk (current + legacy folder)
    auto scanUserDir = [this] (const juce::File& dir)
    {
        if (! dir.isDirectory())
            return;
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.json"))
        {
            const auto parsed = juce::JSON::parse (f.loadFileAsString());
            if (auto* obj = parsed.getDynamicObject())
            {
                PresetInfo info;
                info.name        = obj->getProperty ("name").toString();
                info.category    = obj->getProperty ("category").toString();
                info.author      = obj->getProperty ("author").toString();
                info.description = obj->getProperty ("description").toString();
                info.isFactory   = false;
                info.file        = f;
                if (info.name.isEmpty())
                    info.name = f.getFileNameWithoutExtension();
                if (info.category.isEmpty())
                    info.category = "User";
                // skip duplicates by name (current dir wins over legacy)
                bool duplicate = false;
                for (const auto& existing : presets)
                    if (! existing.isFactory && existing.name == info.name)
                    {
                        duplicate = true;
                        break;
                    }
                if (! duplicate)
                    presets.push_back (std::move (info));
            }
        }
    };
    scanUserDir (userPresetDirectory());
    if (legacyUserPresetDirectory() != userPresetDirectory())
        scanUserDir (legacyUserPresetDirectory());

    // ---- stable display order: category (fixed order), then name
    const auto& order = categoryOrder();
    std::stable_sort (presets.begin(), presets.end(),
        [&order] (const PresetInfo& a, const PresetInfo& b)
        {
            int ca = order.indexOf (a.category); if (ca < 0) ca = order.size();
            int cb = order.indexOf (b.category); if (cb < 0) cb = order.size();
            if (ca != cb) return ca < cb;
            return a.name.compareIgnoreCase (b.name) < 0;
        });
}

//==============================================================================
juce::String PresetManager::getCurrentName() const
{
    return apvts.state.getProperty (kNameProp, "Init").toString();
}

juce::String PresetManager::getCurrentCategory() const
{
    return apvts.state.getProperty (kCategoryProp, "Init").toString();
}

void PresetManager::setCurrentInfo (const juce::String& name, const juce::String& category)
{
    apvts.state.setProperty (kNameProp, name, nullptr);
    apvts.state.setProperty (kCategoryProp, category, nullptr);
}

int PresetManager::getCurrentIndex() const
{
    const auto name = getCurrentName();
    for (int i = 0; i < (int) presets.size(); ++i)
        if (presets[(size_t) i].name == name)
            return i;
    return -1;
}

//==============================================================================
bool PresetManager::loadPresetAt (int index)
{
    if (index < 0 || index >= (int) presets.size())
        return false;

    const auto& info = presets[(size_t) index];
    juce::var root;

    if (info.isFactory)
    {
        int dataSize = 0;
        const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[info.binaryIndex], dataSize);
        if (data == nullptr)
            return false;
        root = juce::JSON::parse (juce::String::fromUTF8 (data, dataSize));
    }
    else
    {
        root = juce::JSON::parse (info.file.loadFileAsString());
    }

    if (! applyPresetJson (root))
        return false;

    setCurrentInfo (info.name, info.category);
    return true;
}

void PresetManager::loadNext (bool forward)
{
    const int n = getNumPresets();
    if (n == 0)
        return;
    int idx = getCurrentIndex();
    idx = idx < 0 ? (forward ? 0 : n - 1)
                  : (idx + (forward ? 1 : -1) + n) % n;
    loadPresetAt (idx);
}

bool PresetManager::applyPresetJson (const juce::var& root)
{
    auto* obj = root.getDynamicObject();
    if (obj == nullptr)
        return false;
    const auto params = obj->getProperty ("params");
    auto* paramsObj = params.getDynamicObject();
    if (paramsObj == nullptr)
        return false;

    setAllParametersToDefaults();
    for (const auto& prop : paramsObj->getProperties())
        setParamReal (prop.name.toString(), (float) (double) prop.value);
    return true;
}

void PresetManager::setAllParametersToDefaults()
{
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
}

void PresetManager::setParamReal (const juce::String& paramID, float realValue)
{
    if (auto* rp = apvts.getParameter (paramID))
    {
        const auto& range = rp->getNormalisableRange();
        const float clamped = juce::jlimit (range.start, range.end, realValue);
        rp->setValueNotifyingHost (range.convertTo0to1 (clamped));
    }
   #if YDCORE_HEADLESS_TESTS
    else
    {
        // tests treat unknown parameter IDs in presets as fatal
        DBG ("Unknown parameter in preset: " << paramID);
        jassertfalse;
    }
   #endif
}

//==============================================================================
juce::var PresetManager::buildPresetJson (const juce::String& name, const juce::String& category) const
{
    auto* paramsObj = new juce::DynamicObject();
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto& range = rp->getNormalisableRange();
            paramsObj->setProperty (rp->paramID, (double) range.convertFrom0to1 (rp->getValue()));
        }

    auto* root = new juce::DynamicObject();
    root->setProperty ("name", name);
    root->setProperty ("category", category);
    root->setProperty ("author", "User");
    root->setProperty ("format", "YDCore-1");
    root->setProperty ("params", juce::var (paramsObj));
    return juce::var (root);
}

bool PresetManager::saveUserPreset (const juce::String& name, const juce::String& category)
{
    if (name.isEmpty())
        return false;

    auto dir = userPresetDirectory();
    if (! dir.isDirectory() && ! dir.createDirectory())
        return false;

    auto safe = juce::File::createLegalFileName (name);
    auto file = dir.getChildFile (safe + ".json");
    const auto json = juce::JSON::toString (buildPresetJson (name, category), false);
    if (! file.replaceWithText (json))
        return false;

    rescan();
    setCurrentInfo (name, category);
    return true;
}

bool PresetManager::deleteUserPreset (int index)
{
    if (index < 0 || index >= (int) presets.size())
        return false;
    auto& info = presets[(size_t) index];
    if (info.isFactory || ! info.file.existsAsFile())
        return false;
    const bool ok = info.file.deleteFile();
    rescan();
    return ok;
}

void PresetManager::initPatch()
{
    setAllParametersToDefaults();
    setCurrentInfo ("Init", "Init");
}

//==============================================================================
void PresetManager::randomizePatch()
{
    auto& rnd = juce::Random::getSystemRandom();
    auto setR = [this] (const juce::String& id, float v) { setParamReal (id, v); };

    setAllParametersToDefaults();

    // oscillators
    const int waves[] = { 0, 1, 2, 3, 4, 5 }; // skip noise wave for musical results
    setR (ids::osc (1, "Wave"), (float) waves[rnd.nextInt (6)]);
    setR (ids::osc (2, "Wave"), (float) waves[rnd.nextInt (6)]);
    setR (ids::osc (2, "On"),   rnd.nextFloat() < 0.7f ? 1.0f : 0.0f);
    setR (ids::osc (2, "Oct"),  (float) (rnd.nextInt (3) - 1));
    setR (ids::osc (2, "Semi"), rnd.nextFloat() < 0.25f ? 7.0f : 0.0f);
    setR (ids::osc (1, "Fine"), rnd.nextFloat() * 12.0f - 6.0f);
    setR (ids::osc (2, "Fine"), rnd.nextFloat() * 20.0f - 10.0f);
    for (int i = 1; i <= 2; ++i)
    {
        setR (ids::osc (i, "Level"),     0.55f + rnd.nextFloat() * 0.3f);
        const int uni[] = { 1, 1, 3, 5, 7 };
        setR (ids::osc (i, "UniCount"),  (float) uni[rnd.nextInt (5)]);
        setR (ids::osc (i, "UniDetune"), 8.0f + rnd.nextFloat() * 30.0f);
        setR (ids::osc (i, "UniSpread"), 0.5f + rnd.nextFloat() * 0.5f);
        setR (ids::osc (i, "Drift"),     rnd.nextFloat() * 0.4f);
        setR (ids::osc (i, "PW"),        0.2f + rnd.nextFloat() * 0.6f);
    }
    if (rnd.nextFloat() < 0.4f) { setR (ids::subOn, 1.0f); setR (ids::subLevel, 0.3f + rnd.nextFloat() * 0.4f); }
    if (rnd.nextFloat() < 0.3f) { setR (ids::noiseLevel, rnd.nextFloat() * 0.25f); setR (ids::noiseType, (float) rnd.nextInt (2)); }

    // filter
    setR (ids::filterType, rnd.nextFloat() < 0.75f ? (float) rnd.nextInt (2) : (float) rnd.nextInt (6));
    setR (ids::cutoff,     200.0f * std::pow (2.0f, rnd.nextFloat() * 6.0f));   // 200 Hz .. 12.8 kHz
    setR (ids::resonance,  rnd.nextFloat() * 0.55f);
    setR (ids::filterDrive, rnd.nextFloat() * 0.5f);
    setR (ids::keyTrack,   rnd.nextFloat() < 0.5f ? 0.0f : 0.5f);
    setR (ids::filterEnvAmt, rnd.nextFloat() * 0.8f);

    // envelope archetype: 0 pluck, 1 pad, 2 keys
    const int arch = rnd.nextInt (3);
    if (arch == 0)
    {
        setR (ids::ampA, 0.001f + rnd.nextFloat() * 0.004f);
        setR (ids::ampD, 0.1f + rnd.nextFloat() * 0.5f);
        setR (ids::ampS, 0.0f);
        setR (ids::ampR, 0.1f + rnd.nextFloat() * 0.4f);
        setR (ids::filA, 0.001f);
        setR (ids::filD, 0.08f + rnd.nextFloat() * 0.4f);
        setR (ids::filS, 0.0f);
        setR (ids::filR, 0.2f);
    }
    else if (arch == 1)
    {
        setR (ids::ampA, 0.3f + rnd.nextFloat() * 1.7f);
        setR (ids::ampD, 0.5f);
        setR (ids::ampS, 0.7f + rnd.nextFloat() * 0.3f);
        setR (ids::ampR, 0.5f + rnd.nextFloat() * 2.0f);
        setR (ids::filA, 0.5f + rnd.nextFloat() * 2.0f);
        setR (ids::filD, 1.0f);
        setR (ids::filS, 0.5f);
        setR (ids::filR, 1.0f);
    }
    else
    {
        setR (ids::ampA, 0.002f + rnd.nextFloat() * 0.01f);
        setR (ids::ampD, 0.3f + rnd.nextFloat() * 0.7f);
        setR (ids::ampS, 0.4f + rnd.nextFloat() * 0.3f);
        setR (ids::ampR, 0.2f + rnd.nextFloat() * 0.6f);
        setR (ids::filA, 0.005f);
        setR (ids::filD, 0.4f + rnd.nextFloat() * 0.6f);
        setR (ids::filS, 0.3f);
        setR (ids::filR, 0.4f);
    }

    // LFO 1 occasionally doing something tasteful
    if (rnd.nextFloat() < 0.5f)
    {
        setR (ids::lfo (1, "Wave"), (float) rnd.nextInt (5));
        setR (ids::lfo (1, "Rate"), 0.1f + rnd.nextFloat() * 7.0f);
        setR (ids::lfo (1, "Dest"), (float) (rnd.nextFloat() < 0.6f ? 4 : 1)); // cutoff or pitch
        setR (ids::lfo (1, "Amount"), (rnd.nextFloat() * 0.3f) * (rnd.nextBool() ? 1.0f : -1.0f));
        setR (ids::lfo (1, "Fade"), rnd.nextFloat() < 0.4f ? rnd.nextFloat() * 1.5f : 0.0f);
    }

    // FX
    if (rnd.nextFloat() < 0.5f) { setR (ids::chOn, 1.0f); setR (ids::chMix, 0.25f + rnd.nextFloat() * 0.4f); }
    if (rnd.nextFloat() < 0.7f) { setR (ids::rvOn, 1.0f); setR (ids::rvMix, 0.12f + rnd.nextFloat() * 0.28f);
                                  setR (ids::rvSize, 0.3f + rnd.nextFloat() * 0.6f); }
    if (rnd.nextFloat() < 0.4f) { setR (ids::dlyOn, 1.0f); setR (ids::dlyMix, 0.12f + rnd.nextFloat() * 0.25f);
                                  setR (ids::dlyFb, 0.2f + rnd.nextFloat() * 0.35f); }
    if (rnd.nextFloat() < 0.2f) { setR (ids::distOn, 1.0f); setR (ids::distDrive, rnd.nextFloat() * 0.5f); }

    setCurrentInfo ("Random " + juce::String (rnd.nextInt (900) + 100), "Experimental");
}

//==============================================================================
void PresetManager::loadFavorites()
{
    favorites.clear();
    auto f = favoritesFile();
    if (! f.existsAsFile())
    {
        // migrate from the legacy YDCore location if present
        const auto legacy = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                               .getChildFile ("YDCore").getChildFile ("favorites.json");
        if (! legacy.existsAsFile())
            return;
        f = legacy;
    }
    const auto parsed = juce::JSON::parse (f.loadFileAsString());
    if (auto* arr = parsed.getArray())
        for (const auto& v : *arr)
            favorites.add (v.toString());
}

void PresetManager::saveFavorites() const
{
    auto f = favoritesFile();
    f.getParentDirectory().createDirectory();
    juce::Array<juce::var> arr;
    for (const auto& s : favorites)
        arr.add (s);
    f.replaceWithText (juce::JSON::toString (juce::var (arr), true));
}

bool PresetManager::isFavorite (const juce::String& name) const
{
    return favorites.contains (name);
}

void PresetManager::toggleFavorite (const juce::String& name)
{
    if (favorites.contains (name))
        favorites.removeString (name);
    else
        favorites.add (name);
    saveFavorites();
}

} // namespace ydc

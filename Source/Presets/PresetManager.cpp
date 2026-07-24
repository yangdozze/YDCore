#include "PresetManager.h"
#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "BinaryData.h"

namespace ydc
{
static constexpr const char* kNameProp     = "presetName";
static constexpr const char* kCategoryProp = "presetCategory";

PresetManager::PresetManager (YDCoreAudioProcessor& p, juce::AudioProcessorValueTreeState& s)
    : proc (p), apvts (s)
{
    rescan();
    loadFavorites();
    if (! apvts.state.hasProperty (kNameProp))
        setCurrentInfo ("Init", "Init");
    captureCleanSnapshot();
}

const juce::StringArray& PresetManager::categoryOrder()
{
    // v1.2 appended Sequence/Digital/Analog (display order only — not a save contract)
    static const juce::StringArray order { "Bass", "Sub Bass", "Lead", "Pluck", "Keys",
                                           "Pad", "Atmosphere", "Arpeggio", "FX", "Experimental",
                                           "Sequence", "Digital", "Analog" };
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
    hasUndo = false;
    captureCleanSnapshot();
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

    // v1.2: optional appended "wavetables" object — presets without it (all
    // legacy presets) resolve to the default bank. Format stays "YDCore-1":
    // unknown keys are ignored by loaders that predate them.
    const auto wt = obj->getProperty ("wavetables");
    for (int i = 0; i < 2; ++i)
    {
        juce::String name;
        if (auto* wtObj = wt.getDynamicObject())
            name = wtObj->getProperty (i == 0 ? "osc1" : "osc2").toString();
        proc.setOscWavetableByName (i, name);   // empty → default bank
    }
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

    // v1.2: persist wavetable identity (appended key; ignored by older loaders)
    auto* wtObj = new juce::DynamicObject();
    wtObj->setProperty ("osc1", proc.getOscWavetableName (0));
    wtObj->setProperty ("osc2", proc.getOscWavetableName (1));
    root->setProperty ("wavetables", juce::var (wtObj));
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
    captureCleanSnapshot();
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
    for (int i = 0; i < 2; ++i)
        proc.setOscWavetableByName (i, {});    // default bank
    setCurrentInfo ("Init", "Init");
    hasUndo = false;
    captureCleanSnapshot();
}

//==============================================================================
// Randomizer: three strengths, section locks, one-step undo.
//==============================================================================
int PresetManager::sectionOfParam (const juce::String& id) const
{
    // never randomized (identity, play configuration, output safety)
    static const juce::StringArray never { ids::masterLevel, ids::playMode, ids::polyLimit,
                                           ids::pitchBendRange, ids::velAmount, ids::notePriority,
                                           ids::glideTime, ids::arpOn, ids::arpMode, ids::arpDiv,
                                           ids::arpGate, ids::arpOct, ids::arpHold };
    if (never.contains (id))
        return -1;
    if (id.startsWith ("osc") || id.startsWith ("sub") || id.startsWith ("noise"))
        return LockOsc;
    if (id.startsWith ("filter") || id == ids::cutoff || id == ids::resonance || id == ids::keyTrack)
        return LockFilter;
    if (id.startsWith ("amp") || id.startsWith ("filt") || id.startsWith ("mod"))
        return LockEnv;      // three envelopes incl. mod env amount/destination
    if (id.startsWith ("lfo") || id.startsWith ("mat"))
        return LockLfoMatrix;
    if (id.startsWith ("dist") || id.startsWith ("chorus") || id.startsWith ("delay")
        || id.startsWith ("reverb") || id.startsWith ("eq") || id == ids::stereoWidth)
        return LockFx;
    return -1;
}

void PresetManager::captureUndoSnapshot()
{
    undoSnapshot.clear();
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            undoSnapshot.push_back ({ rp->paramID, rp->getValue() });
    undoName = getCurrentName();
    undoCategory = getCurrentCategory();
    undoWt[0] = proc.getOscWavetableName (0);
    undoWt[1] = proc.getOscWavetableName (1);
    hasUndo = true;
}

void PresetManager::undoRandomize()
{
    if (! hasUndo)
        return;
    for (const auto& [id, norm] : undoSnapshot)
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (norm);
    proc.setOscWavetableByName (0, undoWt[0]);
    proc.setOscWavetableByName (1, undoWt[1]);
    setCurrentInfo (undoName, undoCategory);
    hasUndo = false;
    captureCleanSnapshot();
}

void PresetManager::captureCleanSnapshot()
{
    cleanSnapshot.clear();
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            cleanSnapshot.push_back (rp->getValue());
    cleanWt[0] = proc.getOscWavetableName (0);
    cleanWt[1] = proc.getOscWavetableName (1);
}

bool PresetManager::isModified() const
{
    if (cleanSnapshot.empty())
        return false;
    if (cleanWt[0] != proc.getOscWavetableName (0) || cleanWt[1] != proc.getOscWavetableName (1))
        return true;
    size_t i = 0;
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (i >= cleanSnapshot.size())
                return true;
            if (std::abs (rp->getValue() - cleanSnapshot[i]) > 1.0e-4f)
                return true;
            ++i;
        }
    return false;
}

void PresetManager::randomizePatch (Strength strength)
{
    captureUndoSnapshot();
    juce::Random rnd ((juce::int64) juce::Time::getMillisecondCounter()
                  ^ (juce::int64) juce::Random::getSystemRandom().nextInt());

    // capture everything that must be preserved: protected params + locked sections
    std::map<juce::String, float> preserved;
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const int sec = sectionOfParam (rp->paramID);
            if (sec < 0 || locks[(size_t) sec].load())
                preserved[rp->paramID] = rp->getValue();
        }

    // wavetable selection is state, not a parameter — the OSC lock covers it too
    const bool oscLocked = locks[LockOsc].load();
    const juce::String keepWt[2] { proc.getOscWavetableName (0), proc.getOscWavetableName (1) };

    applyRandomRecipe (strength, rnd, preserved);

    // restore preserved values exactly
    for (const auto& [id, norm] : preserved)
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (norm);
    if (oscLocked)
    {
        proc.setOscWavetableByName (0, keepWt[0]);
        proc.setOscWavetableByName (1, keepWt[1]);
    }

    const char* tag = strength == Strength::Subtle ? "Subtle" : strength == Strength::Wild ? "Wild" : "Random";
    setCurrentInfo (juce::String (tag) + " " + juce::String (rnd.nextInt (900) + 100), "Experimental");
    // deliberately NOT capturing a clean snapshot: a randomized sound reads as
    // modified until the user saves it (Save As) — factory presets stay safe.
}

void PresetManager::applyRandomRecipe (Strength strength, juce::Random& rnd,
                                       const std::map<juce::String, float>& preserved)
{
    auto setR = [this] (const juce::String& id, float v) { setParamReal (id, v); };
    auto skip = [&preserved] (const juce::String& id) { return preserved.count (id) > 0; };

    if (strength == Strength::Subtle)
    {
        // perturb continuous parameters around the current sound (no wave/mode
        // switches, no resets) — small musical variations
        for (auto* p : proc.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (rp == nullptr || skip (rp->paramID))
                continue;
            const auto& range = rp->getNormalisableRange();
            const bool discrete = range.interval >= 1.0f || rp->paramID.endsWith ("On")
                               || rp->paramID.contains ("Wave") || rp->paramID.contains ("Type")
                               || rp->paramID.contains ("Dest") || rp->paramID.contains ("Src")
                               || rp->paramID.contains ("Dst")  || rp->paramID.contains ("Div")
                               || rp->paramID.contains ("Sync") || rp->paramID.contains ("Retrig")
                               || rp->paramID.contains ("Bipolar") || rp->paramID.contains ("RandPhase")
                               || rp->paramID.contains ("Mode");
            if (discrete)
                continue;
            const float jitter = (rnd.nextFloat() * 2.0f - 1.0f) * 0.06f;   // +/- 6 % of range
            rp->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, rp->getValue() + jitter));
        }
        // subtle safety: keep envelope attack/decay away from degenerate zero
        if (! skip (ids::ampA) && apvts.getRawParameterValue (ids::ampA)->load() < 0.001f) setR (ids::ampA, 0.002f);
        if (! skip (ids::ampD) && apvts.getRawParameterValue (ids::ampD)->load() < 0.05f)  setR (ids::ampD, 0.08f);
        return;
    }

    const bool wild = strength == Strength::Wild;

    // NORMAL / WILD: rebuild the sound from defaults, then dial in a recipe
    setAllParametersToDefaults();

    // ---- v1.2 engines: NORMAL favours the new engines with curated ranges;
    // WILD roams every table and warp mode. Banks come only from the loaded
    // factory registry, so a randomized patch can never dangle.
    {
        const auto& lib = WavetableLibrary::get();
        auto pickBank = [&] () -> juce::String
        {
            if (lib.numBanks() == 0)
                return {};
            if (wild)
                return lib.banks()[(size_t) rnd.nextInt (lib.numBanks())]->name;
            // NORMAL: musical categories only
            static const juce::StringArray tame { "Analog", "Harmonic", "Soft", "Motion", "Formant", "Digital" };
            for (int tries = 0; tries < 12; ++tries)
            {
                const auto& b = lib.banks()[(size_t) rnd.nextInt (lib.numBanks())];
                if (tame.contains (b->category))
                    return b->name;
            }
            return lib.defaultBank()->name;
        };
        for (int o = 1; o <= 2; ++o)
        {
            const float roll = rnd.nextFloat();
            const int engine = roll < 0.45f ? 1 : (roll < 0.80f ? 2 : 0);   // HQ / WT / Legacy
            setR (ids::osc (o, "Engine"), (float) engine);
            if (engine == 2)
            {
                proc.setOscWavetableByName (o - 1, pickBank());
                setR (ids::osc (o, "WtPos"), rnd.nextFloat());
                if (wild)
                {
                    setR (ids::osc (o, "WarpMode"), (float) rnd.nextInt (6));
                    setR (ids::osc (o, "WarpAmt"), rnd.nextFloat());
                }
                else if (rnd.nextFloat() < 0.5f)
                {
                    const int tameWarp[] = { 0, 1, 2, 4 };   // Off / Bend± / Asymmetry
                    setR (ids::osc (o, "WarpMode"), (float) tameWarp[rnd.nextInt (4)]);
                    setR (ids::osc (o, "WarpAmt"), rnd.nextFloat() * 0.5f);
                }
            }
        }
        if (wild)
            for (const char* id : { ids::ampCurveA, ids::ampCurveD, ids::ampCurveR })
                if (rnd.nextFloat() < 0.4f)
                    setR (id, rnd.nextFloat() * 0.8f - 0.4f);
    }

    // ---- oscillators: guarantee at least one audible source
    const int nWaves = wild ? 7 : 6;                       // WILD may pick the noise wave
    setR (ids::osc (1, "Wave"), (float) rnd.nextInt (nWaves));
    setR (ids::osc (1, "On"), 1.0f);
    setR (ids::osc (1, "Level"), 0.6f + rnd.nextFloat() * 0.25f);
    setR (ids::osc (2, "Wave"), (float) rnd.nextInt (nWaves));
    setR (ids::osc (2, "On"),   rnd.nextFloat() < (wild ? 0.85f : 0.7f) ? 1.0f : 0.0f);
    setR (ids::osc (2, "Level"), 0.4f + rnd.nextFloat() * 0.35f);
    // musical intervals: favour unison, octaves and fifths
    const int intervals[] = { 0, 0, 0, 12, -12, 7, wild ? 5 : 0, wild ? -5 : 12 };
    setR (ids::osc (2, "Semi"), (float) intervals[rnd.nextInt (8)]);
    setR (ids::osc (2, "Oct"),  (float) (rnd.nextInt (3) - 1));
    setR (ids::osc (1, "Fine"), rnd.nextFloat() * 10.0f - 5.0f);
    setR (ids::osc (2, "Fine"), rnd.nextFloat() * (wild ? 30.0f : 16.0f) - (wild ? 15.0f : 8.0f));
    for (int i = 1; i <= 2; ++i)
    {
        const int uni[] = { 1, 1, 3, 5, 7 };
        setR (ids::osc (i, "UniCount"),  (float) uni[rnd.nextInt (5)]);
        setR (ids::osc (i, "UniDetune"), 6.0f + rnd.nextFloat() * (wild ? 45.0f : 25.0f));
        setR (ids::osc (i, "UniSpread"), 0.4f + rnd.nextFloat() * 0.6f);
        setR (ids::osc (i, "Drift"),     rnd.nextFloat() * (wild ? 0.7f : 0.35f));
        setR (ids::osc (i, "PW"),        0.2f + rnd.nextFloat() * 0.6f);
    }
    if (rnd.nextFloat() < 0.4f) { setR (ids::subOn, 1.0f); setR (ids::subLevel, 0.25f + rnd.nextFloat() * 0.4f); }
    if (rnd.nextFloat() < (wild ? 0.5f : 0.25f))
    {
        setR (ids::noiseLevel, rnd.nextFloat() * (wild ? 0.5f : 0.2f));
        setR (ids::noiseType, (float) rnd.nextInt (2));
        setR (ids::noiseTone, rnd.nextFloat() * 1.2f - 0.6f);
    }

    // ---- filter: stable resonance/drive pairing (v1.2 may pick appended models)
    if (wild)
        setR (ids::filterType, (float) rnd.nextInt (10));                    // any model
    else if (rnd.nextFloat() < 0.25f)
        setR (ids::filterType, (float) (6 + rnd.nextInt (3)));               // Ladder/OTA/SEM
    else
        setR (ids::filterType, rnd.nextFloat() < 0.7f ? (float) rnd.nextInt (2) : (float) rnd.nextInt (6));
    setR (ids::cutoff, 220.0f * std::pow (2.0f, rnd.nextFloat() * (wild ? 6.5f : 5.5f)));
    const float res = rnd.nextFloat() * (wild ? 0.85f : 0.6f);
    setR (ids::resonance, res);
    setR (ids::filterDrive, res > 0.7f ? rnd.nextFloat() * 0.3f : rnd.nextFloat() * (wild ? 0.8f : 0.5f));
    setR (ids::keyTrack, rnd.nextFloat() < 0.5f ? 0.0f : 0.5f);
    setR (ids::filterEnvAmt, rnd.nextFloat() * (wild ? 1.6f : 0.9f) - (wild ? 0.4f : 0.1f));

    // ---- envelope archetypes (pluck / pad / keys), degenerate times excluded
    const int arch = rnd.nextInt (3);
    auto env = [&] (float a, float d, float s, float r, float fa, float fd, float fs, float fr)
    {
        setR (ids::ampA, a); setR (ids::ampD, d); setR (ids::ampS, s); setR (ids::ampR, r);
        setR (ids::filA, fa); setR (ids::filD, fd); setR (ids::filS, fs); setR (ids::filR, fr);
    };
    if (arch == 0)      env (0.001f + rnd.nextFloat() * 0.004f, 0.12f + rnd.nextFloat() * 0.5f, 0.0f,
                             0.1f + rnd.nextFloat() * 0.4f, 0.001f, 0.08f + rnd.nextFloat() * 0.4f, 0.0f, 0.2f);
    else if (arch == 1) env (0.25f + rnd.nextFloat() * (wild ? 2.5f : 1.4f), 0.6f, 0.75f + rnd.nextFloat() * 0.25f,
                             0.4f + rnd.nextFloat() * 1.8f, 0.5f + rnd.nextFloat() * 2.0f, 1.0f, 0.5f, 1.0f);
    else                env (0.002f + rnd.nextFloat() * 0.01f, 0.3f + rnd.nextFloat() * 0.7f,
                             0.4f + rnd.nextFloat() * 0.35f, 0.2f + rnd.nextFloat() * 0.5f,
                             0.005f, 0.4f + rnd.nextFloat() * 0.6f, 0.3f, 0.4f);

    // ---- LFO 1 quick-assign: tasteful movement
    if (rnd.nextFloat() < (wild ? 0.75f : 0.5f))
    {
        setR (ids::lfo (1, "Wave"), (float) rnd.nextInt (5));
        setR (ids::lfo (1, "Rate"), 0.1f + rnd.nextFloat() * (wild ? 12.0f : 6.0f));
        const int dests[] = { 4, 4, 1, 7 };               // mostly cutoff, some pitch/PW
        setR (ids::lfo (1, "Dest"), (float) dests[rnd.nextInt (4)]);
        const float maxAmt = wild ? 0.5f : 0.18f;          // pitch via quick-assign stays vibrato-sized in NORMAL
        setR (ids::lfo (1, "Amount"), (rnd.nextFloat() * maxAmt) * (rnd.nextBool() ? 1.0f : -1.0f));
        setR (ids::lfo (1, "Fade"), rnd.nextFloat() < 0.4f ? rnd.nextFloat() * 1.5f : 0.0f);
    }

    // ---- matrix: useful, non-duplicate routes
    static const int usefulSrc[] = { 1, 2, 5, 8, 9, 10 };  // vel, wheel, ampenv, lfo1, lfo2, random
    static const int usefulDst[] = { 12, 12, 6, 14, 8, 10 }; // cutoff (favoured), osc1 level, amp, pan, pw
    const int routes = wild ? rnd.nextInt (4) : rnd.nextInt (3);
    juce::Array<int> usedDst;
    for (int s = 0; s < routes; ++s)
    {
        const int dst = usefulDst[rnd.nextInt (6)];
        if (usedDst.contains (dst))
            continue;                                       // no duplicate routes
        usedDst.add (dst);
        setR (ids::slot (s + 1, "Src"), (float) usefulSrc[rnd.nextInt (6)]);
        setR (ids::slot (s + 1, "Dst"), (float) dst);
        setR (ids::slot (s + 1, "Amt"), (rnd.nextFloat() * (wild ? 0.5f : 0.3f)) * (rnd.nextBool() ? 1.0f : -1.0f));
    }

    // ---- FX: bounded, usable
    if (rnd.nextFloat() < 0.5f) { setR (ids::chOn, 1.0f); setR (ids::chMix, 0.2f + rnd.nextFloat() * 0.35f); }
    if (rnd.nextFloat() < 0.65f)
    {
        setR (ids::rvOn, 1.0f);
        setR (ids::rvMix, 0.1f + rnd.nextFloat() * (wild ? 0.5f : 0.3f));
        setR (ids::rvSize, 0.3f + rnd.nextFloat() * 0.6f);
    }
    if (rnd.nextFloat() < 0.4f)
    {
        setR (ids::dlyOn, 1.0f);
        setR (ids::dlyMix, 0.1f + rnd.nextFloat() * 0.25f);
        setR (ids::dlyFb, 0.15f + rnd.nextFloat() * (wild ? 0.45f : 0.3f));
    }
    if (rnd.nextFloat() < (wild ? 0.45f : 0.2f))
    {
        setR (ids::distOn, 1.0f);
        setR (ids::distDrive, rnd.nextFloat() * (wild ? 0.8f : 0.5f));
        setR (ids::distMix, 0.3f + rnd.nextFloat() * 0.5f);
    }
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

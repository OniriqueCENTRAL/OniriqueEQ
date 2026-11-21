#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

class OniriqueEQAudioProcessor;

namespace onirique
{
/*  TOUS LES ONIRIQUE EQ DE LA SESSION (la fenêtre « INSTANCES », façon liste d'instances) : chaque ONIRIQUE EQ ouvert dans le
    DAW s'inscrit ici à sa création et s'en retire à sa destruction. La liste est partagée par toutes les instances chargées
    dans le même processus (le cas des DAW, sauf pont ou bac à sable) ; elle vit tant qu'il reste une instance
    (SharedResourcePointer : rien de statique qui survive au plugin).

    L'interface lit et règle les autres EQ sous le verrou : une instance ne peut pas disparaître pendant qu'on la lit. */
class EqInstances
{
public:
    struct Entry
    {
        int id = 0;
        OniriqueEQAudioProcessor* processor = nullptr;
    };

    int add (OniriqueEQAudioProcessor* p)
    {
        const juce::ScopedLock sl (lock);
        entries.push_back ({ ++lastId, p });
        ++version;
        return lastId;
    }

    void remove (int id)
    {
        const juce::ScopedLock sl (lock);
        entries.erase (std::remove_if (entries.begin(), entries.end(), [id] (const Entry& e) { return e.id == id; }), entries.end());
        ++version;
    }

    /** fn (Entry&) pour chaque instance, dans l'ordre de création, verrou tenu (fn doit être court). */
    template <typename Fn>
    void forEach (Fn&& fn)
    {
        const juce::ScopedLock sl (lock);
        for (auto& e : entries) fn (e);
    }

    /** fn (OniriqueEQAudioProcessor&) sur l'instance id si elle existe encore ; rend false sinon. */
    template <typename Fn>
    bool with (int id, Fn&& fn)
    {
        const juce::ScopedLock sl (lock);
        for (auto& e : entries)
            if (e.id == id) { fn (*e.processor); return true; }
        return false;
    }

    int getVersion() const noexcept { return version.load(); }   // change à chaque instance ajoutée ou retirée

private:
    juce::CriticalSection lock;
    std::vector<Entry> entries;
    int lastId = 0;
    std::atomic<int> version { 0 };
};
} // namespace onirique

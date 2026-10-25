#pragma once

#include "Parameters.h"
#include "ui/OniriquePresets.h"

namespace onirique
{
/*  Presets d'usine d'ONIRIQUE EQ, rangés par usage pour le navigateur de la barre (les tags). Valeurs en unités réelles ;
    une bande non citée reste coupée.
    type : 0 Bell, 1 Low Shelf, 2 High Shelf, 3 Low Cut, 4 High Cut, 5 Notch ; pente : 0..3 = 12..48 dB/oct ;
    dyn : la bande n'agit qu'au-dessus de son seuil (thr, en dB). */
namespace eqpresets
{
enum Tags { vocal = 1, drums = 2, bass = 4, instruments = 8, mix = 16, mastering = 32, effects = 64, cleanup = 128 };

inline juce::StringArray tagNames() { return { "Vocal", "Drums", "Bass", "Instruments", "Mix", "Mastering", "Effects", "Clean-up" }; }

/** Les réglages de session (latence, écoute) : un preset les laisse. */
inline juce::StringArray notStored() { return { ids::bypass, ids::phaseMode, ids::peqListen, ids::peqListenBand }; }

struct B { int type; float freq, gain, q; int slope = 1; bool dyn = false; float thr = -30.0f; };

inline PresetBank::Stock preset (const char* name, int tags, std::initializer_list<B> bands)
{
    PresetBank::Stock s { name, tags, {} };
    int i = 0;
    for (const auto& b : bands)
    {
        if (i >= 8) break;
        s.values.push_back ({ ids::peqOn[i], 1.0f });
        s.values.push_back ({ ids::peqType[i], (float) b.type });
        s.values.push_back ({ ids::peqFreq[i], b.freq });
        s.values.push_back ({ ids::peqGain[i], b.gain });
        s.values.push_back ({ ids::peqQ[i], b.q });
        s.values.push_back ({ ids::peqSlope[i], (float) b.slope });
        s.values.push_back ({ ids::peqDyn[i], b.dyn ? 1.0f : 0.0f });
        s.values.push_back ({ ids::peqThr[i], b.thr });
        ++i;
    }
    return s;
}

inline std::vector<PresetBank::Stock> stockPresets()
{
    return {
        { "Init", 0, {} },
        preset ("Vocal Clarity", vocal, { { 3, 80.0f, 0.0f, 0.71f, 1 }, { 0, 250.0f, -2.5f, 1.2f }, { 0, 3200.0f, 2.0f, 0.8f },
                                          { 2, 11000.0f, 3.0f, 0.71f } }),
        preset ("Vocal Air", vocal, { { 3, 90.0f, 0.0f, 0.71f, 1 }, { 0, 300.0f, -1.5f, 1.0f }, { 2, 12000.0f, 3.5f, 0.6f } }),
        preset ("Vocal Warmth", vocal, { { 3, 70.0f, 0.0f, 0.71f, 1 }, { 1, 200.0f, 2.0f, 0.71f }, { 0, 3000.0f, -1.0f, 1.0f },
                                         { 4, 16000.0f, 0.0f, 0.71f, 0 } }),
        preset ("Rap Vocal Presence", vocal, { { 3, 100.0f, 0.0f, 0.71f, 2 }, { 0, 180.0f, 1.5f, 1.0f }, { 0, 400.0f, -3.0f, 1.4f },
                                               { 0, 2500.0f, 2.5f, 1.0f }, { 2, 9000.0f, 2.5f, 0.71f } }),
        preset ("Vocal De-Harsh", vocal | cleanup, { { 3, 90.0f, 0.0f, 0.71f, 1 }, { 0, 3500.0f, -5.0f, 2.0f, 1, true, -32.0f },
                                                     { 0, 6500.0f, -4.0f, 3.0f, 1, true, -36.0f }, { 2, 12000.0f, 1.5f, 0.71f } }),
        preset ("Podcast Voice", vocal | cleanup, { { 3, 80.0f, 0.0f, 0.71f, 2 }, { 0, 200.0f, -2.0f, 1.0f }, { 0, 3500.0f, 2.5f, 0.9f },
                                                    { 0, 6500.0f, -3.0f, 3.0f, 1, true, -36.0f }, { 2, 12000.0f, 1.0f, 0.71f } }),
        preset ("Kick Punch", drums, { { 3, 28.0f, 0.0f, 0.71f, 2 }, { 0, 60.0f, 3.0f, 1.2f }, { 0, 350.0f, -4.0f, 1.5f },
                                       { 0, 4000.0f, 3.0f, 2.0f } }),
        preset ("Snare Crack", drums, { { 3, 70.0f, 0.0f, 0.71f, 1 }, { 0, 200.0f, 2.0f, 1.2f }, { 0, 900.0f, -3.0f, 2.0f },
                                        { 0, 5000.0f, 3.0f, 1.0f } }),
        preset ("Overheads Air", drums, { { 3, 150.0f, 0.0f, 0.71f, 1 }, { 0, 400.0f, -2.0f, 1.0f }, { 2, 10000.0f, 3.0f, 0.71f } }),
        preset ("Bass Tight", bass, { { 3, 35.0f, 0.0f, 0.71f, 2 }, { 0, 80.0f, 2.0f, 1.0f }, { 0, 250.0f, -3.0f, 1.2f },
                                      { 0, 900.0f, 1.5f, 1.5f }, { 4, 9000.0f, 0.0f, 0.71f, 0 } }),
        preset ("Bass Growl", bass, { { 3, 30.0f, 0.0f, 0.71f, 2 }, { 0, 100.0f, 2.5f, 1.0f }, { 0, 250.0f, -2.5f, 1.5f },
                                      { 0, 700.0f, 2.0f, 1.2f } }),
        preset ("808 Sub", bass, { { 3, 25.0f, 0.0f, 0.71f, 2 }, { 0, 50.0f, 3.0f, 1.0f }, { 0, 300.0f, -3.0f, 1.2f },
                                   { 4, 6000.0f, 0.0f, 0.71f, 1 } }),
        preset ("Acoustic Guitar", instruments, { { 3, 80.0f, 0.0f, 0.71f, 1 }, { 0, 200.0f, -3.0f, 1.0f }, { 0, 5000.0f, 2.0f, 0.9f },
                                                  { 2, 12000.0f, 2.5f, 0.71f } }),
        preset ("Electric Guitar Bite", instruments, { { 3, 90.0f, 0.0f, 0.71f, 1 }, { 0, 400.0f, -2.5f, 1.2f }, { 0, 2800.0f, 2.5f, 1.0f },
                                                       { 4, 11000.0f, 0.0f, 0.71f, 1 } }),
        preset ("Piano Shine", instruments, { { 3, 40.0f, 0.0f, 0.71f, 0 }, { 0, 300.0f, -2.0f, 0.9f }, { 0, 2500.0f, 1.0f, 0.8f },
                                              { 2, 9000.0f, 2.0f, 0.71f } }),
        preset ("Synth Pad Clean", instruments | mix, { { 3, 120.0f, 0.0f, 0.71f, 1 }, { 0, 500.0f, -2.0f, 0.8f }, { 2, 8000.0f, 1.5f, 0.71f } }),
        preset ("Mix Bus Polish", mix, { { 3, 25.0f, 0.0f, 0.71f, 0 }, { 1, 90.0f, 1.0f, 0.71f }, { 0, 350.0f, -1.0f, 0.7f },
                                         { 2, 12000.0f, 1.5f, 0.71f } }),
        preset ("Mix Carve Mud", mix, { { 0, 250.0f, -2.0f, 0.8f }, { 0, 500.0f, -1.0f, 0.9f } }),
        preset ("Master Air", mastering, { { 3, 20.0f, 0.0f, 0.71f, 0 }, { 2, 16000.0f, 2.0f, 0.6f }, { 0, 3000.0f, -0.8f, 0.6f } }),
        preset ("Master Low End Focus", mastering, { { 3, 25.0f, 0.0f, 0.71f, 1 }, { 1, 70.0f, 1.0f, 0.71f }, { 0, 180.0f, -0.8f, 0.8f } }),
        preset ("Radio Voice", vocal | effects, { { 3, 400.0f, 0.0f, 0.71f, 3 }, { 0, 1500.0f, 4.0f, 1.0f }, { 4, 3500.0f, 0.0f, 0.71f, 3 } }),
        preset ("Telephone", effects, { { 3, 500.0f, 0.0f, 0.71f, 3 }, { 0, 1800.0f, 5.0f, 1.2f }, { 4, 3000.0f, 0.0f, 0.71f, 3 } }),
        preset ("Hum Remover 50 Hz", cleanup, { { 5, 50.0f, 0.0f, 8.0f }, { 5, 100.0f, 0.0f, 8.0f }, { 5, 150.0f, 0.0f, 8.0f },
                                                { 5, 200.0f, 0.0f, 8.0f } }),
        preset ("Hum Remover 60 Hz", cleanup, { { 5, 60.0f, 0.0f, 8.0f }, { 5, 120.0f, 0.0f, 8.0f }, { 5, 180.0f, 0.0f, 8.0f },
                                                { 5, 240.0f, 0.0f, 8.0f } }),
    };
}

/** Les presets enregistrés avec l'ancienne barre (fichiers .oeqpreset du même dossier) passent dans le navigateur,
    une fois : chacun est recopié au format de la nouvelle barre, l'original reste en place. */
inline void importOldUserPresets (const PresetBank& bank)
{
    for (const auto& file : bank.getFolder().findChildFiles (juce::File::findFiles, false, "*.oeqpreset"))
    {
        const auto target = file.withFileExtension (bank.getExtension());
        if (target.existsAsFile())
            continue;

        if (const auto xml = juce::XmlDocument::parse (file))
        {
            juce::ValueTree snapshot ("Snapshot");
            for (auto* p : xml->getChildWithTagNameIterator ("PARAM"))
                if (const auto id = p->getStringAttribute ("id"); ! notStored().contains (id))
                    snapshot.setProperty (id, (float) p->getDoubleAttribute ("value"), nullptr);

            if (const auto out = snapshot.createXml())
                out->writeTo (target);
        }
    }
}
} // namespace eqpresets
} // namespace onirique

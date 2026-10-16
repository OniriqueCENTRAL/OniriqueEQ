    // Presets d'usine (inclus dans PresetManager). Valeurs en unités réelles ; une bande non citée reste coupée.
    // type : 0 Bell, 1 Low Shelf, 2 High Shelf, 3 Low Cut, 4 High Cut, 5 Notch ; pente : 0..3 = 12..48 dB/oct
    struct B { int type; float freq, gain, q; int slope = 1; bool dyn = false; float thr = -30.0f; };

    static Factory preset (const char* name, std::initializer_list<B> bands)
    {
        Factory f { name, {} };
        int i = 0;
        for (const auto& b : bands)
        {
            if (i >= 8) break;
            f.values[ids::peqOn[i]] = 1.0f;
            f.values[ids::peqType[i]] = (float) b.type;
            f.values[ids::peqFreq[i]] = b.freq;
            f.values[ids::peqGain[i]] = b.gain;
            f.values[ids::peqQ[i]] = b.q;
            f.values[ids::peqSlope[i]] = (float) b.slope;
            f.values[ids::peqDyn[i]] = b.dyn ? 1.0f : 0.0f;
            f.values[ids::peqThr[i]] = b.thr;
            ++i;
        }
        for (; i < 8; ++i) f.values[ids::peqOn[i]] = 0.0f;
        return f;
    }

    static std::vector<Factory> makeFactory()
    {
        return {
            { "Init", {} },
            preset ("Vocal Clarity",     { { 3, 80.0f, 0.0f, 0.71f, 1 }, { 0, 250.0f, -2.5f, 1.2f }, { 0, 3200.0f, 2.0f, 0.8f },
                                           { 2, 11000.0f, 3.0f, 0.71f } }),
            preset ("Vocal De-Harsh",    { { 3, 90.0f, 0.0f, 0.71f, 1 }, { 0, 3500.0f, -5.0f, 2.0f, 1, true, -32.0f },
                                           { 0, 6500.0f, -4.0f, 3.0f, 1, true, -36.0f }, { 2, 12000.0f, 1.5f, 0.71f } }),
            preset ("Rap Vocal Presence", { { 3, 100.0f, 0.0f, 0.71f, 2 }, { 0, 180.0f, 1.5f, 1.0f }, { 0, 400.0f, -3.0f, 1.4f },
                                           { 0, 2500.0f, 2.5f, 1.0f }, { 2, 9000.0f, 2.5f, 0.71f } }),
            preset ("Kick Punch",        { { 3, 28.0f, 0.0f, 0.71f, 2 }, { 0, 60.0f, 3.0f, 1.2f }, { 0, 350.0f, -4.0f, 1.5f },
                                           { 0, 4000.0f, 3.0f, 2.0f } }),
            preset ("Bass Tight",        { { 3, 35.0f, 0.0f, 0.71f, 2 }, { 0, 80.0f, 2.0f, 1.0f }, { 0, 250.0f, -3.0f, 1.2f },
                                           { 0, 900.0f, 1.5f, 1.5f }, { 4, 9000.0f, 0.0f, 0.71f, 0 } }),
            preset ("Snare Crack",       { { 3, 70.0f, 0.0f, 0.71f, 1 }, { 0, 200.0f, 2.0f, 1.2f }, { 0, 900.0f, -3.0f, 2.0f },
                                           { 0, 5000.0f, 3.0f, 1.0f } }),
            preset ("Acoustic Guitar",   { { 3, 80.0f, 0.0f, 0.71f, 1 }, { 0, 200.0f, -3.0f, 1.0f }, { 0, 5000.0f, 2.0f, 0.9f },
                                           { 2, 12000.0f, 2.5f, 0.71f } }),
            preset ("Piano Shine",       { { 3, 40.0f, 0.0f, 0.71f, 0 }, { 0, 300.0f, -2.0f, 0.9f }, { 0, 2500.0f, 1.0f, 0.8f },
                                           { 2, 9000.0f, 2.0f, 0.71f } }),
            preset ("Mix Bus Polish",    { { 3, 25.0f, 0.0f, 0.71f, 0 }, { 1, 90.0f, 1.0f, 0.71f }, { 0, 350.0f, -1.0f, 0.7f },
                                           { 2, 12000.0f, 1.5f, 0.71f } }),
            preset ("Master Air",        { { 3, 20.0f, 0.0f, 0.71f, 0 }, { 2, 16000.0f, 2.0f, 0.6f }, { 0, 3000.0f, -0.8f, 0.6f } }),
            preset ("Radio Voice",       { { 3, 400.0f, 0.0f, 0.71f, 3 }, { 0, 1500.0f, 4.0f, 1.0f }, { 4, 3500.0f, 0.0f, 0.71f, 3 } }),
            preset ("Hum Remover 50 Hz", { { 5, 50.0f, 0.0f, 8.0f }, { 5, 100.0f, 0.0f, 8.0f }, { 5, 150.0f, 0.0f, 8.0f },
                                           { 5, 200.0f, 0.0f, 8.0f } }),
            preset ("Hum Remover 60 Hz", { { 5, 60.0f, 0.0f, 8.0f }, { 5, 120.0f, 0.0f, 8.0f }, { 5, 180.0f, 0.0f, 8.0f },
                                           { 5, 240.0f, 0.0f, 8.0f } }),
        };
    }

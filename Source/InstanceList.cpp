// La fenêtre INSTANCES : tous les ONIRIQUE EQ de la session (voir InstanceList dans UIKit.h et Instances.h).
#include "UIKit.h"

using namespace onirique;

namespace
{
constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f, kRangeDb = 18.0f;
float hzAtPoint (int i, int n) { return kMinHz * std::pow (kMaxHz / kMinHz, (float) i / (float) (n - 1)); }

// une couleur par EQ quand le DAW ne donne pas celle de la piste
juce::Colour paletteColour (int id)
{
    static const juce::uint32 c[] { 0xffe5384a, 0xff7de3b6, 0xfff2c14e, 0xff6ab7ff, 0xffb48cff, 0xffff8f6b, 0xffff8ec8, 0xff4fd1c5 };
    return juce::Colour (c[(size_t) ((id - 1) % 8)]);
}
} // namespace

InstanceList::InstanceList (OniriqueEQAudioProcessor& p) : processor (p)
{
    window.resize ((size_t) kFft);
    for (int i = 0; i < kFft; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kFft - 1));
    setVisible (false);
    setAlpha (0.0f);
}

InstanceList::~InstanceList() { stopTimer(); }

void InstanceList::setOpen (bool shouldOpen)
{
    if (open == shouldOpen) return;
    open = shouldOpen;
    if (open)
    {
        refresh();
        // la ligne de cet EQ en vue
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].self) { scrollTarget = scroll = juce::jmax (0.0f, (float) i * rowStep() - rowStep()); break; }
        setVisible (true);
        toFront (false);
        startTimerHz (30);
    }
}

float InstanceList::unit() const { return ui::editorScale (*this); }
float InstanceList::rowStep() const { return (kRowH + 10.0f) * unit(); }

juce::Rectangle<float> InstanceList::listArea() const
{
    const float u = unit();
    auto r = getLocalBounds().toFloat().reduced (12.0f * u, 10.0f * u);
    r.removeFromTop (40.0f * u);
    r.removeFromRight (196.0f * u);
    return r;
}

juce::Rectangle<float> InstanceList::namesArea() const
{
    const float u = unit();
    auto r = getLocalBounds().toFloat().reduced (12.0f * u, 10.0f * u);
    r.removeFromTop (40.0f * u);
    return r.removeFromRight (184.0f * u);
}

juce::Rectangle<float> InstanceList::rowRect (int i) const
{
    const auto a = listArea();
    return { a.getX(), a.getY() + (float) i * rowStep() - scroll, a.getWidth(), kRowH * unit() };
}

juce::Rectangle<float> InstanceList::plotRect (int i) const
{
    const float u = unit();
    return rowRect (i).withTrimmedLeft (150.0f * u).reduced (0.0f, 4.0f * u);
}

juce::Point<float> InstanceList::nodePos (const Row& row, int b, juce::Rectangle<float> plot) const
{
    const auto& bd = row.bands[(size_t) b];
    const float x = plot.getX() + plot.getWidth() * std::log (juce::jlimit (kMinHz, kMaxHz, bd.freq) / kMinHz) / std::log (kMaxHz / kMinHz);
    const float g = ParametricEQ::hasGain (bd.type) ? bd.gainDb : 0.0f;
    const float y = plot.getCentreY() - juce::jlimit (-kRangeDb, kRangeDb, g) / kRangeDb * plot.getHeight() * 0.45f;
    return { x, y };
}

void InstanceList::refresh()
{
    std::vector<Row> next;
    const int self = processor.getInstanceId();
    int n = 0;
    processor.instances->forEach ([&] (EqInstances::Entry& e)
    {
        auto& p = *e.processor;
        Row row;
        row.id = e.id;
        row.self = e.id == self;
        row.name = p.getTrackName();
        if (row.name.isEmpty()) row.name = "ONIRIQUE EQ " + juce::String (++n);
        else ++n;
        juce::Colour c;
        row.colour = p.getTrackColour (c) && ! c.isTransparent() ? c : paletteColour (e.id);
        row.fs = p.getSampleRate() > 0.0 ? p.getSampleRate() : 48000.0;
        row.power = p.apvts.getRawParameterValue (ids::peqPower)->load() > 0.5f;
        for (int b = 0; b < 8; ++b)
        {
            auto& x = row.bands[(size_t) b];
            auto v = [&p] (const char* id) { return p.apvts.getRawParameterValue (id)->load(); };
            x.on = v (ids::peqOn[b]) > 0.5f;
            x.type = juce::roundToInt (v (ids::peqType[b]));
            x.freq = v (ids::peqFreq[b]);
            x.gainDb = v (ids::peqGain[b]);
            x.q = v (ids::peqQ[b]);
            x.slope = juce::roundToInt (v (ids::peqSlope[b]));
            x.dyn = v (ids::peqDyn[b]) > 0.5f;
            row.live[(size_t) b] = x.dyn && ParametricEQ::canBeDynamic (x.type) ? p.getPeqBandGainDb (b) : x.gainDb;
        }
        // le spectre de sortie de cet EQ : ce qui est arrivé depuis la dernière image
        auto& sp = spectra[e.id];
        if (sp.hist.empty()) { sp.hist.assign ((size_t) kFft, 0.0f); sp.db.fill (-100.0f); }
        float tmp[4096];
        int got;
        while ((got = p.shareTap.pull (tmp, 4096)) > 0)
            for (int i = 0; i < got; ++i) { sp.hist[(size_t) sp.pos] = tmp[i]; sp.pos = (sp.pos + 1) % kFft; sp.fresh += 1; }
        next.push_back (std::move (row));
    });
    // les spectres des EQ disparus
    for (auto it = spectra.begin(); it != spectra.end();)
        it = std::any_of (next.begin(), next.end(), [&] (const Row& r) { return r.id == it->first; }) ? std::next (it) : spectra.erase (it);
    rows = std::move (next);

    // les courbes (réponse de toutes les bandes ; bandes dynamiques : leur gain en ce moment)
    for (auto& row : rows)
        for (int i = 0; i < kPts; ++i)
        {
            const double hz = std::min ((double) hzAtPoint (i, kPts), 0.49 * row.fs);
            std::complex<double> h (1.0, 0.0);
            if (row.power)
                for (int b = 0; b < 8; ++b)
                    if (row.bands[(size_t) b].on) h *= ParametricEQ::bandResponse (row.fs, row.bands[(size_t) b], hz, row.live[(size_t) b]);
            row.curve[(size_t) i] = (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (h))));
        }
}

void InstanceList::analyse (Spectrum& sp, double fs)
{
    if (sp.fresh < kFft / 4) return;
    sp.fresh = 0;
    std::vector<float> buf ((size_t) kFft * 2, 0.0f);
    for (int i = 0; i < kFft; ++i) buf[(size_t) i] = sp.hist[(size_t) ((sp.pos + i) % kFft)] * window[(size_t) i];
    fft.performFrequencyOnlyForwardTransform (buf.data());
    const float binHz = (float) fs / (float) kFft, norm = 2.0f / (0.5f * (float) kFft);
    for (int i = 0; i < kPts; ++i)
    {
        // le plus fort des bins entre ce point et le suivant
        const float f0 = hzAtPoint (i, kPts), f1 = hzAtPoint (juce::jmin (kPts - 1, i + 1), kPts);
        int b0 = juce::jlimit (1, kFft / 2 - 1, (int) (f0 / binHz)), b1 = juce::jlimit (b0, kFft / 2 - 1, (int) (f1 / binHz));
        float m = 0.0f;
        for (int b = b0; b <= b1; ++b) m = std::max (m, buf[(size_t) b]);
        // pente de +3 dB/oct (comme l'analyseur principal : un mix équilibré paraît plat)
        const float db = juce::Decibels::gainToDecibels (m * norm, -100.0f) + 3.0f * std::log2 (std::max (f0, 20.0f) / 1000.0f);
        auto& d = sp.db[(size_t) i];
        d = db > d ? d + 0.6f * (db - d) : d + 0.15f * (db - d);
    }
}

void InstanceList::timerCallback()
{
    const float a = getAlpha();
    const float target = open ? 1.0f : 0.0f;
    if (std::abs (a - target) > 0.01f) setAlpha (a + 0.25f * (target - a));
    else if (! open) { setAlpha (0.0f); setVisible (false); stopTimer(); return; }
    else setAlpha (1.0f);

    refresh();
    for (auto& row : rows) analyse (spectra[row.id], row.fs);
    const float maxScroll = juce::jmax (0.0f, (float) rows.size() * rowStep() - listArea().getHeight());
    scrollTarget = juce::jlimit (0.0f, maxScroll, scrollTarget);
    scroll += 0.3f * (scrollTarget - scroll);
    if (std::abs (scrollTarget - scroll) < 0.5f) scroll = scrollTarget;
    repaint();
}

void InstanceList::paint (juce::Graphics& g)
{
    const float u = unit();
    const auto all = getLocalBounds().toFloat();
    // le fond : le verre de VOCALBOX, posé sur l'analyseur
    g.setColour (ui::night);
    g.fillRoundedRectangle (all, 14.0f * u);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.25f));
    g.drawRoundedRectangle (all.reduced (0.5f), 14.0f * u, std::max (1.0f, u));

    // en-tête : le titre, le nombre d'EQ, la croix
    auto head = all.reduced (12.0f * u, 10.0f * u).removeFromTop (34.0f * u);
    g.setColour (ui::ember);
    g.fillEllipse (juce::Rectangle<float> (7.0f * u, 7.0f * u).withCentre ({ head.getX() + 8.0f * u, head.getCentreY() }));
    g.setColour (ui::mist);
    const auto tf = ui::font (ui::Face::display, 18.0f * u, true, 0.08f);
    g.setFont (tf);
    g.drawText ("INSTANCES", head.withTrimmedLeft (24.0f * u), juce::Justification::centredLeft, false);
    g.setColour (ui::haze);
    g.setFont (ui::font (ui::Face::mono, 11.0f * u, false, 0.12f));
    const juce::String count = juce::String (rows.size()) + (rows.size() > 1 ? " ONIRIQUE EQs IN THIS SESSION" : " ONIRIQUE EQ IN THIS SESSION")
                             + "  ·  DRAG A POINT TO SET ANY EQ";
    g.drawText (count, head.withTrimmedLeft (24.0f * u + ui::textWidth (tf, "INSTANCES") + 20.0f * u), juce::Justification::centredLeft, false);
    closeRect = juce::Rectangle<float> (26.0f * u, 26.0f * u).withCentre ({ head.getRight() - 13.0f * u, head.getCentreY() });
    {
        const auto c = closeRect.reduced (8.0f * u);
        g.setColour (hoverClose ? ui::bone : ui::haze);
        g.drawLine (c.getX(), c.getY(), c.getRight(), c.getBottom(), 1.6f * u);
        g.drawLine (c.getRight(), c.getY(), c.getX(), c.getBottom(), 1.6f * u);
    }

    // les lignes
    const auto area = listArea();
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (area.toNearestInt());
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const auto rr = rowRect (i);
            if (rr.getBottom() < area.getY() || rr.getY() > area.getBottom()) continue;
            paintRow (g, rows[(size_t) i], i);
        }
    }
    if (rows.size() <= 1)
    {
        g.setColour (ui::haze);
        g.setFont (ui::font (ui::Face::body, 13.0f * u));
        g.drawFittedText ("Only this ONIRIQUE EQ is open right now. Every ONIRIQUE EQ you add to your session shows up here, with the name "
                          "and colour of its track, and you can shape all of them from this window.",
                          area.withTrimmedTop (rowStep() + 10.0f * u).withHeight (60.0f * u).toNearestInt(), juce::Justification::centredTop, 3);
    }

    // à droite : la liste des noms (clic : la ligne de cet EQ)
    const auto names = namesArea();
    g.setColour (juce::Colours::white.withAlpha (0.03f));
    g.fillRoundedRectangle (names, 10.0f * u);
    g.setColour (ui::line);
    g.drawRoundedRectangle (names.reduced (0.5f), 10.0f * u, std::max (1.0f, u));
    const float lh = 24.0f * u;
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto r = names.reduced (8.0f * u).withHeight (lh).translated (0.0f, (float) i * lh);
        if (r.getBottom() > names.getBottom()) break;
        const bool inView = rowRect (i).getBottom() > area.getY() && rowRect (i).getY() < area.getBottom();
        if (i == hoverName) { g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillRoundedRectangle (r, 6.0f * u); }
        g.setColour (rows[(size_t) i].colour);
        g.fillRoundedRectangle (r.withWidth (3.0f * u).reduced (0.0f, 5.0f * u), 1.5f * u);
        g.setColour (inView ? ui::mist : ui::haze);
        g.setFont (ui::font (ui::Face::body, 12.5f * u, rows[(size_t) i].self));
        g.drawText (rows[(size_t) i].name, r.withTrimmedLeft (10.0f * u), juce::Justification::centredLeft, true);
    }
}

void InstanceList::paintRow (juce::Graphics& g, const Row& row, int i)
{
    const float u = unit();
    const auto rr = rowRect (i), plot = plotRect (i);
    // la carte de la ligne
    g.setColour (juce::Colours::white.withAlpha (row.self ? 0.045f : 0.025f));
    g.fillRoundedRectangle (rr, 12.0f * u);
    g.setColour (row.self ? row.colour.withAlpha (0.45f) : ui::line);
    g.drawRoundedRectangle (rr.reduced (0.5f), 12.0f * u, std::max (1.0f, u));

    // le nom et sa couleur (comme dans la liste des pistes du DAW)
    auto label = rr.withWidth (150.0f * u).reduced (14.0f * u, 14.0f * u);
    g.setColour (row.colour);
    g.fillRoundedRectangle (label.removeFromLeft (4.0f * u).withHeight (36.0f * u), 2.0f * u);
    label.removeFromLeft (10.0f * u);
    g.setColour (ui::mist);
    g.setFont (ui::font (ui::Face::body, 14.0f * u, true));
    g.drawFittedText (row.name, label.withHeight (36.0f * u).toNearestInt(), juce::Justification::centredLeft, 2);
    g.setFont (ui::font (ui::Face::mono, 10.0f * u, false, 0.12f));
    g.setColour (row.self ? row.colour : ui::haze);
    g.drawText (row.self ? "THIS EQ" : (row.power ? "ON" : "OFF"), label.withTrimmedTop (42.0f * u).withHeight (14.0f * u), juce::Justification::centredLeft, false);

    // la zone de l'analyseur
    g.setColour (ui::night.withAlpha (0.8f));
    g.fillRoundedRectangle (plot, 8.0f * u);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot.toNearestInt());
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = plot.getX() + plot.getWidth() * std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz);
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        }
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());

        // le spectre de sortie
        const auto sit = spectra.find (row.id);
        if (sit != spectra.end())
        {
            juce::Path sp;
            sp.startNewSubPath (plot.getX(), plot.getBottom());
            for (int k = 0; k < kPts; ++k)
            {
                const float x = plot.getX() + plot.getWidth() * (float) k / (float) (kPts - 1);
                const float y = juce::jmap (juce::jlimit (-90.0f, 0.0f, sit->second.db[(size_t) k]), -90.0f, 0.0f, plot.getBottom(), plot.getY() + plot.getHeight() * 0.08f);
                sp.lineTo (x, y);
            }
            sp.lineTo (plot.getRight(), plot.getBottom());
            sp.closeSubPath();
            g.setColour (ui::haze.withAlpha (0.16f));
            g.fillPath (sp);
        }

        // la courbe de l'EQ
        juce::Path curve;
        for (int k = 0; k < kPts; ++k)
        {
            const float x = plot.getX() + plot.getWidth() * (float) k / (float) (kPts - 1);
            const float y = plot.getCentreY() - juce::jlimit (-kRangeDb, kRangeDb, row.curve[(size_t) k]) / kRangeDb * plot.getHeight() * 0.45f;
            if (k == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        juce::Path fill (curve);
        fill.lineTo (plot.getRight(), plot.getCentreY());
        fill.lineTo (plot.getX(), plot.getCentreY());
        fill.closeSubPath();
        g.setColour (row.colour.withAlpha (row.power ? 0.14f : 0.05f));
        g.fillPath (fill);
        g.setColour (row.colour.withAlpha (row.power ? 0.95f : 0.35f));
        g.strokePath (curve, juce::PathStrokeType (std::max (1.2f, 1.8f * u)));

        // les points des bandes
        for (int b = 0; b < 8; ++b)
        {
            if (! row.bands[(size_t) b].on) continue;
            const auto c = nodePos (row, b, plot);
            const bool hot = (hoverRow == i && hoverBand == b) || (dragRow == row.id && dragBand == b);
            const float r = (hot ? 7.0f : 5.5f) * u;
            g.setColour (ui::night);
            g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c));
            g.setColour (hot ? ui::bone : row.colour);
            g.drawEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c), std::max (1.2f, 1.6f * u));
            g.setFont (ui::font (ui::Face::mono, 9.0f * u, true));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c), juce::Justification::centred, false);
        }
    }
    g.setColour (ui::line);
    g.drawRoundedRectangle (plot.reduced (0.5f), 8.0f * u, std::max (1.0f, u));

    // la valeur du point qu'on tient ou qu'on survole
    if (hoverRow == i && hoverBand >= 0)
    {
        const auto& bd = row.bands[(size_t) hoverBand];
        const juce::String t = (bd.freq >= 1000.0f ? juce::String (bd.freq / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (bd.freq)) + " Hz")
                             + (ParametricEQ::hasGain (bd.type) ? "  " + juce::String (bd.gainDb, 1) + " dB" : juce::String())
                             + "  Q " + juce::String (bd.q, 2);
        const auto f = ui::font (ui::Face::mono, 11.0f * u, true);
        const auto c = nodePos (row, hoverBand, plot);
        auto box = juce::Rectangle<float> (ui::textWidth (f, t) + 16.0f * u, 22.0f * u).withCentre ({ c.x, c.y - 22.0f * u });
        box = box.constrainedWithin (plot.reduced (2.0f * u));
        g.setColour (ui::deep2.withAlpha (0.95f));
        g.fillRoundedRectangle (box, 6.0f * u);
        g.setColour (ui::mist);
        g.setFont (f);
        g.drawText (t, box, juce::Justification::centred, false);
    }
}

// ---- souris : survol, glisser un point (fréquence, gain), molette (Q), la liste des noms, la croix
bool InstanceList::hit (juce::Point<float> p, int& row, int& band) const
{
    row = band = -1;
    if (! listArea().contains (p)) return false;
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto plot = plotRect (i);
        if (! plot.expanded (8.0f * unit()).contains (p)) continue;
        row = i;
        float best = 12.0f * unit();
        for (int b = 0; b < 8; ++b)
        {
            if (! rows[(size_t) i].bands[(size_t) b].on) continue;
            const float d = nodePos (rows[(size_t) i], b, plot).getDistanceFrom (p);
            if (d < best) { best = d; band = b; }
        }
        return band >= 0;
    }
    return false;
}

void InstanceList::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
    int r, b;
    hit (p, r, b);
    const auto names = namesArea().reduced (8.0f * unit());
    const int n = names.contains (p) ? (int) ((p.y - names.getY()) / (24.0f * unit())) : -1;
    const bool hc = closeRect.expanded (4.0f).contains (p);
    if (r != hoverRow || b != hoverBand || n != hoverName || hc != hoverClose)
    {
        hoverRow = r; hoverBand = b; hoverName = n < (int) rows.size() ? n : -1; hoverClose = hc;
        setMouseCursor (b >= 0 || hoverName >= 0 || hc ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void InstanceList::mouseExit (const juce::MouseEvent&) { hoverRow = hoverBand = hoverName = -1; hoverClose = false; repaint(); }

void InstanceList::setOn (int id, const char* param, float value, bool begin, bool end)
{
    processor.instances->with (id, [&] (OniriqueEQAudioProcessor& p)
    {
        if (auto* prm = p.apvts.getParameter (param))
        {
            if (begin) prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
            if (end) prm->endChangeGesture();
        }
    });
}

void InstanceList::mouseDown (const juce::MouseEvent& e)
{
    if (closeRect.expanded (4.0f).contains (e.position)) { setOpen (false); if (onClose) onClose(); return; }
    if (hoverName >= 0) { scrollTarget = (float) hoverName * rowStep(); return; }
    int r, b;
    if (hit (e.position, r, b))
    {
        dragRow = rows[(size_t) r].id;
        dragBand = b;
        dragIndex = r;
        for (const char* id : { ids::peqFreq[b], ids::peqGain[b] })
            processor.instances->with (dragRow, [id] (OniriqueEQAudioProcessor& p) { if (auto* prm = p.apvts.getParameter (id)) prm->beginChangeGesture(); });
    }
}

void InstanceList::mouseDrag (const juce::MouseEvent& e)
{
    if (dragRow < 0 || dragIndex < 0 || dragIndex >= (int) rows.size()) return;
    const auto plot = plotRect (dragIndex);
    const float hz = kMinHz * std::pow (kMaxHz / kMinHz, juce::jlimit (0.0f, 1.0f, (e.position.x - plot.getX()) / plot.getWidth()));
    setOn (dragRow, ids::peqFreq[dragBand], hz, false, false);
    if (ParametricEQ::hasGain (rows[(size_t) dragIndex].bands[(size_t) dragBand].type))
    {
        const float db = juce::jlimit (-kRangeDb, kRangeDb, (plot.getCentreY() - e.position.y) / (plot.getHeight() * 0.45f) * kRangeDb);
        setOn (dragRow, ids::peqGain[dragBand], std::round (db * 10.0f) / 10.0f, false, false);
    }
    refresh();
    repaint();
}

void InstanceList::mouseUp (const juce::MouseEvent&)
{
    if (dragRow >= 0)
        for (const char* id : { ids::peqFreq[dragBand], ids::peqGain[dragBand] })
            processor.instances->with (dragRow, [id] (OniriqueEQAudioProcessor& p) { if (auto* prm = p.apvts.getParameter (id)) prm->endChangeGesture(); });
    dragRow = dragBand = dragIndex = -1;
}

void InstanceList::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int r, b;
    if (hit (e.position, r, b))
    {
        // sur un point : son Q
        const float q = juce::jlimit (0.1f, 10.0f, rows[(size_t) r].bands[(size_t) b].q * std::pow (1.15f, w.deltaY * 4.0f));
        setOn (rows[(size_t) r].id, ids::peqQ[b], q, true, true);
        refresh();
        repaint();
        return;
    }
    scrollTarget -= w.deltaY * 300.0f * unit();   // ailleurs : la liste défile
}

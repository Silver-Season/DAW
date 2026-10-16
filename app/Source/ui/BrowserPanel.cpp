#include "BrowserPanel.h"

#include "../Settings.h"
#include "../Strings.h"
#include "../engine/AudioEngine.h"
#include "Dialogs.h"

#include "daw/synth/PrismaParams.h"

namespace ui
{
namespace
{
constexpr int kRowH = 24;
constexpr int kNavRowH = 22;
constexpr int kBottomH = 38;
constexpr int kNumLabels = 7;
constexpr int kLabelPalette[kNumLabels] { 0, 2, 4, 7, 11, 13, -1 }; // red orange yellow green blue purple grey (-1: dim text)

juce::Colour labelColour (int label)
{
    const int p = kLabelPalette[juce::jlimit (0, kNumLabels - 1, label)];
    return p < 0 ? theme().dimText : theme().palette (p);
}

const juce::String& categoryName (browser::Category c)
{
    namespace B = Strings::browser;
    switch (c)
    {
        case browser::Category::instruments: return B::instruments;
        case browser::Category::presets: return B::presets;
        case browser::Category::effects: return B::effects;
        case browser::Category::plugins: return B::plugins;
        case browser::Category::chains: return B::chains;
        case browser::Category::sounds: return B::sounds;
        case browser::Category::midiFiles: return B::midiFiles;
        case browser::Category::places: return B::places;
        case browser::Category::count: break;
    }
    return B::places;
}

const juce::String& labelName (int label)
{
    static const juce::String names[kNumLabels] { Strings::utf8 ("Красная"), Strings::utf8 ("Оранжевая"), Strings::utf8 ("Жёлтая"),
                                                  Strings::utf8 ("Зелёная"), Strings::utf8 ("Синяя"),    Strings::utf8 ("Фиолетовая"),
                                                  Strings::utf8 ("Серая") };
    return names[juce::jlimit (0, kNumLabels - 1, label)];
}

bool sameNav (int kindA, int idxA, int kindB, int idxB) { return kindA == kindB && idxA == idxB; }
} // namespace

//==============================================================================
BrowserPanel::BrowserPanel (AudioEngine& e, Settings& s, browser::BrowserSources& src, library::UserLibrary& lib)
    : engine (e), settings (s), sources (src), library (lib)
{
    setHelpText (Strings::help::browserPanel);
    setWantsKeyboardFocus (true);

    search.setMultiLine (false);
    search.setReturnKeyStartsNewLine (false);
    search.setScrollbarsShown (false);
    search.setBorder ({ 0, 0, 0, 0 });
    search.setHelpText (Strings::help::browserSearch);
    search.addListener (this);
    search.addKeyListener (this);
    addAndMakeVisible (search);

    vbar.addListener (this);
    vbar.setAutoHide (true);
    addAndMakeVisible (vbar);

    engine.setPreviewVolumeDb ((float) settings.previewVolumeDb());
    lookAndFeelChanged();
    rebuildRows();
}

BrowserPanel::~BrowserPanel()
{
    engine.stopPreview();
}

void BrowserPanel::lookAndFeelChanged()
{
    const auto& t = theme();
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::textColourId, t.text);
    search.setColour (juce::CaretComponent::caretColourId, t.accent);
    search.setTextToShowWhenEmpty (Strings::browser::searchPlaceholder, t.dimText);
    search.applyFontToAllText (t.uiFont (12.5f));
    search.setFont (t.uiFont (12.5f));
}

void BrowserPanel::visibilityChanged()
{
    if (isVisible())
        sources.scanPlaces (true); // re-scan the folders if the last scan is older than ten seconds
    else
        stopPreview();
}

void BrowserPanel::stopPreview()
{
    engine.stopPreview();
}

void BrowserPanel::focusSearch()
{
    search.grabKeyboardFocus();
    search.selectAll();
}

void BrowserPanel::setSearchText (const juce::String& text)
{
    search.setText (text, true);
}

void BrowserPanel::selectCategory (browser::Category c)
{
    current = { Nav::category, (int) c };
    search.clear();
    scrollY = 0;
    rebuildRows();
}

int BrowserPanel::numItemRows() const
{
    int n = 0;
    for (const auto& r : rows)
        n += r.kind == Row::entry ? 1 : 0;
    return n;
}

juce::String BrowserPanel::selectedItemName() const
{
    return selectedRow >= 0 && selectedRow < (int) rows.size() ? rows[(size_t) selectedRow].item.name : juce::String();
}

void BrowserPanel::selectItemByName (const juce::String& name)
{
    for (int i = 0; i < (int) rows.size(); ++i)
        if (rows[(size_t) i].kind == Row::entry && rows[(size_t) i].item.name == name)
        {
            select (i, false);
            return;
        }
}

void BrowserPanel::noteUsed (const juce::String& key)
{
    if (key.isEmpty())
        return;
    settings.addRecentItem (key);
    if (current.kind == Nav::recent)
        rebuildRows();
    repaint();
}

void BrowserPanel::sourcesChanged()
{
    rebuildRows();
}

//==============================================================================
std::vector<BrowserPanel::Nav> BrowserPanel::navEntries() const
{
    std::vector<Nav> list;
    list.push_back ({ Nav::recent, 0 });
    for (int label = 0; label < kNumLabels; ++label)
        if (navCount ({ Nav::label, label }) > 0)
            list.push_back ({ Nav::label, label });
    for (int c = 0; c < browser::kNumCategories; ++c)
        list.push_back ({ Nav::category, c });
    return list;
}

juce::String BrowserPanel::navTitle (const Nav& n) const
{
    switch (n.kind)
    {
        case Nav::recent: return Strings::browser::recent;
        case Nav::label: return labelName (n.index);
        case Nav::category: break;
    }
    return categoryName ((browser::Category) n.index);
}

int BrowserPanel::navCount (const Nav& n) const
{
    if (n.kind != Nav::label)
        return -1;
    int count = 0;
    for (const auto& [key, colour] : settings.favourites())
        if (colour == n.index && sources.catalog().find (key) != nullptr)
            ++count;
    return count;
}

juce::Rectangle<int> BrowserPanel::navRect (int index) const
{
    return { 6, navArea.getY() + index * kNavRowH, getWidth() - 12, kNavRowH - 1 };
}

int BrowserPanel::navAt (juce::Point<int> p) const
{
    const auto entries = navEntries();
    for (int i = 0; i < (int) entries.size(); ++i)
        if (navRect (i).contains (p))
            return i;
    return -1;
}

//==============================================================================
void BrowserPanel::rebuildRows()
{
    rows.clear();
    const auto& catalog = sources.catalog();
    const auto query = search.getText().trim();
    const bool searching = query.isNotEmpty();

    auto addGrouped = [this] (const std::vector<browser::Item>& items, bool withCategory) {
        juce::String lastGroup = "\n"; // never equal to a real group
        browser::Category lastCategory = browser::Category::count;
        for (const auto& item : items)
        {
            if (item.group != lastGroup || (withCategory && item.category != lastCategory))
            {
                Row h;
                h.kind = Row::header;
                h.text = withCategory ? categoryName (item.category) + (item.group.isNotEmpty() ? juce::String::fromUTF8 (" \xc2\xb7 ") + item.group : juce::String())
                                      : item.group;
                if (h.text.isNotEmpty())
                    rows.push_back (h);
                lastGroup = item.group;
                lastCategory = item.category;
            }
            Row r;
            r.kind = Row::entry;
            r.item = item;
            rows.push_back (r);
        }
    };

    if (searching)
    {
        addGrouped (catalog.search (query), true);
        if (rows.empty())
            rows.push_back ({ Row::message, {}, Strings::browser::notFound (query) });
    }
    else if (current.kind == Nav::recent)
    {
        for (const auto& key : settings.recentItems())
            if (const auto* item = catalog.find (key))
            {
                Row r;
                r.item = *item;
                rows.push_back (r);
            }
        if (rows.empty())
            rows.push_back ({ Row::message, {}, Strings::browser::emptyRecent });
    }
    else if (current.kind == Nav::label)
    {
        std::vector<browser::Item> labelled;
        const auto favs = settings.favourites();
        for (int c = 0; c < browser::kNumCategories; ++c)
            for (const auto& item : catalog.items ((browser::Category) c))
                if (const auto it = favs.find (item.key); it != favs.end() && it->second == current.index)
                    labelled.push_back (item);
        addGrouped (labelled, true);
        if (rows.empty())
            rows.push_back ({ Row::message, {}, Strings::browser::emptyLabel });
    }
    else
    {
        const auto category = (browser::Category) current.index;
        addGrouped (catalog.items (category), false);
        if (category == browser::Category::places)
        {
            Row action;
            action.kind = Row::action;
            action.text = Strings::browser::addFolder;
            rows.push_back (action);
            if (sources.places().isEmpty())
                rows.insert (rows.begin(), Row { Row::message, {}, Strings::browser::noPlaces });
        }
        else if (rows.empty())
            rows.push_back ({ Row::message, {}, Strings::browser::emptyCategory });
    }

    // keep the selection if the item is still there
    selectedRow = -1;
    for (int i = 0; i < (int) rows.size(); ++i)
        if (rows[(size_t) i].kind == Row::entry && rows[(size_t) i].item.key == selectedKey)
            selectedRow = i;

    resized();
    repaint();
}

int BrowserPanel::contentHeight() const
{
    return (int) rows.size() * kRowH;
}

juce::Rectangle<int> BrowserPanel::rowRect (int index) const
{
    return { listArea.getX(), listArea.getY() + index * kRowH - scrollY, listArea.getWidth(), kRowH };
}

int BrowserPanel::rowAt (juce::Point<int> p) const
{
    if (! listArea.contains (p))
        return -1;
    const int i = (p.y - listArea.getY() + scrollY) / kRowH;
    return i >= 0 && i < (int) rows.size() ? i : -1;
}

void BrowserPanel::resized()
{
    searchArea = { 10, 10, getWidth() - 20, 32 };
    search.setBounds (searchArea.withTrimmedLeft (30).withTrimmedRight (8).reduced (0, 6));
    const int navTop = searchArea.getBottom() + 6;
    const int navHeight = (int) navEntries().size() * kNavRowH;
    navArea = { 0, navTop, getWidth(), navHeight };
    bottomArea = { 0, getHeight() - kBottomH, getWidth(), kBottomH };
    listArea = { 0, navArea.getBottom() + 6, getWidth(), juce::jmax (0, bottomArea.getY() - navArea.getBottom() - 6) };

    scrollY = juce::jlimit (0, juce::jmax (0, contentHeight() - listArea.getHeight()), scrollY);
    vbar.setBounds (listArea.getRight() - 8, listArea.getY(), 8, listArea.getHeight());
    vbar.setRangeLimits (0.0, (double) juce::jmax (contentHeight(), listArea.getHeight()));
    vbar.setCurrentRange ((double) scrollY, (double) listArea.getHeight(), juce::dontSendNotification);
}

void BrowserPanel::scrollBarMoved (juce::ScrollBar*, double start)
{
    scrollY = (int) start;
    repaint (listArea);
}

void BrowserPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! listArea.contains (e.getPosition()))
        return;
    scrollY = juce::jlimit (0, juce::jmax (0, contentHeight() - listArea.getHeight()),
                            scrollY - juce::roundToInt (w.deltaY * (w.isReversed ? -1.0f : 1.0f) * 360.0f));
    vbar.setCurrentRange ((double) scrollY, (double) listArea.getHeight(), juce::dontSendNotification);
    repaint (listArea);
}

juce::Rectangle<int> BrowserPanel::previewButtonRect() const
{
    return { 10, bottomArea.getY() + 5, getWidth() - 20, kBottomH - 10 };
}

//==============================================================================
void BrowserPanel::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.chrome);

    fillCell (g, searchArea.toFloat(), t.panel);
    strokeCell (g, searchArea.toFloat(), search.hasKeyboardFocus (true) ? t.accent : t.line);
    g.setColour (t.dimText);
    g.fillPath (icons::search (juce::Rectangle<float> (13.0f, 13.0f).withCentre ({ (float) searchArea.getX() + 17.0f, (float) searchArea.getCentreY() })));

    // navigation
    const bool searching = search.getText().isNotEmpty();
    const auto entries = navEntries();
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto r = navRect (i);
        const auto& n = entries[(size_t) i];
        const bool selected = ! searching && sameNav (n.kind, n.index, current.kind, current.index);
        if (selected)
            fillCell (g, r.toFloat(), t.panel2, 0.8f);
        else if (i == hoveredNav)
            fillCell (g, r.toFloat(), t.panel, 0.8f);

        juce::Colour dot = t.dimText;
        if (n.kind == Nav::label)
            dot = labelColour (n.index);
        else if (n.kind == Nav::recent)
            dot = t.accent2;
        else
            dot = t.palette (n.index * 2 % 16);
        g.setColour (dot);
        g.fillRoundedRectangle (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) r.getX() + 14.0f, (float) r.getCentreY() }), 2.0f);
        g.setColour (t.text);
        g.setFont (t.uiFont (12.5f));
        g.drawText (navTitle (n), r.withTrimmedLeft (28).withTrimmedRight (30), juce::Justification::centredLeft, true);
        if (n.kind == Nav::label)
        {
            g.setColour (t.dimText);
            g.setFont (t.numFont (10.5f));
            g.drawText (juce::String (navCount (n)), r.withTrimmedRight (8), juce::Justification::centredRight, false);
        }
    }

    g.setColour (t.line);
    g.fillRect (0, listArea.getY() - 3, getWidth(), 1);

    // items
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (listArea);
        const auto favs = settings.favourites();
        const int first = juce::jmax (0, scrollY / kRowH), last = juce::jmin ((int) rows.size() - 1, (scrollY + listArea.getHeight()) / kRowH);
        for (int i = first; i <= last; ++i)
        {
            const auto& r = rows[(size_t) i];
            auto box = rowRect (i);
            if (r.kind == Row::message)
            {
                g.setColour (t.dimText);
                g.setFont (t.uiFont (11.5f, 500));
                g.drawFittedText (r.text, box.reduced (14, 2).withHeight (44), juce::Justification::topLeft, 3);
                continue;
            }
            if (r.kind == Row::header)
            {
                g.setColour (t.dimText);
                g.setFont (t.uiFont (10.5f, 600));
                g.drawText (r.text.toUpperCase(), box.withTrimmedLeft (12).withTrimmedRight (10), juce::Justification::bottomLeft, true);
                continue;
            }
            if (r.kind == Row::action)
            {
                g.setColour (i == hoveredRow ? t.text : t.accent);
                g.setFont (t.uiFont (12.5f, 500));
                g.drawText (juce::String::fromUTF8 ("+  ") + r.text, box.withTrimmedLeft (12), juce::Justification::centredLeft, true);
                continue;
            }

            const bool selected = i == selectedRow;
            const auto cell = box.reduced (6, 1);
            if (selected)
            {
                fillCell (g, cell.toFloat(), t.panel2, 0.8f);
                g.setColour (t.accent);
                g.fillRect (cell.getX(), cell.getY() + 3, 2, cell.getHeight() - 6);
            }
            else if (i == hoveredRow)
                fillCell (g, cell.toFloat(), t.panel, 0.8f);

            const auto fav = favs.find (r.item.key);
            if (fav != favs.end())
            {
                g.setColour (labelColour (fav->second));
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ (float) cell.getX() + 11.0f, (float) cell.getCentreY() }));
            }
            g.setColour (r.item.placeholder ? t.dimText : t.text);
            g.setFont (t.uiFont (12.5f, selected ? 500 : 400));
            auto textArea = cell.withTrimmedLeft (22).withTrimmedRight (10);
            if (r.item.detail.isNotEmpty())
            {
                g.setColour (t.dimText);
                g.setFont (t.uiFont (10.0f));
                const int w = juce::jmin (70, (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), r.item.detail)));
                g.drawText (r.item.detail, textArea.removeFromRight (w), juce::Justification::centredRight, true);
                textArea.removeFromRight (6);
            }
            g.setColour (r.item.placeholder ? t.dimText : t.text);
            g.setFont (t.uiFont (12.5f, selected ? 500 : 400));
            g.drawText (r.item.name, textArea, juce::Justification::centredLeft, true);
        }
    }

    // preview switch
    g.setColour (t.line);
    g.fillRect (0, bottomArea.getY(), getWidth(), 1);
    const auto b = previewButtonRect();
    const bool on = settings.previewEnabled();
    fillCell (g, b.toFloat(), on ? t.accent : t.panel);
    strokeCell (g, b.toFloat(), on ? t.accent : t.line);
    g.setColour (on ? t.clipInk : t.dimText);
    g.fillPath (icons::headphones (juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ (float) b.getX() + 18.0f, (float) b.getCentreY() })));
    g.setFont (t.uiFont (12.0f, 500));
    g.drawText (Strings::browser::previewToggle, b.withTrimmedLeft (34), juce::Justification::centredLeft, true);
}

//==============================================================================
void BrowserPanel::textEditorTextChanged (juce::TextEditor&)
{
    scrollY = 0;
    rebuildRows();
}

void BrowserPanel::textEditorEscapeKeyPressed (juce::TextEditor&)
{
    search.clear();
    search.giveAwayKeyboardFocus();
    if (auto* top = getTopLevelComponent())
        top->grabKeyboardFocus();
}

void BrowserPanel::select (int rowIndex, bool preview)
{
    if (rowIndex < 0 || rowIndex >= (int) rows.size() || rows[(size_t) rowIndex].kind != Row::entry)
        return;
    selectedRow = rowIndex;
    selectedKey = rows[(size_t) rowIndex].item.key;
    ensureVisible (rowIndex);
    repaint (listArea);
    if (preview)
        previewSelected();
}

void BrowserPanel::moveSelection (int delta)
{
    int i = selectedRow;
    do
        i += delta;
    while (i >= 0 && i < (int) rows.size() && rows[(size_t) i].kind != Row::entry);
    if (i < 0)
        return;
    if (i >= (int) rows.size())
        return;
    select (i, true);
}

void BrowserPanel::ensureVisible (int rowIndex)
{
    const int top = rowIndex * kRowH, bottom = top + kRowH;
    if (top < scrollY)
        scrollY = top;
    else if (bottom > scrollY + listArea.getHeight())
        scrollY = bottom - listArea.getHeight();
    scrollY = juce::jlimit (0, juce::jmax (0, contentHeight() - listArea.getHeight()), scrollY);
    vbar.setCurrentRange ((double) scrollY, (double) listArea.getHeight(), juce::dontSendNotification);
}

void BrowserPanel::previewSelected()
{
    if (! settings.previewEnabled() || selectedRow < 0)
    {
        engine.stopPreview();
        return;
    }
    const auto& item = rows[(size_t) selectedRow].item;
    if (item.kind == browser::kinds::prismaPreset)
        engine.previewFactoryPreset (item.id.getIntValue());
    else if (item.kind == browser::kinds::userPreset && item.id == "prisma")
    {
        const auto data = library::UserLibrary::readPreset (juce::File (item.path));
        juce::MemoryBlock block;
        if (data.valid && block.fromBase64Encoding (data.state))
            engine.previewPresetState (block);
        else
            engine.stopPreview();
    }
    else
        engine.stopPreview(); // no pre-listening for hosted plugins
}

void BrowserPanel::activate (const browser::Item& item)
{
    if (item.placeholder)
        return;
    browser::DragItem d;
    if (browser::parseDrag (browser::makeDrag (item), d) && onActivate)
        onActivate (d);
}

bool BrowserPanel::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    // from the search field: the arrows and Enter drive the list
    if (key == juce::KeyPress::downKey)
    {
        moveSelection (1);
        return true;
    }
    if (key == juce::KeyPress::upKey)
    {
        moveSelection (-1);
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        if (selectedRow < 0)
            moveSelection (1);
        if (selectedRow >= 0)
            activate (rows[(size_t) selectedRow].item);
        return true;
    }
    return false;
}

bool BrowserPanel::keyPressed (const juce::KeyPress& key)
{
    if (keyPressed (key, nullptr))
        return true;
    // a focused list must not let Delete reach the project
    return key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey;
}

//==============================================================================
void BrowserPanel::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    pressPosition = e.getPosition();
    dragStarted = false;

    if (const int n = navAt (e.getPosition()); n >= 0)
    {
        const auto entries = navEntries();
        current = entries[(size_t) n];
        search.clear();
        scrollY = 0;
        rebuildRows();
        return;
    }
    if (previewButtonRect().contains (e.getPosition()))
    {
        settings.setPreviewEnabled (! settings.previewEnabled());
        if (settings.previewEnabled())
            previewSelected();
        else
            engine.stopPreview();
        repaint (bottomArea);
        return;
    }
    if (const int i = rowAt (e.getPosition()); i >= 0)
    {
        const auto& r = rows[(size_t) i];
        if (r.kind == Row::action)
        {
            addPlaceFolder();
            return;
        }
        if (r.kind == Row::entry)
        {
            select (i, true);
            if (e.mods.isPopupMenu())
                showItemMenu (r, e.getScreenPosition());
        }
    }
}

void BrowserPanel::mouseUp (const juce::MouseEvent&)
{
    dragStarted = false;
}

juce::Image BrowserPanel::dragImage (const juce::String& text) const
{
    const auto& t = theme();
    const auto font = t.uiFont (12.5f, 500);
    const int w = juce::jlimit (90, 240, (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, text)) + 28), h = 28;
    juce::Image img (juce::Image::ARGB, w, h, true);
    juce::Graphics g (img);
    g.setColour (t.panel2.withAlpha (0.92f));
    g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, t.radius);
    g.setColour (t.accent);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, t.radius, 1.2f);
    g.setColour (t.text);
    g.setFont (font);
    g.drawText (text, 12, 0, w - 24, h, juce::Justification::centredLeft, true);
    return img;
}

void BrowserPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 6)
        return;
    const int i = rowAt (pressPosition);
    if (i < 0 || rows[(size_t) i].kind != Row::entry || rows[(size_t) i].item.placeholder)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragStarted = true;
        const auto& item = rows[(size_t) i].item;
        container->startDragging (browser::makeDrag (item), this, juce::ScaledImage (dragImage (item.name)), true);
    }
}

void BrowserPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int i = rowAt (e.getPosition()); i >= 0 && rows[(size_t) i].kind == Row::entry)
        activate (rows[(size_t) i].item);
}

void BrowserPanel::mouseMove (const juce::MouseEvent& e)
{
    const int n = navAt (e.getPosition()), r = rowAt (e.getPosition());
    if (n != hoveredNav || r != hoveredRow)
    {
        hoveredNav = n;
        hoveredRow = r;
        repaint();
    }
}

void BrowserPanel::mouseExit (const juce::MouseEvent&)
{
    if (hoveredNav != -1 || hoveredRow != -1)
    {
        hoveredNav = hoveredRow = -1;
        repaint();
    }
}

juce::String BrowserPanel::helpAt (juce::Point<int> p) const
{
    if (previewButtonRect().contains (p))
        return Strings::help::browserPreview;
    if (const int n = navAt (p); n >= 0)
        return Strings::help::browserCategory (navTitle (navEntries()[(size_t) n]));
    if (const int i = rowAt (p); i >= 0)
    {
        const auto& r = rows[(size_t) i];
        if (r.kind == Row::entry)
            return Strings::help::make (r.item.name, r.item.placeholder ? Strings::help::soon6 : Strings::help::browserItem);
        if (r.kind == Row::action)
            return Strings::help::make (Strings::browser::addFolder, Strings::help::browserAddFolder);
    }
    return {};
}

//==============================================================================
void BrowserPanel::showItemMenu (const Row& row, juce::Point<int> screenPosition)
{
    const auto item = row.item;
    juce::PopupMenu menu;
    if (! item.placeholder)
        menu.addItem (Strings::browser::useOnTrack, [this, item] { activate (item); });

    juce::PopupMenu labels;
    const int currentLabel = settings.favouriteColour (item.key);
    for (int c = 0; c < kNumLabels; ++c)
    {
        juce::PopupMenu::Item mi (labelName (c));
        mi.setTicked (c == currentLabel);
        mi.setColour (labelColour (c));
        mi.action = [this, key = item.key, c] {
            settings.setFavouriteColour (key, c);
            rebuildRows();
        };
        labels.addItem (mi);
    }
    labels.addSeparator();
    labels.addItem (Strings::browser::labelNone, currentLabel >= 0, false, [this, key = item.key] {
        settings.setFavouriteColour (key, -1);
        if (current.kind == Nav::label && navCount (current) == 0)
            current = { Nav::category, 1 };
        rebuildRows();
    });
    menu.addSubMenu (Strings::browser::labelColour, labels);

    if (item.userEditable)
    {
        menu.addSeparator();
        menu.addItem (Strings::browser::renamePreset, [this, item] {
            promptText (Strings::browser::renamePreset, item.name, item.name, [this, item] (const juce::String& name) {
                const juce::File file (item.path);
                const auto renamed = item.kind == browser::kinds::chain ? library::UserLibrary::renameChain (file, name)
                                                                         : library::UserLibrary::renamePreset (file, name);
                if (renamed != juce::File())
                {
                    settings.setFavouriteColour (item.key, -1);
                    sources.refreshLibrary();
                    if (onLibraryEdited)
                        onLibraryEdited();
                }
            });
        });
        menu.addItem (Strings::browser::deletePreset, [this, item] {
            confirm (Strings::browser::deleteTitle, Strings::browser::deleteQuestion (item.name), Strings::browser::deleteOk, [this, item] (bool ok) {
                if (! ok)
                    return;
                library::UserLibrary::deleteFile (juce::File (item.path));
                settings.setFavouriteColour (item.key, -1);
                sources.refreshLibrary();
                if (onLibraryEdited)
                    onLibraryEdited();
            });
        });
    }
    if (item.path.isNotEmpty())
        menu.addItem (Strings::browser::showInFinder, [item] { juce::File (item.path).revealToUser(); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }));
}

void BrowserPanel::addPlaceFolder()
{
    chooser = std::make_unique<juce::FileChooser> (Strings::browser::addFolderTitle, juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
        const auto result = fc.getResult();
        if (result == juce::File())
            return;
        auto folders = settings.placeFolders();
        folders.addIfNotAlreadyThere (result.getFullPathName());
        settings.setPlaceFolders (folders);
        sources.scanPlaces (false);
    });
}
} // namespace ui

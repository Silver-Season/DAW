#pragma once

#include "ArrangementView.h"

namespace ui
{
// The lanes of the Arrangement View: the grid, the clips, the time selection and the gestures on them (move, copy,
// stretch, draw, select time). Its x axis is the time axis of the view (ArrangementView::beatToX), its y axis the
// lanes minus the vertical scroll. The picture is cached; the playhead lives in a component above.
class ArrangementLanes final : public juce::Component, public HelpProvider
{
public:
    explicit ArrangementLanes (ArrangementView&);

    // Where an ACLIP is drawn, in the coordinates of the lanes (empty if its track is not shown).
    juce::Rectangle<int> clipBounds (int64_t aclipId) const;
    // Opens the in-place name editor on a clip.
    void beginRenameClip (int64_t aclipId);

    enum class Zone
    {
        none,
        body,
        leftEdge,
        rightEdge
    };
    struct Hit
    {
        ArrangementClipRef clip;
        Zone zone = Zone::none;
        int row = -1;
    };
    Hit clipHit (juce::Point<int>) const;

    // Automation mode: what lies under a point of the lanes. `point` is the index of a dot, `onLine` is true within a few
    // pixels of the envelope line (also where the line is only the current mixer value of a lane without points).
    struct AutoHit
    {
        int row = -1;
        int point = -1;
        bool onLine = false;
    };
    AutoHit autoHitAt (juce::Point<int>) const;
    // Where the envelope of a row is drawn: the lane minus a margin; value 1 is the top.
    juce::Rectangle<float> envelopeArea (int row) const;
    bool isGestureRunning() const { return gesture != Gesture::none; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    juce::String helpAt (juce::Point<int>) const override;

private:
    enum class Gesture
    {
        none,
        move,
        resizeLeft,
        resizeRight,
        selectTime,
        autoMove,
        autoBend
    };

    void paintGrid (juce::Graphics&, juce::Rectangle<int> area) const;
    void paintClip (juce::Graphics&, const ArrangementClipRef&, juce::Rectangle<float> r, double clipStart, double clipLength, double clipOffset,
                    float alpha, bool selected, bool inSession, bool muted) const;
    void paintNotes (juce::Graphics&, const ArrangementClipRef&, juce::Rectangle<float> r, double clipStart, double clipLength,
                     double clipOffset, juce::Colour ink) const;
    // Audio clips: the waveform of the source with the fades over it; false if the file is missing (nothing is drawn).
    bool paintAudio (juce::Graphics&, const ArrangementClipRef&, juce::Rectangle<float> r, double clipLength, double clipOffset,
                     juce::Colour ink) const;
    juce::Rectangle<float> rectFor (int row, double start, double length) const;
    void showClipMenu (juce::Point<int> screenPosition);
    void showEmptyMenu (juce::Point<int> screenPosition, double beat);
    void updateMoveGesture (const juce::MouseEvent&);
    void paintEnvelope (juce::Graphics&, int row) const;
    // Starts an envelope gesture if the press is on the envelope of a lane; false lets the clips have the click.
    bool beginAutomationGesture (const juce::MouseEvent&);
    void dragAutomation (const juce::MouseEvent&);
    float valueForY (int row, float y) const;
    void finishRename (const juce::String& text, int64_t aclipId);

    ArrangementView& view;

    Gesture gesture = Gesture::none;
    // move / copy
    std::vector<ArrangementView::Dragged> dragged;
    int64_t anchorClip = -1;
    int anchorRow = 0;
    double anchorStart = 0.0, pointerBeatOffset = 0.0;
    double deltaBeats = 0.0;
    int deltaTracks = 0;
    bool copying = false;
    // stretch
    int64_t stretchClip = -1;
    double stretchValue = 0.0;
    // time selection
    double timeAnchor = 0.0;
    int timeRowAnchor = 0;
    bool movedEnough = false;
    bool pressedOnEmpty = false;
    std::unique_ptr<InlineEditor> renameEditor;
    // automation
    juce::ValueTree autoPointNode;
    int64_t autoTrackId = -1;
    juce::String autoTargetId, readoutText;
    float bendStart = 0.0f, bendDirection = 1.0f;
    juce::Point<float> readoutAt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementLanes)
};
} // namespace ui

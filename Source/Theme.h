#pragma once
// ============================================================================================================
// One place for the editor's look. Calm dark-slate palette with a warm amber accent: lower contrast than before so
// it is easier on the eyes in a dim room, but text still passes a comfortable reading contrast.
// To retheme the whole plugin, edit only this file. Tools/preview_gui.py reads these values to draw preview pictures.
// Format matters: keep exactly one "inline const juce::Colour name (0xAARRGGBB);" per line.
// ============================================================================================================
#include <juce_gui_basics/juce_gui_basics.h>

namespace rfx { namespace theme
{
// ---- surfaces
inline const juce::Colour bgTop        (0xff1a1e29);   // window background, top
inline const juce::Colour bgBottom     (0xff12151d);   // window background, bottom
inline const juce::Colour panelTop     (0xff232937);   // panel gradient, top
inline const juce::Colour panelBottom  (0xff1d222e);   // panel gradient, bottom
inline const juce::Colour edge         (0x14ffffff);   // hairline round panels and buttons (white at 8 %)
inline const juce::Colour edgeStrong   (0x2effffff);   // hairline round inputs (white at 18 %)
inline const juce::Colour well         (0xff171a23);   // recessed areas: drop-downs, meters, empty lane cells
inline const juce::Colour cellA        (0xff1f2431);   // lane cells, beats 1-4 of every group of 8
inline const juce::Colour cellB        (0xff1a1e29);   // lane cells, the other group
inline const juce::Colour button       (0xff2a3040);   // buttons and drop-down fill
inline const juce::Colour buttonHover  (0xff343b4e);

// ---- text
inline const juce::Colour text         (0xffe3e7f0);   // primary
inline const juce::Colour subText      (0xffaab3c6);   // knob captions
inline const juce::Colour dim          (0xff7b8499);   // panel titles, hints
inline const juce::Colour onAccent     (0xff241705);   // text drawn on the accent colour

// ---- accent and lane colours (all slightly desaturated so large bars do not glare)
inline const juce::Colour accent       (0xfff3a65c);   // amber
inline const juce::Colour knobTrack    (0xff343b4c);   // unfilled part of a knob's arc
inline const juce::Colour knobFace     (0xff2a3040);   // the disc inside the arc
inline const juce::Colour teal         (0xff5cc6dd);   // filter lane
inline const juce::Colour rose         (0xffec7b95);   // amp lane
inline const juce::Colour sage         (0xff94d58c);   // pan lane
inline const juce::Colour drumKick     (0xfff3a65c);
inline const juce::Colour drumSnare    (0xffe8cf72);
inline const juce::Colour drumHat      (0xff6fcf86);
inline const juce::Colour drumClap     (0xffa98af0);

constexpr float panelRadius = 10.f;    // corner radius of panels
constexpr float inputRadius = 6.f;     // corner radius of drop-downs and buttons
}} // namespace rfx::theme

#pragma once

#include "core/document/Cel.h"

#include <memory>
#include <vector>

namespace mothion::doc {

// Frames per second as a fraction, so 23.976 (24000/1001) is exact.
struct FrameRate
{
    int numerator = 24;
    int denominator = 1;

    double fps() const { return double(numerator) / denominator; }
    bool operator==(const FrameRate &) const = default;
};

// The whole project: canvas size, frame rate, scene length and the cel
// stack. Changes that must be undoable go through Commands (see History);
// the methods here are the raw operations those commands use.
class Document
{
public:
    Document(int width, int height, FrameRate frameRate = {});

    int width() const { return m_width; }
    int height() const { return m_height; }
    FrameRate frameRate() const { return m_frameRate; }
    void setFrameRate(FrameRate rate) { m_frameRate = rate; }

    // Scene length in frames (>= 1). Exposures may extend past it.
    int frameCount() const { return m_frameCount; }
    void setFrameCount(int count) { m_frameCount = count < 1 ? 1 : count; }

    Pixel paperColor() const { return m_paperColor; }
    void setPaperColor(Pixel color) { m_paperColor = color; }

    // Cels from bottom (index 0) to top.
    int celCount() const { return int(m_cels.size()); }
    Cel &celAt(int index) { return *m_cels[std::size_t(index)]; }
    const Cel &celAt(int index) const { return *m_cels[std::size_t(index)]; }
    int indexOf(CelId id) const; // -1 if absent
    Cel *cel(CelId id);
    const Cel *cel(CelId id) const;

    // Cels are heap objects, so Cel references stay valid across reordering.
    Cel &insertCel(int index, std::unique_ptr<Cel> cel);
    std::unique_ptr<Cel> takeCel(CelId id);
    void moveCel(CelId id, int newIndex);

    // Fresh ids; never reused within this document.
    CelId allocateCelId() { return m_nextCelId++; }
    DrawingId allocateDrawingId() { return m_nextDrawingId++; }
    // The next ids to allocate, as a saved file records them.
    CelId nextCelId() const { return m_nextCelId; }
    DrawingId nextDrawingId() const { return m_nextDrawingId; }
    // For loading: never moves the counters back below an id already in use.
    void restoreIdCounters(CelId nextCel, DrawingId nextDrawing);

private:
    int m_width;
    int m_height;
    FrameRate m_frameRate;
    int m_frameCount;
    Pixel m_paperColor = 0xffffffff;
    std::vector<std::unique_ptr<Cel>> m_cels;
    CelId m_nextCelId = 1;
    DrawingId m_nextDrawingId = 1;
};

} // namespace mothion::doc

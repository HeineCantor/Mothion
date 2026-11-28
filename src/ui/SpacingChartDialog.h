#pragma once

#include <QDialog>
#include <QPointF>
#include <QWidget>

#include <optional>
#include <vector>

class QLabel;
class QPushButton;

namespace mothion::ui {

// The chart of a spacing chart: a line from the first key (A) to the second (B), ready drawn, with room past both ends,
// on which the person draws the ticks freehand. A tick is where its stroke crosses the line (the middle of one that does
// not reach it); one drawn on A or B is that end marked again, not a tick of its own.
class SpacingChartCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit SpacingChartCanvas(QWidget *parent = nullptr);

    // How far from A (0) to B (1) each tick is, in order along the line.
    std::vector<double> shares() const;
    void undo();
    void clear();
    bool hasStrokes() const { return !m_strokes.empty(); }
    // Where on the widget a share of the way is, on the line.
    QPointF pointAt(double share) const;

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    using Stroke = std::vector<QPointF>;
    std::optional<double> shareOf(const Stroke &stroke) const;

    std::vector<Stroke> m_strokes;
    bool m_drawing = false;
};

// Asks for the spacing chart between two keys, on frames `from` and `to` (0-based), `between` being how many keyframes
// there are between them now (the chart replaces them), and `block` the last frame between them that a channel left out
// of the chart has a key on (`from` if none; see doc::CelMotion::spacingBlock()). It says on which frames the new keys
// will go.
class SpacingChartDialog : public QDialog
{
    Q_OBJECT
public:
    SpacingChartDialog(int from, int to, int between, int block, QWidget *parent = nullptr);

    SpacingChartCanvas *canvas() const { return m_canvas; }
    std::vector<double> shares() const { return m_canvas->shares(); }

private:
    void refresh();

    int m_from;
    int m_to;
    int m_between;
    int m_block;
    SpacingChartCanvas *m_canvas = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_ok = nullptr;
    QPushButton *m_undo = nullptr;
    QPushButton *m_clear = nullptr;
};

} // namespace mothion::ui

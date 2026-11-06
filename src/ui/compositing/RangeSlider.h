#pragma once

#include <QWidget>

namespace mothion::ui {

// A slider with two handles: the part of 0..maximum between them is the range. Big handles, to take with a pen.
class RangeSlider : public QWidget
{
    Q_OBJECT
public:
    explicit RangeSlider(QWidget *parent = nullptr);

    int maximum() const { return m_maximum; }
    int first() const { return m_first; }
    int last() const { return m_last; }

    // The slider goes from 0 to `maximum`; the range is kept inside it.
    void setMaximum(int maximum);
    // Sets the range without telling anyone.
    void setRange(int first, int last);

signals:
    void rangeChanged(int first, int last); // the user moved a handle

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    QSize sizeHint() const override { return {200, 26}; }

private:
    int valueAt(qreal x) const;
    qreal positionOf(int value) const;

    int m_maximum = 0;
    int m_first = 0;
    int m_last = 0;
    int m_dragging = 0; // 1: the first handle, 2: the last, 0: none
};

} // namespace mothion::ui

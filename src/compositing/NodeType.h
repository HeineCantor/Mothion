#pragma once

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <memory>
#include <vector>

namespace mothion::compositing {

// A setting of a node. Every kind is stored as an integer (a color as its packed ARGB value): a slider, the
// index of a choice, 0 or 1 for a toggle. Make one with slider(), color(), choice() or toggle().
struct ParamInfo
{
    enum class Kind { Slider, Color, Choice, Toggle };

    QString key;
    QString label;
    Kind kind = Kind::Slider;
    int min = 0;
    int max = 100;
    QVariant initial;     // an int, or a QColor
    QStringList choices; // Choice: what the menu offers

    static ParamInfo slider(const QString &key, const QString &label, int min, int max, int initial)
    {
        ParamInfo p;
        p.key = key;
        p.label = label;
        p.min = min;
        p.max = max;
        p.initial = initial;
        return p;
    }
    static ParamInfo color(const QString &key, const QString &label, const QColor &initial)
    {
        ParamInfo p;
        p.key = key;
        p.label = label;
        p.kind = Kind::Color;
        p.initial = initial;
        return p;
    }
    static ParamInfo choice(const QString &key, const QString &label, const QStringList &choices, int initial = 0)
    {
        ParamInfo p;
        p.key = key;
        p.label = label;
        p.kind = Kind::Choice;
        p.min = 0;
        p.max = static_cast<int>(choices.size()) - 1;
        p.initial = initial;
        p.choices = choices;
        return p;
    }
    static ParamInfo toggle(const QString &key, const QString &label, bool initial)
    {
        ParamInfo p;
        p.key = key;
        p.label = label;
        p.kind = Kind::Toggle;
        p.min = 0;
        p.max = 1;
        p.initial = initial ? 1 : 0;
        return p;
    }
};

// What a node may look at while it works: the frame the Frame nodes hold, which frame of the animation it is, and
// how big that picture is next to the document (a preview is smaller): sizes in pixels, like a blur's radius, are
// given for the document and are to be multiplied by `scale`. `paper` is the document's background color.
struct EvalContext
{
    QImage frame;
    int frameIndex = 0;
    double scale = 1.0;
    QRgb paper = 0xffffffff; // the color of the page (ARGB): what some nodes leave alone
};

inline QString nodeTr(const char *text) { return QCoreApplication::translate("Compositing", text); }

// One kind of node: what it is called, which sockets and settings it has, and what it does to the images
// that come in. A type has no state (a node's settings live in the Graph), so one object serves all the
// nodes of its kind, from any thread.
class NodeType
{
public:
    virtual ~NodeType() = default;

    virtual QString id() const = 0; // stored in project files: never change it
    virtual QString title() const = 0;
    virtual QColor tint() const = 0;
    virtual QStringList inputs() const { return {}; }  // names of the input sockets
    virtual QStringList outputs() const { return {}; } // names of the output sockets
    virtual QList<ParamInfo> params() const { return {}; }
    // True if the result changes with the frame being shown (the cache then looks at the frame).
    virtual bool dependsOnFrame() const { return false; }
    // True if, with these settings, the result changes with the number of the frame (an animated grain).
    virtual bool usesFrameIndex(const QVariantMap &params) const
    {
        Q_UNUSED(params)
        return false;
    }

    // `inputs` has one image per input socket, null where nothing is connected. Settings come as
    // key -> value, with every key of params() present (and kDataKey, if the node has data).
    virtual QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const = 0;
    // One image for each output socket, in order. The default is the one of process(); a type with several
    // outputs overrides this (and process() gives the first).
    virtual QList<QImage> processOutputs(const QList<QImage> &inputs, const QVariantMap &params,
                                         const EvalContext &context) const
    {
        return {process(inputs, params, context)};
    }
};

// Where a type finds the node's `data` (Node::data) among the settings it is handed. Only there when it is not empty.
inline const QString kDataKey = QStringLiteral("data");

// Every kind of node there is. To add one: write a NodeType and list it in NodeType.cpp.
class NodeRegistry
{
public:
    static const NodeRegistry &instance();

    const NodeType *find(const QString &id) const; // null if there is no such type
    const std::vector<std::unique_ptr<NodeType>> &all() const { return m_types; }

private:
    NodeRegistry();
    std::vector<std::unique_ptr<NodeType>> m_types;
};

} // namespace mothion::compositing

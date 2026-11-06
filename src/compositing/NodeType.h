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

// A setting of a node: a slider (an integer from min to max) or a color (a QColor with alpha).
struct ParamInfo
{
    enum class Kind { Slider, Color };

    QString key;
    QString label;
    Kind kind = Kind::Slider;
    int min = 0;
    int max = 100;
    QVariant initial; // an int, or a QColor
};

// What a node may look at while it works: the frame the Frame nodes hold.
struct EvalContext
{
    QImage frame;
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

    // `inputs` has one image per input socket, null where nothing is connected. Settings come as
    // key -> value, with every key of params() present.
    virtual QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const = 0;
};

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

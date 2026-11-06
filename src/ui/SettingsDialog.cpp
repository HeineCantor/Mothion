#include "ui/SettingsDialog.h"

#include "tools/Preferences.h"
#include "ui/Shortcuts.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace mothion::ui {

namespace {

constexpr int kCommandRole = Qt::UserRole + 1;

// A line of text for a command's keys: "Ctrl+Z, Ctrl+Y", or a dash when it has none.
QString keysText(const QString &id)
{
    QStringList texts;
    for (const QKeySequence &k : Shortcuts::instance().keys(id))
        texts << k.toString(QKeySequence::NativeText);
    return texts.isEmpty() ? QStringLiteral("—") : texts.join(QStringLiteral(", "));
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_sections(new QListWidget(this))
    , m_pages(new QStackedWidget(this))
{
    setObjectName(QStringLiteral("SettingsDialog"));
    setWindowTitle(tr("Settings"));
    resize(760, 520);

    m_sections->setFixedWidth(150);
    const std::pair<QString, QWidget *> pages[] = {
        {tr("General"), generalPage()},
        {tr("Project"), projectPage()},
        {tr("Canvas"), canvasPage()},
        {tr("Input"), inputPage()},
        {tr("Shortcuts"), shortcutsPage()},
        {tr("Advanced"), advancedPage()},
    };
    for (const auto &[name, page] : pages) {
        m_sections->addItem(name);
        m_pages->addWidget(page);
    }
    connect(m_sections, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_sections->setCurrentRow(0);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    auto *reset = buttons->addButton(tr("Reset all settings"), QDialogButtonBox::ResetRole);
    connect(reset, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Reset all settings"),
                                  tr("Put every setting and shortcut back to its default?"))
            != QMessageBox::Yes)
            return;
        tools::Preferences::instance().resetAll();
        Shortcuts::instance().resetAll();
        setThemeChoice(*qApp, ThemeChoice::System);
        // The pages show what they hold now.
        const int row = m_sections->currentRow();
        while (m_pages->count() > 0) {
            QWidget *page = m_pages->widget(0);
            m_pages->removeWidget(page);
            delete page;
        }
        m_commands = nullptr;
        m_pages->addWidget(generalPage());
        m_pages->addWidget(projectPage());
        m_pages->addWidget(canvasPage());
        m_pages->addWidget(inputPage());
        m_pages->addWidget(shortcutsPage());
        m_pages->addWidget(advancedPage());
        m_pages->setCurrentIndex(row);
    });

    auto *body = new QHBoxLayout;
    body->addWidget(m_sections);
    body->addWidget(m_pages, 1);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addWidget(buttons);
}

QWidget *SettingsDialog::generalPage()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    tools::Preferences &prefs = tools::Preferences::instance();

    auto *discord = new QCheckBox(tr("Show what I am doing in Discord"), page);
    discord->setObjectName(QStringLiteral("discord"));
    discord->setToolTip(tr("\"Mothion\", for how long, and the frame of the timeline, in your Discord status"));
    discord->setChecked(prefs.discordEnabled());
    connect(discord, &QCheckBox::toggled, this, [](bool on) { tools::Preferences::instance().setDiscordEnabled(on); });
    form->addRow(tr("Discord"), discord);

    auto *theme = new QComboBox(page);
    theme->setObjectName(QStringLiteral("theme"));
    const std::pair<QString, ThemeChoice> themes[] = {
        {tr("System"), ThemeChoice::System},
        {tr("Light"), ThemeChoice::Light},
        {tr("Dark"), ThemeChoice::Dark},
    };
    for (const auto &[text, choice] : themes) {
        theme->addItem(text, int(choice));
        if (choice == themeChoice())
            theme->setCurrentIndex(theme->count() - 1);
    }
    connect(theme, &QComboBox::activated, this,
            [theme](int index) { setThemeChoice(*qApp, ThemeChoice(theme->itemData(index).toInt())); });
    form->addRow(tr("Theme"), theme);

    auto *recent = new QSpinBox(page);
    recent->setObjectName(QStringLiteral("recent"));
    recent->setRange(1, 30);
    recent->setValue(prefs.recentLimit());
    connect(recent, &QSpinBox::valueChanged, this, [](int n) { tools::Preferences::instance().setRecentLimit(n); });
    auto *clear = new QPushButton(tr("Clear the list"), page);
    clear->setObjectName(QStringLiteral("clearRecent"));
    connect(clear, &QPushButton::clicked, this, &SettingsDialog::recentListCleared);
    auto *recentRow = new QHBoxLayout;
    recentRow->addWidget(recent);
    recentRow->addWidget(clear);
    recentRow->addStretch(1);
    form->addRow(tr("Recent projects to remember"), recentRow);
    return page;
}

QWidget *SettingsDialog::canvasPage()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    tools::Preferences &prefs = tools::Preferences::instance();

    auto *zoom = new QSpinBox(page);
    zoom->setObjectName(QStringLiteral("zoomStep"));
    zoom->setRange(5, 100);
    zoom->setSuffix(QStringLiteral(" %"));
    zoom->setValue(prefs.zoomStepPercent());
    connect(zoom, &QSpinBox::valueChanged, this, [](int v) { tools::Preferences::instance().setZoomStepPercent(v); });
    form->addRow(tr("Zoom step"), zoom);

    auto *rotate = new QSpinBox(page);
    rotate->setObjectName(QStringLiteral("rotateStep"));
    rotate->setRange(1, 90);
    rotate->setSuffix(QStringLiteral("°"));
    rotate->setValue(prefs.rotateStep());
    connect(rotate, &QSpinBox::valueChanged, this, [](int v) { tools::Preferences::instance().setRotateStep(v); });
    form->addRow(tr("Rotation step"), rotate);

    auto *outline = new QCheckBox(tr("Show the brush outline instead of the pointer"), page);
    outline->setToolTip(tr("With a drawing tool the pointer is hidden over the canvas, and the brush shows as an "
                           "outline at its size"));
    outline->setObjectName(QStringLiteral("brushOutline"));
    outline->setChecked(prefs.brushOutline());
    connect(outline, &QCheckBox::toggled, this, [](bool on) { tools::Preferences::instance().setBrushOutline(on); });
    form->addRow(tr("Brush"), outline);
    return page;
}

QWidget *SettingsDialog::projectPage()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    tools::Preferences &prefs = tools::Preferences::instance();
    auto *note = new QLabel(tr("What File > New makes (a project that is open keeps its own)."), page);
    note->setEnabled(false);
    form->addRow(note);

    auto *width = new QSpinBox(page);
    width->setObjectName(QStringLiteral("newWidth"));
    width->setRange(16, 16000);
    width->setSuffix(QStringLiteral(" px"));
    width->setValue(prefs.newWidth());
    auto *height = new QSpinBox(page);
    height->setObjectName(QStringLiteral("newHeight"));
    height->setRange(16, 16000);
    height->setSuffix(QStringLiteral(" px"));
    height->setValue(prefs.newHeight());
    auto size = [width, height] { tools::Preferences::instance().setNewSize(width->value(), height->value()); };
    connect(width, &QSpinBox::valueChanged, this, size);
    connect(height, &QSpinBox::valueChanged, this, size);
    form->addRow(tr("Width"), width);
    form->addRow(tr("Height"), height);

    auto *fps = new QSpinBox(page);
    fps->setObjectName(QStringLiteral("newFps"));
    fps->setRange(1, 120);
    fps->setSuffix(tr(" fps"));
    fps->setValue(prefs.newFrameRate());
    connect(fps, &QSpinBox::valueChanged, this, [](int v) { tools::Preferences::instance().setNewFrameRate(v); });
    form->addRow(tr("Frame rate"), fps);

    auto *frames = new QSpinBox(page);
    frames->setObjectName(QStringLiteral("newFrames"));
    frames->setRange(1, 5000);
    frames->setSuffix(tr(" frames"));
    frames->setValue(prefs.newFrames());
    connect(frames, &QSpinBox::valueChanged, this, [](int v) { tools::Preferences::instance().setNewFrames(v); });
    form->addRow(tr("Length"), frames);

    auto *paper = new QPushButton(page);
    paper->setObjectName(QStringLiteral("paper"));
    auto show = [paper] {
        QPixmap swatch(40, 16);
        swatch.fill(tools::Preferences::instance().paperColor());
        paper->setIcon(QIcon(swatch));
        paper->setText(tools::Preferences::instance().paperColor().name());
    };
    show();
    connect(paper, &QPushButton::clicked, this, [this, show] {
        const QColor c = QColorDialog::getColor(tools::Preferences::instance().paperColor(), this, tr("Paper color"));
        if (c.isValid()) {
            tools::Preferences::instance().setPaperColor(c);
            show();
        }
    });
    form->addRow(tr("Paper"), paper);
    return page;
}

QWidget *SettingsDialog::inputPage()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    auto *pressure = new QSpinBox(page);
    pressure->setObjectName(QStringLiteral("mousePressure"));
    pressure->setRange(10, 100);
    pressure->setSuffix(QStringLiteral(" %"));
    pressure->setValue(tools::Preferences::instance().mousePressurePercent());
    pressure->setToolTip(tr("A mouse has no pressure: this is the one it draws with (a pen sends its own). Its "
                            "buttons and keys are set in the tablet's driver."));
    connect(pressure, &QSpinBox::valueChanged, this,
            [](int v) { tools::Preferences::instance().setMousePressurePercent(v); });
    form->addRow(tr("Pressure of a mouse"), pressure);
    return page;
}

QWidget *SettingsDialog::advancedPage()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    tools::Preferences &prefs = tools::Preferences::instance();

    auto *steps = new QSpinBox(page);
    steps->setObjectName(QStringLiteral("undoSteps"));
    steps->setRange(10, 5000);
    steps->setValue(prefs.undoSteps());
    steps->setToolTip(tr("How many steps of a project can be undone (the oldest go first)"));
    connect(steps, &QSpinBox::valueChanged, this, [](int v) { tools::Preferences::instance().setUndoSteps(v); });
    form->addRow(tr("Undo steps"), steps);

    auto *folder = new QPushButton(tr("Open the data folder"), page);
    folder->setObjectName(QStringLiteral("dataFolder"));
    folder->setToolTip(tr("Where the imported brushes are kept"));
    connect(folder, &QPushButton::clicked, this, [] {
        const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    form->addRow(tr("Data"), folder);
    return page;
}

QWidget *SettingsDialog::shortcutsPage()
{
    auto *page = new QWidget;
    m_filter = new QLineEdit(page);
    m_filter->setPlaceholderText(tr("Search commands"));
    m_filter->setClearButtonEnabled(true);

    m_commands = new QTreeWidget(page);
    m_commands->setObjectName(QStringLiteral("commands"));
    m_commands->setColumnCount(2);
    m_commands->setHeaderLabels({tr("Command"), tr("Keys")});
    m_commands->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_commands->setRootIsDecorated(true);
    fillShortcuts();

    m_keyEdit = new QKeySequenceEdit(page);
    m_keyEdit->setObjectName(QStringLiteral("keyEdit"));
    m_keyEdit->setMaximumSequenceLength(1); // one key press, or a chord: one sequence
    auto *assign = new QPushButton(tr("Assign"), page);
    assign->setObjectName(QStringLiteral("assign"));
    auto *none = new QPushButton(tr("No key"), page);
    none->setObjectName(QStringLiteral("noKey"));
    auto *reset = new QPushButton(tr("Default"), page);
    reset->setObjectName(QStringLiteral("defaultKey"));
    auto *resetAll = new QPushButton(tr("Reset all shortcuts"), page);
    resetAll->setObjectName(QStringLiteral("resetShortcuts"));

    auto *editRow = new QHBoxLayout;
    editRow->addWidget(new QLabel(tr("New key:"), page));
    editRow->addWidget(m_keyEdit, 1);
    editRow->addWidget(assign);
    editRow->addWidget(none);
    editRow->addWidget(reset);

    auto *fixed = new QLabel(tr("Fixed keys: Space or Tab held: pan the view (with Ctrl: zoom, with Shift: rotate) · "
                                "Shift held: a straight line from the last point · Alt held: pick a color · "
                                "Enter and Esc: apply or drop what a tool has open"),
                             page);
    fixed->setWordWrap(true);
    fixed->setEnabled(false);

    auto *layout = new QVBoxLayout(page);
    layout->addWidget(m_filter);
    layout->addWidget(m_commands, 1);
    layout->addLayout(editRow);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(fixed, 1);
    bottom->addWidget(resetAll);
    layout->addLayout(bottom);

    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int i = 0; i < m_commands->topLevelItemCount(); ++i) {
            QTreeWidgetItem *group = m_commands->topLevelItem(i);
            int visible = 0;
            for (int j = 0; j < group->childCount(); ++j) {
                QTreeWidgetItem *item = group->child(j);
                const bool match = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive)
                                   || item->text(1).contains(text, Qt::CaseInsensitive);
                item->setHidden(!match);
                visible += match;
            }
            group->setHidden(visible == 0);
        }
    });
    connect(m_commands, &QTreeWidget::currentItemChanged, this, [this] {
        if (!selectedCommand().isEmpty())
            m_keyEdit->clear();
    });
    connect(assign, &QPushButton::clicked, this, &SettingsDialog::assignKey);
    connect(none, &QPushButton::clicked, this, [this] {
        if (const QString id = selectedCommand(); !id.isEmpty())
            Shortcuts::instance().setKeys(id, {});
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        if (const QString id = selectedCommand(); !id.isEmpty())
            Shortcuts::instance().reset(id);
    });
    connect(resetAll, &QPushButton::clicked, this, [] { Shortcuts::instance().resetAll(); });
    connect(&Shortcuts::instance(), &Shortcuts::changed, m_commands, [this] {
        for (int i = 0; i < m_commands->topLevelItemCount(); ++i)
            for (int j = 0; j < m_commands->topLevelItem(i)->childCount(); ++j)
                refreshShortcutRow(m_commands->topLevelItem(i)->child(j));
    });
    return page;
}

void SettingsDialog::fillShortcuts()
{
    m_commands->clear();
    QHash<QString, QTreeWidgetItem *> groups;
    for (const ShortcutCommand &c : Shortcuts::instance().commands()) {
        QTreeWidgetItem *&group = groups[c.category];
        if (!group) {
            group = new QTreeWidgetItem(m_commands, {c.category});
            QFont bold = group->font(0);
            bold.setBold(true);
            group->setFont(0, bold);
            group->setFlags(Qt::ItemIsEnabled);
        }
        auto *item = new QTreeWidgetItem(group, {c.text, QString()});
        item->setData(0, kCommandRole, c.id);
        refreshShortcutRow(item);
    }
    m_commands->expandAll();
}

void SettingsDialog::refreshShortcutRow(QTreeWidgetItem *item)
{
    const QString id = item->data(0, kCommandRole).toString();
    item->setText(1, keysText(id));
    QFont font = item->font(1);
    font.setBold(Shortcuts::instance().isCustom(id)); // the ones changed stand out
    item->setFont(1, font);
}

QString SettingsDialog::selectedCommand() const
{
    const QTreeWidgetItem *item = m_commands ? m_commands->currentItem() : nullptr;
    return item ? item->data(0, kCommandRole).toString() : QString();
}

void SettingsDialog::assignKey()
{
    const QString id = selectedCommand();
    const QKeySequence key = m_keyEdit->keySequence();
    if (id.isEmpty() || key.isEmpty())
        return;
    Shortcuts &keymap = Shortcuts::instance();
    const QString other = keymap.conflictFor(key, id);
    if (!other.isEmpty()) {
        const QString name = keymap.command(other)->text;
        if (QMessageBox::question(this, tr("Shortcut in use"),
                                  tr("%1 is already the key of “%2”. Give it to this command instead?")
                                      .arg(key.toString(QKeySequence::NativeText), name))
            != QMessageBox::Yes)
            return;
    }
    keymap.assign(id, key);
    m_keyEdit->clear();
}

} // namespace mothion::ui

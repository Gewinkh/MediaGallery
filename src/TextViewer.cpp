#include "TextViewer.h"
#include "AppSettings.h"
#include "Strings.h"
#include "Icons.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QShortcut>
#include <QKeySequence>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>

TextViewer::TextViewer(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // ── Toolbar ───────────────────────────────────────────────────────────────
    m_toolbar = new QWidget(this);
    m_toolbar->setFixedHeight(44);
    auto* tbLay = new QHBoxLayout(m_toolbar);
    tbLay->setContentsMargins(10, 6, 10, 6);
    tbLay->setSpacing(10);

    m_saveBtn = new QToolButton(m_toolbar);
    m_saveBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    connect(m_saveBtn, &QToolButton::clicked, this, &TextViewer::saveFile);
    tbLay->addWidget(m_saveBtn);

    m_pathLabel = new QLabel(m_toolbar);
    m_pathLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    tbLay->addWidget(m_pathLabel, 1);

    lay->addWidget(m_toolbar);

    // ── Editor ────────────────────────────────────────────────────────────────
    m_editor = new QPlainTextEdit(this);
    m_editor->setReadOnly(false);
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_editor->setTabChangesFocus(false);
    {
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPointSize(11);
        m_editor->setFont(mono);
    }
    connect(m_editor, &QPlainTextEdit::textChanged, this, &TextViewer::onTextChanged);
    lay->addWidget(m_editor, 1);

    // ── Ctrl+S shortcut (widget-scoped) ───────────────────────────────────────
    auto* saveSc = new QShortcut(QKeySequence::Save, this);
    saveSc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveSc, &QShortcut::activated, this, &TextViewer::saveFile);

    // ── Auto-save ─────────────────────────────────────────────────────────────
    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setSingleShot(false);
    connect(m_autoSaveTimer, &QTimer::timeout, this, &TextViewer::onAutoSaveTick);
    connect(&AppSettings::instance(), &AppSettings::autoSaveSettingsChanged,
            this, &TextViewer::reconfigureAutoSave);
    reconfigureAutoSave();

    applyTheme();
    retranslate();
    setDirty(false);
}

bool TextViewer::loadFile(const QString& path) {
    // Confirm before discarding unsaved changes
    if (m_dirty) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(Strings::get(StringKey::EditorUnsavedTitle));
        box.setText(Strings::get(StringKey::EditorUnsavedMsg));
        box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Save);
        const int res = box.exec();
        if (res == QMessageBox::Cancel)
            return false;
        if (res == QMessageBox::Save)
            saveFile();
        // Discard → fall through and load the new file
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this,
            Strings::get(StringKey::EditorUnsavedTitle),
            Strings::get(StringKey::EditorLoadError));
        return false;
    }
    QTextStream in(&file);
    const QString content = in.readAll();
    file.close();

    m_loading = true;
    m_currentPath = path;
    m_editor->setPlainText(content);
    m_loading = false;

    setDirty(false);
    updatePathLabel();
    return true;
}

void TextViewer::saveFile() {
    if (m_currentPath.isEmpty()) return;
    if (!writeToDisk()) {
        QMessageBox::warning(this,
            Strings::get(StringKey::EditorUnsavedTitle),
            Strings::get(StringKey::EditorLoadError));
        return;
    }
    setDirty(false);
}

bool TextViewer::writeToDisk() {
    if (m_currentPath.isEmpty()) return false;
    QFile file(m_currentPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    QTextStream out(&file);
    out << m_editor->toPlainText();
    out.flush();
    file.close();
    return true;
}

void TextViewer::onTextChanged() {
    if (m_loading) return;
    setDirty(true);
}

void TextViewer::onAutoSaveTick() {
    if (!AppSettings::instance().autoSaveEnabled()) return;
    if (!m_dirty || m_currentPath.isEmpty()) return;
    if (writeToDisk())
        setDirty(false);
}

void TextViewer::reconfigureAutoSave() {
    const auto& s = AppSettings::instance();
    if (s.autoSaveEnabled()) {
        m_autoSaveTimer->setInterval(qMax(5, s.autoSaveIntervalSeconds()) * 1000);
        m_autoSaveTimer->start();
    } else {
        m_autoSaveTimer->stop();
    }
}

void TextViewer::setDirty(bool dirty) {
    if (m_dirty == dirty) {
        updatePathLabel();
        m_saveBtn->setEnabled(dirty);
        return;
    }
    m_dirty = dirty;
    m_saveBtn->setEnabled(dirty);
    updatePathLabel();
    emit unsavedChangesChanged(m_dirty);
}

void TextViewer::updatePathLabel() {
    QString name = m_currentPath.isEmpty()
                       ? QString()
                       : QFileInfo(m_currentPath).fileName();
    if (m_dirty && !name.isEmpty())
        name += QStringLiteral(" *");
    m_pathLabel->setText(name);
    m_pathLabel->setToolTip(m_currentPath);
}

void TextViewer::applyTheme() {
    const ThemeColors t = AppSettings::instance().currentTheme();

    m_toolbar->setStyleSheet(QString(
        "background: %1; border-bottom: 1px solid %2;")
        .arg(t.pdfToolbarBg.name(), t.border.name()));

    m_saveBtn->setStyleSheet(
        "QToolButton { background: rgba(0,180,160,0.22); border: 1px solid rgba(0,180,160,0.5);"
        "border-radius: 8px; color: #00e8d0; font-size: 13px; font-weight: 600; padding: 4px 14px; }"
        "QToolButton:hover { background: rgba(0,200,180,0.4); }"
        "QToolButton:disabled { background: rgba(255,255,255,0.05); color: #5a6a68;"
        "border-color: rgba(255,255,255,0.1); }");

    m_pathLabel->setStyleSheet(QString(
        "color: %1; font-size: 13px; font-weight: 600; background: transparent;")
        .arg(t.textPrimary.name()));

    m_editor->setStyleSheet(QString(
        "QPlainTextEdit { background: %1; color: %2; border: none; selection-background-color: %3;"
        "selection-color: #ffffff; padding: 8px; }"
        "QScrollBar:vertical { width: 10px; background: transparent; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,0.2); border-radius: 4px; }"
        "QScrollBar:horizontal { height: 10px; background: transparent; }"
        "QScrollBar::handle:horizontal { background: rgba(255,255,255,0.2); border-radius: 4px; }")
        .arg(t.pdfViewerBg.name(), t.textPrimary.name(), t.accent.name()));
}

void TextViewer::retranslate() {
    m_saveBtn->setText(Strings::get(StringKey::EditorSave));
    m_saveBtn->setToolTip(Strings::get(StringKey::EditorSave) + QStringLiteral(" (Ctrl+S)"));
    updatePathLabel();
}

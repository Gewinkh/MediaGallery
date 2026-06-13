#pragma once
#include <QWidget>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QLabel>
#include <QTimer>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
//  TextViewer – editable plain-text viewer used inside FullscreenView.
//
//  • QPlainTextEdit (editable, plain text)
//  • Toolbar with Save button (+ Ctrl+S shortcut) and a path label that shows an
//    unsaved-changes indicator ("*").
//  • Optional auto-save driven by a QTimer; enabled flag + interval (seconds)
//    are read from AppSettings and kept in sync via autoSaveSettingsChanged().
// ─────────────────────────────────────────────────────────────────────────────
class TextViewer : public QWidget {
    Q_OBJECT
public:
    explicit TextViewer(QWidget* parent = nullptr);

    // Loads `path` into the editor. If there are unsaved changes the user is
    // asked (Save / Discard / Cancel). On Cancel the load is aborted and the
    // method returns false; otherwise it returns true.
    bool loadFile(const QString& path);

    // Writes the current buffer back to disk (interactive: reports errors).
    void saveFile();

    bool hasUnsavedChanges() const { return m_dirty; }

    void applyTheme();
    void retranslate();

signals:
    void unsavedChangesChanged(bool hasChanges);

private slots:
    void onTextChanged();
    void onAutoSaveTick();
    void reconfigureAutoSave();

private:
    QWidget*        m_toolbar;
    QToolButton*    m_saveBtn;
    QLabel*         m_pathLabel;
    QPlainTextEdit* m_editor;
    QTimer*         m_autoSaveTimer;

    QString m_currentPath;
    bool    m_dirty   = false;
    bool    m_loading = false;   // guard: suppress dirty-marking during loadFile()

    void setDirty(bool dirty);
    void updatePathLabel();
    bool writeToDisk();          // silent write; returns success
};

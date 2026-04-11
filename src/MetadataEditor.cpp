#include "MetadataEditor.h"
#include "Strings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QTimer>
#include <QKeyEvent>

MetadataEditor::MetadataEditor(const MediaItem& item,
                               const QDateTime& suggestedDateTime,
                               QWidget* parent,
                               bool focusDaySection)
    : QDialog(parent)
{
    setWindowTitle(Strings::get(StringKey::MetaTitle));
    setFixedSize(360, suggestedDateTime.isValid() ? 220 : 180);
    setStyleSheet(
        "QDialog { background: #111e24; }"
        "QLabel { color: #c8dbd5; }"
        "QDateTimeEdit { background: rgba(255,255,255,0.07); border: 1px solid rgba(40,60,70,0.9);"
        "border-radius: 6px; color: #dcebd8; padding: 4px 8px; }"
        "QPushButton { background: rgba(0,180,160,0.2); border: 1px solid rgba(0,180,160,0.4);"
        "border-radius: 6px; color: #00c8b4; padding: 5px 14px; }"
        "QPushButton:hover { background: rgba(0,180,160,0.4); }");

    auto* lay = new QVBoxLayout(this);
    lay->setSpacing(10);
    lay->setContentsMargins(20, 16, 20, 16);

    // ── Dateiname ─────────────────────────────────────────────────────────────
    auto* infoLbl = new QLabel(Strings::get(StringKey::MetaFile) + item.fileName(), this);
    infoLbl->setStyleSheet("color: rgba(180,200,195,0.7); font-size: 11px;");
    infoLbl->setWordWrap(true);
    lay->addWidget(infoLbl);

    // ── Vorlage-Hinweis (nur wenn ein letztes Datum existiert) ────────────────
    if (suggestedDateTime.isValid()) {
        auto* hintLbl = new QLabel(
            tr("Vorlage: ") + suggestedDateTime.toString("dd.MM.yyyy HH:mm:ss"), this);
        hintLbl->setStyleSheet(
            "color: rgba(0,220,180,0.75); font-size: 11px;"
            "background: rgba(0,180,160,0.08); border: 1px solid rgba(0,180,160,0.25);"
            "border-radius: 5px; padding: 4px 8px;");
        lay->addWidget(hintLbl);
    }

    // ── Datum-Editor ──────────────────────────────────────────────────────────
    auto* form = new QFormLayout();
    form->setSpacing(8);
    m_dateEdit = new QDateTimeEdit(this);
    m_dateEdit->setDisplayFormat("dd.MM.yyyy HH:mm:ss");
    m_dateEdit->setCalendarPopup(true);
    m_dateEdit->setDateTime(suggestedDateTime.isValid() ? suggestedDateTime : item.dateTime);
    // EventFilter auf dem DateEdit installieren, damit 'D' VOR der Widget-Verarbeitung abgefangen wird
    m_dateEdit->installEventFilter(this);
    form->addRow(Strings::get(StringKey::MetaDate), m_dateEdit);
    lay->addLayout(form);

    if (focusDaySection) {
        QTimer::singleShot(0, this, [this]() {
            m_dateEdit->setFocus();
            m_dateEdit->setCurrentSection(QDateTimeEdit::DaySection);
        });
    }

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto* btnLay = new QHBoxLayout();
    m_clearBtn = new QPushButton(Strings::get(StringKey::MetaReset), this);
    m_clearBtn->setStyleSheet(
        "QPushButton { background: rgba(180,60,60,0.2); border-color: rgba(180,60,60,0.4); color: #e07878; }"
        "QPushButton:hover { background: rgba(180,60,60,0.4); }");
    m_okBtn     = new QPushButton(Strings::get(StringKey::MetaOk), this);
    m_cancelBtn = new QPushButton(Strings::get(StringKey::MetaCancel), this);

    connect(m_okBtn,     &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_clearBtn,  &QPushButton::clicked, this, [this]() { done(2); });

    m_okBtn->setDefault(true);

    btnLay->addWidget(m_clearBtn);
    btnLay->addStretch();
    btnLay->addWidget(m_cancelBtn);
    btnLay->addWidget(m_okBtn);
    lay->addLayout(btnLay);
}

bool MetadataEditor::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == m_dateEdit && ev->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        // 'D' abfangen bevor QDateTimeEdit es als Buchstabeneingabe verarbeitet
        if (ke->key() == Qt::Key_D) {
            accept();
            return true;  // Ereignis nicht weiterleiten
        }
    }
    return QDialog::eventFilter(obj, ev);
}

QDateTime MetadataEditor::selectedDateTime() const { return m_dateEdit->dateTime(); }

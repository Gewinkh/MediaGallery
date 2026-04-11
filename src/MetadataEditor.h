#pragma once
#include <QDialog>
#include <QDateTimeEdit>
#include <QPushButton>
#include <QDateTime>
#include "MediaItem.h"

class MetadataEditor : public QDialog {
    Q_OBJECT
public:
    explicit MetadataEditor(const MediaItem& item,
                            const QDateTime& suggestedDateTime = QDateTime(),
                            QWidget* parent = nullptr,
                            bool focusDaySection = false);

    QDateTime selectedDateTime() const;
    bool useCustomDate() const { return true; }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    QDateTimeEdit* m_dateEdit;
    QPushButton*   m_okBtn;
    QPushButton*   m_cancelBtn;
    QPushButton*   m_clearBtn;
};

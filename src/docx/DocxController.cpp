#include "docx/DocxController.h"
#include "core/ISettings.h"

DocxController::DocxController(ISettings& settings, QObject* parent)
    : QObject(parent), m_settings(settings) {}

bool DocxController::saveDirect() const {
    return m_settings.docxSaveDirect();
}

void DocxController::setSaveDirect(bool v) {
    if (m_settings.docxSaveDirect() == v)
        return;
    m_settings.setDocxSaveDirect(v);
    emit saveDirectChanged();
}

// ── Was beim Weg „DOCX -> PDF" gilt ──────────────────────────────────────────
int DocxController::pdfPaddingMm() const { return m_settings.docxPdfPaddingMm(); }
void DocxController::setPdfPaddingMm(int mm) {
    if (m_settings.docxPdfPaddingMm() == mm) return;
    m_settings.setDocxPdfPaddingMm(mm);
    emit pdfOptionsChanged();
}
int DocxController::pdfPageNumberPos() const { return m_settings.docxPdfPageNumberPos(); }
void DocxController::setPdfPageNumberPos(int pos) {
    if (m_settings.docxPdfPageNumberPos() == pos) return;
    m_settings.setDocxPdfPageNumberPos(pos);
    emit pdfOptionsChanged();
}
int DocxController::pdfPageNumberStyle() const { return m_settings.docxPdfPageNumberStyle(); }
void DocxController::setPdfPageNumberStyle(int style) {
    if (m_settings.docxPdfPageNumberStyle() == style) return;
    m_settings.setDocxPdfPageNumberStyle(style);
    emit pdfOptionsChanged();
}

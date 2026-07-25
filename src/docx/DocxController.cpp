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

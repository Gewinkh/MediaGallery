#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// PdfViewer.h  –  extended with unified Audio/Video annotation support
// ══════════════════════════════════════════════════════════════════════════════
#include <QWidget>
#include <QPdfDocument>
#include <QPdfView>
#include <QToolButton>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QScrollBar>
#include <QLineEdit>
#include <QListWidget>
#include <QSplitter>
#include <QWheelEvent>
#include <QRectF>
#include <QVector>
#include <QDialog>

#include "PdfMediaHandler.h"
#include "MediaOverlayWidget.h"

/**
 * PdfViewer – cross-platform PDF viewer based on Qt6::Pdf.
 *
 * New in this revision:
 *  - Unified Audio + Video annotation support via PdfMediaHandler
 *  - MediaOverlayWidget renders clickable badges over QPdfView viewport
 *  - Video playback via QVideoWidget in a floating QDialog
 *  - Audio playback via bottom panel
 *  - Sidecar file fallback (audio only)
 */
class PdfViewer : public QWidget {
    Q_OBJECT
public:
    explicit PdfViewer(QWidget* parent = nullptr);
    ~PdfViewer() override;

    void loadFile(const QString& path);
    void closeDocument();

signals:
    void pageChanged(int page, int total);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

private slots:
    void goToPrevPage();
    void goToNextPage();
    void onPageSpinChanged(int page);
    void onZoomComboChanged(int index);
    void zoomIn();
    void zoomOut();
    void fitPage();
    void fitWidth();
    void toggleScrollMode();
    void onDocumentStatusChanged(QPdfDocument::Status status);
    void onNavigatorPageChanged(int page);
    void onThumbnailClicked(int row);

    // Audio playback
    void onPlayAudioClicked();
    void onAudioPlayerStateChanged();
    void onAudioPositionChanged(qint64 pos);
    void onAudioDurationChanged(qint64 dur);

    // Video playback
    void onVideoPositionChanged(qint64 pos);
    void onVideoDurationChanged(qint64 dur);
    void onVideoPlayerStateChanged();

    // Media annotation click (from overlay)
    void onAnnotationClicked(int index);

private:
    // Document & view
    QPdfDocument* m_doc      = nullptr;
    QPdfView*     m_view     = nullptr;
    QSplitter*    m_splitter = nullptr;

    // Overlay (transparent widget on top of QPdfView viewport)
    MediaOverlayWidget* m_overlay = nullptr;

    // Media annotation scanner
    PdfMediaHandler* m_mediaHandler = nullptr;

    // Thumbnail panel
    QListWidget* m_thumbPanel = nullptr;

    // Toolbar
    QWidget*     m_toolbar         = nullptr;
    QToolButton* m_prevBtn         = nullptr;
    QToolButton* m_nextBtn         = nullptr;
    QSpinBox*    m_pageSpin        = nullptr;
    QLabel*      m_totalPagesLabel = nullptr;
    QComboBox*   m_zoomCombo       = nullptr;
    QToolButton* m_zoomInBtn       = nullptr;
    QToolButton* m_zoomOutBtn      = nullptr;
    QToolButton* m_fitPageBtn      = nullptr;
    QToolButton* m_fitWidthBtn     = nullptr;
    QToolButton* m_scrollModeBtn   = nullptr;
    QToolButton* m_thumbToggleBtn  = nullptr;
    QLabel*      m_docTitleLabel   = nullptr;

    // Audio bottom panel
    QWidget*     m_audioPanel      = nullptr;
    QLabel*      m_audioLabel      = nullptr;
    QToolButton* m_audioPlayBtn    = nullptr;
    QSlider*     m_audioSeekSlider = nullptr;
    QLabel*      m_audioTimeLabel  = nullptr;
    QToolButton* m_audioMuteBtn    = nullptr;
    QSlider*     m_audioVolSlider  = nullptr;

    QMediaPlayer* m_audioPlayer  = nullptr;
    QAudioOutput* m_audioOutput  = nullptr;
    bool          m_audioSeeking = false;

    // Video floating dialog
    QDialog*      m_videoDialog      = nullptr;
    QVideoWidget* m_videoWidget      = nullptr;
    QMediaPlayer* m_videoPlayer      = nullptr;
    QAudioOutput* m_videoAudio       = nullptr;
    QLabel*       m_videoLabel       = nullptr;
    QToolButton*  m_videoPlayBtn     = nullptr;
    QSlider*      m_videoSeekSlider  = nullptr;
    QLabel*       m_videoTimeLabel   = nullptr;
    bool          m_videoSeeking     = false;

    // State
    QString m_currentPath;
    double  m_zoomFactor     = 1.0;
    bool    m_continuousMode = true;
    bool    m_updatingThumbs = false;
    int     m_currentPage    = 0;

    // Setup helpers
    void setupUi();
    void setupToolbar();
    void setupAudioPanel();
    void setupVideoDialog();
    void buildThumbnails();
    void syncThumbnailSelection(int page0based);
    void syncPageSpinner(int page0based);
    void updatePageControls();
    void repositionOverlay();

    // Zoom helpers
    void updateZoomCombo();
    void applyZoom(double factor);
    void handleWheelZoom(QWheelEvent* we);

    // Media helpers
    void playAudioAnnotation(const MediaAnnotation& ann);
    void playVideoAnnotation(const MediaAnnotation& ann);
    void loadAudioSource(const QString& path, const QString& label);
    void extractAndPrepareAudio();

    static QString formatTime(qint64 ms);
    static QString toolButtonStyle();
};

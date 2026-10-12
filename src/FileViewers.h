#pragma once
#include <QImage>
#include <QString>
#include <QWidget>

class QLabel;
class QScrollArea;
class QToolButton;

// Which kind of tab a file opens in.
namespace FileKinds {
enum Kind { Text, Image, Pdf, External };
Kind classify(const QString& path);
bool pdfSupported();
QString imageFormatsHint();
}

// Image viewer: PNG, JPEG, GIF, BMP, ICO, ICNS, WebP, TIFF, SVG, ... (whatever Qt's image plugins provide).
class ImageViewer : public QWidget {
    Q_OBJECT
public:
    explicit ImageViewer(const QString& path, QWidget* parent = nullptr);
    bool ok() const { return !m_image.isNull(); }
    QString errorText() const { return m_error; }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void load();
    void applyZoom();
    void setZoom(double z);
    void fit();

    QString m_path, m_error, m_details;
    QImage m_image;
    QScrollArea* m_scroll = nullptr;
    QLabel* m_label = nullptr;
    QLabel* m_info = nullptr;
    double m_zoom = 1.0;
    bool m_fit = true;
    bool m_dragging = false;
    QPoint m_dragStart;
};

// PDF viewer (Qt PDF). Only compiled in when the Qt PDF module is available.
#ifdef CRUCIBLE_HAVE_PDF
class QPdfDocument;
class QPdfView;
class PdfViewer : public QWidget {
    Q_OBJECT
public:
    explicit PdfViewer(const QString& path, QWidget* parent = nullptr);
    bool ok() const { return m_ok; }
    QString errorText() const { return m_error; }

private:
    void updatePageLabel();

    QPdfDocument* m_doc = nullptr;
    QPdfView* m_view = nullptr;
    QLabel* m_page = nullptr;
    QString m_error;
    bool m_ok = false;
};
#endif

// Page for files CrucibleCode cannot show itself (Office documents, archives, media, ...).
class ExternalFileView : public QWidget {
    Q_OBJECT
public:
    ExternalFileView(const QString& path, const QString& note, bool offerText, QWidget* parent = nullptr);

signals:
    void openAsTextRequested(const QString& path);
};

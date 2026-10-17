#include "FileViewers.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#ifdef CRUCIBLE_HAVE_PDF
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#endif

namespace FileKinds {

static const QStringList kImageExt = {"png", "jpg", "jpeg", "jpe", "gif", "bmp", "ico", "cur", "icns", "webp",
                                      "tif", "tiff", "svg", "svgz", "tga", "avif", "heic", "jfif", "pbm", "pgm", "ppm"};
static const QStringList kExternalExt = {
    "doc", "docx", "xls", "xlsx", "ppt", "pptx", "odt", "ods", "odp", "rtf", "pages", "numbers", "key", "epub",
    "zip", "7z", "rar", "tar", "gz", "tgz", "bz2", "xz", "zst", "iso", "dmg", "msi", "pkg", "deb", "rpm", "apk",
    "exe", "dll", "so", "dylib", "a", "lib", "o", "obj", "pdb", "class", "jar", "bin", "dat", "db", "sqlite",
    "mp3", "wav", "flac", "ogg", "m4a", "mp4", "mkv", "avi", "mov", "webm", "ttf", "otf", "woff", "woff2", "psd"};

bool pdfSupported()
{
#ifdef CRUCIBLE_HAVE_PDF
    return true;
#else
    return false;
#endif
}

Kind classify(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (kImageExt.contains(ext)) {
        const QList<QByteArray> supported = QImageReader::supportedImageFormats();
        const QByteArray e = (ext == QLatin1String("jpg") || ext == QLatin1String("jpe") || ext == QLatin1String("jfif"))
                                 ? QByteArray("jpeg") : ext.toLatin1();
        if (supported.contains(e) || (e == "jpeg" && supported.contains("jpg")) || (e == "tif" && supported.contains("tiff")))
            return Image;
        return External;   // known image type but this Qt build lacks the plugin
    }
    if (ext == QLatin1String("pdf")) return pdfSupported() ? Pdf : External;
    if (kExternalExt.contains(ext)) return External;
    return Text;
}

QString imageFormatsHint()
{
    QStringList names;
    for (const QByteArray& f : QImageReader::supportedImageFormats()) names << QString::fromLatin1(f);
    names.sort();
    return names.join(QStringLiteral(", "));
}

} // namespace FileKinds

// ---------------------------------------------------------------------------------------
//  ImageViewer
// ---------------------------------------------------------------------------------------

ImageViewer::ImageViewer(const QString& path, QWidget* parent) : QWidget(parent), m_path(path)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* bar = new QWidget;
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(6, 3, 6, 3);
    const auto button = [&](const QString& text, const QString& tip) {
        auto* b = new QToolButton;
        b->setText(text);
        b->setToolTip(tip);
        bl->addWidget(b);
        return b;
    };
    QToolButton* zin = button(QStringLiteral("+"), tr("Zoom in"));
    QToolButton* zout = button(QStringLiteral("-"), tr("Zoom out"));
    QToolButton* fitBtn = button(tr("Fit"), tr("Fit to window"));
    QToolButton* actual = button(QStringLiteral("100%"), tr("Actual size"));
    m_info = new QLabel;
    bl->addSpacing(12);
    bl->addWidget(m_info, 1);
    lay->addWidget(bar);

    m_scroll = new QScrollArea;
    m_scroll->setAlignment(Qt::AlignCenter);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_label = new QLabel;
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setScaledContents(true);
    m_scroll->setWidget(m_label);
    m_scroll->setWidgetResizable(false);
    m_scroll->viewport()->installEventFilter(this);
    m_scroll->viewport()->setCursor(Qt::OpenHandCursor);
    lay->addWidget(m_scroll, 1);

    connect(zin, &QToolButton::clicked, this, [this] { m_fit = false; setZoom(m_zoom * 1.25); });
    connect(zout, &QToolButton::clicked, this, [this] { m_fit = false; setZoom(m_zoom / 1.25); });
    connect(fitBtn, &QToolButton::clicked, this, [this] { m_fit = true; fit(); });
    connect(actual, &QToolButton::clicked, this, [this] { m_fit = false; setZoom(1.0); });

    load();
}

void ImageViewer::load()
{
    QImageReader reader(m_path);
    reader.setAutoTransform(true);
    const int count = reader.imageCount();
    QStringList sizes;
    QImage best;

    const QString sfx = QFileInfo(m_path).suffix().toLower();
    const bool multi = sfx == QLatin1String("ico") || sfx == QLatin1String("cur") || sfx == QLatin1String("icns")
                    || sfx == QLatin1String("tif") || sfx == QLatin1String("tiff");
    if (count > 1 && multi) {            // icons / multi-page TIFF: show the largest image
        for (int i = 0; i < count; ++i) {
            QImageReader r(m_path);
            r.setAutoTransform(true);
            if (!r.jumpToImage(i)) continue;
            const QImage img = r.read();
            if (img.isNull()) continue;
            sizes << QStringLiteral("%1x%2").arg(img.width()).arg(img.height());
            if (img.width() * img.height() > best.width() * best.height()) best = img;
        }
    } else {
        const QSize natural = reader.size();
        if (QFileInfo(m_path).suffix().startsWith(QLatin1String("svg"), Qt::CaseInsensitive) && natural.isValid()) {
            // vector image: rasterise large enough to stay sharp when zoomed
            const double k = qMax(1.0, 1024.0 / qMax(1, qMax(natural.width(), natural.height())));
            reader.setScaledSize(QSize(int(natural.width() * k), int(natural.height() * k)));
        }
        best = reader.read();
        if (best.isNull()) m_error = reader.errorString();
    }

    if (best.isNull()) {
        m_info->setText(m_error.isEmpty() ? tr("Could not read this image.") : m_error);
        return;
    }
    const QFileInfo fi(m_path);
    m_imgSize = best.size();
    QString info = tr("%1 x %2 px   -   %3 KB").arg(m_imgSize.width()).arg(m_imgSize.height()).arg(fi.size() / 1024.0, 0, 'f', 1);
    if (!sizes.isEmpty()) info += tr("   -   %n image(s): %1", nullptr, int(sizes.size())).arg(sizes.join(QStringLiteral(", ")));
    m_info->setText(info);
    m_label->setPixmap(QPixmap::fromImage(std::move(best)));
    fit();
}

void ImageViewer::fit()
{
    if (!m_imgSize.isValid()) return;
    const QSize avail = m_scroll->viewport()->size() - QSize(8, 8);
    double z = qMin(double(avail.width()) / m_imgSize.width(), double(avail.height()) / m_imgSize.height());
    z = qMin(z, m_imgSize.width() < 128 && m_imgSize.height() < 128 ? 8.0 : 1.0);   // upscale tiny icons, never big images
    setZoom(qMax(0.02, z));
}

void ImageViewer::setZoom(double z)
{
    m_zoom = qBound(0.02, z, 32.0);
    applyZoom();
}

void ImageViewer::applyZoom()
{
    if (!m_imgSize.isValid()) return;
    m_label->setFixedSize(qMax(1, int(m_imgSize.width() * m_zoom)), qMax(1, int(m_imgSize.height() * m_zoom)));
    m_scroll->viewport()->setCursor(Qt::OpenHandCursor);
}

bool ImageViewer::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_scroll->viewport()) {
        switch (ev->type()) {
        case QEvent::Resize:
            if (m_fit) fit();
            break;
        case QEvent::Wheel: {
            auto* w = static_cast<QWheelEvent*>(ev);
            if (w->modifiers() & Qt::ControlModifier) {
                m_fit = false;
                setZoom(m_zoom * (w->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15));
                return true;
            }
            break;
        }
        case QEvent::MouseButtonPress: {
            auto* m = static_cast<QMouseEvent*>(ev);
            if (m->button() == Qt::LeftButton) {
                m_dragging = true;
                m_dragStart = m->pos();
                m_scroll->viewport()->setCursor(Qt::ClosedHandCursor);
                return true;
            }
            break;
        }
        case QEvent::MouseMove:
            if (m_dragging) {
                const QPoint d = static_cast<QMouseEvent*>(ev)->pos() - m_dragStart;
                m_dragStart = static_cast<QMouseEvent*>(ev)->pos();
                m_scroll->horizontalScrollBar()->setValue(m_scroll->horizontalScrollBar()->value() - d.x());
                m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->value() - d.y());
                return true;
            }
            break;
        case QEvent::MouseButtonRelease:
            if (m_dragging) {
                m_dragging = false;
                m_scroll->viewport()->setCursor(Qt::OpenHandCursor);
                return true;
            }
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(obj, ev);
}

// ---------------------------------------------------------------------------------------
//  PdfViewer
// ---------------------------------------------------------------------------------------

#ifdef CRUCIBLE_HAVE_PDF
PdfViewer::PdfViewer(const QString& path, QWidget* parent) : QWidget(parent)
{
    m_doc = new QPdfDocument(this);
    const QPdfDocument::Error err = m_doc->load(path);
    if (err != QPdfDocument::Error::None) {
        m_error = err == QPdfDocument::Error::IncorrectPassword ? tr("This PDF is password protected.")
                                                                : tr("Could not open this PDF (error %1).").arg(int(err));
        return;
    }
    m_ok = true;

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* bar = new QWidget;
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(6, 3, 6, 3);
    const auto button = [&](const QString& text, const QString& tip) {
        auto* b = new QToolButton;
        b->setText(text);
        b->setToolTip(tip);
        bl->addWidget(b);
        return b;
    };
    QToolButton* prev = button(QStringLiteral("<"), tr("Previous page"));
    QToolButton* next = button(QStringLiteral(">"), tr("Next page"));
    m_page = new QLabel;
    bl->addWidget(m_page);
    bl->addSpacing(12);
    QToolButton* zin = button(QStringLiteral("+"), tr("Zoom in"));
    QToolButton* zout = button(QStringLiteral("-"), tr("Zoom out"));
    QToolButton* fitW = button(tr("Fit width"), tr("Fit width"));
    QToolButton* fitP = button(tr("Fit page"), tr("Fit page"));
    bl->addStretch(1);
    lay->addWidget(bar);

    m_view = new QPdfView;
    m_view->setDocument(m_doc);
    m_view->setPageMode(QPdfView::PageMode::MultiPage);
    m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    lay->addWidget(m_view, 1);

    QPdfPageNavigator* nav = m_view->pageNavigator();
    connect(nav, &QPdfPageNavigator::currentPageChanged, this, [this] { updatePageLabel(); });
    connect(prev, &QToolButton::clicked, this, [this, nav] {
        if (nav->currentPage() > 0) nav->jump(nav->currentPage() - 1, QPointF(), nav->currentZoom());
    });
    connect(next, &QToolButton::clicked, this, [this, nav] {
        if (nav->currentPage() < m_doc->pageCount() - 1) nav->jump(nav->currentPage() + 1, QPointF(), nav->currentZoom());
    });
    connect(zin, &QToolButton::clicked, this, [this] {
        m_view->setZoomMode(QPdfView::ZoomMode::Custom);
        m_view->setZoomFactor(m_view->zoomFactor() * 1.2);
    });
    connect(zout, &QToolButton::clicked, this, [this] {
        m_view->setZoomMode(QPdfView::ZoomMode::Custom);
        m_view->setZoomFactor(m_view->zoomFactor() / 1.2);
    });
    connect(fitW, &QToolButton::clicked, this, [this] { m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth); });
    connect(fitP, &QToolButton::clicked, this, [this] { m_view->setZoomMode(QPdfView::ZoomMode::FitInView); });
    updatePageLabel();
}

void PdfViewer::updatePageLabel()
{
    m_page->setText(tr("Page %1 / %2").arg(m_view->pageNavigator()->currentPage() + 1).arg(m_doc->pageCount()));
}
#endif

// ---------------------------------------------------------------------------------------
//  ExternalFileView
// ---------------------------------------------------------------------------------------

ExternalFileView::ExternalFileView(const QString& path, const QString& note, bool offerText, QWidget* parent)
    : QWidget(parent)
{
    const QFileInfo fi(path);
    auto* lay = new QVBoxLayout(this);
    lay->addStretch(1);

    auto* title = new QLabel(QStringLiteral("<h2>%1</h2>").arg(fi.fileName().toHtmlEscaped()));
    title->setAlignment(Qt::AlignCenter);
    lay->addWidget(title);

    auto* info = new QLabel(tr("%1   -   %2 KB").arg(fi.suffix().isEmpty() ? tr("file") : fi.suffix().toUpper(),
                                                      QString::number(fi.size() / 1024.0, 'f', 1)));
    info->setAlignment(Qt::AlignCenter);
    lay->addWidget(info);

    auto* text = new QLabel(note.isEmpty() ? tr("CrucibleCode cannot display this type of file.") : note);
    text->setAlignment(Qt::AlignCenter);
    text->setWordWrap(true);
    lay->addWidget(text);

    auto* row = new QHBoxLayout;
    row->addStretch(1);
    auto* openBtn = new QPushButton(tr("Open with default application"));
    auto* showBtn = new QPushButton(tr("Show in folder"));
    row->addWidget(openBtn);
    row->addWidget(showBtn);
    if (offerText) {
        auto* txtBtn = new QPushButton(tr("Open as text"));
        row->addWidget(txtBtn);
        connect(txtBtn, &QPushButton::clicked, this, [this, path] { emit openAsTextRequested(path); });
    }
    row->addStretch(1);
    lay->addLayout(row);
    lay->addStretch(2);

    connect(openBtn, &QPushButton::clicked, this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
    connect(showBtn, &QPushButton::clicked, this, [path] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
}

#include "AiSetupDialog.h"
#include "AiCompletion.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

AiSetupDialog::AiSetupDialog(AiCompletion* ai, QWidget* parent) : QDialog(parent), m_ai(ai)
{
    setWindowTitle(tr("AI tab completion - local model"));
    setMinimumWidth(560);
    QSettings s;

    auto* intro = new QLabel(tr(
        "<b>Runs entirely on this computer.</b> Nothing you type is sent anywhere. "
        "Enabling it needs two one-time downloads:<br>"
        "&bull; the <b>llama.cpp</b> runtime (<tt>llama-server</tt>, MIT) from the official GitHub releases, "
        "unless you already have <tt>llama-server</tt> installed;<br>"
        "&bull; a small <b>code model</b> in GGUF format from Hugging Face (default: Qwen2.5-Coder 0.5B, Apache-2.0)."));
    intro->setWordWrap(true);

    m_model = new QComboBox;
    for (const AiCompletion::ModelPreset& p : AiCompletion::presets()) m_model->addItem(p.title, p.id);
    m_model->addItem(tr("Custom GGUF model..."), QStringLiteral("custom"));
    const int idx = m_model->findData(s.value(QStringLiteral("ai/model"), QStringLiteral("qwen-0.5b")));
    m_model->setCurrentIndex(idx < 0 ? 0 : idx);

    m_url = new QLineEdit(s.value(QStringLiteral("ai/customUrl")).toString());
    m_url->setPlaceholderText(tr("https://.../model.gguf  (download URL)"));
    m_path = new QLineEdit(s.value(QStringLiteral("ai/customPath")).toString());
    m_path->setPlaceholderText(tr("or a .gguf file already on this computer"));
    auto* browseModel = new QPushButton(tr("Browse..."));
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browseModel);

    m_server = new QLineEdit(s.value(QStringLiteral("ai/serverPath")).toString());
    m_server->setPlaceholderText(tr("optional: path to your own llama-server"));

    auto* form = new QFormLayout;
    form->addRow(tr("Model:"), m_model);
    form->addRow(tr("Custom URL:"), m_url);
    form->addRow(tr("Custom file:"), pathRow);
    form->addRow(tr("llama-server:"), m_server);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_bar = new QProgressBar;
    m_bar->setTextVisible(true);
    m_bar->hide();
    m_log = new QLabel;
    m_log->setWordWrap(true);

    m_download = new QPushButton(tr("Download && enable"));
    m_cancel = new QPushButton(tr("Cancel download"));
    m_cancel->setEnabled(false);
    auto* remove = new QPushButton(tr("Remove downloaded files"));
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_download);
    buttons->addWidget(m_cancel);
    buttons->addStretch(1);
    buttons->addWidget(remove);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto* lay = new QVBoxLayout(this);
    lay->addWidget(intro);
    lay->addLayout(form);
    lay->addWidget(m_status);
    lay->addWidget(m_bar);
    lay->addWidget(m_log);
    lay->addLayout(buttons);
    lay->addWidget(box);

    connect(browseModel, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Choose a GGUF model"), QString(), tr("GGUF models (*.gguf)"));
        if (!f.isEmpty()) { m_path->setText(f); m_model->setCurrentIndex(m_model->findData(QStringLiteral("custom"))); }
    });
    connect(m_model, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(m_download, &QPushButton::clicked, this, &AiSetupDialog::startDownloads);
    connect(m_cancel, &QPushButton::clicked, m_ai, &AiCompletion::cancelDownload);
    connect(remove, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Remove"), tr("Delete the downloaded runtime and models?")) == QMessageBox::Yes)
            m_ai->removeDownloads();
        refresh();
    });

    connect(m_ai, &AiCompletion::progress, this, [this](const QString& what, qint64 got, qint64 total) {
        m_bar->show();
        m_bar->setMaximum(total > 0 ? 1000 : 0);
        if (total > 0) m_bar->setValue(int(1000.0 * got / total));
        m_bar->setFormat(tr("%1: %2 / %3 MB").arg(what).arg(got / 1048576.0, 0, 'f', 1).arg(total / 1048576.0, 0, 'f', 1));
    });
    connect(m_ai, &AiCompletion::message, this, [this](const QString& t) { m_log->setText(t); });
    connect(m_ai, &AiCompletion::downloadFinished, this, [this](bool ok, const QString& text) {
        m_bar->hide();
        m_cancel->setEnabled(false);
        m_download->setEnabled(true);
        m_log->setText(text);
        if (!ok) { refresh(); return; }
        if (!m_wantModel && !m_ai->modelAvailable()) {      // runtime done, model next
            m_wantModel = true;
            m_cancel->setEnabled(true);
            m_download->setEnabled(false);
            m_ai->downloadModel();
            return;
        }
        QSettings().setValue(QStringLiteral("ai/enabled"), true);
        m_ai->applySettings();
        refresh();
    });
    connect(m_ai, &AiCompletion::stateChanged, this, [this] { refresh(); });

    refresh();
}

void AiSetupDialog::saveFields()
{
    QSettings s;
    s.setValue(QStringLiteral("ai/model"), m_model->currentData().toString());
    s.setValue(QStringLiteral("ai/customUrl"), m_url->text().trimmed());
    s.setValue(QStringLiteral("ai/customPath"), m_path->text().trimmed());
    s.setValue(QStringLiteral("ai/serverPath"), m_server->text().trimmed());
}

void AiSetupDialog::refresh()
{
    const bool custom = m_model->currentData().toString() == QLatin1String("custom");
    m_url->setEnabled(custom);
    m_path->setEnabled(custom);
    saveFields();
    m_status->setText(tr("Runtime: %1<br>Model: %2<br>State: %3")
        .arg(m_ai->runtimeAvailable() ? tr("<b>installed</b> (%1)").arg(m_ai->runtimePath().toHtmlEscaped()) : tr("not installed"),
             m_ai->modelAvailable() ? tr("<b>ready</b> (%1)").arg(m_ai->modelPath().toHtmlEscaped()) : tr("not downloaded"),
             m_ai->statusText().toHtmlEscaped()));
}

void AiSetupDialog::startDownloads()
{
    saveFields();
    if (m_ai->runtimeAvailable() && m_ai->modelAvailable()) {
        QSettings().setValue(QStringLiteral("ai/enabled"), true);
        m_ai->applySettings();
        refresh();
        return;
    }
    m_wantModel = false;
    m_download->setEnabled(false);
    m_cancel->setEnabled(true);
    if (!m_ai->runtimeAvailable()) {
        m_ai->downloadRuntime();
    } else {
        m_wantModel = true;
        m_ai->downloadModel();
    }
}

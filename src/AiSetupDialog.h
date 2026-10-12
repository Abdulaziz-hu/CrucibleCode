#pragma once
#include <QDialog>

class AiCompletion;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

// Choose / download the local AI model and runtime used for AI tab completion.
class AiSetupDialog : public QDialog {
    Q_OBJECT
public:
    AiSetupDialog(AiCompletion* ai, QWidget* parent = nullptr);

private:
    void refresh();
    void saveFields();
    void startDownloads();

    AiCompletion* m_ai;
    QComboBox* m_model = nullptr;
    QLineEdit* m_url = nullptr;
    QLineEdit* m_path = nullptr;
    QLineEdit* m_server = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_log = nullptr;
    QProgressBar* m_bar = nullptr;
    QPushButton* m_download = nullptr;
    QPushButton* m_cancel = nullptr;
    bool m_wantModel = false;
};

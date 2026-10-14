#pragma once
#include <QString>

struct LspDiagnostic {
    int line = 0, startChar = 0, endLine = 0, endChar = 0;   // 0-based, UTF-16 columns
    int severity = 1;                                       // 1 error, 2 warning, 3 info, 4 hint
    QString message;
    QString source;
    QString code;
};

struct LspCompletionItem {
    QString label;
    QString insertText;       // what is inserted (label when the server gave nothing else)
    QString detail;
    QString sortText;
    int kind = 0;
    bool snippet = false;     // insertText uses snippet syntax ($1, ${2:x})
};

#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

namespace coeus {

// The four-line status header that makes a folder a project (SPEC.md §Status header).
struct StatusHeader {
    QString status;
    QString provenance;
    QString lastReviewed;
    QString why;
};

// One entry of a register file (SPEC.md §Registers), e.g. "### D-005 Vault…".
struct RegisterEntry {
    QString kind;   // F C D AV BUG IMP
    int number = 0;
    QString ident;  // canonical, e.g. "D-005"
    QString title;
    QString status; // the Status field's text, as written
    int line = 0;   // 1-based line of the heading
    QList<QPair<QString, QString>> fields; // name -> value, in order
    QStringList references; // idents cited in Related fields, canonical, in order, unique
};

// Pure parsing of the scaffold standard's documents (F-015). No file access.
class RegisterParser {
public:
    // FEATURES.md, CLAIMS.md, DECISIONS.md, ATTACK_VECTORS.md, BUGS.md, IMPROVEMENTS.md.
    static bool isRegisterFile(const QString& fileName);
    // The register kind a file holds by name ("D" for DECISIONS.md), or empty.
    static QString kindOfFile(const QString& fileName);

    static QList<RegisterEntry> parse(QStringView text);
    // The header, if the first four lines are one, in order.
    static std::optional<StatusHeader> statusHeader(QStringView text);

    // "D-5" or "D-0005" -> "D-005"; empty if `text` is not an ident.
    static QString canonical(const QString& text);
};

} // namespace coeus

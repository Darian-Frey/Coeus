#pragma once

#include "ui/WikilinkCompletion.hpp"

#include <QList>
#include <QObject>

class QAbstractItemView;
class QCompleter;
class QKeyEvent;
class QModelIndex;
class QPlainTextEdit;
class QStandardItemModel;

namespace coeus {

class NoteCatalogue;

// The popup that opens on "[[" in the editor (F-004): note titles and
// aliases, or after "[[Note#" that note's headings. Choosing one writes the
// link text that resolves to the chosen note (WikilinkCompletion::linkText)
// and closes the link with "]]" if it is not closed already. It also opens on
// a register ID prefix such as "D-" with the current project's entries of
// that kind, inserting the ID alone (F-015).
class WikilinkCompleter : public QObject {
    Q_OBJECT

public:
    WikilinkCompleter(QPlainTextEdit* editor, const NoteCatalogue* catalogue, const QString& notePath);

    // Called by the editor before handling a key: true if the popup owns it.
    bool handleKeyPress(QKeyEvent* event);
    // Called after a typed key: opens, narrows or closes the popup.
    void refresh();

    QAbstractItemView* popup() const;
    bool isOpen() const;

private:
    void insert(const QModelIndex& index);
    void show(const QList<Completion>& completions);
    void close();

    QPlainTextEdit* m_editor;
    const NoteCatalogue* m_catalogue;
    QString m_notePath;
    QCompleter* m_completer = nullptr;
    QStandardItemModel* m_model = nullptr;
    int m_replaceFrom = -1; // document position after "[[", after '#', or at an ID's start
    bool m_closesLink = true;
};

} // namespace coeus

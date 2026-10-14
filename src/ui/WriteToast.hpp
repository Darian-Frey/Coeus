#pragma once

#include <QByteArray>
#include <QString>
#include <QWidget>

#include <optional>

class QLabel;
class QPushButton;
class QTimer;

namespace coeus {

// The notice an MCP write raises in the window (F-033): what was written,
// with Undo for kUndoMs. Undo asks MainWindow to put the file back
// (undoRequested); the toast then says how that went and goes.
class WriteToast : public QWidget {
    Q_OBJECT

public:
    static constexpr int kUndoMs = 10'000;

    WriteToast(const QString& path, const std::optional<QByteArray>& before, const QByteArray& afterHash,
               const QString& summary, QWidget* parent);

    QString path() const { return m_path; }
    const std::optional<QByteArray>& before() const { return m_before; }
    QByteArray afterHash() const { return m_afterHash; }
    QPushButton* undoButton() const { return m_undo; }
    QString text() const;
    // Shows the outcome of an undo and closes shortly after.
    void finish(const QString& outcome);

signals:
    void undoRequested(coeus::WriteToast* toast);

private:
    QString m_path;
    std::optional<QByteArray> m_before;
    QByteArray m_afterHash;
    QLabel* m_label = nullptr;
    QPushButton* m_undo = nullptr;
    QTimer* m_timer = nullptr;
};

} // namespace coeus

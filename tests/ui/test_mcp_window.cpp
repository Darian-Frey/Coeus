// F-033 with the window open (AV-012): `coeus --mcp` as a real process
// asks the window about unsaved notes, and its writes raise a toast whose
// Undo puts the file back.

#include "../index/IndexDump.hpp"

#include "mcp/GuiLink.hpp"
#include "ui/Editor.hpp"
#include "ui/MainWindow.hpp"
#include "ui/McpLink.hpp"
#include "ui/NoteCatalogue.hpp"
#include "ui/WriteToast.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace coeus;
using coeus::testing::writeFile;

namespace {

constexpr int kWaitMs = 15'000;

QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

const QByteArray kHeader = "> **Status:** Active\n> **Provenance:** T\n> **Last reviewed:** 2026-10-14\n"
                           "> **Why this status:** T\n\n# Alpha\n";

} // namespace

class TestMcpWindow : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    QTemporaryDir m_vault;
    std::unique_ptr<MainWindow> m_window;
    QProcess m_mcp;
    int m_id = 0;

    QString vault(const char* relative) const { return m_vault.filePath(QLatin1String(relative)); }

    // One tools/call to the real server; the window keeps answering it meanwhile.
    QJsonObject call(const QString& tool, const QJsonObject& arguments, bool* ok)
    {
        const QJsonObject request{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), ++m_id},
                                  {QStringLiteral("method"), QStringLiteral("tools/call")},
                                  {QStringLiteral("params"), QJsonObject{{QStringLiteral("name"), tool}, {QStringLiteral("arguments"), arguments}}}};
        m_mcp.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
        // The event loop runs while waiting, so the window can answer the server.
        if (!QTest::qWaitFor([this] { return m_mcp.canReadLine(); }, kWaitMs)) {
            *ok = false;
            return {{QStringLiteral("error"), QStringLiteral("no reply from coeus --mcp")}};
        }
        const QJsonObject result = QJsonDocument::fromJson(m_mcp.readLine()).object().value(QStringLiteral("result")).toObject();
        *ok = !result.value(QStringLiteral("isError")).toBool();
        return *ok ? result.value(QStringLiteral("structuredContent")).toObject()
                   : QJsonObject{{QStringLiteral("error"), result.value(QStringLiteral("content")).toArray().at(0).toObject().value(QStringLiteral("text"))}};
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<coeus::WriteToast*>();
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        writeFile(vault(".coeus/config.toml"), "[mcp]\nallow_writes = true\n");
        writeFile(vault("projects/alpha/README.md"), kHeader);
        writeFile(vault("projects/alpha/DECISIONS.md"), "# Decisions\n\n### D-001 First\n**Status:** Accepted\n**Reversal conditions.** x\n");
        writeFile(vault("notes/plan.md"), "# Plan\n\nDraft.\n");
        writeFile(vault("notes/open.md"), "# Open\n");
        m_window = std::make_unique<MainWindow>();
        m_window->resize(1100, 750);
        m_window->show();
        QSignalSpy reloaded(m_window->catalogue(), &NoteCatalogue::reloaded);
        m_window->openVault(*Vault::open(m_vault.path()));
        QVERIFY(reloaded.wait(kWaitMs));
        QVERIFY(m_window->mcpLink()->isListening());
        QVERIFY(QFile::exists(GuiLink::socketPath(m_vault.path())));

        m_mcp.setProgram(QStringLiteral(COEUS_BINARY));
        m_mcp.setArguments({QStringLiteral("--mcp"), QStringLiteral("--vault"), m_vault.path()});
        m_mcp.start();
        QVERIFY(m_mcp.waitForStarted());
    }

    void notesOpenWithUnsavedChangesAreRefused()
    {
        m_window->openNote(vault("notes/open.md"));
        Editor* editor = m_window->currentEditor();
        editor->moveCursor(QTextCursor::End);
        editor->insertPlainText(QStringLiteral("Typing, not saved.\n"));
        bool ok = true;
        const QString error = call(QStringLiteral("write_note"), {{QStringLiteral("path"), QStringLiteral("notes/open")},
                                                                  {QStringLiteral("body"), QStringLiteral("Overwritten?")},
                                                                  {QStringLiteral("mode"), QStringLiteral("replace")}}, &ok)
                                  .value(QStringLiteral("error")).toString();
        QVERIFY(!ok);
        QVERIFY(error.contains(QStringLiteral("unsaved changes")));
        QCOMPARE(read(vault("notes/open.md")), QByteArray("# Open\n"));

        // The same for a register open with unsaved edits.
        m_window->openNote(vault("projects/alpha/DECISIONS.md"));
        m_window->currentEditor()->moveCursor(QTextCursor::End);
        m_window->currentEditor()->insertPlainText(QStringLiteral("\n### D-002 Typed by hand\n"));
        QVERIFY(call(QStringLiteral("append_entry"), {{QStringLiteral("project"), QStringLiteral("alpha")},
                                                      {QStringLiteral("kind"), QStringLiteral("D")},
                                                      {QStringLiteral("title"), QStringLiteral("From Claude")},
                                                      {QStringLiteral("fields"), QJsonObject{{QStringLiteral("Reversal conditions"), QStringLiteral("x")}}}}, &ok)
                    .value(QStringLiteral("error")).toString().contains(QStringLiteral("unsaved")));
        QVERIFY(!ok);
        // next_id counts the unsaved D-002, as Insert Next ID does.
        QCOMPARE(call(QStringLiteral("next_id"), {{QStringLiteral("project"), QStringLiteral("alpha")}, {QStringLiteral("kind"), QStringLiteral("D")}}, &ok)
                     .value(QStringLiteral("ident")).toString(), QStringLiteral("D-003"));
        m_window->currentEditor()->document()->undo();
        m_window->currentEditor()->document()->setModified(false);
        editor->document()->undo();
        editor->document()->setModified(false);
    }

    void aWriteRaisesAToastWhoseUndoPutsItBack()
    {
        QSignalSpy shown(m_window.get(), &MainWindow::toastShown);
        bool ok = false;
        call(QStringLiteral("write_note"), {{QStringLiteral("path"), QStringLiteral("notes/plan")}, {QStringLiteral("body"), QStringLiteral("Changed by Claude.")},
                                            {QStringLiteral("mode"), QStringLiteral("append")}}, &ok);
        QVERIFY(ok);
        QTRY_VERIFY_WITH_TIMEOUT(!shown.isEmpty(), kWaitMs);
        WriteToast* toast = shown.front().at(0).value<WriteToast*>();
        QVERIFY(toast->isVisible());
        QCOMPARE(toast->text(), QStringLiteral("An AI session added to notes/plan.md"));
        QVERIFY(read(vault("notes/plan.md")).endsWith("Changed by Claude.\n"));

        QSignalSpy undone(m_window.get(), &MainWindow::writeUndone);
        toast->undoButton()->click();
        QCOMPARE(undone.size(), 1);
        QVERIFY(undone.front().at(1).toBool());
        QCOMPARE(read(vault("notes/plan.md")), QByteArray("# Plan\n\nDraft.\n"));
        QVERIFY(toast->text().startsWith(QStringLiteral("Undone")));
    }

    void undoRemovesWhatWasCreatedAndNotWhatChangedSince()
    {
        QSignalSpy shown(m_window.get(), &MainWindow::toastShown);
        bool ok = false;
        call(QStringLiteral("append_entry"), {{QStringLiteral("project"), QStringLiteral("alpha")}, {QStringLiteral("kind"), QStringLiteral("IMP")},
                                              {QStringLiteral("title"), QStringLiteral("Better")},
                                              {QStringLiteral("fields"), QJsonObject{{QStringLiteral("Trade-offs"), QStringLiteral("None.")}}}}, &ok);
        QVERIFY(ok);
        QTRY_VERIFY_WITH_TIMEOUT(!shown.isEmpty(), kWaitMs);
        WriteToast* created = shown.takeFirst().at(0).value<WriteToast*>();
        QVERIFY(created->text().contains(QStringLiteral("IMP-001")));
        QVERIFY(QFile::exists(vault("projects/alpha/IMPROVEMENTS.md")));
        created->undoButton()->click();
        QVERIFY(!QFile::exists(vault("projects/alpha/IMPROVEMENTS.md"))); // it did not exist before

        call(QStringLiteral("write_note"), {{QStringLiteral("path"), QStringLiteral("notes/plan")}, {QStringLiteral("body"), QStringLiteral("Again.")},
                                            {QStringLiteral("mode"), QStringLiteral("append")}}, &ok);
        QVERIFY(ok);
        QTRY_VERIFY_WITH_TIMEOUT(!shown.isEmpty(), kWaitMs);
        WriteToast* toast = shown.takeFirst().at(0).value<WriteToast*>();
        writeFile(vault("notes/plan.md"), "Edited by the author meanwhile.\n");
        QSignalSpy undone(m_window.get(), &MainWindow::writeUndone);
        toast->undoButton()->click();
        QVERIFY(!undone.front().at(1).toBool());
        QCOMPARE(read(vault("notes/plan.md")), QByteArray("Edited by the author meanwhile.\n")); // left alone
        QVERIFY(toast->text().startsWith(QStringLiteral("Not undone")));
    }

    void toastsGoByThemselves()
    {
        QSignalSpy shown(m_window.get(), &MainWindow::toastShown);
        bool ok = false;
        call(QStringLiteral("write_note"), {{QStringLiteral("path"), QStringLiteral("notes/later")}, {QStringLiteral("body"), QStringLiteral("x")},
                                            {QStringLiteral("mode"), QStringLiteral("create")}}, &ok);
        QVERIFY(ok);
        QTRY_VERIFY_WITH_TIMEOUT(!shown.isEmpty(), kWaitMs);
        QPointer<WriteToast> toast = shown.front().at(0).value<WriteToast*>();
        QTRY_VERIFY_WITH_TIMEOUT(toast.isNull(), WriteToast::kUndoMs + 5000); // closed and deleted after 10 s
        QVERIFY(QFile::exists(vault("notes/later.md")));                      // and the write kept
    }

    void cleanupTestCase()
    {
        m_mcp.closeWriteChannel();
        m_mcp.waitForFinished(5000);
        m_window.reset();
        QVERIFY(!QFile::exists(GuiLink::socketPath(m_vault.path()))); // the window took its socket with it
    }
};

QTEST_MAIN(TestMcpWindow)
#include "test_mcp_window.moc"

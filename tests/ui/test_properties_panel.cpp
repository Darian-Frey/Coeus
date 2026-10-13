// F-008: frontmatter as typed fields above the note; edits go into the
// buffer one key at a time; vocabulary errors are shown, never rewritten.

#include "ui/Editor.hpp"
#include "ui/NoteView.hpp"
#include "ui/PropertiesPanel.hpp"

#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTest>
#include <QToolButton>

using namespace coeus;

class TestPropertiesPanel : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_vault;

    std::unique_ptr<NoteView> open(const QByteArray& content)
    {
        const QString path = m_vault.filePath(QStringLiteral("n.md"));
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(content);
        file.close();
        auto view = std::make_unique<NoteView>(*Vault::open(m_vault.path()), path, QStringLiteral("n.md"));
        if (!view->editor()->load())
            return nullptr;
        view->properties()->refresh();
        return view;
    }

    // Lets queued refreshes run.
    static void settle() { QCoreApplication::processEvents(); }

private slots:
    void showsTypesAndFlagsVocabularyErrors()
    {
        auto view = open("---\nstatus: Done\nstage: idea\nrank: 3\ndecided: 2026-10-13\ntags: [a, b]\nnote: hello\n---\nBody\n");
        PropertiesPanel* panel = view->properties();
        QCOMPARE(panel->properties().size(), 6);
        QCOMPARE(panel->errorCount(), 1); // status: Done is not a project status
        auto* status = qobject_cast<QComboBox*>(panel->field(QStringLiteral("status")));
        QVERIFY(status);
        QCOMPARE(status->currentText(), QStringLiteral("Done")); // shown as written
        QVERIFY(qobject_cast<QComboBox*>(panel->field(QStringLiteral("stage"))));
        QCOMPARE(qobject_cast<QLineEdit*>(panel->field(QStringLiteral("tags")))->text(), QStringLiteral("a, b"));
        // Displaying never changes the buffer.
        QVERIFY(!view->editor()->isModified());
        QVERIFY(view->editor()->toPlainText().contains(QStringLiteral("status: Done")));
    }

    void choosingAValidValueRewritesOnlyThatLine()
    {
        auto view = open("---\n# keep me\nstatus: Done   # old\nother: x\n---\nBody\n");
        auto* status = qobject_cast<QComboBox*>(view->properties()->field(QStringLiteral("status")));
        status->setCurrentText(QStringLiteral("Active"));
        emit status->activated(status->currentIndex());
        settle();
        QCOMPARE(view->editor()->toPlainText(), QStringLiteral("---\n# keep me\nstatus: Active\nother: x\n---\nBody\n"));
        QVERIFY(view->editor()->isModified()); // in the buffer, saved like any edit
        QCOMPARE(view->properties()->errorCount(), 0);

        view->editor()->undo(); // one step
        QCOMPARE(view->editor()->toPlainText(), QStringLiteral("---\n# keep me\nstatus: Done   # old\nother: x\n---\nBody\n"));
    }

    void editsTextListsNumbersAndDates()
    {
        auto view = open("---\ntitle: Old\ntags:\n  - a\nrank: 3\ndecided: 2026-10-13\n---\n");
        PropertiesPanel* panel = view->properties();
        auto edit = [&](const char* key, const QString& value) {
            auto* line = qobject_cast<QLineEdit*>(panel->field(QLatin1String(key)));
            QVERIFY(line);
            line->setText(value);
            emit line->editingFinished();
            settle();
        };
        edit("title", QStringLiteral("2026")); // text that looks like a number stays text
        edit("tags", QStringLiteral("a, b c,  d"));
        edit("rank", QStringLiteral("not a number")); // refused
        edit("decided", QStringLiteral("13/10/2026")); // refused
        QCOMPARE(view->editor()->toPlainText(),
                 QStringLiteral("---\ntitle: \"2026\"\ntags: [a, b c, d]\nrank: 3\ndecided: 2026-10-13\n---\n"));
        edit("rank", QStringLiteral("4.5"));
        QVERIFY(view->editor()->toPlainText().contains(QStringLiteral("rank: 4.5\n")));
    }

    void removeAndAdd()
    {
        auto view = open("---\nkeep: 1\ngone:\n  - x\n---\nBody\n");
        PropertiesPanel* panel = view->properties();
        panel->findChild<QToolButton*>(QStringLiteral("remove:gone"))->click();
        settle();
        QCOMPARE(view->editor()->toPlainText(), QStringLiteral("---\nkeep: 1\n---\nBody\n"));

        panel->addButton()->click();
        panel->newKey()->setText(QStringLiteral("status"));
        emit panel->newKey()->returnPressed();
        settle();
        QCOMPARE(view->editor()->toPlainText(), QStringLiteral("---\nkeep: 1\nstatus: Active\n---\nBody\n"));
    }

    void addingToANoteWithoutFrontmatterCreatesIt()
    {
        auto view = open("# Just a body\n");
        PropertiesPanel* panel = view->properties();
        QCOMPARE(panel->properties().size(), 0);
        panel->addButton()->click();
        panel->newKey()->setText(QStringLiteral("summary"));
        emit panel->newKey()->returnPressed();
        settle();
        QCOMPARE(view->editor()->toPlainText(), QStringLiteral("---\nsummary: \"\"\n---\n# Just a body\n"));
    }

    void invalidYamlIsReadOnly()
    {
        auto view = open("---\nbad: [\n---\nBody\n");
        PropertiesPanel* panel = view->properties();
        QCOMPARE(panel->errorCount(), 1);
        QVERIFY(!panel->addButton()->isEnabled());
    }

    void followsTheBufferAndHidesInReadingMode()
    {
        auto view = open("---\na: 1\n---\n");
        QSignalSpy refreshed(view->properties(), &PropertiesPanel::refreshed);
        QTextCursor cursor(view->editor()->document()->findBlockByNumber(1));
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertText(QStringLiteral("\nb: 2"));
        QVERIFY(refreshed.wait(2000));
        QCOMPARE(view->properties()->properties().size(), 2);

        view->setMode(NoteView::Mode::Reading);
        QVERIFY(view->properties()->isHidden());
        view->setMode(NoteView::Mode::Source);
        QVERIFY(!view->properties()->isHidden());
    }
};

QTEST_MAIN(TestPropertiesPanel)
#include "test_properties_panel.moc"

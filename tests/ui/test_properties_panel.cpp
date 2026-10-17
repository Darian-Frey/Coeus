// F-008: frontmatter as typed fields above the note; edits go into the
// buffer one key at a time; values outside a declared list (D-025) are
// shown, never rewritten; other text fields suggest the vault's values.

#include "index/Indexer.hpp"
#include "index/IndexService.hpp"
#include "index/IndexStore.hpp"
#include "ui/Editor.hpp"
#include "ui/NoteView.hpp"
#include "ui/PropertiesPanel.hpp"

#include <QAbstractItemModel>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
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

    static void write(const QString& path, const QByteArray& content)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(content);
    }

    // Opens `relative` in the vault holding `content`, as the window does.
    std::unique_ptr<NoteView> open(const QByteArray& content, const QString& relative = QStringLiteral("n.md"))
    {
        const QString path = m_vault.filePath(relative);
        write(path, content);
        auto view = std::make_unique<NoteView>(*Vault::open(m_vault.path()), path, relative);
        if (!view->editor()->load())
            return nullptr;
        view->setIndexPath(IndexService::indexPath(m_vault.path()));
        view->setPropertyContext(m_vault.path(), true);
        return view;
    }

    // Lets queued refreshes run.
    static void settle() { QCoreApplication::processEvents(); }

private slots:
    void initTestCase()
    {
        // The author's declared values (D-025): scenes in prose/ have their own statuses.
        write(m_vault.filePath(QStringLiteral("Coeus properties.md")),
              "# Coeus properties\n\nScenes:\n\n## status\nFolders: prose\n- outline\n- draft\n- revised\n- final\n"
              "\n## stars\n- 1\n- 2\n- 3\n");
    }

    void statusIsTheAuthorsOwnUnlessDeclared()
    {
        // Outside prose/ nothing is declared: status is plain text, and so is
        // stage outside ideas/ (a project's status lives in its README header).
        auto view = open("---\nstatus: Done\nstage: idea\nrank: 3\ndecided: 2026-10-13\ntags: [a, b]\nnote: hello\n---\nBody\n");
        PropertiesPanel* panel = view->properties();
        QCOMPARE(panel->properties().size(), 6);
        QCOMPARE(panel->errorCount(), 0);
        QCOMPARE(qobject_cast<QLineEdit*>(panel->field(QStringLiteral("status")))->text(), QStringLiteral("Done"));
        QVERIFY(qobject_cast<QLineEdit*>(panel->field(QStringLiteral("stage"))));
        QCOMPARE(qobject_cast<QLineEdit*>(panel->field(QStringLiteral("tags")))->text(), QStringLiteral("a, b"));

        // In ideas/, stage keeps Coeus's idea stages.
        auto idea = open("---\nstage: someday\n---\n", QStringLiteral("ideas/x.md"));
        QCOMPARE(idea->properties()->errorCount(), 1);
        QVERIFY(qobject_cast<QComboBox*>(idea->properties()->field(QStringLiteral("stage"))));
    }

    void declaredValuesAreOfferedAndOthersFlagged()
    {
        auto view = open("---\nchapter: Part 1\nstatus: drfat\n---\nScene\n", QStringLiteral("prose/act1/scene.md"));
        PropertiesPanel* panel = view->properties();
        QCOMPARE(panel->errorCount(), 1);
        auto* status = qobject_cast<QComboBox*>(panel->field(QStringLiteral("status")));
        QVERIFY(status);
        QCOMPARE(status->currentText(), QStringLiteral("drfat")); // shown as written
        QCOMPARE(status->count(), 5);                              // it, then the four declared
        QCOMPARE(status->itemText(2), QStringLiteral("draft"));
        // Displaying never changes the buffer.
        QVERIFY(!view->editor()->isModified());
        QVERIFY(view->editor()->toPlainText().contains(QStringLiteral("status: drfat")));
    }

    void choosingAValidValueRewritesOnlyThatLine()
    {
        auto view = open("---\n# keep me\nstatus: drfat   # old\nother: x\n---\nBody\n", QStringLiteral("prose/s.md"));
        auto* status = qobject_cast<QComboBox*>(view->properties()->field(QStringLiteral("status")));
        status->setCurrentText(QStringLiteral("draft"));
        emit status->activated(status->currentIndex());
        settle();
        QCOMPARE(view->editor()->toPlainText(), QStringLiteral("---\n# keep me\nstatus: draft\nother: x\n---\nBody\n"));
        QVERIFY(view->editor()->isModified()); // in the buffer, saved like any edit
        QCOMPARE(view->properties()->errorCount(), 0);

        view->editor()->undo(); // one step
        QCOMPARE(view->editor()->toPlainText(), QStringLiteral("---\n# keep me\nstatus: drfat   # old\nother: x\n---\nBody\n"));
    }

    void aDeclaredNumberStaysANumber()
    {
        auto view = open("---\nstars: 1\n---\n");
        auto* stars = qobject_cast<QComboBox*>(view->properties()->field(QStringLiteral("stars")));
        QVERIFY(stars);
        stars->setCurrentText(QStringLiteral("3"));
        emit stars->activated(stars->currentIndex());
        settle();
        QCOMPARE(view->editor()->toPlainText(), QStringLiteral("---\nstars: 3\n---\n")); // not "3", which would be text
    }

    void textFieldsSuggestTheVaultsValues()
    {
        write(m_vault.filePath(QStringLiteral("prose/a.md")), "---\npov: Theo Hartmann\n---\n");
        write(m_vault.filePath(QStringLiteral("prose/b.md")), "---\npov: Theo Hartmann\n---\n");
        write(m_vault.filePath(QStringLiteral("prose/c.md")), "---\npov: Ada Kerr\n---\n");
        QDir().mkpath(m_vault.filePath(QStringLiteral(".coeus")));
        {
            auto store = IndexStore::open(IndexService::indexPath(m_vault.path()));
            QVERIFY(store.has_value());
            Indexer indexer(*store);
            QVERIFY(indexer.scan({0, QStringLiteral("vault"), m_vault.path()}).has_value());
        }
        auto view = open("---\npov: \"\"\n---\n", QStringLiteral("prose/d.md"));
        auto* pov = qobject_cast<QLineEdit*>(view->properties()->field(QStringLiteral("pov")));
        QVERIFY(pov);
        QTRY_VERIFY(pov->completer()); // read off the UI thread
        QAbstractItemModel* model = pov->completer()->model();
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(model->index(0, 0).data().toString(), QStringLiteral("Theo Hartmann")); // the most used first
        QCOMPARE(model->index(1, 0).data().toString(), QStringLiteral("Ada Kerr"));
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
        QCOMPARE(view->editor()->toPlainText(), QStringLiteral("---\nkeep: 1\nstatus: \"\"\n---\nBody\n")); // nothing declared here

        // Under prose/ a declared key starts at its first value.
        auto scene = open("---\nkeep: 1\n---\n", QStringLiteral("prose/new.md"));
        scene->properties()->addButton()->click();
        scene->properties()->newKey()->setText(QStringLiteral("status"));
        emit scene->properties()->newKey()->returnPressed();
        settle();
        QCOMPARE(scene->editor()->toPlainText(), QStringLiteral("---\nkeep: 1\nstatus: outline\n---\n"));
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

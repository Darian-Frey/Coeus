// F-033: `coeus --mcp`, the protocol and the read tools, in process and as
// the real binary over stdio.

#include "../git/GitFixture.hpp"
#include "../index/IndexDump.hpp"

#include "mcp/McpServer.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

using namespace coeus;
using coeus::testing::GitFixture;
using coeus::testing::writeFile;

namespace {

const QByteArray kHeader = "> **Status:** Active\n> **Provenance:** T\n> **Last reviewed:** 2026-10-01\n"
                           "> **Why this status:** T\n\n# Alpha\n";

QJsonObject request(int id, const QString& method, const QJsonObject& params = {})
{
    QJsonObject message{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("method"), method}};
    if (!params.isEmpty())
        message.insert(QStringLiteral("params"), params);
    return message;
}

QByteArray line(const QJsonObject& message)
{
    return QJsonDocument(message).toJson(QJsonDocument::Compact);
}

} // namespace

class TestMcpServer : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::unique_ptr<GitFixture> m_git;
    std::unique_ptr<McpServer> m_server;
    int m_id = 0;

    QString vault(const char* relative = "") const { return m_dir.filePath(QStringLiteral("vault/")) + QLatin1String(relative); }

    QJsonObject send(const QString& method, const QJsonObject& params = {})
    {
        const auto reply = m_server->handle(line(request(++m_id, method, params)));
        return reply ? QJsonDocument::fromJson(*reply).object() : QJsonObject();
    }

    // A tools/call's result: its structured content, or the error text with ok false.
    QJsonObject call(const QString& tool, const QJsonObject& arguments, bool* ok = nullptr)
    {
        const QJsonObject result = send(QStringLiteral("tools/call"), {{QStringLiteral("name"), tool}, {QStringLiteral("arguments"), arguments}})
                                       .value(QStringLiteral("result")).toObject();
        const bool failed = result.value(QStringLiteral("isError")).toBool();
        if (ok)
            *ok = !failed;
        if (failed)
            return {{QStringLiteral("error"), result.value(QStringLiteral("content")).toArray().at(0).toObject().value(QStringLiteral("text"))}};
        return result.value(QStringLiteral("structuredContent")).toObject();
    }

private slots:
    void initTestCase()
    {
        m_git = std::make_unique<GitFixture>(vault());
        m_git->write(QStringLiteral("projects/alpha/README.md"), kHeader);
        m_git->write(QStringLiteral("projects/alpha/ROADMAP.md"), "## Phase 1 — Core\n**Status:** In progress\n");
        m_git->write(QStringLiteral("projects/alpha/DECISIONS.md"),
                     "### D-001 First\n**Status:** Accepted\n**Related:** F-001\n**Reversal conditions.** None.\n\n"
                     "### D-002 Second\n**Status:** Proposed\n**Reversal conditions.** None.\n");
        m_git->write(QStringLiteral("projects/alpha/FEATURES.md"), "### F-001 Shell\n**Status:** Complete\n**Related:** D-001\n");
        m_git->write(QStringLiteral("projects/beta/alpha/README.md"), kHeader); // a second "alpha", nested in beta's folder
        m_git->write(QStringLiteral("projects/beta/README.md"), kHeader);
        m_git->write(QStringLiteral("notes/plan.md"),
                     "---\ntags: [plan, q4]\nowner: Ada\n---\n# Plan\n\nSee [[DECISIONS]] and [[Missing note]]. The lighthouse budget.\n");
        m_git->write(QStringLiteral("notes/other.md"), "Links to [[plan]].\n");
        m_git->write(QStringLiteral("ideas/theia.md"), "---\nstage: scoped\ncodename: Theia\nsummary: A lamp\n---\n# Theia\n");
        m_git->write(QStringLiteral("ideas/hermes.md"), "---\nstage: idea\n---\n# Hermes\n");
        m_git->commit(QStringLiteral("Start"), {QStringLiteral("projects/alpha/README.md")}, 1'790'000'000);
        m_server = std::make_unique<McpServer>(vault().chopped(1));
        m_server->setAlwaysRescan(true);
    }

    void initializeNegotiatesTheVersion()
    {
        const QJsonObject result = send(QStringLiteral("initialize"), {{QStringLiteral("protocolVersion"), QStringLiteral("2025-03-26")},
                                                                       {QStringLiteral("capabilities"), QJsonObject()},
                                                                       {QStringLiteral("clientInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("t")}}}})
                                       .value(QStringLiteral("result")).toObject();
        QCOMPARE(result.value(QStringLiteral("protocolVersion")).toString(), QStringLiteral("2025-03-26"));
        QVERIFY(result.value(QStringLiteral("capabilities")).toObject().contains(QStringLiteral("tools")));
        QCOMPARE(result.value(QStringLiteral("serverInfo")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("coeus"));
        const QJsonObject future = send(QStringLiteral("initialize"), {{QStringLiteral("protocolVersion"), QStringLiteral("2099-01-01")}})
                                       .value(QStringLiteral("result")).toObject();
        QCOMPARE(future.value(QStringLiteral("protocolVersion")).toString(), McpServer::protocolVersions().front());
    }

    void protocolEdges()
    {
        QVERIFY(!m_server->handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})")); // no reply to a notification
        QVERIFY(send(QStringLiteral("ping")).contains(QStringLiteral("result")));
        const QJsonObject unknown = send(QStringLiteral("resources/list"));
        QCOMPARE(unknown.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32601);
        const QJsonObject garbled = QJsonDocument::fromJson(*m_server->handle("{not json")).object();
        QCOMPARE(garbled.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32700);
        QVERIFY(garbled.value(QStringLiteral("id")).isNull());
        const QJsonObject noTool = send(QStringLiteral("tools/call"), {{QStringLiteral("name"), QStringLiteral("rm_rf")}});
        QCOMPARE(noTool.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32602);
    }

    void listsTheReadTools()
    {
        QStringList names;
        for (const QJsonValue& tool : send(QStringLiteral("tools/list")).value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray()) {
            names.append(tool.toObject().value(QStringLiteral("name")).toString());
            QVERIFY(tool.toObject().value(QStringLiteral("inputSchema")).toObject().value(QStringLiteral("type")).toString() == QLatin1String("object"));
        }
        QCOMPARE(names, (QStringList{QStringLiteral("search"), QStringLiteral("get_note"), QStringLiteral("list_registers"),
                                     QStringLiteral("project_status"), QStringLiteral("list_ideas")}));
    }

    void searchFindsNotesAndCatchesUp()
    {
        QJsonArray results = call(QStringLiteral("search"), {{QStringLiteral("query"), QStringLiteral("lighthouse")}}).value(QStringLiteral("results")).toArray();
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].toObject().value(QStringLiteral("root")).toString(), QStringLiteral("vault"));
        QCOMPARE(results[0].toObject().value(QStringLiteral("path")).toString(), QStringLiteral("notes/plan.md"));
        QVERIFY(results[0].toObject().value(QStringLiteral("snippet")).toString().contains(QStringLiteral("**lighthouse**")));
        // Written after the first call, as Claude Code might: found on the next one.
        writeFile(vault("notes/later.md"), "Another lighthouse.\n");
        results = call(QStringLiteral("search"), {{QStringLiteral("query"), QStringLiteral("lighthouse")}}).value(QStringLiteral("results")).toArray();
        QCOMPARE(results.size(), 2);
        bool ok = true;
        QVERIFY(call(QStringLiteral("search"), {{QStringLiteral("query"), QStringLiteral("x")}, {QStringLiteral("mode"), QStringLiteral("semantic")}}, &ok)
                    .value(QStringLiteral("error")).toString().contains(QStringLiteral("Mnemosyne")));
        QVERIFY(!ok);
        call(QStringLiteral("search"), {}, &ok);
        QVERIFY(!ok); // query is required
    }

    void getNoteReadsFrontmatterBodyAndLinks()
    {
        const QJsonObject note = call(QStringLiteral("get_note"), {{QStringLiteral("path"), QStringLiteral("notes/plan")}});
        QCOMPARE(note.value(QStringLiteral("path")).toString(), QStringLiteral("notes/plan.md"));
        QCOMPARE(note.value(QStringLiteral("frontmatter")).toObject().value(QStringLiteral("owner")).toString(), QStringLiteral("Ada"));
        QCOMPARE(note.value(QStringLiteral("frontmatter")).toObject().value(QStringLiteral("tags")).toArray().size(), 2);
        QVERIFY(note.value(QStringLiteral("body")).toString().startsWith(QStringLiteral("# Plan")));
        const QJsonArray links = note.value(QStringLiteral("links")).toArray();
        QCOMPARE(links.size(), 2);
        QCOMPARE(links[0].toObject().value(QStringLiteral("resolved")).toObject().value(QStringLiteral("path")).toString(),
                 QStringLiteral("projects/alpha/DECISIONS.md"));
        QVERIFY(links[1].toObject().value(QStringLiteral("resolved")).isNull()); // [[Missing note]]
        const QJsonArray backlinks = note.value(QStringLiteral("backlinks")).toArray();
        QCOMPARE(backlinks.size(), 1);
        QCOMPARE(backlinks[0].toObject().value(QStringLiteral("path")).toString(), QStringLiteral("notes/other.md"));

        bool ok = true;
        for (const char* path : {"../outside", "/etc/passwd", "notes/nothing-here", ""}) {
            call(QStringLiteral("get_note"), {{QStringLiteral("path"), QLatin1String(path)}}, &ok);
            QVERIFY2(!ok, path);
        }
        QVERIFY(call(QStringLiteral("get_note"), {{QStringLiteral("root"), QStringLiteral("nope")}, {QStringLiteral("path"), QStringLiteral("x")}}, &ok)
                    .value(QStringLiteral("error")).toString().contains(QStringLiteral("the roots are: vault")));
    }

    void listRegistersWithFieldsAndFilters()
    {
        QJsonObject result = call(QStringLiteral("list_registers"), {{QStringLiteral("project"), QStringLiteral("projects/alpha")}});
        QCOMPARE(result.value(QStringLiteral("project")).toString(), QStringLiteral("projects/alpha"));
        QJsonArray entries = result.value(QStringLiteral("entries")).toArray();
        QCOMPARE(entries.size(), 3); // D-001, D-002, F-001: by kind, then number
        QCOMPARE(entries[0].toObject().value(QStringLiteral("ident")).toString(), QStringLiteral("D-001"));
        QCOMPARE(entries[0].toObject().value(QStringLiteral("fields")).toObject().value(QStringLiteral("Related")).toString(), QStringLiteral("F-001"));
        QCOMPARE(entries[0].toObject().value(QStringLiteral("file")).toString(), QStringLiteral("projects/alpha/DECISIONS.md"));
        entries = call(QStringLiteral("list_registers"), {{QStringLiteral("project"), QStringLiteral("projects/alpha")},
                                                          {QStringLiteral("kind"), QStringLiteral("D")},
                                                          {QStringLiteral("status"), QStringLiteral("proposed")}})
                      .value(QStringLiteral("entries")).toArray();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries[0].toObject().value(QStringLiteral("ident")).toString(), QStringLiteral("D-002"));
        // "beta" is unique by folder name; "alpha" is not.
        bool ok = false;
        call(QStringLiteral("list_registers"), {{QStringLiteral("project"), QStringLiteral("beta")}}, &ok);
        QVERIFY(ok);
        QVERIFY(call(QStringLiteral("list_registers"), {{QStringLiteral("project"), QStringLiteral("alpha")}}, &ok)
                    .value(QStringLiteral("error")).toString().contains(QStringLiteral("ambiguous")));
        QVERIFY(call(QStringLiteral("list_registers"), {{QStringLiteral("project"), QStringLiteral("gamma")}}, &ok)
                    .value(QStringLiteral("error")).toString().contains(QStringLiteral("projects/alpha")));
    }

    void projectStatusAsTheBoardSeesIt()
    {
        const QJsonArray projects = call(QStringLiteral("project_status"), {{QStringLiteral("project"), QStringLiteral("projects/alpha")}})
                                        .value(QStringLiteral("projects")).toArray();
        QCOMPARE(projects.size(), 1);
        const QJsonObject alpha = projects[0].toObject();
        QCOMPARE(alpha.value(QStringLiteral("status")).toString(), QStringLiteral("Active"));
        QCOMPARE(alpha.value(QStringLiteral("last_reviewed")).toString(), QStringLiteral("2026-10-01"));
        QCOMPARE(alpha.value(QStringLiteral("phase")).toString(), QStringLiteral("Phase 1 — Core · In progress (0 of 1 complete)"));
        const QJsonObject git = alpha.value(QStringLiteral("git")).toObject();
        QCOMPARE(git.value(QStringLiteral("branch")).toString(), QStringLiteral("main"));
        QVERIFY(git.value(QStringLiteral("dirty")).toInt() >= 3); // ROADMAP, DECISIONS, FEATURES never committed
        QVERIFY(!git.contains(QStringLiteral("ahead")));           // no upstream
        QCOMPARE(call(QStringLiteral("project_status"), {}).value(QStringLiteral("projects")).toArray().size(), 3);
    }

    void listIdeasByStage()
    {
        QCOMPARE(call(QStringLiteral("list_ideas"), {}).value(QStringLiteral("ideas")).toArray().size(), 2);
        const QJsonArray scoped = call(QStringLiteral("list_ideas"), {{QStringLiteral("stage"), QStringLiteral("scoped")}}).value(QStringLiteral("ideas")).toArray();
        QCOMPARE(scoped.size(), 1);
        QCOMPARE(scoped[0].toObject().value(QStringLiteral("codename")).toString(), QStringLiteral("Theia"));
        QCOMPARE(scoped[0].toObject().value(QStringLiteral("path")).toString(), QStringLiteral("ideas/theia.md"));
    }

    void theBinaryOverStdioWithoutADisplay()
    {
        QProcess process;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("DISPLAY"));
        env.remove(QStringLiteral("WAYLAND_DISPLAY"));
        env.remove(QStringLiteral("QT_QPA_PLATFORM"));
        env.insert(QStringLiteral("COEUS_VAULT"), vault().chopped(1));
        process.setProcessEnvironment(env);
        process.start(QStringLiteral(COEUS_BINARY), {QStringLiteral("--mcp")});
        QVERIFY(process.waitForStarted());
        process.write(line(request(1, QStringLiteral("initialize"), {{QStringLiteral("protocolVersion"), QStringLiteral("2025-06-18")}})) + '\n');
        process.write(R"({"jsonrpc":"2.0","method":"notifications/initialized"})" "\n");
        process.write(line(request(2, QStringLiteral("tools/call"), {{QStringLiteral("name"), QStringLiteral("list_ideas")}, {QStringLiteral("arguments"), QJsonObject()}})) + '\n');
        process.closeWriteChannel();
        QVERIFY(process.waitForFinished(30'000));
        QCOMPARE(process.exitCode(), 0);
        const QList<QByteArray> replies = process.readAllStandardOutput().split('\n');
        QCOMPARE(replies.size(), 3); // two replies and the empty piece after the last newline: nothing else on stdout
        QCOMPARE(QJsonDocument::fromJson(replies[0]).object().value(QStringLiteral("id")).toInt(), 1);
        const QJsonObject ideas = QJsonDocument::fromJson(replies[1]).object();
        QCOMPARE(ideas.value(QStringLiteral("result")).toObject().value(QStringLiteral("structuredContent")).toObject()
                     .value(QStringLiteral("ideas")).toArray().size(), 2);

        QProcess nameless;
        env.remove(QStringLiteral("COEUS_VAULT"));
        nameless.setProcessEnvironment(env);
        nameless.start(QStringLiteral(COEUS_BINARY), {QStringLiteral("--mcp")});
        QVERIFY(nameless.waitForFinished(30'000));
        QCOMPARE(nameless.exitCode(), 2);
        QVERIFY(nameless.readAllStandardError().contains("--vault"));
    }
};

QTEST_GUILESS_MAIN(TestMcpServer)
#include "test_mcp_server.moc"

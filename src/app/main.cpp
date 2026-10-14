#include "mcp/McpServer.hpp"
#include "registers/LintCommand.hpp"
#include "ui/MainWindow.hpp"
#include "vault/Vault.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QLoggingCategory>
#include <QSettings>
#include <QTextStream>

#include <cstdio>
#include <cstring>
#include <iostream>

namespace {

// `coeus --lint` and `coeus --mcp`: no window, so no display (IMP-004,
// F-033). Decided before any application object exists, since QApplication
// needs a display.
bool wants(int argc, char* argv[], const char* option)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], option) == 0)
            return true;
    }
    return false;
}

int mcp(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("coeus"));
    QCoreApplication::setApplicationVersion(QStringLiteral(COEUS_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Serve the vault's index to AI sessions over MCP on stdio (F-033)."));
    parser.addHelpOption();
    const QCommandLineOption mcpOption(QStringLiteral("mcp"), QStringLiteral("Serve MCP instead of opening a window."));
    const QCommandLineOption vaultOption(QStringLiteral("vault"),
                                         QStringLiteral("The vault folder (default: the COEUS_VAULT environment variable)."),
                                         QStringLiteral("path"));
    parser.addOptions({mcpOption, vaultOption});
    parser.process(app);
    QString folder = parser.value(vaultOption);
    if (folder.isEmpty())
        folder = qEnvironmentVariable("COEUS_VAULT");
    if (folder.isEmpty()) {
        std::fprintf(stderr, "coeus --mcp: name the vault with --vault <path> or COEUS_VAULT\n");
        return 2;
    }
    const auto vault = coeus::Vault::open(folder);
    if (!vault) {
        std::fprintf(stderr, "coeus --mcp: %s\n", qPrintable(vault.error()));
        return 2;
    }
    coeus::McpServer server(vault->rootPath());
    return server.run(std::cin, std::cout);
}

int lint(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("coeus"));
    QCoreApplication::setApplicationVersion(QStringLiteral(COEUS_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Lint the registers of the projects in a folder (F-016)."));
    parser.addHelpOption();
    const QCommandLineOption lintOption(QStringLiteral("lint"), QStringLiteral("Lint instead of opening a window."));
    const QCommandLineOption strict(QStringLiteral("strict"), QStringLiteral("Exit 1 on warnings as well as errors."));
    const QCommandLineOption quiet(QStringLiteral("quiet"), QStringLiteral("Print errors only."));
    const QCommandLineOption mentions(QStringLiteral("mentions"),
                                      QStringLiteral("Also report IDs mentioned anywhere in a project's notes that it does not define."));
    parser.addOptions({lintOption, strict, quiet, mentions});
    parser.addPositionalArgument(QStringLiteral("folder"), QStringLiteral("Folder to lint (a vault or a repository)."),
                                 QStringLiteral("<folder>"));
    parser.process(app);
    const QStringList positional = parser.positionalArguments();
    if (positional.size() != 1) {
        std::fprintf(stderr, "coeus --lint: expected one folder\n");
        return 2;
    }
    coeus::LintCommand::Options options;
    options.strict = parser.isSet(strict);
    options.quiet = parser.isSet(quiet);
    options.mentions = parser.isSet(mentions);
    QTextStream out(stdout);
    QTextStream err(stderr);
    return coeus::LintCommand::run(positional.front(), options, out, err);
}

} // namespace

int main(int argc, char* argv[])
{
    if (wants(argc, argv, "--mcp"))
        return mcp(argc, argv);
    if (wants(argc, argv, "--lint"))
        return lint(argc, argv);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("coeus"));
    QApplication::setApplicationDisplayName(QStringLiteral("Coeus"));
    QApplication::setOrganizationName(QStringLiteral("coeus"));
    QApplication::setApplicationVersion(QStringLiteral(COEUS_VERSION));
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Local-first Markdown knowledge base."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption verbose(QStringLiteral("verbose"), QStringLiteral("Enable debug logging."));
    parser.addOption(verbose);
    parser.addOption(QCommandLineOption(QStringLiteral("mcp"),
                                        QStringLiteral("Serve the vault's index over MCP on stdio and exit; see coeus --mcp --help.")));
    parser.addOption(QCommandLineOption(QStringLiteral("lint"),
                                        QStringLiteral("Lint the registers in <folder> and exit; see coeus --lint --help.")));
    parser.addPositionalArgument(QStringLiteral("vault"), QStringLiteral("Folder to open as a vault."),
                                 QStringLiteral("[vault]"));
    parser.process(app);

    QLoggingCategory::setFilterRules(parser.isSet(verbose) ? QStringLiteral("coeus.*.debug=true")
                                                           : QStringLiteral("coeus.*.debug=false"));

    coeus::MainWindow window;

    const QStringList positional = parser.positionalArguments();
    if (positional.size() > 1) {
        std::fprintf(stderr, "coeus: expected at most one vault folder\n");
        return 2;
    }
    if (!positional.isEmpty()) {
        const auto vault = coeus::Vault::open(positional.front());
        if (!vault) {
            std::fprintf(stderr, "coeus: %s\n", qPrintable(vault.error()));
            return 2;
        }
        window.openVault(*vault);
    }

    window.show();
    return QApplication::exec();
}

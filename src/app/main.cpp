#include "ui/MainWindow.hpp"
#include "vault/Vault.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QLoggingCategory>
#include <QSettings>

#include <cstdio>

int main(int argc, char* argv[])
{
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

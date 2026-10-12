#include "ui/FuzzyMatch.hpp"

#include <QTest>

using coeus::fuzzyScore;

class TestFuzzyMatch : public QObject {
    Q_OBJECT

private slots:
    void emptyPatternMatchesEverything()
    {
        QCOMPARE(fuzzyScore(u"", u"Open Vault"), std::optional<int>(0));
    }

    void subsequenceMatches()
    {
        QVERIFY(fuzzyScore(u"ov", u"Open Vault").has_value());
        QVERIFY(fuzzyScore(u"OPEN", u"Open Vault").has_value());
    }

    void nonSubsequenceFails()
    {
        QVERIFY(!fuzzyScore(u"vo", u"Open Vault").has_value());
        QVERIFY(!fuzzyScore(u"x", u"Open Vault").has_value());
        QVERIFY(!fuzzyScore(u"open vaults", u"Open Vault").has_value());
    }

    void wordStartsBeatScatteredLetters()
    {
        // "tp" hits word starts in "Toggle Palette" but not in "Stop".
        QVERIFY(*fuzzyScore(u"tp", u"Toggle Palette") > *fuzzyScore(u"tp", u"Stop"));
    }

    void consecutiveBeatsSpread()
    {
        QVERIFY(*fuzzyScore(u"clo", u"Close Tab") > *fuzzyScore(u"clo", u"Command Palette Logo"));
    }

    void camelCaseCountsAsWordStart()
    {
        QVERIFY(*fuzzyScore(u"cp", u"commandPalette") > *fuzzyScore(u"cp", u"accept"));
    }
};

QTEST_GUILESS_MAIN(TestFuzzyMatch)
#include "test_fuzzy_match.moc"

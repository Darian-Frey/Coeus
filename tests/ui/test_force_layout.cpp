// F-026: the graph view's force layout settles, repeatably, with linked
// nodes nearer than unlinked ones, and keeps places across changes.

#include "ui/ForceLayout.hpp"

#include <QElapsedTimer>
#include <QTest>

#include <cmath>

using namespace coeus;

namespace {

double distance(const ForceLayout& layout, int a, int b)
{
    const QPointF d = layout.position(a) - layout.position(b);
    return std::hypot(d.x(), d.y());
}

QList<QString> names(int count)
{
    QList<QString> keys;
    for (int i = 0; i < count; ++i)
        keys.append(QStringLiteral("n%1").arg(i));
    return keys;
}

int settle(ForceLayout& layout)
{
    int steps = 0;
    while (layout.step())
        ++steps;
    return steps;
}

} // namespace

class TestForceLayout : public QObject {
    Q_OBJECT

private slots:
    void settlesWithLinkedNodesTogether()
    {
        // Two triangles, joined by nothing.
        ForceLayout layout;
        layout.setGraph(names(6), {{0, 1}, {1, 2}, {2, 0}, {3, 4}, {4, 5}, {5, 3}});
        const int steps = settle(layout);
        QVERIFY(layout.settled());
        QVERIFY2(steps > 100 && steps < 400, qPrintable(QString::number(steps)));
        QVERIFY(!layout.step()); // nothing moves once settled
        // Each triangle near its resting length; the two kept apart.
        QVERIFY(distance(layout, 0, 1) < ForceLayout::kLinkDistance * 2);
        QVERIFY(distance(layout, 0, 3) > distance(layout, 0, 1));
        QVERIFY(distance(layout, 0, 3) > distance(layout, 3, 4));
        // Nothing flies off.
        QVERIFY(layout.bounds().width() < 2000 && layout.bounds().height() < 2000);
    }

    void repeatable()
    {
        ForceLayout one;
        ForceLayout two;
        const QList<std::pair<int, int>> edges{{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 4}};
        one.setGraph(names(5), edges);
        two.setGraph(names(5), edges);
        settle(one);
        settle(two);
        for (int i = 0; i < 5; ++i)
            QCOMPARE(one.position(i), two.position(i));
    }

    void keepsPlacesAcrossChanges()
    {
        ForceLayout layout;
        layout.setGraph(names(3), {{0, 1}, {1, 2}});
        settle(layout);
        const QPointF before = layout.position(1);
        // n1 is now second in a larger graph: it starts where it was, and the
        // newcomer beside its neighbour.
        layout.setGraph({QStringLiteral("n9"), QStringLiteral("n1"), QStringLiteral("n0"), QStringLiteral("n2")}, {{1, 0}, {1, 2}, {1, 3}});
        QCOMPARE(layout.position(1), before);
        QVERIFY(distance(layout, 0, 1) < 20);
        QVERIFY(!layout.settled()); // warmed to fit the newcomer in
    }

    void fixedNodesStayPut()
    {
        ForceLayout layout;
        layout.setGraph(names(4), {{0, 1}, {0, 2}, {0, 3}});
        layout.fix(0, QPointF(500, -200));
        settle(layout);
        QCOMPARE(layout.position(0), QPointF(500, -200));
        QVERIFY(distance(layout, 0, 1) < ForceLayout::kLinkDistance * 3); // the others follow it
        layout.release(0);
        layout.reheat();
        QVERIFY(layout.step());
    }

    void largeGraphsStepQuickly()
    {
        // A vault-sized graph: 3000 notes, each linked to a few others.
        constexpr int kNodes = 3000;
        QList<std::pair<int, int>> edges;
        for (int i = 1; i < kNodes; ++i) {
            edges.append({i, i / 2}); // a tree
            if (i % 3 == 0)
                edges.append({i, (i * 13 + 5) % kNodes});
        }
        ForceLayout layout;
        layout.setGraph(names(kNodes), edges);
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 20; ++i)
            layout.step();
        // Barnes–Hut: well under a frame per step in a release build; generous for Debug and sanitizers.
        QVERIFY2(timer.elapsed() < 20 * 150, qPrintable(QStringLiteral("%1 ms for 20 steps").arg(timer.elapsed())));
        for (int i = 0; i < kNodes; ++i)
            QVERIFY(std::isfinite(layout.position(i).x()) && std::isfinite(layout.position(i).y()));
    }
};

QTEST_GUILESS_MAIN(TestForceLayout)
#include "test_force_layout.moc"

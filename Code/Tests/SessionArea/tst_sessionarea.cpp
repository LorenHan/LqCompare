#include <QtTest>
#include <QLabel>
#include <QTabBar>
#include "sessionarea.h"
#include "comparesession.h"
#include "homepage.h"
using namespace LqCompare;
class TestSession : public CompareSession {
public:
    TestSession() : CompareSession(QStringLiteral("text")) { setTitle(QStringLiteral("Example")); }
    bool failSave = false;
    int saves = 0;
    bool closed = false;
protected:
    QWidget *createView(QWidget *parent) override { return new QLabel(QStringLiteral("Content"), parent); }
    bool doSave(QString *error) override { ++saves; if (failSave && error) *error = QStringLiteral("disk full"); return !failSave; }
    void doClose() override { closed = true; }
};
class SessionAreaTests : public QObject {
    Q_OBJECT
private slots:
    void dirtyCloseCancelAndDiscard() {
        SessionArea area;
        auto *session = new TestSession;
        QVERIFY(session->open());
        QCOMPARE(area.addSession(session), 1);
        session->setDirty(true);
        QVERIFY(area.tabText(1).endsWith(QStringLiteral(" *")));
        area.setClosePrompt([](CompareSession *) { return SessionArea::CloseChoice::Cancel; });
        QVERIFY(!area.closeSession(1));
        QCOMPARE(area.sessionCount(), 1);
        QVERIFY(session->isDirty());
        area.setClosePrompt([](CompareSession *) { return SessionArea::CloseChoice::Discard; });
        QVERIFY(area.closeSession(1));
        QVERIFY(session->closed);
        QCOMPARE(area.sessionCount(), 0);
        QVERIFY(area.isHomeCurrent());
    }
    void failedSaveKeepsTab() {
        SessionArea area;
        auto *session = new TestSession;
        QVERIFY(session->open());
        area.addSession(session);
        session->setDirty(true);
        session->failSave = true;
        area.setClosePrompt([](CompareSession *) { return SessionArea::CloseChoice::Save; });
        QVERIFY(!area.closeSession(1));
        QCOMPARE(session->saves, 1);
        QVERIFY(session->isDirty());
        QCOMPARE(area.sessionCount(), 1);
        session->failSave = false;
        QVERIFY(area.closeSession(1));
        QCOMPARE(session->saves, 2);
    }
    void closeAllCancelKeepsEveryTab() {
        SessionArea area;
        for (int i = 0; i < 3; ++i) {
            auto *session = new TestSession;
            QVERIFY(session->open());
            area.addSession(session);
            session->setDirty(true);
        }
        int questions = 0;
        area.setClosePrompt([&questions](CompareSession *) {
            return ++questions == 2 ? SessionArea::CloseChoice::Cancel : SessionArea::CloseChoice::Discard;
        });
        QVERIFY(!area.closeAllSessions());
        QCOMPARE(questions, 2);
        QCOMPARE(area.sessionCount(), 3);
        for (int i = 1; i <= 3; ++i) QVERIFY(area.sessionAt(i)->isDirty());
    }
    void homeStaysProtectedWhenReordered() {
        SessionArea area;
        auto *first = new TestSession;
        auto *second = new TestSession;
        first->open(); second->open();
        area.addSession(first); area.addSession(second);
        auto *bar = area.findChild<QTabBar *>();
        QVERIFY(bar);
        bar->moveTab(2, 0);
        QCOMPARE(area.indexOf(area.homePage()), 0);
        QVERIFY(!area.closeSession(0));
        QCOMPARE(area.sessionCount(), 2);
        QCOMPARE(area.sessionAt(area.indexOf(first->widget())), first);
    }
    void onlyActiveStatusIsForwarded() {
        SessionArea area;
        auto *first = new TestSession;
        auto *second = new TestSession;
        first->open(); second->open();
        area.addSession(first); area.addSession(second);
        QSignalSpy spy(&area, &SessionArea::statusTextChanged);
        first->setStatusText(QStringLiteral("Background"));
        QCOMPARE(spy.count(), 0);
        second->setStatusText(QStringLiteral("Foreground"));
        QCOMPARE(spy.count(), 1);
        area.setCurrentWidget(first->widget());
        QCOMPARE(spy.last().first().toString(), QStringLiteral("Background"));
    }
};
QTEST_MAIN(SessionAreaTests)
#include "tst_sessionarea.moc"

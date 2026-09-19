#ifndef LQCOMPARE_TST_COMMANDREGISTRY_H
#define LQCOMPARE_TST_COMMANDREGISTRY_H

#include <QObject>
#include <QtTest>

///
/// \brief The TstCommandRegistry class
/// 命令注册中心的护栏测试（PRD: UI-023、UI-024、UI-025）。
///
/// 这些用例的价值在于「反向验证」：如果把某条命令的图标或说明摘掉，
/// 用例必须失败。否则护栏只是装饰。
///
class TstCommandRegistry : public QObject
{
    Q_OBJECT

private slots:
    void rejectsDuplicateId();
    void rejectsInvalidIdShape();
    void rejectsEmptyFields();
    void exposesCommandsInRegistrationOrder();
    void triggerReturnsFalseForUnimplemented();
    void triggerRunsHandler();
    void validateReportsMissingDescription();
    void validateReportsMissingIcon();
    void validateReportsMissingActionId();
    void validateReportsShortcutConflict();
    void validateAcceptsWellFormedCommand();
    void unknownCommandDoesNotThrow();
};

#endif // LQCOMPARE_TST_COMMANDREGISTRY_H

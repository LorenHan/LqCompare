#ifndef LQCOMPARE_MEDIACOMPAREVIEW_H
#define LQCOMPARE_MEDIACOMPAREVIEW_H

#include <QPointer>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableView;

namespace LqCompare {
class MediaCompareSession;
class MediaFieldModel;

class MediaCompareView : public QWidget
{
    Q_OBJECT
public:
    explicit MediaCompareView(MediaCompareSession *session, QWidget *parent = nullptr);

private:
    void refresh();
    void openPaths();
    void reloadPaths();
    void updateDetail();
    void updateEnabledState();

    QPointer<MediaCompareSession> m_session;
    QLineEdit *m_leftPath;
    QLineEdit *m_rightPath;
    QPushButton *m_compare;
    QPushButton *m_reload;
    QCheckBox *m_ignoreTechnical;
    QLabel *m_leftInfo;
    QLabel *m_rightInfo;
    QLabel *m_status;
    QLabel *m_detailTitle;
    QTableView *m_table;
    MediaFieldModel *m_model;
    QPlainTextEdit *m_leftDetail;
    QPlainTextEdit *m_rightDetail;
};
}
#endif

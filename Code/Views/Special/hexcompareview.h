#ifndef LQCOMPARE_HEXCOMPAREVIEW_H
#define LQCOMPARE_HEXCOMPAREVIEW_H
#include <QWidget>
#include <QPointer>
class QLabel;
class QLineEdit;
namespace LqCompare {
class HexCompareSession;
class HexPane;
class HexCompareView : public QWidget {
    Q_OBJECT
public:
    explicit HexCompareView(HexCompareSession *session, QWidget *parent = nullptr);
private:
    void comparePaths();
    QString m_lastSearchSignature;
    QPointer<HexCompareSession> m_session;
    QLineEdit *m_leftPath, *m_rightPath, *m_offset;
    QLabel *m_error;
    HexPane *m_left, *m_right;
};
}
#endif
